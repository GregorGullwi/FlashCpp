// Parser_Templates.cpp - Unity umbrella for template-related parsing
// This file includes all template parsing shards in sequence as a single translation unit.
//
// Shard layout:
//   Parser_Templates_Class.cpp          - parse_bitfield_width, parse_template_declaration,
//                                          parse_member_struct_template
//   Parser_Templates_Params.cpp         - parse_template_parameter_list, parse_template_parameter,
//                                          parse_template_template_parameter_forms/form,
//                                          parse_explicit_template_arguments, could_be_template_arguments,
//                                          parse_qualified_identifier_with_templates
//   Parser_Templates_Function.cpp       - parse_template_function_declaration_body,
//                                          parse_member_function_template,
//                                          parse_member_template_or_function,
//                                          try_evaluate_constant_expression
//   Parser_Templates_Variable.cpp       - parse_member_template_alias, parse_member_variable_template
//   Parser_Templates_Concepts.cpp       - concept declaration, requires_expression,
//                                          constexpr checks for constraints
//   Parser_Templates_Instantiation.cpp  - try_instantiate_*, instantiate_*, explicit instantiation paths
//   Parser_Templates_Lazy.cpp           - instantiateLazy*, evaluateLazyTypeAlias, phase-based/lazy flows
//   Parser_Templates_MemberOutOfLine.cpp - out-of-line template member parsing helpers
//   Parser_Templates_Substitution.cpp   - substituteTemplateParameters, expression substitution,
//                                          pack expansion, name helpers

#include "Parser_Templates_Class.cpp"
#include "Parser_Templates_Params.cpp"
#include "Parser_Templates_Function.cpp"
#include "Parser_Templates_Variable.cpp"
#include "Parser_Templates_Concepts.cpp"
#include "Parser_Templates_Instantiation.cpp"
#include "Parser_Templates_Lazy.cpp"
#include "Parser_Templates_MemberOutOfLine.cpp"
#include "Parser_Templates_Substitution.cpp"
