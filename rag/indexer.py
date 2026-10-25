"""Build a small, local text index from Leo's documentation and source."""

from __future__ import annotations

import json
from pathlib import Path
import re
from typing import Iterator


ROOT = Path(__file__).resolve().parents[1]
INDEX_PATH = Path(__file__).resolve().parent / "data" / "index.json"
INCLUDE_DIRS = ("docs", "language", "src", "include", "examples", "web")
INCLUDE_ROOT_FILES = ("README.md",)
ALLOWED_SUFFIXES = {".md", ".txt", ".c", ".h", ".py", ".js", ".html", ".css", ".leo"}
EXCLUDED_PARTS = {"node_modules", "build", "output", ".git", "__pycache__"}
CHUNK_LINES = 60
OVERLAP_LINES = 10


def source_files(root: Path = ROOT) -> Iterator[Path]:
    """Yield relevant project text files in a stable order."""
    candidates = [root / name for name in INCLUDE_ROOT_FILES]
    for directory in INCLUDE_DIRS:
        base = root / directory
        if base.exists():
            candidates.extend(base.rglob("*"))

    for path in sorted(set(candidates)):
        if not path.is_file() or path.suffix.lower() not in ALLOWED_SUFFIXES:
            continue
        if any(part in EXCLUDED_PARTS or part.startswith(".") for part in path.relative_to(root).parts):
            continue
        yield path


def _chunk_text(path: Path, root: Path) -> Iterator[dict[str, object]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError):
        return

    if not lines:
        return

    relative = path.relative_to(root).as_posix()
    start = 0
    while start < len(lines):
        end = min(start + CHUNK_LINES, len(lines))
        body = "\n".join(lines[start:end]).strip()
        if body:
            yield {
                "source": relative,
                "start_line": start + 1,
                "end_line": end,
                "text": body,
            }
        if end == len(lines):
            break
        start = end - OVERLAP_LINES


def build_index(root: Path = ROOT, output: Path = INDEX_PATH) -> int:
    """Index docs and project text files into JSON chunks; return chunk count."""
    chunks = [chunk for path in source_files(root) for chunk in _chunk_text(path, root)]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(chunks, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return len(chunks)


if __name__ == "__main__":
    count = build_index()
    print(f"Indexed {count} chunks into {INDEX_PATH.relative_to(ROOT)}")
