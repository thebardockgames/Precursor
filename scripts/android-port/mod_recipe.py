#!/usr/bin/env python3
"""Apply a small, reviewable mod recipe to an OpenGOAL data tree.

`mod_overlay.py` finds candidate changes in a bundled mod release.  This script
applies the curated subset that is known to belong to the mod and to compile
against the current tree.  Recipes stay text based so they are easy to review,
diff, and update when upstream OpenGOAL changes.

(AI-assisted)
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path
from typing import Any


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_text(path: Path, text: str, dry_run: bool) -> None:
    if dry_run:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="")


def copy_file(src: Path, dst: Path, dry_run: bool) -> None:
    if dry_run:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_tree(src: Path, dst: Path, dry_run: bool) -> None:
    if dry_run:
        return
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def apply_recipe(recipe: dict[str, Any], root: Path, mod_data: Path | None, dry_run: bool) -> list[str]:
    changed: list[str] = []
    for index, op in enumerate(recipe.get("operations", []), start=1):
        kind = op["op"]
        rel = op.get("path")
        label = f"{index}:{kind}:{rel or op.get('dst')}"

        if kind == "copy_file":
            if mod_data is None:
                raise ValueError(f"{label}: copy_file requires --mod-data")
            copy_file(mod_data / op["src"], root / op["dst"], dry_run)
            changed.append(label)
            continue

        if kind == "copy_tree":
            if mod_data is None:
                raise ValueError(f"{label}: copy_tree requires --mod-data")
            copy_tree(mod_data / op["src"], root / op["dst"], dry_run)
            changed.append(label)
            continue

        if rel is None:
            raise ValueError(f"{label}: text operation requires path")
        path = root / rel
        text = read_text(path)
        original = text

        if kind == "replace":
            count = int(op.get("count", 1))
            old = op["old"]
            new = op["new"]
            seen = text.count(old)
            if seen < count:
                raise RuntimeError(f"{label}: expected at least {count} match(es), found {seen}")
            text = text.replace(old, new, count)
        elif kind == "replace_all":
            old = op["old"]
            seen = text.count(old)
            if seen == 0:
                raise RuntimeError(f"{label}: expected at least one match")
            text = text.replace(old, op["new"])
        elif kind == "regex_replace":
            flags = re.MULTILINE
            if op.get("dotall"):
                flags |= re.DOTALL
            count = int(op.get("count", 1))
            text, seen = re.subn(op["pattern"], op["new"], text, count=count, flags=flags)
            if seen < count:
                raise RuntimeError(f"{label}: expected {count} regex match(es), found {seen}")
        elif kind == "insert_before":
            marker = op["marker"]
            pos = text.find(marker)
            if pos < 0:
                raise RuntimeError(f"{label}: marker not found")
            text = text[:pos] + op["text"] + text[pos:]
        elif kind == "insert_after":
            marker = op["marker"]
            pos = text.find(marker)
            if pos < 0:
                raise RuntimeError(f"{label}: marker not found")
            pos += len(marker)
            text = text[:pos] + op["text"] + text[pos:]
        else:
            raise ValueError(f"{label}: unknown operation")

        if text != original:
            write_text(path, text, dry_run)
            changed.append(label)

    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True, help="OpenGOAL data/repo root to patch")
    parser.add_argument("--mod-data", type=Path, help="optional extracted mod data/ root for copy ops")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
    changed = apply_recipe(
        recipe,
        args.root.resolve(),
        args.mod_data.resolve() if args.mod_data else None,
        args.dry_run,
    )
    print(json.dumps({"recipe": recipe.get("name"), "changed": changed}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
