#!/usr/bin/env python3
"""Fail if a release tag does not match CMakeLists.txt project version."""

import argparse
import re
import sys
from pathlib import Path


PROJECT_VERSION_RE = re.compile(
    r"project\s*\(\s*CompactPhone\b.*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})",
    re.DOTALL,
)


def cmake_project_version(cmake_file: Path) -> str:
    text = cmake_file.read_text(encoding="utf-8")
    match = PROJECT_VERSION_RE.search(text)
    if not match:
        raise ValueError(f"could not find project(CompactPhone VERSION ...) in {cmake_file}")
    return match.group(1)


def release_core_version(tag: str) -> str:
    ref = tag.strip()
    if ref.startswith("refs/tags/"):
        ref = ref.removeprefix("refs/tags/")
    if ref.startswith("v"):
        ref = ref[1:]
    return ref.split("-", 1)[0]


def validate_release_version(tag: str, cmake_file: Path) -> tuple[bool, str]:
    cmake_version = cmake_project_version(cmake_file)
    tag_version = release_core_version(tag)
    if tag_version != cmake_version:
        return (
            False,
            f"release tag {tag!r} resolves to version {tag_version}, "
            f"but {cmake_file} declares {cmake_version}",
        )
    return True, f"release tag {tag!r} matches CMake project version {cmake_version}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tag", help="Release tag, for example v0.1.0 or refs/tags/v0.1.0")
    parser.add_argument("--cmake-file", default="CMakeLists.txt")
    args = parser.parse_args()

    try:
        ok, message = validate_release_version(args.tag, Path(args.cmake_file))
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not ok:
        print(f"error: {message}", file=sys.stderr)
        return 1
    print(message)
    return 0


if __name__ == "__main__":
    sys.exit(main())
