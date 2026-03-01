// FlashCppUnity.h - Unity build file for FlashCpp compiler
// Include this single file in main.cpp or test files to compile
// the entire compiler as one translation unit.
//
// Adding a new .cpp file? Just add an #include line below.
// No need to edit the Makefile or .vcxproj files.
#pragma once

// Prevent Windows from defining min/max macros that conflict with std::min/std::max
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <string>
#include <string_view>
#include <optional>
#include <variant>
#include <map>
#include <vector>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <iomanip>

#include "StringTable.h"
#include "FileTree.h"
#include "FileReader.h"
#include "FileReader_Core.cpp"
#include "FileReader_Macros.cpp"
#include "CompileContext.h"
#include "CommandLineParser.h"
#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "StackString.h"
#include "IRTypes.h"
#include "CrashHandler.h"
#include "Log.h"
#include "ObjFileWriter.h"
#include "NameMangling.h"
#include "TemplateProfilingStats.h"
#include "AstNodeTypes.h"
#include "NamespaceRegistry.h"
#include "LazyMemberResolver.h"
#include "InstantiationQueue.h"

// Only include ELF writer on non-Windows platforms
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include "ElfFileWriter.h"
#endif

// Shared globals (g_enable_debug_output, gNamespaceRegistry, etc.)
#include "Globals.cpp"

// Library implementations
#include "AstNodeTypes.cpp"
#include "CodeViewDebug.cpp"
#include "ExpressionSubstitutor.cpp"

// Parser implementation (split by C++20 grammar area)
#include "Parser_Core.cpp"          // Includes, statics, token handling, infrastructure
#include "Parser_Declarations.cpp"  // Top-level, declarations, struct/class/enum, namespace, using
#include "Parser_Types.cpp"         // Type specifiers, qualifiers, function headers/bodies
#include "Parser_Statements.cpp"    // Blocks, statements, variable declarations, initializers
#include "Parser_Expressions.cpp"   // Expressions, control flow, lambdas, utilities
#include "Parser_Templates.cpp"     // Templates, concepts, requires, instantiation
