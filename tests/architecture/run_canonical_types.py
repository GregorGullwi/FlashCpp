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
        for header in ("CanonicalTypes.h", "TypeQualifiers.h", "CanonicalTypeAdapter.h", "ArenaAccounting.h"):
            if 'Include="src\\' + header + '"' not in project:
                raise RuntimeError("missing project registration: " + header)
    adapter = re.sub(r"//[^\n]*", "", (ROOT / "src/CanonicalTypeAdapter.h").read_text())
    for forbidden in ("StringTable", "gTypeInfo", "matches_signature", "Parser"):
        if re.search(r"\b" + forbidden + r"\b", adapter):
            raise RuntimeError("adapter identity dependency: " + forbidden)
    for name in ("DeclarationBuilder.h", "DeclarationBuilder.cpp"):
        bridge = (ROOT / "src" / name).read_text()
        if re.search(r"\bTypeId\s+(signature_id|return_type_id|internDeclaratorType|internParameterListSignature)\b", bridge):
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
            "lost_pointee": (".child = pointee,", ".child = TypeId{1},"),
            "lost_cv_union": ("qualifiers |= input.qualifiers;", "qualifiers = input.qualifiers;"),
            "lost_reference_collapse": ("kind = CanonicalTypeKind::LValueReference;",
                                        "kind = CanonicalTypeKind::RValueReference;"),
            "lost_rollback": ("if (!commit) {", "if (!commit && false) {"),
            "cv_on_reference": ("qualifiers == CVQualifier::None || isReference(input.kind)",
                                "qualifiers == CVQualifier::None"),
            "lost_array_extent": (".array_extent = extent,", ".array_extent = 1,"),
            "lost_array_element": (".child = element,", ".child = TypeId{1},"),
            "lost_unknown_bound": ("return arrayUnlocked(element, 0, CanonicalTypeNodeFlags::None);",
                                   "return arrayUnlocked(element, 1, CanonicalTypeNodeFlags::KnownArrayBound);"),
            "lost_array_cv": ("while (input.kind == CanonicalTypeKind::Array) {",
                              "while (false && input.kind == CanonicalTypeKind::Array) {"),
        }
        for name, (before, after) in mutations.items():
            if original.count(before) != 1:
                raise RuntimeError("mutation anchor changed: " + name)
            directory = OUTPUT / name
            directory.mkdir(parents=True, exist_ok=True)
            for header in ("CanonicalTypes.h", "CanonicalTypeAdapter.h", "ArenaAccounting.h"):
                (directory / header).write_text((ROOT / "src" / header).read_text())
            (directory / HEADER.name).write_text(original.replace(before, after))
            build_and_run(name, directory, 1)
        for name, header, before, after in (
            ("adapter_cv", "CanonicalTypeAdapter.h", "table.qualify(id, syntax.cv_qualifier())", "table.qualify(id, CVQualifier::None)"),
            ("adapter_array_order", "CanonicalTypeAdapter.h",
             "for (size_t index = dimensions.size(); index-- > first_dimension;)",
             "for (size_t index = first_dimension; index < dimensions.size(); ++index)"),
            ("adapter_array_binding", "CanonicalTypeAdapter.h", "if (has_pointee_array) {",
             "if (false && has_pointee_array) {"),
            ("adapter_parameter_decay", "CanonicalTypeAdapter.h",
             "context == CanonicalTypeImportContext::FunctionParameter",
             "context == CanonicalTypeImportContext::Exact"),
            ("aggregate_peak", "ArenaAccounting.h", "stats_.peak_bytes = stats_.current_bytes;",
             "stats_.peak_bytes += stats_.current_bytes;"),
        ):
            directory = OUTPUT / name
            directory.mkdir(parents=True, exist_ok=True)
            for sibling in ("CanonicalTypes.h", "CanonicalTypeAdapter.h", "ArenaAccounting.h"):
                text = (ROOT / "src" / sibling).read_text()
                if sibling == header:
                    if text.count(before) != 1:
                        raise RuntimeError("mutation anchor changed: " + name)
                    text = text.replace(before, after)
                (directory / sibling).write_text(text)
            build_and_run(name, directory, 1)


if __name__ == "__main__":
    main()
