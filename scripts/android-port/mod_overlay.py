#!/usr/bin/env python3
"""Extract the meaningful overlay from an OpenGOAL/JakMods release bundle.

The JakMods release zips commonly ship a full OpenGOAL `data/` tree.  For ARM
ports that is the wrong unit to consume: the old bundled tree can compile but
still crash against the current Android runtime.  This tool compares a mod
bundle against the current repository and writes a reviewable overlay:

  - added files copied verbatim
  - modified files as unified patches
  - unchanged files omitted
  - a JSON report with simple risk tags

The output is intentionally conservative.  It prepares the evidence needed for
an adapter/recipe step; it does not blindly patch the active repository.

(AI-assisted)
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import shutil
from dataclasses import asdict, dataclass
from pathlib import Path


TEXT_EXTENSIONS = {
    ".gc",
    ".gd",
    ".gp",
    ".gs",
    ".json",
    ".jsonc",
    ".txt",
    ".md",
    ".cfg",
    ".ini",
    ".yml",
    ".yaml",
    ".frag",
    ".vert",
    ".glsl",
}


SCANNED_ROOTS = (
    "goal_src",
    "custom_assets",
    "game/assets",
    "game/graphics/opengl_renderer/shaders",
    "decompiler/config",
)


@dataclass
class OverlayEntry:
    path: str
    kind: str
    size: int
    sha256: str
    risk: str
    reason: str


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_text(path: Path) -> bool:
    return path.suffix.lower() in TEXT_EXTENSIONS


def rel_posix(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def read_text(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)


def classify(path: str, kind: str, text: str | None) -> tuple[str, str]:
    if path.startswith("goal_src/") and "/engine/mods/" in path:
        return "low", "new mod support file"
    if path.startswith("custom_assets/"):
        return "medium", "custom asset input"
    if path.endswith((".gd", ".gp")):
        return "medium", "build graph / DGO membership change"
    if text and ("mod-base-change" in text or "og:noorb" in text or "orbless" in text.lower()):
        return "high", "base source hook/mod marker"
    if path.startswith("goal_src/"):
        return "high", "base GOAL source differs from current tree"
    return "medium", "data/config differs from current tree"


def iter_mod_files(mod_data: Path) -> list[Path]:
    files: list[Path] = []
    for root in SCANNED_ROOTS:
        base = mod_data / root
        if base.exists():
            files.extend(path for path in base.rglob("*") if path.is_file())
    return sorted(files)


def write_added(mod_file: Path, mod_data: Path, output: Path) -> None:
    dst = output / "files" / rel_posix(mod_file, mod_data)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(mod_file, dst)


def write_patch(base_file: Path, mod_file: Path, rel: str, output: Path) -> None:
    patch_path = output / "patches" / f"{rel.replace('/', '__')}.patch"
    patch_path.parent.mkdir(parents=True, exist_ok=True)
    diff = difflib.unified_diff(
        read_text(base_file),
        read_text(mod_file),
        fromfile=f"a/{rel}",
        tofile=f"b/{rel}",
        n=3,
    )
    patch_path.write_text("".join(diff), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd(), help="current OpenGOAL repo root")
    parser.add_argument("--mod-data", type=Path, required=True, help="path to extracted mod data/ directory")
    parser.add_argument("--out", type=Path, required=True, help="overlay output directory")
    parser.add_argument("--game", choices=("jak1", "jak2", "jak3"), required=True)
    parser.add_argument("--clean", action="store_true", help="delete output directory before writing")
    args = parser.parse_args()

    repo = args.repo.resolve()
    mod_data = args.mod_data.resolve()
    output = args.out.resolve()

    if args.clean and output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)

    entries: list[OverlayEntry] = []
    skipped = 0

    for mod_file in iter_mod_files(mod_data):
        rel = rel_posix(mod_file, mod_data)
        # Keep the selected game plus shared renderer/assets.  Drop other games from trilogy bundles.
        if rel.startswith("goal_src/") and not rel.startswith(f"goal_src/{args.game}/"):
            skipped += 1
            continue
        if rel.startswith("custom_assets/") and not rel.startswith(f"custom_assets/{args.game}/"):
            skipped += 1
            continue
        if rel.startswith("game/assets/") and not rel.startswith(f"game/assets/{args.game}/"):
            skipped += 1
            continue
        if rel.startswith("decompiler/config/") and not rel.startswith(f"decompiler/config/{args.game}/"):
            skipped += 1
            continue

        base_file = repo / rel
        digest = sha256(mod_file)
        if base_file.exists() and base_file.is_file() and sha256(base_file) == digest:
            skipped += 1
            continue

        text = mod_file.read_text(encoding="utf-8", errors="replace") if is_text(mod_file) else None
        kind = "added" if not base_file.exists() else "modified"
        risk, reason = classify(rel, kind, text)
        entries.append(OverlayEntry(rel, kind, mod_file.stat().st_size, digest, risk, reason))

        if kind == "added":
            write_added(mod_file, mod_data, output)
        elif is_text(mod_file) and is_text(base_file):
            write_patch(base_file, mod_file, rel, output)
        else:
            write_added(mod_file, mod_data, output)

    report = {
        "schema": 1,
        "game": args.game,
        "mod_data": str(mod_data),
        "repo": str(repo),
        "summary": {
            "entries": len(entries),
            "added": sum(1 for item in entries if item.kind == "added"),
            "modified": sum(1 for item in entries if item.kind == "modified"),
            "skipped_unchanged_or_other_game": skipped,
        },
        "entries": [asdict(item) for item in entries],
    }
    (output / "overlay-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
