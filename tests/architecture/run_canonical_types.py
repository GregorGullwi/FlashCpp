"""Build the native canonical type regression; --mutations checks broken copies.

Run from any directory with Python and clang-cl (Windows) or clang++ (Unix).
Outputs stay in x64/canonical-types. This is a host architecture test, not input
to FlashCpp's source-language test runner.
"""

import argparse
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/architecture/canonical_types_ret0.cpp"
HEADER = ROOT / "src/CanonicalTypes.h"
OUTPUT = ROOT / "x64/canonical-types"


def check_guards():
    code = re.sub(r"//[^\n]*", "", HEADER.read_text())
    for forbidden in ("StringHandle", "TypeIndex", "Parser", "matches_signature",
                      "TypeSpecifierNode", "TelemetryTypeId"):
        if re.search(r"\b" + forbidden + r"\b", code):
            raise RuntimeError("canonical identity dependency: " + forbidden)
    for name in ("FlashCpp.vcxproj", "FlashCppMSVC.vcxproj"):
        project = (ROOT / name).read_text()
        for header in ("CanonicalTypes.h", "TypeQualifiers.h"):
            if 'Include="src\\' + header + '"' not in project:
                raise RuntimeError("missing project registration: " + header)
    for name in ("DeclarationBuilder.h", "DeclarationBuilder.cpp"):
        bridge = (ROOT / "src" / name).read_text()
        if re.search(r"\bTypeId\b", bridge):
            raise RuntimeError("telemetry bridge still uses canonical TypeId")


def build_and_run(name, include, expected):
    directory = OUTPUT / name
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / ("test.exe" if sys.platform == "win32" else "test")
    if sys.platform == "win32":
        command = ["clang-cl", "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX",
                   "/I" + str(include), "/I" + str(ROOT / "src"), str(SOURCE),
                   "/Fo" + str(directory / "test.obj"), "/Fe" + str(executable),
                   "/clang:-fstack-usage", "/link", "/STACK:1048576"]
    else:
        command = ["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I" + str(include), "-I" + str(ROOT / "src"), str(SOURCE),
                   "-fstack-usage", "-o", str(executable)]
    subprocess.run(command, cwd=directory, check=True)
    result = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True)
    # Neither a crash nor a compiler error counts as a rejected mutation.
    if result.returncode != expected:
        raise RuntimeError(f"{name}: exit {result.returncode}, expected {expected}\n"
                           + result.stdout + result.stderr)
    print(name + ": " + (result.stdout.strip() or result.stderr.strip()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutations", action="store_true")
    options = parser.parse_args()
    check_guards()
    build_and_run("baseline", ROOT / "src", 0)
    if options.mutations:
        original = HEADER.read_text()
        mutations = {
            "duplicate_identity": ("if (existing != ids_.end()) {", "if (false) {"),
            "lost_pointee": ("return internUnlocked({pointee,", "return internUnlocked({TypeId{1},"),
            "lost_cv_union": ("qualifiers |= input.qualifiers;", "qualifiers = input.qualifiers;"),
            "lost_reference_collapse": ("kind = CanonicalTypeKind::LValueReference;",
                                        "kind = CanonicalTypeKind::RValueReference;"),
            "cv_on_reference": ("qualifiers == CVQualifier::None || isReference(input.kind)",
                                "qualifiers == CVQualifier::None"),
        }
        for name, (before, after) in mutations.items():
            if original.count(before) != 1:
                raise RuntimeError("mutation anchor changed: " + name)
            directory = OUTPUT / name
            directory.mkdir(parents=True, exist_ok=True)
            (directory / HEADER.name).write_text(original.replace(before, after))
            build_and_run(name, directory, 1)


if __name__ == "__main__":
    main()
