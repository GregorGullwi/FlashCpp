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

#include "FileTree.h"
#include "FileReader.h"
#include "CompileContext.h"
#include "CommandLineParser.h"
#include "Lexer.h"
#include "Parser.h"
// #include "LibClangIRGenerator.h"  // Disabled for now due to LLVM dependency
#include "CodeGen.h"
#include "StackString.h"
#include "IRTypes.h"
#include "CrashHandler.h"
#include "Log.h"
#include "ObjFileWriter.h"
#include "NameMangling.h"

// Only include ELF writer on non-Windows platforms
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include "ElfFileWriter.h"
#endif

// Global debug flag
bool g_enable_debug_output = false;

// Timing helper
struct PhaseTimer {
    std::chrono::high_resolution_clock::time_point start;
    const char* phase_name;
    bool print_enabled;
    double* accumulator = nullptr;  // Optional accumulator for phase timing

    PhaseTimer(const char* name, bool print_enable, double* accum = nullptr)
        : phase_name(name), print_enabled(print_enable), accumulator(accum) {
        // Always start timing
        start = std::chrono::high_resolution_clock::now();
    }

    ~PhaseTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double ms = duration.count() / 1000.0;

        if (accumulator) {
            *accumulator += ms;
        }

        if (print_enabled) {
            FLASH_LOG(General, Info, "  ", phase_name, ": ", std::fixed, std::setprecision(3), ms, " ms\n");
        }
    }
};

// Helper function to print timing summary
void printTimingSummary(double preprocessing_time, double parsing_time, double ir_conversion_time,
                       double template_time, double codegen_time,
                       std::chrono::high_resolution_clock::time_point total_start) {
    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start);
    double total_ms = total_duration.count() / 1000.0;

    FLASH_LOG(General, Info, "\n=== Compilation Timing ===");

    // Calculate percentages
    double prep_pct = (preprocessing_time / total_ms) * 100.0;
    double parse_pct = (parsing_time / total_ms) * 100.0;
    double ir_pct = (ir_conversion_time / total_ms) * 100.0;
    double temp_pct = (template_time / total_ms) * 100.0;
    double code_pct = (codegen_time / total_ms) * 100.0;

    // Print table header
    FLASH_LOG(General, Info, "Phase            | Time (ms)  | Percentage");
    FLASH_LOG(General, Info, "-----------------|------------|-----------");

    // Print each phase
    FLASH_LOG(General, Info, "Preprocessing    | ", std::fixed, std::setprecision(3), std::setw(10), preprocessing_time, " | ", std::setw(9), prep_pct, "%");
    FLASH_LOG(General, Info, "Parsing          | ", std::fixed, std::setprecision(3), std::setw(10), parsing_time, " | ", std::setw(9), parse_pct, "%");
    FLASH_LOG(General, Info, "IR Conversion    | ", std::fixed, std::setprecision(3), std::setw(10), ir_conversion_time, " | ", std::setw(9), ir_pct, "%");
    FLASH_LOG(General, Info, "Template Inst    | ", std::fixed, std::setprecision(3), std::setw(10), template_time, " | ", std::setw(9), temp_pct, "%");
    FLASH_LOG(General, Info, "Code Generation  | ", std::fixed, std::setprecision(3), std::setw(10), codegen_time, " | ", std::setw(9), code_pct, "%");
    FLASH_LOG(General, Info, "-----------------|------------|-----------");
    FLASH_LOG(General, Info, "TOTAL            | ", std::fixed, std::setprecision(3), std::setw(10), total_ms, " | ", std::setw(9), 100.0, "%");
    FLASH_LOG(General, Info, "\n");
}

// Helper function to set mangling style in both CompileContext and NameMangling namespace
static void setManglingStyle(CompileContext& context, CompileContext::ManglingStyle style) {
    context.setManglingStyle(style);
    // Sync with NameMangling global (enum values match by design)
    NameMangling::g_mangling_style = static_cast<NameMangling::ManglingStyle>(static_cast<int>(style));
}

int main(int argc, char *argv[]) {
    // Install crash handler for automatic crash logging with stack traces
    CrashHandler::install();

    auto total_start = std::chrono::high_resolution_clock::now();

    CompileContext context;
    CommandLineParser argsparser(argc, argv, context);

    // Helper functions for parsing log levels and categories
    auto parseLevel = [](std::string_view sv) -> FlashCpp::LogLevel {
        if (sv == "error" || sv == "0") return FlashCpp::LogLevel::Error;
        if (sv == "warning" || sv == "1") return FlashCpp::LogLevel::Warning;
        if (sv == "info" || sv == "2") return FlashCpp::LogLevel::Info;
        if (sv == "debug" || sv == "3") return FlashCpp::LogLevel::Debug;
        if (sv == "trace" || sv == "4") return FlashCpp::LogLevel::Trace;
        return FlashCpp::LogLevel::Info; // default
    };

    auto parseCategory = [](std::string_view sv) -> FlashCpp::LogCategory {
        if (sv == "General") return FlashCpp::LogCategory::General;
        if (sv == "Parser") return FlashCpp::LogCategory::Parser;
        if (sv == "Lexer") return FlashCpp::LogCategory::Lexer;
        if (sv == "Templates") return FlashCpp::LogCategory::Templates;
        if (sv == "Symbols") return FlashCpp::LogCategory::Symbols;
        if (sv == "Types") return FlashCpp::LogCategory::Types;
        if (sv == "Codegen") return FlashCpp::LogCategory::Codegen;
        if (sv == "Scope") return FlashCpp::LogCategory::Scope;
        if (sv == "Mangling") return FlashCpp::LogCategory::Mangling;
        if (sv == "All") return FlashCpp::LogCategory::All;
        return FlashCpp::LogCategory::General; // default
    };

    // Handle log level setting from command line
    if (argsparser.hasOption("log-level")) {
        auto level_str = argsparser.optionValue("log-level");
        if (std::holds_alternative<std::string_view>(level_str)) {
            std::string_view level_sv = std::get<std::string_view>(level_str);
            size_t colon_pos = level_sv.find(':');
            if (colon_pos != std::string_view::npos) {
                // Category-specific: category:level
                std::string_view cat_sv = level_sv.substr(0, colon_pos);
                std::string_view lev_sv = level_sv.substr(colon_pos + 1);
                FlashCpp::LogCategory cat = parseCategory(cat_sv);
                FlashCpp::LogLevel level = parseLevel(lev_sv);
                // Check if category is enabled at compile time
                if ((static_cast<uint32_t>(cat) & FLASHCPP_LOG_CATEGORIES) != 0 || cat == FlashCpp::LogCategory::General) {
                    FlashCpp::LogConfig::setLevel(cat, level);
                    FLASH_LOG(General, Info, "Set log level for category ", cat_sv, " to ", lev_sv);
                } else {
                    FLASH_LOG(General, Error, "Cannot set log level for category ", cat_sv, ": category disabled at compile time");
                }
            } else {
                // Global level
                FlashCpp::LogLevel level = parseLevel(level_sv);
                FlashCpp::LogConfig::setLevel(level);
                FLASH_LOG(General, Info, "Set global log level to ", level_sv);
            }
        }
    }

    if (argsparser.hasOption("h") || argsparser.hasOption("help")) {
        FLASH_LOG(General, Info, "Help message\n");
        return 0;
    }

    if (argsparser.hasOption("o")) {
        auto output_file = argsparser.optionValue("o");
        if (std::holds_alternative<std::string_view>(output_file))
            context.setOutputFile(std::get<std::string_view>(output_file));
    }

    context.setVerboseMode(argsparser.hasFlag("v") || argsparser.hasFlag("verbose"));
    context.setPreprocessorOnlyMode(argsparser.hasFlag("E"));
    context.setDisableAccessControl(argsparser.hasFlag("fno-access-control") || argsparser.hasFlag("no-access-control"));
    
    // Compiler mode - default is MSVC, use -fgcc-compat or -fclang-compat for GCC/Clang mode
    // Enables compiler-specific builtin macros like __SIZE_TYPE__, __PTRDIFF_TYPE__, etc.
    if (argsparser.hasFlag("fgcc-compat") || argsparser.hasFlag("fclang-compat")) {
        context.setCompilerMode(CompileContext::CompilerMode::GCC);
    }

    // Name mangling style - auto-detected by platform but can be overridden
    // for cross-compilation support
    if (argsparser.hasOption("fmangling")) {
        auto mangling_opt = argsparser.optionValue("fmangling");
        if (std::holds_alternative<std::string_view>(mangling_opt)) {
            std::string_view mangling_str = std::get<std::string_view>(mangling_opt);
            FLASH_LOG(General, Info, "Using name mangling style: ", mangling_str);
            if (mangling_str == "msvc") {
                setManglingStyle(context, CompileContext::ManglingStyle::MSVC);
            } else if (mangling_str == "itanium") {
                setManglingStyle(context, CompileContext::ManglingStyle::Itanium);
            } else {
                FLASH_LOG(General, Warning, "Unknown mangling style: ", mangling_str, " (use 'msvc' or 'itanium')");
            }
        }
    } else {
        // Auto-detect based on platform if not specified
        #if defined(_WIN32) || defined(_WIN64)
            setManglingStyle(context, CompileContext::ManglingStyle::MSVC);
            FLASH_LOG(General, Debug, "Auto-detected name mangling style: MSVC (Windows)");
        #else
            setManglingStyle(context, CompileContext::ManglingStyle::Itanium);
            FLASH_LOG(General, Debug, "Auto-detected name mangling style: Itanium (Linux/Unix)");
        #endif
    }

    bool show_debug = argsparser.hasFlag("d") || argsparser.hasFlag("debug");
    bool show_perf_stats = argsparser.hasFlag("perf-stats") || argsparser.hasFlag("stats");
    bool show_timing = argsparser.hasFlag("time") || argsparser.hasFlag("timing") || show_perf_stats;

    // Always show basic timing information (total time + breakdown)
    bool show_basic_timing = true;

    // Set global debug flag (also enabled by verbose mode)
    g_enable_debug_output = show_debug || context.isVerboseMode();

    // Process input file arguments here...
    const auto& inputFileArgs = argsparser.inputFileArgs();
    if (inputFileArgs.empty()) {
            FLASH_LOG(General, Error, "No input file specified\n");
            return 1;
    }
    std::filesystem::path inputFilePath(inputFileArgs.front());
    inputFilePath = std::filesystem::absolute(inputFilePath);
    context.setInputFile(inputFilePath.string());

    // If no output file was specified, generate default output filename
    if (context.getOutputFile().empty()) {
        std::filesystem::path outputPath = inputFilePath;
        outputPath.replace_extension(".obj");
        // Strip directory - output to current directory
        outputPath = outputPath.filename();
        context.setOutputFile(outputPath.string());
    }

    // Add the directory of the input source file as an implicit include directory
    std::filesystem::path inputDirPath = inputFilePath.parent_path();
    context.addIncludeDir(inputDirPath.string());

    // Add system include directory for standard library headers
    // The include directory is located relative to the executable
    #if defined(_WIN32) || defined(_WIN64)
        // On Windows, the executable is in x64/Debug/, so we need to go up two levels
        std::filesystem::path execPath = std::filesystem::canonical(std::filesystem::path(__FILE__).parent_path());
        std::filesystem::path includeDir = execPath.parent_path() / "include";
    #else
        // On Linux/Unix, use /proc/self/exe
        std::filesystem::path execPath = std::filesystem::canonical("/proc/self/exe");
        std::filesystem::path includeDir = execPath.parent_path().parent_path().parent_path() / "include";
    #endif
    if (std::filesystem::exists(includeDir)) {
        context.addIncludeDir(includeDir.string());
    }

    // Collect timing data silently
    double preprocessing_time = 0.0, parsing_time = 0.0, ir_conversion_time = 0.0, template_time = 0.0, codegen_time = 0.0;

    FileTree file_tree;
    FileReader file_reader(context, file_tree);
    {
        PhaseTimer timer("Preprocessing", false, &preprocessing_time);
        if (!file_reader.readFile(context.getInputFile().value())) {
            FLASH_LOG(General, Error, "Failed to read input file: ", context.getInputFile().value());
            return 1;
        }
    }

    // Copy dependencies from FileTree to CompileContext for later use
    for (const auto& dep : file_tree.getAllDependencies()) {
        context.addDependency(dep);
    }

    // If preprocessor-only mode, we're done - the preprocessor already output the result
    if (context.isPreprocessorOnlyMode()) {
        return 0;
    }

    FLASH_LOG(General, Debug, "Verbose mode = ", (context.isVerboseMode() ? "true" : "false"));
    if (context.isVerboseMode()) {
        // Use context and file_tree to perform the desired operation
        FLASH_LOG(General, Debug, "Output file: ", context.getOutputFile());
        FLASH_LOG(General, Debug, "Verbose mode: ", (context.isVerboseMode() ? "enabled" : "disabled"));
        FLASH_LOG(General, Debug, "Input file: ", context.getInputFile().value());
    }

    const std::string& preprocessed_source = file_reader.get_result();

    // Count source lines for operand storage reservation
    size_t source_line_count = std::count(preprocessed_source.begin(), preprocessed_source.end(), '\n');

#ifdef USE_CHUNKED_OPERAND_STORAGE
    // Reserve space in global operand storage
    // Estimate: ~8 operands per source line (empirical heuristic)
    // This accounts for complex expressions, function calls, and temporary values
    size_t estimated_operands = source_line_count * 8;
    GlobalOperandStorage::instance().reserve(estimated_operands);

    if (show_perf_stats) {
        FLASH_LOG(General, Debug, "Source lines: ", source_line_count);
        FLASH_LOG(General, Debug, "Estimated operands: ", estimated_operands, " (8 per line)");
    }
#endif

    FLASH_LOG(General, Info, "===== FLASHCPP VERSION ", __DATE__, " " , __TIME__, " =====");
#if USE_OLD_STRING_APPROACH
    FLASH_LOG(General, Debug, "String approach: std::string (baseline)");
#else
    FLASH_LOG(General, Debug, "String approach: StackString<32> (optimized)");
#endif

    Lexer lexer(preprocessed_source, file_reader.get_line_map(), file_reader.get_file_paths());
    Parser parser(lexer, context);
    {
        PhaseTimer timer("Parsing", false, &parsing_time);
        // Note: Lexing happens lazily during parsing in this implementation
        auto parse_result = parser.parse();

        if (parse_result.is_error()) {
            // Print formatted error with file:line:column information and include stack
            FLASH_LOG(Parser, Info, parse_result.format_error(lexer.file_paths(), file_reader.get_line_map(), &lexer));
            return 1;
        }
    }

    const auto& ast = parser.get_nodes();
    FLASH_LOG(Parser, Debug, "After parsing, AST has ", ast.size(), " nodes\n");

    AstToIr converter(gSymbolTable, context, parser);

    // Reserve space for IR instructions
    // Estimate: ~2 instructions per source line (empirical heuristic)
    // This accounts for variable declarations, expressions, control flow, etc.
    size_t estimated_instructions = source_line_count * 2;
    converter.reserveInstructions(estimated_instructions);

    if (show_perf_stats) {
        FLASH_LOG(General, Info, "Estimated instructions: ", estimated_instructions, " (2 per line)");
    }

    if (show_debug) {
        FLASH_LOG(Codegen, Debug, "Visiting ", ast.size(), " AST nodes\n");
        for (size_t i = 0; i < ast.size(); ++i) {
            const auto& node = ast[i];
            FLASH_LOG(Codegen, Debug, "  Node ", i, ": type=", node.type_name());
            if (node.is<StructDeclarationNode>()) {
                FLASH_LOG(Codegen, Debug, " (struct: ", node.as<StructDeclarationNode>().name(), ")");
            } else if (node.is<FunctionDeclarationNode>()) {
                FLASH_LOG(Codegen, Debug, " (function: ", node.as<FunctionDeclarationNode>().decl_node().identifier_token().value(), ")");
            }
            FLASH_LOG(Codegen, Debug, "\n");
        }
    }

    // IR conversion (visiting AST nodes)
    {
        PhaseTimer ir_timer("IR Conversion", false, &ir_conversion_time);
        for (auto& node_handle : ast) {
            if (show_debug && node_handle.is<FunctionDeclarationNode>()) {
                const auto& func = node_handle.as<FunctionDeclarationNode>();
                bool has_def = func.get_definition().has_value();
                FLASH_LOG(Codegen, Debug, "Visiting FunctionDeclarationNode: ", func.decl_node().identifier_token().value(),
                          " has_definition=", has_def);
                if (has_def) {
                    const BlockNode& def_block = func.get_definition().value().as<BlockNode>();
                    FLASH_LOG(Codegen, Debug, "  -> Block has ", def_block.get_statements().size(), " statements");
                }
            }
            converter.visit(node_handle);
        }
    }

    // Template instantiation (if any remaining)
    {
        PhaseTimer template_timer("Template Inst", false, &template_time);
        // Generate all collected lambdas after visiting all nodes
        converter.generateCollectedLambdas();

        // Generate all collected local struct member functions after visiting all nodes
        converter.generateCollectedLocalStructMembers();

        // Template instantiations now happen during parsing
        // converter.generateCollectedTemplateInstantiations();
    }

    const auto& ir = converter.getIr();

    if (FLASH_LOG_ENABLED(Codegen, Debug)) {
        FLASH_LOG(Codegen, Debug, "\n=== IR Instructions ===\n");
        for (const auto& instruction : ir.getInstructions()) {
           FLASH_LOG(Codegen, Debug, instruction.getReadableString());
        }
        FLASH_LOG(Codegen, Debug, "=== End IR ===\n\n");
    }

    // Platform detection: Use ELF on Linux/Unix, COFF on Windows
    // This can be overridden with command-line flags in the future
    bool useElfFormat = false;
    #if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        useElfFormat = true;
    #elif defined(_WIN32) || defined(_WIN64)
        useElfFormat = false;
    #endif

    try {
        PhaseTimer timer("Code Generation", false, &codegen_time);
        
        #if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        if (useElfFormat) {
            // Use ELF format (Linux/Unix)
            FLASH_LOG(Codegen, Info, "Generating ELF object file (Linux/Unix target)");
            IrToObjConverter<ElfFileWriter> irConverter;
            irConverter.convert(ir, context.getOutputFile(), context.getInputFile().value(), show_timing);
        } else
        #endif
        {
            // Use COFF format (Windows)
            FLASH_LOG(Codegen, Info, "Generating COFF object file (Windows target)");
            IrToObjConverter<ObjectFileWriter> irConverter;
            irConverter.convert(ir, context.getOutputFile(), context.getInputFile().value(), show_timing);
        }
    } catch (const std::exception& e) {
        FLASH_LOG(General, Error, "Code generation failed: ", e.what());
        printTimingSummary(preprocessing_time, parsing_time, ir_conversion_time, template_time, codegen_time, total_start);
        if (show_perf_stats) {
            StackStringStats::print_stats();
        }
        return 1;
    } catch (...) {
        FLASH_LOG(General, Error, "Code generation failed with unknown exception");
        printTimingSummary(preprocessing_time, parsing_time, ir_conversion_time, template_time, codegen_time, total_start);
        if (show_perf_stats) {
            StackStringStats::print_stats();
        }
        return 1;
    }

    // Print final timing summary
    printTimingSummary(preprocessing_time, parsing_time, ir_conversion_time, template_time, codegen_time, total_start);

    // Show additional details if --time flag is used
    if (show_timing) {
        FLASH_LOG(General, Info, "Phase Details:");
        FLASH_LOG(General, Info, "  Parsing includes: lexing + template instantiation from parsing phase");
        FLASH_LOG(General, Info, "  Template Inst: lambda/struct member function generation");
    }

    if (show_perf_stats) {
        StackStringStats::print_stats();

#ifdef USE_GLOBAL_OPERAND_STORAGE
        FLASH_LOG(General, Info, "\n");
        GlobalOperandStorage::instance().printStats();
#else
        FLASH_LOG(General, Info, "\nNote: Chunked operand storage is disabled. Enable USE_GLOBAL_OPERAND_STORAGE to see operand stats.\n\n");
#endif

        // Print IR instruction statistics
        ir.printStats();
    }

    return 0;
}
