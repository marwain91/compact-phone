#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("verify-release-version.py")
SPEC = importlib.util.spec_from_file_location("verify_release_version", SCRIPT)
verify_release_version = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify_release_version)


def write_cmake(tmp: Path, version: str) -> Path:
    cmake = tmp / "CMakeLists.txt"
    cmake.write_text(
        f"""
cmake_minimum_required(VERSION 3.21)
project(CompactPhone
    VERSION {version}
    DESCRIPTION "test"
    LANGUAGES CXX
)
""".lstrip(),
        encoding="utf-8",
    )
    return cmake


class VerifyReleaseVersionTest(unittest.TestCase):
    def test_accepts_matching_plain_tag(self):
        with tempfile.TemporaryDirectory() as td:
            ok, message = verify_release_version.validate_release_version(
                "v0.1.0", write_cmake(Path(td), "0.1.0")
            )
        self.assertTrue(ok)
        self.assertIn("matches", message)

    def test_accepts_test_tag_when_core_version_matches(self):
        with tempfile.TemporaryDirectory() as td:
            ok, _ = verify_release_version.validate_release_version(
                "refs/tags/v0.1.0-test7", write_cmake(Path(td), "0.1.0")
            )
        self.assertTrue(ok)

    def test_rejects_tag_when_cmake_version_was_not_bumped(self):
        with tempfile.TemporaryDirectory() as td:
            ok, message = verify_release_version.validate_release_version(
                "v0.1.1", write_cmake(Path(td), "0.1.0")
            )
        self.assertFalse(ok)
        self.assertIn("declares 0.1.0", message)


if __name__ == "__main__":
    unittest.main()
