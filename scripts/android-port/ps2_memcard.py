#!/usr/bin/env python3
"""Minimal read-only PS2 memory card (.ps2/.mcr raw image) filesystem reader.

Extracts files out of a raw memory card dump by walking its real on-disk
structure (superblock -> indirect FAT -> FAT -> cluster chains -> directory
entries), the same format the original PS2 game (and OpenGOAL's reimplementation
of its memcard I/O, game/kernel/common/kmemcard.cpp) used to store each save as
8 flat "bankN.bin" files inside a "BASCUS-97124AYBABTU!"-style directory.

Reference: the well-documented community PS2 MC format (superblock fields,
2-level indirect FAT, 512-byte directory entries) as implemented by tools like
mymc/ps2mc-tools. No console-specific secrets are involved - this format has
always been fully public, unlike the PS3/PS4 save encryption.

Usage: python ps2_memcard.py <Mcd001.ps2> [extract-dir]
"""
import struct
import sys
import os

PAGE_SIZE_DEFAULT = 512


class Superblock:
    def __init__(self, data):
        self.magic = data[0:28]
        self.version = data[28:40]
        (self.page_size, self.pages_per_cluster, self.pages_per_block,
         _unused0) = struct.unpack_from("<HHHH", data, 40)
        (self.clusters_per_card, self.alloc_offset, self.alloc_end,
         self.rootdir_cluster, self.backup_block1, self.backup_block2
         ) = struct.unpack_from("<6I", data, 48)
        self.ifc_list = struct.unpack_from("<32I", data, 80)
        self.cluster_size = self.page_size * self.pages_per_cluster

    def valid(self):
        return self.magic.startswith(b"Sony PS2 Memory Card Format")


class DirEntry:
    __slots__ = ("mode", "length", "cluster", "name", "is_dir")

    def __init__(self, raw):
        self.mode, _unused0, self.length = struct.unpack_from("<HHI", raw, 0)
        self.cluster = struct.unpack_from("<I", raw, 16)[0]
        name_raw = raw[64:64 + 32]
        self.name = name_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        # DF_EXISTS=0x8000, DF_DIRECTORY=0x0020 (community-documented PS2 MC mode bits).
        self.is_dir = bool(self.mode & 0x0020)

    def exists(self):
        return bool(self.mode & 0x8000) and self.name not in ("", ".", "..")


class Ps2MemoryCard:
    # Some raw dump tools store each page with 16 extra "spare"/ECC bytes appended
    # (528-byte physical pages instead of 512), even though the filesystem's own
    # cluster/page-size fields only describe the 512 bytes of real data.
    SPARE_BYTES = 16

    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.sb = Superblock(self.data)
        if not self.sb.valid():
            raise ValueError("Not a PS2 memory card image (bad superblock magic)")
        pages_total = self.sb.clusters_per_card * self.sb.pages_per_cluster
        plain_size = pages_total * self.sb.page_size
        spare_size = pages_total * (self.sb.page_size + self.SPARE_BYTES)
        if len(self.data) >= spare_size:
            self.page_stride = self.sb.page_size + self.SPARE_BYTES
        elif len(self.data) >= plain_size:
            self.page_stride = self.sb.page_size
        else:
            raise ValueError(f"Image too small: {len(self.data)} bytes, "
                              f"expected at least {plain_size}")
        self._fat_cluster_table = self._read_indirect_fat()

    def _cluster_bytes(self, absolute_cluster):
        ppc = self.sb.pages_per_cluster
        first_page = absolute_cluster * ppc
        parts = []
        for p in range(ppc):
            off = (first_page + p) * self.page_stride
            parts.append(self.data[off:off + self.sb.page_size])
        return b"".join(parts)

    def _read_indirect_fat(self):
        entries_per_cluster = self.sb.cluster_size // 4
        table = []
        for ifc in self.sb.ifc_list:
            if ifc == 0:
                continue
            raw = self._cluster_bytes(ifc)
            table.extend(struct.unpack_from("<%dI" % entries_per_cluster, raw, 0))
        return table

    def _fat_entry(self, relative_cluster):
        entries_per_cluster = self.sb.cluster_size // 4
        fat_cluster_idx = relative_cluster // entries_per_cluster
        offset_in_cluster = relative_cluster % entries_per_cluster
        fat_cluster_abs = self._fat_cluster_table[fat_cluster_idx]
        raw = self._cluster_bytes(fat_cluster_abs)
        return struct.unpack_from("<I", raw, offset_in_cluster * 4)[0]

    def _cluster_chain(self, start_relative_cluster):
        """Yields absolute cluster numbers for a file/dir's data, following the FAT."""
        relative = start_relative_cluster
        seen = set()
        while True:
            if relative in seen:
                raise ValueError("FAT cluster chain loop detected")
            seen.add(relative)
            yield self.sb.alloc_offset + relative
            entry = self._fat_entry(relative)
            if entry == 0xFFFFFFFF:
                break
            relative = entry & 0x7FFFFFFF

    def _read_chain_bytes(self, start_relative_cluster, length=None):
        chunks = []
        total = 0
        for abs_cluster in self._cluster_chain(start_relative_cluster):
            chunks.append(self._cluster_bytes(abs_cluster))
            total += self.sb.cluster_size
            if length is not None and total >= length:
                break
        blob = b"".join(chunks)
        return blob[:length] if length is not None else blob

    def list_dir(self, start_relative_cluster, entry_count):
        """entry_count includes '.' and '..'; each dirent is one page (not one cluster)."""
        page_size = self.sb.page_size
        raw = self._read_chain_bytes(start_relative_cluster, length=entry_count * page_size)
        entries = []
        for i in range(entry_count):
            off = i * page_size
            if off + page_size > len(raw):
                break
            entry = DirEntry(raw[off:off + page_size])
            if entry.exists():
                entries.append(entry)
        return entries

    def root_entries(self):
        # Like any directory, root's own first entry is "." (self-reference), whose
        # `length` field gives the true entry count - read that one page first so the
        # chain walk below stays bounded instead of running off into unrelated clusters.
        first_cluster_abs = self.sb.alloc_offset + self.sb.rootdir_cluster
        first_page = self._cluster_bytes(first_cluster_abs)[:self.sb.page_size]
        dot_entry = DirEntry(first_page)
        return self.list_dir(self.sb.rootdir_cluster, dot_entry.length)

    def find_child(self, parent_cluster_relative, parent_length, name):
        for entry in self.list_dir(parent_cluster_relative, parent_length):
            if entry.name == name:
                return entry
        return None

    def read_file(self, entry):
        return self._read_chain_bytes(entry.cluster, entry.length)


def extract_game_saves(mc, game_dir_name, out_dir):
    root = mc.root_entries()
    game_dir = next((e for e in root if e.name == game_dir_name and e.is_dir), None)
    if game_dir is None:
        print(f"  (no directory named {game_dir_name!r} on this card)")
        return []
    children = mc.list_dir(game_dir.cluster, game_dir.length)
    os.makedirs(out_dir, exist_ok=True)
    written = []
    for entry in children:
        if entry.is_dir or not entry.name.startswith("bank") or not entry.name.endswith(".bin"):
            continue
        blob = mc.read_file(entry)
        dest = os.path.join(out_dir, entry.name)
        with open(dest, "wb") as f:
            f.write(blob)
        written.append(dest)
        print(f"  extracted {entry.name}: {len(blob)} bytes -> {dest}")
    return written


GAME_DIRS = {
    "jak1": "BASCUS-97124AYBABTU!",
    "jak2": "BASCUS-97265AYBABTU!",
    "jak3": "BASCUS-97330AYBABTU!",
}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    mc = Ps2MemoryCard(sys.argv[1])
    out_root = sys.argv[2] if len(sys.argv) > 2 else "ps2_memcard_extracted"
    sb = mc.sb
    print(f"page_size={sb.page_size} cluster_size={sb.cluster_size} "
          f"clusters_per_card={sb.clusters_per_card} alloc_offset={sb.alloc_offset}")
    root = mc.root_entries()
    print(f"root directory entries: {len(root)}")
    for e in root:
        print(f"  {'<DIR>' if e.is_dir else '     '} {e.name!r} length={e.length} cluster={e.cluster}")
    for game, dirname in GAME_DIRS.items():
        print(f"--- {game} ({dirname}) ---")
        extract_game_saves(mc, dirname, os.path.join(out_root, game))


if __name__ == "__main__":
    main()
