// FlashCppUnity.h - Unity build file for FlashCpp compiler
// Include this single file in main.cpp or test files to compile
// the entire compiler as one translation unit.
//
// Adding a new .cpp file? Just add an #include line below.
// No need to edit the Makefile or .vcxproj files.
#pragma once

// Shared globals (g_enable_debug_output, gNamespaceRegistry, etc.)
#include "Globals.cpp"

// Library implementations
#include "AstNodeTypes.cpp"
#include "ChunkedAnyVector.cpp"
#include "CodeViewDebug.cpp"
#include "ExpressionSubstitutor.cpp"

// Parser implementation (split by C++20 grammar area)
#include "Parser_Core.cpp"          // Includes, statics, token handling, infrastructure
#include "Parser_Declarations.cpp"  // Top-level, declarations, struct/class/enum, namespace, using
#include "Parser_Types.cpp"         // Type specifiers, qualifiers, function headers/bodies
#include "Parser_Statements.cpp"    // Blocks, statements, variable declarations, initializers
#include "Parser_Expressions.cpp"   // Expressions, control flow, lambdas, utilities
#include "Parser_Templates.cpp"     // Templates, concepts, requires, instantiation
