#!/usr/bin/env python3
"""Validate a product release tag against its CMake project version."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
IDENTIFIER = r"(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
SEMVER = re.compile(
    rf"(?P<core>(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*))"
    rf"(?:-(?P<prerelease>{IDENTIFIER}(?:\.{IDENTIFIER})*))?"
    r"(?:\+(?P<build>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
)
PROJECT_VERSION = re.compile(
    r"(?im)^\s*project\s*\(\s*{name}\s+VERSION\s+"
    r"(?P<version>[0-9]+\.[0-9]+\.[0-9]+)(?:\s|\))"
)


def validate(tag: str) -> tuple[str, str, str, bool]:
    match = re.fullmatch(r"(?P<product>qypr|waylaunch)-v(?P<version>.+)", tag)
    if not match:
        raise ValueError("tag must be qypr-v<semver> or waylaunch-v<semver>")

    product = match.group("product")
    version = match.group("version")
    semver = SEMVER.fullmatch(version)
    if not semver:
        raise ValueError(f"{version!r} is not a valid SemVer version")

    cmake_path = ROOT / product / "CMakeLists.txt"
    source = cmake_path.read_text(encoding="utf-8")
    project_match = re.search(
        PROJECT_VERSION.pattern.format(name=re.escape(product)), source
    )
    if not project_match:
        raise ValueError(f"could not read {product} project version from {cmake_path}")

    core = semver.group("core")
    cmake_version = project_match.group("version")
    if core != cmake_version:
        raise ValueError(
            f"tag version {core} does not match {product}/CMakeLists.txt ({cmake_version})"
        )

    suffix = version[len(core) :]
    return product, version, suffix, semver.group("prerelease") is not None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag", help="release tag to validate")
    parser.add_argument(
        "--github-output",
        type=Path,
        help="append validated values to a GitHub Actions output file",
    )
    args = parser.parse_args()

    try:
        product, version, suffix, prerelease = validate(args.tag)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    outputs = {
        "product": product,
        "version": version,
        "version_extra": suffix,
        "is_prerelease": str(prerelease).lower(),
    }
    if args.github_output:
        with args.github_output.open("a", encoding="utf-8") as output:
            for key, value in outputs.items():
                output.write(f"{key}={value}\n")
    else:
        for key, value in outputs.items():
            print(f"{key}={value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
