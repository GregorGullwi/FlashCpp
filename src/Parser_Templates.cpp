// Parser_Templates.cpp - Unity umbrella for template-related parsing
// This file includes all template parsing shards in sequence as a single translation unit.
//
// Shard layout:
//   Parser_Templates_Decls.cpp          - template declaration parsing, parameter list/forms,
//                                          explicit template args, member template declaration parsing
//   Parser_Templates_Concepts.cpp       - concept declaration, requires_expression,
//                                          constexpr checks for constraints
//   Parser_Templates_Instantiation.cpp  - try_instantiate_*, instantiate_*, explicit instantiation paths
//   Parser_Templates_Lazy.cpp           - instantiateLazy*, evaluateLazyTypeAlias, phase-based/lazy flows
//   Parser_Templates_MemberOutOfLine.cpp - out-of-line template member parsing helpers
//   Parser_Templates_Substitution.cpp   - substituteTemplateParameters, expression substitution,
//                                          pack expansion, name helpers

#include "Parser_Templates_Decls.cpp"
#include "Parser_Templates_Concepts.cpp"
#include "Parser_Templates_Instantiation.cpp"
#include "Parser_Templates_Lazy.cpp"
#include "Parser_Templates_MemberOutOfLine.cpp"
#include "Parser_Templates_Substitution.cpp"
