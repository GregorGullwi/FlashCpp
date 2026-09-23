"""Build the native canonical type regression; --mutations checks broken copies.

Run from any directory with Python and clang-cl (Windows) or clang++ (Unix).
Outputs stay in x64/canonical-types. This is a host architecture test, not input
to FlashCpp's source-language test runner.
"""

import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import threading


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/architecture/canonical_types_ret0.cpp"
HEADER = ROOT / "src/CanonicalTypes.h"
IMPL = ROOT / "src/CanonicalTypes.cpp"
OUTPUT = ROOT / "x64/canonical-types"

# Files copied into every mutation directory. The list is also what decides
# whether a translation unit can reuse the pristine object: a mutation that
# leaves a TU's inputs byte-identical cannot change that TU's object.
PROJECT_FILES = ("CanonicalTypes.h", "CanonicalTypes.cpp", "CanonicalTypeAdapter.h",
                 "ArenaAccounting.h", "TemplateDeclTable.h")
# The regression source includes the adapter and template tables; the
# implementation only includes CanonicalTypes.h (which pulls ArenaAccounting.h).
TEST_TU_INPUTS = ("CanonicalTypes.h", "CanonicalTypeAdapter.h", "ArenaAccounting.h",
                  "TemplateDeclTable.h")
IMPL_TU_INPUTS = ("CanonicalTypes.h", "CanonicalTypes.cpp", "ArenaAccounting.h")
OBJECT_SUFFIX = ".obj" if sys.platform == "win32" else ".o"

PRINT_LOCK = threading.Lock()


def deindent(text):
    """Remove one leading tab per line, matching the header-to-cpp move."""
    return "\n".join(line[1:] if line.startswith("\t") else line for line in text.split("\n"))


def check_guards():
    for source in (HEADER, IMPL):
        code = re.sub(r"//[^\n]*", "", source.read_text())
        for forbidden in ("StringHandle", "TypeIndex", "Parser", "matches_signature",
                          "TypeSpecifierNode", "TelemetryTypeId"):
            if re.search(r"\b" + forbidden + r"\b", code):
                raise RuntimeError("canonical identity dependency in " + source.name + ": " + forbidden)
    for name in ("FlashCpp.vcxproj", "FlashCppMSVC.vcxproj"):
        project = (ROOT / name).read_text()
        for header in ("CanonicalTypes.h", "TypeQualifiers.h", "CanonicalTypeAdapter.h", "ArenaAccounting.h"):
            if 'Include="src\\' + header + '"' not in project:
                raise RuntimeError("missing project registration: " + header)
        if 'Include="src\\CanonicalTypes.cpp"' not in project:
            raise RuntimeError("missing project registration: CanonicalTypes.cpp")
    adapter = re.sub(r"//[^\n]*", "", (ROOT / "src/CanonicalTypeAdapter.h").read_text())
    for forbidden in ("StringTable", "gTypeInfo", "matches_signature", "Parser"):
        if re.search(r"\b" + forbidden + r"\b", adapter):
            raise RuntimeError("adapter identity dependency: " + forbidden)
    for name in ("DeclarationBuilder.h", "DeclarationBuilder.cpp"):
        bridge = (ROOT / "src" / name).read_text()
        if re.search(r"\bTypeId\s+(signature_id|return_type_id|internDeclaratorType|internParameterListSignature)\b", bridge):
            raise RuntimeError("telemetry bridge still uses canonical TypeId")


def compile_tu(source, include, obj, cwd):
    """Compile one translation unit to an object against the given headers."""
    if sys.platform == "win32":
        command = ["clang-cl", "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX",
                   "/I" + str(include), "/I" + str(ROOT / "src"),
                   "/c", str(source), "/Fo" + str(obj), "/clang:-fstack-usage"]
    else:
        command = ["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I" + str(include), "-I" + str(ROOT / "src"),
                   "-c", str(source), "-fstack-usage", "-o", str(obj)]
    subprocess.run(command, cwd=cwd, check=True)


def link_executable(objects, executable):
    if sys.platform == "win32":
        command = ["clang-cl", "/nologo", *(str(obj) for obj in objects),
                   "/Fe" + str(executable), "/link", "/STACK:1048576"]
    else:
        command = ["clang++", *(str(obj) for obj in objects), "-o", str(executable)]
    subprocess.run(command, cwd=executable.parent, check=True)


def changed_project_files(include):
    """Names of project files whose copy under include differs from src/.

    Files are compared as text so CRLF copies (write_text on Windows) match the
    LF originals, and a file missing from include resolves through the fallback
    -I to the pristine src/ copy, so it is not a change either.
    """
    changed = set()
    for name in PROJECT_FILES:
        candidate = include / name
        if candidate.exists() and candidate.read_text() != (ROOT / "src" / name).read_text():
            changed.add(name)
    return changed


def build_pristine():
    """Compile both translation units once against the untouched headers.

    Mutations that leave a TU's inputs byte-identical reuse these objects, so
    the 5s regression TU is not rebuilt for every implementation mutation.
    """
    cache = OUTPUT / "_objcache"
    cache.mkdir(parents=True, exist_ok=True)
    test_obj = cache / ("canonical_types_ret0" + OBJECT_SUFFIX)
    impl_obj = cache / ("CanonicalTypes" + OBJECT_SUFFIX)
    compile_tu(SOURCE, ROOT / "src", test_obj, cache)
    compile_tu(ROOT / "src/CanonicalTypes.cpp", ROOT / "src", impl_obj, cache)
    return {"test": test_obj, "impl": impl_obj}


def build_and_run(name, include, expected, pristine):
    directory = OUTPUT / name
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / ("test.exe" if sys.platform == "win32" else "test")
    changed = changed_project_files(include)
    if changed & set(TEST_TU_INPUTS):
        test_obj = directory / ("canonical_types_ret0" + OBJECT_SUFFIX)
        compile_tu(SOURCE, include, test_obj, directory)
    else:
        test_obj = pristine["test"]
    if changed & set(IMPL_TU_INPUTS):
        impl_obj = directory / ("CanonicalTypes" + OBJECT_SUFFIX)
        compile_tu(include / "CanonicalTypes.cpp", include, impl_obj, directory)
    else:
        impl_obj = pristine["impl"]
    link_executable((test_obj, impl_obj), executable)
    result = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True)
    # Neither a crash nor a compiler error counts as a rejected mutation.
    if result.returncode != expected:
        raise RuntimeError(f"{name}: exit {result.returncode}, expected {expected}\n"
                           + result.stdout + result.stderr)
    with PRINT_LOCK:
        print(name + ": " + (result.stdout.strip() or result.stderr.strip()))


def run_jobs(jobs, pristine, workers):
    if workers <= 1:
        for name, include, expected in jobs:
            build_and_run(name, include, expected, pristine)
        return
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(build_and_run, name, include, expected, pristine): name
                   for name, include, expected in jobs}
        for future in concurrent.futures.as_completed(futures):
            try:
                future.result()
            except Exception as error:
                failures.append(f"{futures[future]}: {error}")
    if failures:
        raise RuntimeError("mutation builds failed:\n" + "\n".join(sorted(failures)))


def run_template_owner_tag_mutation():
    name = "template_owner_tag"
    before = "const Key key{owner.value, name, kind, signature_index};"
    after = ("const Key key{isTemplateOwnedOwnerId(owner) ?\n"
             "\t\t\t(owner.value & kOwnerIdPayloadMask) | kClassOwnerIdTag : owner.value,\n"
             "\t\t\tname, kind, signature_index};")
    original_template_decls = (ROOT / "src" / "TemplateDeclTable.h").read_text()
    if original_template_decls.count(before) != 1:
        raise RuntimeError("mutation anchor changed: " + name)
    directory = OUTPUT / name
    directory.mkdir(parents=True, exist_ok=True)
    for sibling in (
        "CanonicalTypes.h", "CanonicalTypes.cpp", "CanonicalTypeAdapter.h",
        "ArenaAccounting.h", "TemplateDeclTable.h"):
        text = (ROOT / "src" / sibling).read_text()
        if sibling == "TemplateDeclTable.h":
            text = text.replace(before, after)
        (directory / sibling).write_text(text)
    return (name, directory, 1)


def run_adapter_order_mutation(name, before, after):
    directory = OUTPUT / name
    directory.mkdir(parents=True, exist_ok=True)
    for sibling in (
        "CanonicalTypes.h", "CanonicalTypes.cpp", "CanonicalTypeAdapter.h",
        "ArenaAccounting.h"):
        text = (ROOT / "src" / sibling).read_text()
        if sibling == "CanonicalTypeAdapter.h":
            if text.count(before) != 1:
                raise RuntimeError("mutation anchor changed: " + name)
            text = text.replace(before, after)
        (directory / sibling).write_text(text)
    return (name, directory, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutations", action="store_true")
    parser.add_argument("--template-owner-tag-mutation", action="store_true")
    parser.add_argument("--jobs", type=int, default=0,
                        help="parallel mutation builds (0 = logical CPUs - 1)")
    options = parser.parse_args()
    check_guards()
    pristine = build_pristine()
    build_and_run("baseline", ROOT / "src", 0, pristine)
    workers = options.jobs if options.jobs > 0 else max(1, (os.cpu_count() or 2) - 1)
    if options.template_owner_tag_mutation:
        run_jobs([run_template_owner_tag_mutation()], pristine, workers)
        return
    if options.mutations:
        jobs = [
            run_adapter_order_mutation(
                "reversed_declarator_import",
                "for (size_t index = components.size(); index-- > 0;) {",
                "for (size_t index = 0; index < components.size(); ++index) {"),
            run_adapter_order_mutation(
                "skipped_declarator_export",
                "result.components.push_back(\n"
                "\t\t\t\tDeclaratorComponent::pointer(pending_pointer_cv));",
                "if (false) result.components.push_back(\n"
                "\t\t\t\tDeclaratorComponent::pointer(pending_pointer_cv));"),
        ]
        original = IMPL.read_text()
        mutations = {
            "lost_dependent_qualifier": (
                ".child = qualifier,\n\t\t\t.kind = CanonicalTypeKind::DependentName,",
                ".child = TypeId{1},\n\t\t\t.kind = CanonicalTypeKind::DependentName,"),
            "lost_specialization_dependent_qualifier": (
                "kind == CanonicalTypeKind::TemplateParameter ||\n"
                "\t\t\tkind == CanonicalTypeKind::TemplateSpecialization ||\n"
                "\t\t\tkind == CanonicalTypeKind::AliasTemplateSpecialization ||\n"
                "\t\t\tkind == CanonicalTypeKind::DependentName ||",
                "kind == CanonicalTypeKind::TemplateParameter ||\n"
                "\t\t\tkind == CanonicalTypeKind::DependentName ||"),
            "lost_dependent_identifier": (
                "return name_link;",
                "return TypeId{1};"),
            "lost_dependent_template_member_args": (
                "packDependentTemplateMemberExtent(\n"
                "\t\t\t\tpackIdentifierBytesUnlocked(identifier), arg_link),",
                "packDependentTemplateMemberExtent(\n"
                "\t\t\t\tpackIdentifierBytesUnlocked(identifier), TypeId{}),"),
            "lost_dependent_name_tail": (
                ".child = name_link,\n\t\t\t\t.kind = CanonicalTypeKind::NameBytes,",
                ".child = TypeId{},\n\t\t\t\t.kind = CanonicalTypeKind::NameBytes,"),
            "name_bytes_as_type": (
                "kind == CanonicalTypeKind::NameBytes;",
                "false;"),
            "duplicate_identity": ("if (existing != ids_.end()) {", "if (false) {"),
            "lost_pointee": (
                ".child = pointee,\n"
                "\t\t\t.kind = CanonicalTypeKind::Pointer,",
                ".child = TypeId{1},\n"
                "\t\t\t.kind = CanonicalTypeKind::Pointer,"),
            "lost_cv_union": ("qualifiers |= input.qualifiers;", "qualifiers = input.qualifiers;"),
            "lost_reference_collapse": (
                "// [dcl.ref] reference collapsing: only && combined with && stays &&.\n"
                "\t\t\tif (input.kind == CanonicalTypeKind::LValueReference) {\n"
                "\t\t\t\tkind = CanonicalTypeKind::LValueReference;",
                "// [dcl.ref] reference collapsing: only && combined with && stays &&.\n"
                "\t\t\tif (input.kind == CanonicalTypeKind::LValueReference) {\n"
                "\t\t\t\tkind = CanonicalTypeKind::RValueReference;"),
            "lost_rollback": ("if (!commit) {", "if (!commit && false) {"),
            "cv_on_reference": ("qualifiers == CVQualifier::None || isReference(input.kind)",
                                "qualifiers == CVQualifier::None"),
            "lost_array_extent": (".array_extent = extent,", ".array_extent = 1 + extent * 0,"),
            "lost_array_element": (".child = element,", ".child = TypeId{1},"),
            "lost_unknown_bound": ("return arrayUnlocked(element, 0, CanonicalTypeNodeFlags::None);",
                                   "return arrayUnlocked(element, 1, CanonicalTypeNodeFlags::KnownArrayBound);"),
            "lost_array_cv": ("while (input.kind == CanonicalTypeKind::Array) {",
                              "while (false && input.kind == CanonicalTypeKind::Array) {"),
            "lost_function_param": (
                ".array_extent = packFunctionArrayExtent(param_link, dependent_noexcept),",
                ".array_extent = packFunctionArrayExtent(TypeId{}, dependent_noexcept),"),
            "lost_dependent_noexcept": (
                "if (dependent_noexcept) {\n"
                "\t\t\tflags |= CanonicalTypeNodeFlags::DependentNoexceptFunction;\n"
                "\t\t}",
                "if (false && dependent_noexcept) {\n"
                "\t\t\tflags |= CanonicalTypeNodeFlags::DependentNoexceptFunction;\n"
                "\t\t}"),
            "lost_function_cv_merge": (
                "// [dcl.fct]: cv-qualifiers on a function type are part of that type.\n"
                "\t\tif (input.kind == CanonicalTypeKind::Function) {\n"
                "\t\t\tinput.qualifiers |= qualifiers;\n"
                "\t\t\treturn internUnlocked(input);\n"
                "\t\t}",
                "// [dcl.fct]: cv-qualifiers on a function type are part of that type.\n"
                "\t\tif (input.kind == CanonicalTypeKind::Function) {\n"
                "\t\t\treturn type;\n"
                "\t\t}"),
            "lost_variadic": (
                "if (is_variadic) {\n\t\t\tflags |= CanonicalTypeNodeFlags::VariadicFunction;\n\t\t}",
                "if (false && is_variadic) {\n\t\t\tflags |= CanonicalTypeNodeFlags::VariadicFunction;\n\t\t}"),
            "lost_noexcept": (
                "if (is_noexcept) {\n\t\t\tflags |= CanonicalTypeNodeFlags::NoexceptFunction;\n\t\t}",
                "if (false && is_noexcept) {\n\t\t\tflags |= CanonicalTypeNodeFlags::NoexceptFunction;\n\t\t}"),
            "lost_calling_convention": (
                ".builtin = static_cast<CanonicalBuiltinKind>(calling_convention),",
                ".builtin = static_cast<CanonicalBuiltinKind>(CanonicalCallingConvention::Default),"),
            "lost_dll_linkage": (
                "if (dll_linkage == CanonicalDllLinkage::Import) {\n"
                "\t\t\tflags |= CanonicalTypeNodeFlags::FunctionDllImport;\n"
                "\t\t} else if (dll_linkage == CanonicalDllLinkage::Export) {\n"
                "\t\t\tflags |= CanonicalTypeNodeFlags::FunctionDllExport;\n"
                "\t\t}",
                "if (false && dll_linkage == CanonicalDllLinkage::Import) {\n"
                "\t\t\tflags |= CanonicalTypeNodeFlags::FunctionDllImport;\n"
                "\t\t} else if (false && dll_linkage == CanonicalDllLinkage::Export) {\n"
                "\t\t\tflags |= CanonicalTypeNodeFlags::FunctionDllExport;\n"
                "\t\t}"),
            "lost_member_owner": (
                "TypeId CanonicalTypeTable::memberObjectPointer(TypeId owner, TypeId pointee) {\n"
                "\t\tstd::lock_guard lock(mutex_);\n"
                "\t\tcheckTransactionThread();\n"
                "\t\tconst TypeId record_owner = recordOwnerUnlocked(owner);",
                "TypeId CanonicalTypeTable::memberObjectPointer(TypeId owner, TypeId pointee) {\n"
                "\t\tstd::lock_guard lock(mutex_);\n"
                "\t\tcheckTransactionThread();\n"
                "\t\trecordOwnerUnlocked(owner);\n"
                "\t\tconst TypeId record_owner = TypeId{1};"),
            "lost_record_entity": (
                ".kind = CanonicalTypeKind::Record,\n"
                "\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t.array_extent = entity.value,",
                ".kind = CanonicalTypeKind::Record,\n"
                "\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t.array_extent = 1,"),
            "lost_enum_entity": (
                ".kind = CanonicalTypeKind::Enum,\n"
                "\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t.array_extent = entity.value,",
                ".kind = CanonicalTypeKind::Enum,\n"
                "\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t.array_extent = 1,"),
            "lost_template_parameter": (
                ".array_extent = packTemplateParameterExtent(template_decl, parameter_index),",
                ".array_extent = packTemplateParameterExtent(TemplateDeclId{1}, parameter_index),"),
            "lost_substitute_parameter": (
                "memo.emplace(frame.id.value, args[index].type);",
                "memo.emplace(frame.id.value, frame.id);"),
            "lost_alias_parameter_kind_check": (
                "\t\t\t\t\tfor (size_t index = 0; index < arguments.size(); ++index) {\n"
                "\t\t\t\t\t\tif (arguments[index].kind != target->second.parameter_kinds[index]) {\n"
                "\t\t\t\t\t\t\texpand = false;\n"
                "\t\t\t\t\t\t\tbreak;\n"
                "\t\t\t\t\t\t}",
                "\t\t\t\t\tfor (size_t index = 0; index < arguments.size(); ++index) {\n"
                "\t\t\t\t\t\tif (false &&\n"
                "\t\t\t\t\t\t\targuments[index].kind != target->second.parameter_kinds[index]) {\n"
                "\t\t\t\t\t\t\texpand = false;\n"
                "\t\t\t\t\t\t\tbreak;\n"
                "\t\t\t\t\t\t}"),
            "lost_nested_pointer_redirect": (
                "\t\t\tcase CanonicalTypeKind::Pointer: {\n"
                "\t\t\t\tconst TypeId child = memo.at(memoKey(frame.scope, node.child));",
                "\t\t\tcase CanonicalTypeKind::Pointer: {\n"
                "\t\t\t\tconst TypeId child = node.child;"),
            "lost_member_alias_owner_dependent_argument": (
                "if (isDependentAliasArgumentUnlocked(value) ||\n"
                "\t\t\t\tisInternalLink(nodeUnlocked(value).kind)) {\n"
                "\t\t\t\treturn std::nullopt;\n"
                "\t\t\t}",
                "if (false) {\n"
                "\t\t\t\treturn std::nullopt;\n"
                "\t\t\t}"),
            "lost_member_alias_owner_arity": (
                "if (!templateParameterReferencesCoveredUnlocked(published->second.target,\n"
                "\t\t\t\tpublished->second.owner, owner_arguments.size()) ||",
                "if (false ||"),
            "lost_member_alias_use_identity": (
                "packDependentMemberAliasExtent(member, arg_link),",
                "packDependentMemberAliasExtent(TemplateDeclId{member.value + 1u}, arg_link),"),
            "lost_alias_template_placeholder_replacement": (
                "if (placeholder_decl == env && placeholder_index < args.size() &&\n"
                "\t\t\t\t\t\t\targs[placeholder_index].kind == CanonicalTemplateArgKind::Template) {\n"
                "\t\t\t\t\t\t\trebuilt_mixed.push_back(CanonicalTemplateArgument::makeTemplate(\n"
                "\t\t\t\t\t\t\t\targs[placeholder_index].template_decl));\n"
                "\t\t\t\t\t\t\tunchanged = false;",
                "if (false) {\n"
                "\t\t\t\t\t\t\trebuilt_mixed.push_back(CanonicalTemplateArgument::makeTemplate(\n"
                "\t\t\t\t\t\t\t\targs[placeholder_index].template_decl));\n"
                "\t\t\t\t\t\t\tunchanged = false;"),
            "lost_substitute_function_return": (
                "const TypeId substituted_return = memo.at(node.child.value);",
                "const TypeId substituted_return = node.child;"),
            "lost_substitute_function_parameter": (
                "const CanonicalTypeNode parameter_link_node = nodeUnlocked(param_link);\n"
                "\t\t\t\t\tconst TypeId original =\n"
                "\t\t\t\t\t\tTypeId{static_cast<uint32_t>(parameter_link_node.array_extent)};\n"
                "\t\t\t\t\tconst TypeId substituted = memo.at(original.value);",
                "const CanonicalTypeNode parameter_link_node = nodeUnlocked(param_link);\n"
                "\t\t\t\t\tconst TypeId original =\n"
                "\t\t\t\t\t\tTypeId{static_cast<uint32_t>(parameter_link_node.array_extent)};\n"
                "\t\t\t\t\tconst TypeId substituted = original;"),
            "lost_substitute_member_pointer_pointee": (
                "const TypeId substituted_pointee = memo.at(node.child.value);",
                "const TypeId substituted_pointee = node.child;"),
            "lost_substitute_function_dependent_noexcept": (
                ".array_extent = packFunctionArrayExtent(\n"
                "\t\t\t\t\t\trebuilt_param_link, unpackFunctionDependentNoexcept(node.array_extent)),",
                ".array_extent = packFunctionArrayExtent(\n"
                "\t\t\t\t\t\trebuilt_param_link, ExprId{}),"),
            "lost_substitute_function_cv": (
                ".kind = CanonicalTypeKind::Function,\n"
                "\t\t\t\t\t.builtin = node.builtin,\n"
                "\t\t\t\t\t.qualifiers = node.qualifiers,\n"
                "\t\t\t\t\t.flags = node.flags,",
                ".kind = CanonicalTypeKind::Function,\n"
                "\t\t\t\t\t.builtin = node.builtin,\n"
                "\t\t\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t\t\t.flags = node.flags,"),
            "lost_template_specialization": (
                ".child = rebuildMixedTemplateArgListUnlocked(arguments),\n"
                "\t\t\t.kind = CanonicalTypeKind::TemplateSpecialization,\n"
                "\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t.array_extent = primary.value,",
                ".child = rebuildMixedTemplateArgListUnlocked(arguments),\n"
                "\t\t\t.kind = CanonicalTypeKind::TemplateSpecialization,\n"
                "\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t.array_extent = 1,"),
            "lost_specialization_argument_order": (
                "TypeId CanonicalTypeTable::rebuildMixedTemplateArgListUnlocked(std::span<const CanonicalTemplateArgument> arguments) {\n"
                "\t\tTypeId arg_link{};\n"
                "\t\tfor (size_t index = arguments.size(); index-- > 0;) {",
                "TypeId CanonicalTypeTable::rebuildMixedTemplateArgListUnlocked(std::span<const CanonicalTemplateArgument> arguments) {\n"
                "\t\tTypeId arg_link{};\n"
                "\t\tfor (size_t index = 0; index < arguments.size(); ++index) {"),
            "lost_nttp_spec_arg": (
                ".kind = CanonicalTypeKind::NonTypeTemplateArg,\n"
                "\t\t\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t\t\t.array_extent = argument.expr.value,",
                ".kind = CanonicalTypeKind::NonTypeTemplateArg,\n"
                "\t\t\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t\t\t.array_extent = 1,"),
            "lost_template_template_spec_arg": (
                ".kind = CanonicalTypeKind::TemplateTemplateArg,\n"
                "\t\t\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t\t\t.array_extent = argument.template_decl.value,",
                ".kind = CanonicalTypeKind::TemplateTemplateArg,\n"
                "\t\t\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t\t\t.array_extent = 1,"),
            "lost_dependent_template_template_spec_arg": (
                ".kind = CanonicalTypeKind::DependentTemplateTemplateArg,\n"
                "\t\t\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t\t\t.array_extent = packTemplateParameterExtent(\n"
                "\t\t\t\t\t\targument.template_decl,\n"
                "\t\t\t\t\t\targument.template_parameter_index),",
                ".kind = CanonicalTypeKind::DependentTemplateTemplateArg,\n"
                "\t\t\t\t\t.builtin = CanonicalBuiltinKind::Void,\n"
                "\t\t\t\t\t.qualifiers = CVQualifier::None,\n"
                "\t\t\t\t\t.flags = CanonicalTypeNodeFlags::None,\n"
                "\t\t\t\t\t.array_extent = 0,"),
            "lost_record_layout": (
                "return entity && record_layout_ids_.contains(entity.value);",
                "return entity && false;"),
            "lost_enum_layout": (
                "return entity && enum_layout_ids_.contains(entity.value);",
                "return entity && false;"),
            "lost_record_field_schema": (
                "return entity && record_field_schema_ids_.contains(entity.value);",
                "return entity && false;"),
            "lost_named_type_member_schema": (
                "return entity && named_type_member_schema_ids_.contains(entity.value);",
                "return entity && false;"),
            "lost_dependent_tip_resolve": (
                "if (nodeUnlocked(type).kind != CanonicalTypeKind::DependentName) {\n"
                "\t\t\treturn type;\n"
                "\t\t}",
                "if (nodeUnlocked(type).kind != CanonicalTypeKind::DependentName) {\n"
                "\t\t\treturn type;\n"
                "\t\t}\n"
                "\t\treturn type;"),
        }
        for name, (before, after) in mutations.items():
            before = deindent(before)
            after = deindent(after)
            if original.count(before) != 1:
                raise RuntimeError("mutation anchor changed: " + name)
            directory = OUTPUT / name
            directory.mkdir(parents=True, exist_ok=True)
            for sibling in ("CanonicalTypes.h", "CanonicalTypes.cpp",
                            "CanonicalTypeAdapter.h", "ArenaAccounting.h"):
                (directory / sibling).write_text((ROOT / "src" / sibling).read_text())
            (directory / IMPL.name).write_text(original.replace(before, after))
            jobs.append((name, directory, 1))
        for name, header, before, after in (
            ("adapter_dependent_name", "CanonicalTypeAdapter.h",
             "if (syntax.has_dependent_name_type()) {",
             "if (false && syntax.has_dependent_name_type()) {"),
            ("adapter_dependent_name_kind", "CanonicalTypeAdapter.h",
             "if (base_kind != CanonicalTypeKind::DependentName &&\n"
             "\t\t\tbase_kind != CanonicalTypeKind::DependentTemplateMember &&\n"
             "\t\t\tbase_kind != CanonicalTypeKind::DependentMemberAlias) {",
             "if (base_kind == CanonicalTypeKind::DependentName ||\n"
             "\t\t\tbase_kind == CanonicalTypeKind::DependentTemplateMember ||\n"
             "\t\t\tbase_kind == CanonicalTypeKind::DependentMemberAlias) {"),
            ("adapter_cv", "CanonicalTypeAdapter.h",
             "auto id = table.builtin(builtin);\n\tid = table.qualify(id, syntax.cv_qualifier());",
             "auto id = table.builtin(builtin);\n\tid = table.qualify(id, CVQualifier::None);"),
            ("adapter_array_order", "CanonicalTypeAdapter.h",
             "for (size_t index = dimensions.size(); index-- > first_dimension;)",
             "for (size_t index = first_dimension; index < dimensions.size(); ++index)"),
            ("adapter_array_binding", "CanonicalTypeAdapter.h",
             "auto id = table.builtin(builtin);\n"
             "\tid = table.qualify(id, syntax.cv_qualifier());\n"
             "\tid = applyCanonicalPointerArrayReference(\n"
             "\t\ttable, id, syntax, context, has_ordinary_array, has_pointee_array);",
             "auto id = table.builtin(builtin);\n"
             "\tid = table.qualify(id, syntax.cv_qualifier());\n"
             "\tid = applyCanonicalPointerArrayReference(\n"
             "\t\ttable, id, syntax, context, has_ordinary_array, false);"),
            ("adapter_parameter_decay", "CanonicalTypeAdapter.h",
             "has_ordinary_array && context == CanonicalTypeImportContext::FunctionParameter &&",
             "has_ordinary_array && context == CanonicalTypeImportContext::Exact &&"),
            ("adapter_function_pointer", "CanonicalTypeAdapter.h",
             "if (syntax.category() == TypeCategory::FunctionPointer || !syntax.pointer_levels().empty()) {",
             "if (false && (syntax.category() == TypeCategory::FunctionPointer || !syntax.pointer_levels().empty())) {"),
            ("adapter_function_param_decay", "CanonicalTypeAdapter.h",
             "if (context == CanonicalTypeImportContext::FunctionParameter &&\n"
             "\t\ttable.node(table.withoutTopLevelQualifiers(id)).kind == CanonicalTypeKind::Function) {",
             "if (false && context == CanonicalTypeImportContext::FunctionParameter &&\n"
             "\t\ttable.node(table.withoutTopLevelQualifiers(id)).kind == CanonicalTypeKind::Function) {"),
            ("adapter_enum", "CanonicalTypeAdapter.h",
             "if (syntax.category() == TypeCategory::Struct || syntax.category() == TypeCategory::Enum) {",
             "if (syntax.category() == TypeCategory::Struct) {"),
            ("adapter_unstructured_signature", "CanonicalTypeAdapter.h",
             "const CanonicalTypeImport imported_return = signature.hasStructuredTypes()\n"
             "\t\t? importCanonicalFunctionTypeComponent(\n"
             "\t\t\ttable, signature.return_type(), CanonicalTypeImportContext::Exact)\n"
             "\t\t: importCanonicalFunctionComponentFromProjection(\n"
             "\t\t\ttable,\n"
             "\t\t\tsignature.return_type_index,\n"
             "\t\t\tsignature.return_pointer_depth,\n"
             "\t\t\tsignature.return_reference_qualifier,\n"
             "\t\t\tCanonicalTypeImportContext::Exact);",
             "if (!signature.hasStructuredTypes()) {\n"
             "\t\treturn {{}, CanonicalTypeImportStatus::UnmigratedCallable};\n"
             "\t}\n"
             "\tconst CanonicalTypeImport imported_return = importCanonicalFunctionTypeComponent(\n"
             "\t\ttable, signature.return_type(), CanonicalTypeImportContext::Exact);"),
            ("adapter_dependent_noexcept", "CanonicalTypeAdapter.h",
             "if (signature.noexcept_expression.has_value() && !signature.dependent_noexcept) {\n"
             "\t\treturn {{}, CanonicalTypeImportStatus::Unresolved};\n"
             "\t}",
             "if (signature.dependent_noexcept) {\n"
             "\t\treturn {{}, CanonicalTypeImportStatus::Unresolved};\n"
             "\t}"),
            ("adapter_template_parameter", "CanonicalTypeAdapter.h",
             "if (!syntax.has_template_parameter_decl()) {\n"
             "\t\treturn {{}, CanonicalTypeImportStatus::Unresolved};\n"
             "\t}",
             "if (false && !syntax.has_template_parameter_decl()) {\n"
             "\t\treturn {{}, CanonicalTypeImportStatus::Unresolved};\n"
             "\t}"),
            ("adapter_template_specialization", "CanonicalTypeAdapter.h",
             "if (syntax.has_template_specialization()) {\n"
             "\t\tCanonicalTypeTransaction transaction(table);\n"
             "\t\tconst auto imported = importCanonicalTemplateSpecialization(table, syntax, context);",
             "if (false && syntax.has_template_specialization()) {\n"
             "\t\tCanonicalTypeTransaction transaction(table);\n"
             "\t\tconst auto imported = importCanonicalTemplateSpecialization(table, syntax, context);"),
            ("adapter_member_object_pointee", "CanonicalTypeAdapter.h",
             "const CanonicalTypeImport imported_pointee = importCanonicalTypeImpl(\n"
             "\t\t\ttable, syntax.member_object_pointee(), CanonicalTypeImportContext::Exact);",
             "const CanonicalTypeImport imported_pointee = {owner, CanonicalTypeImportStatus::Supported};"),
            ("aggregate_peak", "ArenaAccounting.h", "stats_.peak_bytes = stats_.current_bytes;",
             "stats_.peak_bytes += stats_.current_bytes;"),
        ):
            directory = OUTPUT / name
            directory.mkdir(parents=True, exist_ok=True)
            for sibling in ("CanonicalTypes.h", "CanonicalTypes.cpp",
                            "CanonicalTypeAdapter.h", "ArenaAccounting.h"):
                text = (ROOT / "src" / sibling).read_text()
                if sibling == header:
                    if text.count(before) != 1:
                        raise RuntimeError("mutation anchor changed: " + name)
                    text = text.replace(before, after)
                (directory / sibling).write_text(text)
            jobs.append((name, directory, 1))

        jobs.append(run_template_owner_tag_mutation())
        run_jobs(jobs, pristine, workers)


if __name__ == "__main__":
    main()
