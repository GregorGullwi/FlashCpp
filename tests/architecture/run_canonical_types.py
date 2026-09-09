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
            "lost_dependent_qualifier": (
                ".child = qualifier,\n\t\t\t.kind = CanonicalTypeKind::DependentName,",
                ".child = TypeId{1},\n\t\t\t.kind = CanonicalTypeKind::DependentName,"),
            "lost_specialization_dependent_qualifier": (
                "kind == CanonicalTypeKind::TemplateParameter ||\n"
                "\t\t\tkind == CanonicalTypeKind::TemplateSpecialization ||\n"
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
                "TypeId memberObjectPointer(TypeId owner, TypeId pointee) {\n"
                "\t\tstd::lock_guard lock(mutex_);\n"
                "\t\tcheckTransactionThread();\n"
                "\t\tconst TypeId record_owner = recordOwnerUnlocked(owner);",
                "TypeId memberObjectPointer(TypeId owner, TypeId pointee) {\n"
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
                "memo.emplace(frame.id.value, args[index]);",
                "memo.emplace(frame.id.value, frame.id);"),
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
            if original.count(before) != 1:
                raise RuntimeError("mutation anchor changed: " + name)
            directory = OUTPUT / name
            directory.mkdir(parents=True, exist_ok=True)
            for header in ("CanonicalTypes.h", "CanonicalTypeAdapter.h", "ArenaAccounting.h"):
                (directory / header).write_text((ROOT / "src" / header).read_text())
            (directory / HEADER.name).write_text(original.replace(before, after))
            build_and_run(name, directory, 1)
        for name, header, before, after in (
            ("adapter_dependent_name", "CanonicalTypeAdapter.h",
             "if (syntax.has_dependent_name_type()) {",
             "if (false && syntax.has_dependent_name_type()) {"),
            ("adapter_dependent_name_kind", "CanonicalTypeAdapter.h",
             "if (base_kind != CanonicalTypeKind::DependentName &&\n"
             "\t\t\tbase_kind != CanonicalTypeKind::DependentTemplateMember) {",
             "if (base_kind == CanonicalTypeKind::DependentName ||\n"
             "\t\t\tbase_kind == CanonicalTypeKind::DependentTemplateMember) {"),
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
