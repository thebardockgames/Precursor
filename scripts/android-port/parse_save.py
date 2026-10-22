#!/usr/bin/env python3
"""Parse an OpenGOAL Jak 1 memory-card bank file (bankN.bin) and print its contents.

Format (see goal_src/jak1/engine/game/game-save.gc and game/kernel/common/kmemcard.cpp):
  [0:1024]      McHeader   (save_count, checksum, magic, preview_data[64])
  [1024:66560]  payload    (game-save struct: version/allocated-length/length/info-int32[16] + tags)
  [66560:67584] McHeader   (footer, identical copy of the header)

Usage: python parse_save.py <bank0.bin>
"""
import struct
import sys

if sys.stdout.encoding is None or sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8")

HEADER_SIZE = 0x400
BANK_SIZE = 0x10000  # jak1 payload size

# *level-task-data* array order (goal_src/jak1/engine/ui/progress/progress-static.gc)
# -> Spanish (es-ES) level name, pulled from game_text.txt (text-id at index 3 = es-ES)
LEVEL_TASK_ORDER = [
    "training", "village1", "beach", "jungle", "misty", "firecanyon", "village2",
    "sunken", "swamp", "rolling", "ogre", "village3", "snow", "cave", "lavatube", "citadel",
]
LEVEL_NAMES_ES = {
    "training": "Roca del Géiser",
    "village1": "Aldea de Sandover",
    "beach": "Playa del Centinela",
    "jungle": "Selva Prohibida",
    "misty": "Isla de Misty",
    "firecanyon": "Desfiladero del Fuego",
    "village2": "Aldea de Piedra",
    "sunken": "Ciudad Perdida de Precursor",
    "swamp": "Pantano Cenagoso",
    "rolling": "Cuenca de Precursor",
    "ogre": "Paso entre las Montañas",
    "village3": "Cráter Volcánico",
    "snow": "Montaña Nevada",
    "cave": "Cueva de las Arañas",
    "lavatube": "Conducto de Lava",
    "citadel": "Ciudadela de Gol y Maia",
}
# *level-task-data-remap* (same file): save's `level-index` (1-based) -> index into
# LEVEL_TASK_ORDER above. Index 0 of this list corresponds to level-index 1.
LEVEL_INDEX_REMAP = [
    0, 1, 2, 3, 3, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13, 13, 13, 14, 15, 15, 4, 4,
]

# Totals used by the load-menu (goal_src/jak1/engine/ui/progress/progress-draw.gc)
TOTAL_POWER_CELLS = 101   # shown as "/100" until you actually have >100
TOTAL_ORBS = 2000
TOTAL_BUZZERS = 112


def level_name_es(level_index):
    """Reproduce the load-menu's level-index -> Spanish name lookup."""
    if level_index < 1 or level_index > len(LEVEL_INDEX_REMAP):
        return None
    task_idx = LEVEL_INDEX_REMAP[level_index - 1]
    key = LEVEL_TASK_ORDER[task_idx]
    return LEVEL_NAMES_ES[key]


GAME_SAVE_ELT = {
    100: "name", 101: "base-time", 102: "real-time", 103: "game-time", 104: "integral-time",
    200: "continue", 201: "life", 202: "money", 203: "money-total", 204: "money-per-level",
    205: "buzzer-total", 206: "fuel-cell", 207: "death-movie-tick",
    300: "task-list", 301: "perm-list", 303: "hint-list", 304: "text-list", 305: "level-open-list",
    400: "total-deaths", 401: "continue-deaths", 402: "fuel-cell-deaths", 403: "game-start-time",
    404: "continue-timke", 405: "death-time", 406: "hit-time", 407: "fuel-cell-pickup-time",
    408: "continue-time", 409: "fuel-cell-time", 410: "enter-level-time", 411: "deaths-per-level",
    412: "death-pos", 413: "auto-save-count", 414: "in-level-time",
    500: "sfx-volume", 501: "music-volume", 502: "dialog-volume", 503: "language",
    504: "screenx", 505: "screeny", 506: "vibration", 507: "play-hints", 508: "video-mode",
    509: "aspect-ratio",
}


def parse_header(buf, off, label):
    save_count, checksum, magic = struct.unpack_from("<III", buf, off)
    preview = buf[off + 12: off + 12 + 64]
    print(f"--- {label} ---")
    print(f"  save_count : {save_count}")
    print(f"  checksum   : 0x{checksum:08X}")
    print(f"  magic      : 0x{magic:08X} {'(OK)' if magic == 0x12345678 else '(BAD!)'}")
    print(f"  preview[0:16]: {preview[:16].hex()}")
    return save_count, checksum, magic


def parse_payload(buf, off):
    # `off` points at the start of the game-save `basic` object, which begins
    # with a 4-byte type-tag word (see decompiler/config/jak1/all-types.gc:18546,
    # `version` is declared at offset-assert 4, not 0).
    type_tag = struct.unpack_from("<I", buf, off)[0]
    version, allocated_length, length = struct.unpack_from("<iii", buf, off + 4)
    print("--- game-save payload ---")
    print(f"  type-tag (raw)   : 0x{type_tag:08X}")
    print(f"  version          : {version}")
    print(f"  allocated-length : {allocated_length}")
    print(f"  length           : {length}")

    info_off = off + 16
    info_i32 = struct.unpack_from("<16i", buf, info_off)
    level_index = info_i32[0]
    fuel_cell_count = struct.unpack_from("<f", buf, info_off + 4)[0]
    money_count = struct.unpack_from("<f", buf, info_off + 8)[0]
    buzzer_count = struct.unpack_from("<f", buf, info_off + 12)[0]
    completion = struct.unpack_from("<f", buf, info_off + 16)[0]
    info_i8 = buf[info_off: info_off + 64]
    minute, hour, week, day = info_i8[20], info_i8[21], info_i8[22], info_i8[23]
    month, year = info_i8[24], info_i8[25]
    new_game = struct.unpack_from("<i", buf, info_off + 28)[0]

    print(f"  level-index      : {level_index}")
    print(f"  fuel-cell-count  : {fuel_cell_count:.2f}")
    print(f"  money-count      : {money_count:.2f}")
    print(f"  buzzer-count     : {buzzer_count:.2f}")
    print(f"  completion %     : {completion:.4f}")
    print(f"  saved at         : {hour:02d}:{minute:02d} day {day} week {week} month {month} year {year}")
    print(f"  new-game flag    : {new_game}")

    name = level_name_es(level_index) or "OLD SAVE GAME"
    cells = int(fuel_cell_count)
    cells_total = TOTAL_POWER_CELLS if cells > 100 else 100
    print("\n--- pantalla de carga (reconstruida) ---")
    print(f"  {name}")
    print(f"    Power Cells : {cells}/{cells_total}")
    print(f"    Orbes       : {int(money_count)}/{TOTAL_ORBS}")
    print(f"    Scout Flies : {int(buzzer_count)}/{TOTAL_BUZZERS}")
    print(f"    Completado  : {int(completion)}%")

    # tags start at offset-assert 80 (object start incl. type-tag), and `length`
    # (set in save-game!) is measured as bytes-of-tags, i.e. relative to tag_start.
    tag_start = off + 80
    tag_end = tag_start + length
    pos = tag_start
    print("--- tags ---")
    idx = 0
    while pos < tag_end and pos + 16 <= len(buf):
        user_bytes = buf[pos: pos + 8]
        elt_count, elt_size, elt_type = struct.unpack_from("<iHH", buf, pos + 8)
        name = GAME_SAVE_ELT.get(elt_type, f"unknown({elt_type})")
        extra = ""
        if name in ("name", "continue"):
            data = buf[pos + 16: pos + 16 + elt_count]
            try:
                extra = " = " + data.split(b"\x00", 1)[0].decode("ascii", errors="replace")
            except Exception:
                pass
        elif elt_count == 0:
            user_u64 = struct.unpack_from("<Q", user_bytes)[0]
            user_f0 = struct.unpack_from("<f", user_bytes)[0]
            extra = f" uint64={user_u64} float0={user_f0:.3f}"
        print(f"  [{idx:3d}] {name:<20s} count={elt_count:<6d} size={elt_size:<4d}{extra}")
        adv = (elt_size * elt_count + 31) & ~15
        pos += adv
        idx += 1


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    with open(sys.argv[1], "rb") as f:
        buf = f.read()

    if len(buf) != HEADER_SIZE + BANK_SIZE + HEADER_SIZE:
        print(f"WARNING: unexpected file size {len(buf)}, expected {HEADER_SIZE + BANK_SIZE + HEADER_SIZE}")

    h1 = parse_header(buf, 0, "header")
    parse_payload(buf, HEADER_SIZE)
    h2 = parse_header(buf, HEADER_SIZE + BANK_SIZE, "footer")
    print(f"\nheader == footer: {h1 == h2}")


if __name__ == "__main__":
    main()
