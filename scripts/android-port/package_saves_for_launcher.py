#!/usr/bin/env python3
"""Package extracted PS2/Vita/PSV bank files into .zip files the Android launcher's
"Importar partida" button accepts (SavesActivity.doImport: any zip entries whose byte
size matches a valid header+payload+footer save are used, up to 2 per slot).

Reads real bank sizes from each file itself (header magic at +8, footer magic at
len-1024+8) rather than assuming a fixed size, since real hardware/Vita/PS3 dumps
carry extra trailing bytes PC saves don't have.

Usage: python package_saves_for_launcher.py <extracted-root> [out-dir]
  <extracted-root> must contain jak1/jak2/jak3 subfolders with bank0.bin..bank7.bin,
  as produced by ps2_memcard.py, or any other source using the same bank0..bank7 naming.
"""
import os
import struct
import sys
import zipfile

MAGIC = 0x12345678
HEADER_SIZE = 0x400

LEVEL_INDEX_REMAP = [0, 1, 2, 3, 3, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13, 13, 13, 14, 15, 15, 4, 4]
LEVEL_TASK_ORDER = ["training", "village1", "beach", "jungle", "misty", "firecanyon", "village2",
                     "sunken", "swamp", "rolling", "ogre", "village3", "snow", "cave", "lavatube", "citadel"]
LEVEL_NAMES_ES = {
    "training": "Roca del Geiser", "village1": "Aldea de Sandover", "beach": "Playa del Centinela",
    "jungle": "Selva Prohibida", "misty": "Isla de Misty", "firecanyon": "Desfiladero del Fuego",
    "village2": "Aldea de Piedra", "sunken": "Ciudad Perdida de Precursor", "swamp": "Pantano Cenagoso",
    "rolling": "Cuenca de Precursor", "ogre": "Paso entre las Montanas", "village3": "Crater Volcanico",
    "snow": "Montana Nevada", "cave": "Cueva de las Aranas", "lavatube": "Conducto de Lava",
    "citadel": "Ciudadela de Gol y Maia",
}
# jak2/jak3 don't have a verified level-name table yet (see conversation notes); fall
# back to a numbered label rather than guessing wrong names.
COMPLETION_OFFSET = {"jak1": 32, "jak2": 28, "jak3": 28}


def level_name(game, level_index):
    if game == "jak1" and 1 <= level_index <= len(LEVEL_INDEX_REMAP):
        return LEVEL_NAMES_ES[LEVEL_TASK_ORDER[LEVEL_INDEX_REMAP[level_index - 1]]]
    return f"nivel-{level_index}" if level_index > 0 else "partida-nueva"


def slugify(text):
    slug = "".join(c.lower() if c.isalnum() else "-" for c in text)
    while "--" in slug:
        slug = slug.replace("--", "-")
    return slug.strip("-") or "partida"


def read_bank(path):
    """Trims a bank blob down to header+payload+footer (some real-hardware/Vita dumps
    carry extra trailing bytes past the footer - see conversation notes). The footer is
    wherever the header's own 12-byte prefix (save_count+checksum+magic) repeats next,
    since the payload length between them varies per game/disc revision."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HEADER_SIZE * 2:
        return None
    header_prefix = data[0:12]
    save_count, checksum, magic = struct.unpack_from("<III", header_prefix, 0)
    if magic != MAGIC:
        return None
    footer_off = data.find(header_prefix, HEADER_SIZE)
    if footer_off == -1 or footer_off + HEADER_SIZE > len(data):
        return None
    trimmed = data[:footer_off + HEADER_SIZE]
    # game-save object starts right after the McHeader; level-index is its first
    # info-int32 entry, past its own type-tag(4)+version/alloc/length(12).
    level_index = struct.unpack_from("<i", data, HEADER_SIZE + 4 + 12)[0]
    return {"data": trimmed, "save_count": save_count, "level_index": level_index}


def package_game(game_dir, game, out_dir):
    banks = {}
    for slot in range(4):
        for pair in range(2):
            bank_num = slot * 2 + pair
            path = os.path.join(game_dir, f"bank{bank_num}.bin")
            if os.path.isfile(path):
                info = read_bank(path)
                if info:
                    banks.setdefault(slot, {})[pair] = (path, info)

    if not banks:
        print(f"  {game}: no valid banks found in {game_dir}")
        return

    os.makedirs(out_dir, exist_ok=True)
    for slot, pairs in sorted(banks.items()):
        newest = max(pairs.values(), key=lambda pi: pi[1]["save_count"])[1]
        name = level_name(game, newest["level_index"])
        zip_name = f"{game}-ranura{slot + 1}-{slugify(name)}.zip"
        zip_path = os.path.join(out_dir, zip_name)
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
            for pair, (path, info) in sorted(pairs.items()):
                z.writestr(f"bank{slot * 2 + pair}.bin", info["data"])
        print(f"  {game} ranura {slot + 1} ({name}): {zip_path}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    root = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "_launcher_import")
    for game in ("jak1", "jak2", "jak3"):
        game_dir = os.path.join(root, game)
        if os.path.isdir(game_dir):
            print(f"=== {game} ===")
            package_game(game_dir, game, out_dir)


if __name__ == "__main__":
    main()
