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

    FLASH_LOG(General, Info, "\n=== Compilation Timing ===\n");
#if USE_OLD_STRING_APPROACH
    FLASH_LOG(General, Info, "String approach: std::string (baseline)\n\n");
#else
    FLASH_LOG(General, Info, "String approach: StackString<32> (optimized)\n\n");
#endif

    FLASH_LOG(General, Info, "  Preprocessing: ", std::fixed, std::setprecision(3), preprocessing_time, " ms\n");
    FLASH_LOG(General, Info, "  Parsing: ", std::fixed, std::setprecision(3), parsing_time, " ms\n");
    FLASH_LOG(General, Info, "  IR Conversion: ", std::fixed, std::setprecision(3), ir_conversion_time, " ms\n");
    FLASH_LOG(General, Info, "  Template Inst: ", std::fixed, std::setprecision(3), template_time, " ms\n");
    FLASH_LOG(General, Info, "  Code Generation: ", std::fixed, std::setprecision(3), codegen_time, " ms\n");
    FLASH_LOG(General, Info, "  TOTAL: ", std::fixed, std::setprecision(3), total_ms, " ms\n");
    FLASH_LOG(General, Info, "==========================\n\n");
}

int main(int argc, char *argv[]) {
    // Install crash handler for automatic crash logging with stack traces
    CrashHandler::install();

    auto total_start = std::chrono::high_resolution_clock::now();

    CompileContext context;
    CommandLineParser argsparser(argc, argv, context);

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

    // Collect timing data silently
    double preprocessing_time = 0.0, parsing_time = 0.0, ir_conversion_time = 0.0, template_time = 0.0, codegen_time = 0.0;

    FileTree file_tree;
    FileReader file_reader(context, file_tree);
    {
        PhaseTimer timer("Preprocessing", false, &preprocessing_time);
        if (!file_reader.readFile(context.getInputFile().value())) {
            FLASH_LOG(General, Error, "Failed to read input file: ", context.getInputFile().value(), "\n");
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

    FLASH_LOG(General, Debug, "Verbose mode = ", (context.isVerboseMode() ? "true\n" : "false\n"));
    if (context.isVerboseMode()) {
        // Use context and file_tree to perform the desired operation
        FLASH_LOG(General, Debug, "Output file: ", context.getOutputFile(), "\n");
        FLASH_LOG(General, Debug, "Verbose mode: ", (context.isVerboseMode() ? "enabled" : "disabled"), "\n");
        FLASH_LOG(General, Debug, "Input file: ", context.getInputFile().value(), "\n");
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
        FLASH_LOG(General, Debug, "Source lines: ", source_line_count, "\n");
        FLASH_LOG(General, Debug, "Estimated operands: ", estimated_operands, " (8 per line)\n");
    }
#endif

    FLASH_LOG(General, Info, "===== FLASHCPP VERSION ", __DATE__, " " , __TIME__, " =====\n");

    Lexer lexer(preprocessed_source, file_reader.get_line_map(), file_reader.get_file_paths());
    Parser parser(lexer, context);
    {
        PhaseTimer timer("Parsing", false, &parsing_time);
        // Note: Lexing happens lazily during parsing in this implementation
        auto parse_result = parser.parse();

        if (parse_result.is_error()) {
            // Print formatted error with file:line:column information and include stack
            FLASH_LOG(Parser, Info, parse_result.format_error(lexer.file_paths(), file_reader.get_line_map(), &lexer), "\n");
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
        FLASH_LOG(General, Info, "Estimated instructions: ", estimated_instructions, " (2 per line)\n");
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
                          " has_definition=", has_def, "\n");
                if (has_def) {
                    const BlockNode& def_block = func.get_definition().value().as<BlockNode>();
                    FLASH_LOG(Codegen, Debug, "  -> Block has ", def_block.get_statements().size(), " statements\n");
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

    if (context.isVerboseMode()) {
        FLASH_LOG(Codegen, Debug, "\n=== IR Instructions ===\n");
        for (const auto& instruction : ir.getInstructions()) {
           FLASH_LOG(Codegen, Debug, instruction.getReadableString(), "\n");
        }
        FLASH_LOG(Codegen, Debug, "=== End IR ===\n\n");
    }

    IrToObjConverter irConverter;

    try {
        PhaseTimer timer("Code Generation", false, &codegen_time);
        irConverter.convert(ir, context.getOutputFile(), context.getInputFile().value(), show_timing);
    } catch (const std::exception& e) {
        FLASH_LOG(General, Error, "Code generation failed: ", e.what(), "\n");
        printTimingSummary(preprocessing_time, parsing_time, ir_conversion_time, template_time, codegen_time, total_start);
        if (show_perf_stats) {
            StackStringStats::print_stats();
        }
        return 1;
    } catch (...) {
        FLASH_LOG(General, Error, "Code generation failed with unknown exception\n");
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
        FLASH_LOG(General, Info, "Phase Details:\n");
        FLASH_LOG(General, Info, "  Parsing includes: lexing + template instantiation from parsing phase\n");
        FLASH_LOG(General, Info, "  Template Inst: lambda/struct member function generation\n");
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
