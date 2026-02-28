// Parser_Types.cpp — umbrella unity-build file
// Includes shard files in dependency order (each shard is a plain code fragment,
// no #pragma once / no includes of their own).

#include "Parser_TypeSpecifiers.cpp"    // get_builtin_type_info, parse_type_specifier, cv/ref qualifiers, decltype
#include "Parser_FunctionHeaders.cpp"   // parse_parameter_list, parse_function_arguments, parse_function_header, create_function_from_header
#include "Parser_FunctionBodies.cpp"    // parse_function_body_with_context, delayed body, parameter registration, validate_signature_match
