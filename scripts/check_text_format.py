#!/usr/bin/env python3
"""Validate text files and optionally restore Git-tracked executable bits."""

from __future__ import annotations

import argparse
import os
import stat
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

BINARY_EXTENSIONS = {
    ".png",
    ".jpg",
    ".jpeg",
    ".gif",
    ".bmp",
    ".ico",
    ".webp",
    ".svgz",
    ".mp3",
    ".wav",
    ".ogg",
    ".flac",
    ".aac",
    ".m4a",
    ".mp4",
    ".mov",
    ".avi",
    ".mkv",
    ".webm",
    ".264",
    ".265",
    ".h264",
    ".h265",
    ".yuv",
    ".zip",
    ".gz",
    ".tgz",
    ".bz2",
    ".xz",
    ".7z",
    ".rar",
    ".jar",
    ".war",
    ".pdf",
    ".woff",
    ".woff2",
    ".ttf",
    ".otf",
    ".so",
    ".a",
    ".o",
    ".obj",
    ".dll",
    ".exe",
    ".bin",
    ".db",
    ".sqlite",
    ".sqlite3",
}

SKIP_DIRS = {
    ".git",
    "node_modules",
    "build",
    "dist",
    "out",
    ".venv",
    "venv",
    "__pycache__",
}


def git_files() -> list[Path]:
    files: set[Path] = set()
    commands = [
        ["git", "ls-files", "-z"],
        ["git", "ls-files", "--others", "--exclude-standard", "-z"],
    ]

    for command in commands:
        output = subprocess.check_output(command, cwd=ROOT)
        for raw in output.split(b"\x00"):
            if not raw:
                continue
            files.add(Path(raw.decode("utf-8", errors="ignore")))

    return sorted(files)


def git_index_modes() -> dict[Path, str]:
    output = subprocess.check_output(["git", "ls-files", "-s", "-z"], cwd=ROOT)
    modes: dict[Path, str] = {}

    for raw in output.split(b"\x00"):
        if not raw:
            continue
        meta, relpath = raw.split(b"\t", 1)
        mode = meta.split(maxsplit=1)[0].decode("ascii")
        modes[Path(relpath.decode("utf-8", errors="ignore"))] = mode

    return modes


def restore_file_modes() -> list[str]:
    changed: list[str] = []
    exec_bits = stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH

    for relpath, mode in git_index_modes().items():
        if mode not in {"100644", "100755"}:
            continue

        path = ROOT / relpath
        if not path.exists() or not path.is_file():
            continue

        current_mode = stat.S_IMODE(path.stat().st_mode)
        should_be_executable = mode == "100755"
        current_exec_bits = current_mode & exec_bits
        expected_exec_bits = exec_bits if should_be_executable else 0

        if current_exec_bits == expected_exec_bits:
            continue

        if should_be_executable:
            new_mode = current_mode | exec_bits
        else:
            new_mode = current_mode & ~exec_bits

        os.chmod(path, new_mode)
        changed.append(str(relpath).replace("\\", "/"))

    return changed


def is_binary_content(data: bytes) -> bool:
    if not data:
        return False

    sample = data[:4096]
    if b"\x00" in sample:
        return True

    control_count = sum(1 for b in sample if (b < 9) or (13 < b < 32))
    return (control_count / len(sample)) > 0.30


def should_skip(relpath: Path) -> bool:
    if any(part.lower() in SKIP_DIRS for part in relpath.parts):
        return True
    return relpath.suffix.lower() in BINARY_EXTENSIONS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--restore-modes",
        action="store_true",
        help="restore executable bits from the Git index and exit",
    )
    args = parser.parse_args()

    if args.restore_modes:
        changed_modes = restore_file_modes()
        if changed_modes:
            print("Restored executable bits:")
            for item in changed_modes:
                print(f"- {item}")
        else:
            print("No executable bit changes needed")
        return 0

    invalid_encoding: list[str] = []
    invalid_line_endings: list[str] = []

    for relpath in git_files():
        path = ROOT / relpath
        if not path.is_file() or should_skip(relpath):
            continue

        data = path.read_bytes()
        if is_binary_content(data):
            continue

        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            invalid_encoding.append(str(relpath).replace("\\", "/"))
            continue

        if b"\r" in data:
            invalid_line_endings.append(str(relpath).replace("\\", "/"))

    if not invalid_encoding and not invalid_line_endings:
        print("PASS: all checked text files are UTF-8 + LF")
        return 0

    print("FAIL: file format check failed")

    if invalid_encoding:
        print("\nNon-UTF-8 files:")
        for item in invalid_encoding:
            print(f"- {item}")

    if invalid_line_endings:
        print("\nFiles containing CR/CRLF:")
        for item in invalid_line_endings:
            print(f"- {item}")

    return 1


if __name__ == "__main__":
    sys.exit(main())
