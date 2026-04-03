// CompilerIncludes.h - shared umbrella header plus unity build source list
// In modular builds it provides common declarations only.
// In unity builds (UNITY_BUILD defined) it also pulls in the implementation .cpp files.
//
// Adding a new .cpp file? Just add an #include line below.
// Modular build manifests still need to know about the file.
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

#include "VariantUtils.h"
#include "StringTable.h"
#include "FileTree.h"
#include "FileReader.h"
#include "CompileContext.h"
#include "CommandLineParser.h"
#include "Lexer.h"
#include "StackString.h"
#include "IRTypes.h"
#include "CrashHandler.h"
#include "ObjFileWriter.h"
#include "Parser.h"
#include "IrGenerator.h"
#include "Log.h"
#include "NameMangling.h"

#include "TemplateProfilingStats.h"
#include "AstNodeTypes.h"
#include "NamespaceRegistry.h"
#include "LazyMemberResolver.h"
#include "InstantiationQueue.h"
#include "SemanticAnalysis.h"

// Only include ELF writer on non-Windows platforms
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include "ElfFileWriter.h"
#endif

#ifdef UNITY_BUILD
#if !defined(UNITY_SHARD_SUPPORT) && !defined(UNITY_SHARD_PARSER_CORE) && !defined(UNITY_SHARD_PARSER_TEMPLATES) && !defined(UNITY_SHARD_BACKEND)
#define UNITY_SHARD_SUPPORT
#define UNITY_SHARD_PARSER_CORE
#define UNITY_SHARD_PARSER_TEMPLATES
#define UNITY_SHARD_BACKEND
#define UNITY_ALL_SHARDS_DEFINED
#endif

#if defined(UNITY_SHARD_SUPPORT)
#include "FileReader_Core.cpp"
#include "FileReader_Macros.cpp"

// Shared globals (g_enable_debug_output, gNamespaceRegistry, etc.)
#include "Globals.cpp"

// Header implementation units kept as separate .cpp files for organization
#include "IrType.cpp"
#include "TypeTraitEvaluator.cpp"

// Library implementations
#include "AstNodeTypes.cpp"
#include "CodeViewDebug.cpp"
#include "ExpressionSubstitutor.cpp"
#include "ConstExprEvaluator_Core.cpp"
#include "ConstExprEvaluator_Members.cpp"
#include "SemanticAnalysis.cpp"
#endif

#if defined(UNITY_SHARD_PARSER_CORE)
// Parser implementation (split by C++20 grammar area)
#include "Parser_Core.cpp"					   // Includes, statics, token handling, infrastructure
#include "Parser_Decl_TopLevel.cpp"			// Top-level declarations
#include "Parser_Decl_DeclaratorCore.cpp"	  // Declarator parsing
#include "Parser_Decl_FunctionOrVar.cpp"		 // Function and variable declarations
#include "Parser_Decl_TypedefUsing.cpp"		// typedef/using declarations
#include "Parser_Decl_StructEnum.cpp"		  // struct/class/enum declarations
#include "Parser_TypeSpecifiers.cpp"			 // Type specifiers and qualifiers
#include "Parser_FunctionHeaders.cpp"		  // Function headers and parameter lists
#include "Parser_FunctionBodies.cpp"			 // Function bodies and delayed definitions
#include "Parser_Statements.cpp"				 // Blocks, statements, variable declarations, initializers
#include "Parser_Expr_PrimaryUnary.cpp"		// Primary/unary expressions
#include "Parser_Expr_BinaryPrecedence.cpp"	// Binary operator precedence parsing
#include "Parser_Expr_PostfixCalls.cpp"		// Postfix operators and calls
#include "Parser_Expr_PrimaryExpr.cpp"		   // Primary-expression helpers
#include "Parser_Expr_ControlFlowStmt.cpp"	   // Expression-level control flow helpers
#include "Parser_Expr_QualLookup.cpp"		  // Qualified lookup helpers
#endif

#if defined(UNITY_SHARD_PARSER_TEMPLATES)
#include "Parser_Templates_Class.cpp"				  // Template class parsing
#include "Parser_Templates_Params.cpp"			   // Template parameter parsing
#include "Parser_Templates_Function.cpp"			 // Template function parsing
#include "Parser_Templates_Variable.cpp"			 // Template variable parsing
#include "Parser_Templates_Concepts.cpp"			 // Concepts and requires expressions
#include "Parser_Templates_Inst_Deduction.cpp"	   // Template deduction
#include "Parser_Templates_Inst_Substitution.cpp"	  // Template substitution
#include "Parser_Templates_Inst_ClassTemplate.cpp"  // Class template instantiation
#include "Parser_Templates_Inst_MemberFunc.cpp"		// Member function template instantiation
#include "Parser_Templates_Lazy.cpp"				 // Lazy template flows
#include "Parser_Templates_MemberOutOfLine.cpp"		// Out-of-line template members
#include "Parser_Templates_Substitution.cpp"		 // Template substitution helpers
#endif

#if defined(UNITY_SHARD_BACKEND)
// AstToIr method definitions (for unity build)
#include "IrGenerator_Helpers.cpp"			   // AstToIr small utility helpers
#include "IrGenerator_Visitors_TypeInit.cpp"	 // AstToIr type construction/initialization visitors
#include "IrGenerator_Visitors_Decl.cpp"		 // AstToIr declaration visitors
#include "IrGenerator_Visitors_Namespace.cpp" // AstToIr namespace visitors
#include "IrGenerator_Stmt_Control.cpp"		// AstToIr control-flow statements
#include "IrGenerator_Stmt_TryCatchSeh.cpp"	// AstToIr EH / SEH statements
#include "IrGenerator_Stmt_Decl.cpp"			 // AstToIr declaration statements
#include "IrGenerator_Expr_Primitives.cpp"	   // AstToIr primitive expressions
#include "IrGenerator_Expr_Conversions.cpp"	// AstToIr conversion expressions
#include "IrGenerator_Expr_Operators.cpp"	  // AstToIr operators
#include "IrGenerator_Call_Direct.cpp"		   // AstToIr direct calls
#include "IrGenerator_Call_Indirect.cpp"		 // AstToIr indirect calls
#include "IrGenerator_MemberAccess.cpp"		// AstToIr member access
#include "IrGenerator_NewDeleteCast.cpp"		 // AstToIr new/delete/casts
#include "IrGenerator_Lambdas.cpp"			   // AstToIr lambda methods

// ObjFileWriter method definitions (for unity build)
#include "ObjFileWriter_Symbols.cpp"
#include "ObjFileWriter_Debug.cpp"
#include "ObjFileWriter_EH.cpp"
#include "ObjFileWriter_RTTI.cpp"

// ElfFileWriter method definitions (for unity build)
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include "ElfFileWriter_GlobalRTTI.cpp"
#include "ElfFileWriter_FuncSig.cpp"
#include "ElfFileWriter_EH.cpp"
#endif

#include "IRConverter_ConvertMain.cpp"
#endif

#ifdef UNITY_ALL_SHARDS_DEFINED
#undef UNITY_SHARD_SUPPORT
#undef UNITY_SHARD_PARSER_CORE
#undef UNITY_SHARD_PARSER_TEMPLATES
#undef UNITY_SHARD_BACKEND
#undef UNITY_ALL_SHARDS_DEFINED
#endif
#endif
