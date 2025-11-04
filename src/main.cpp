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

// Global debug flag
bool g_enable_debug_output = false;

// Timing helper
struct PhaseTimer {
    std::chrono::high_resolution_clock::time_point start;
    const char* phase_name;
    bool enabled;

    PhaseTimer(const char* name, bool enable) : phase_name(name), enabled(enable) {
        if (enabled) {
            start = std::chrono::high_resolution_clock::now();
        }
    }

    ~PhaseTimer() {
        if (enabled) {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            printf("  %-20s: %8.3f ms\n", phase_name, duration.count() / 1000.0);
        }
    }
};

int main(int argc, char *argv[]) {
    auto total_start = std::chrono::high_resolution_clock::now();

    CompileContext context;
    CommandLineParser argsparser(argc, argv, context);

    if (argsparser.hasOption("h") || argsparser.hasOption("help")) {
        std::cout << "Help message" << std::endl;
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

    bool show_debug = argsparser.hasFlag("d") || argsparser.hasFlag("debug");
    bool show_perf_stats = argsparser.hasFlag("perf-stats") || argsparser.hasFlag("stats");
    bool show_timing = argsparser.hasFlag("time") || argsparser.hasFlag("timing") || show_perf_stats;

    // Set global debug flag
    g_enable_debug_output = show_debug;

    // Process input file arguments here...
    const auto& inputFileArgs = argsparser.inputFileArgs();
    if (inputFileArgs.empty()) {
            std::cerr << "No input file specified" << std::endl;
            return 1;
    }
    std::filesystem::path inputFilePath(inputFileArgs.front());
    inputFilePath = std::filesystem::absolute(inputFilePath);
    context.setInputFile(inputFilePath.string());

    // If no output file was specified, generate default output filename
    if (context.getOutputFile().empty()) {
        std::filesystem::path outputPath = inputFilePath;
        outputPath.replace_extension(".obj");
        context.setOutputFile(outputPath.string());
    }

    // Add the directory of the input source file as an implicit include directory
    std::filesystem::path inputDirPath = inputFilePath.parent_path();
    context.addIncludeDir(inputDirPath.string());

    if (show_timing) {
        printf("\n=== Compilation Timing ===\n");
#if USE_OLD_STRING_APPROACH
        printf("String approach: std::string (baseline)\n\n");
#else
        printf("String approach: StackString<32> (optimized)\n\n");
#endif
    }

    FileTree file_tree;
    FileReader file_reader(context, file_tree);
    {
        PhaseTimer timer("File I/O", show_timing);
        if (!file_reader.readFile(context.getInputFile().value())) {
            std::cerr << "Failed to read input file: " << context.getInputFile().value() << std::endl;
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

    if (context.isVerboseMode()) {
        // Use context and file_tree to perform the desired operation
        std::cout << "Output file: " << context.getOutputFile() << std::endl;
        std::cout << "Verbose mode: " << (context.isVerboseMode() ? "enabled" : "disabled") << std::endl;
        std::cout << "Input file: " << context.getInputFile().value() << std::endl;
        std::cout << "Dependencies:" << std::endl;
        for (const auto& dep : context.getDependencies()) {
            std::cout << "- " << dep << std::endl;
        }
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
        printf("Source lines: %zu\n", source_line_count);
        printf("Estimated operands: %zu (8 per line)\n", estimated_operands);
    }
#endif

    Lexer lexer(preprocessed_source);

    std::cerr << "===== FLASHCPP VERSION " << __DATE__ << " " << __TIME__ << " =====\n";
    std::cerr << "===== STDERR TEST - THIS SHOULD ALWAYS PRINT =====\n";

    Parser parser(lexer, context);
    {
        PhaseTimer timer("Lexing + Parsing", show_timing);
        auto parse_result = parser.parse();

        if (parse_result.is_error()) {
            std::cerr << "Error: " << parse_result.error_message() << std::endl;
            return 1;
        }
    }

    const auto& ast = parser.get_nodes();
    if (show_debug) {
        std::cerr << "DEBUG: After parsing, AST has " << ast.size() << " nodes\n";
    }

    AstToIr converter(gSymbolTable, context, parser);

    // Reserve space for IR instructions
    // Estimate: ~2 instructions per source line (empirical heuristic)
    // This accounts for variable declarations, expressions, control flow, etc.
    size_t estimated_instructions = source_line_count * 2;
    converter.reserveInstructions(estimated_instructions);

    if (show_perf_stats) {
        printf("Estimated instructions: %zu (2 per line)\n", estimated_instructions);
    }

    {
        PhaseTimer timer("AST to IR", show_timing);
        if (show_debug) {
            std::cerr << "DEBUG: Visiting " << ast.size() << " AST nodes\n";
            for (size_t i = 0; i < ast.size(); ++i) {
                const auto& node = ast[i];
                std::cerr << "  Node " << i << ": type=" << node.type_name();
                if (node.is<StructDeclarationNode>()) {
                    std::cerr << " (struct: " << node.as<StructDeclarationNode>().name() << ")";
                } else if (node.is<FunctionDeclarationNode>()) {
                    std::cerr << " (function: " << node.as<FunctionDeclarationNode>().decl_node().identifier_token().value() << ")";
                }
                std::cerr << "\n";
            }
        }
        for (auto& node_handle : ast) {
            if (show_debug && node_handle.is<FunctionDeclarationNode>()) {
                const auto& func = node_handle.as<FunctionDeclarationNode>();
                bool has_def = func.get_definition().has_value();
                std::cerr << "DEBUG: Visiting FunctionDeclarationNode: " << func.decl_node().identifier_token().value() 
                          << " has_definition=" << has_def << "\n";
                if (has_def) {
                    const BlockNode* def_block = *func.get_definition();
                    std::cerr << "  -> Block has " << def_block->get_statements().size() << " statements\n";
                }
            }
            converter.visit(node_handle);
        }

        // Generate all collected lambdas after visiting all nodes
        converter.generateCollectedLambdas();
        
        // Template instantiations now happen during parsing
        // converter.generateCollectedTemplateInstantiations();
    }

    const auto& ir = converter.getIr();

    std::cerr << "DEBUG: Verbose mode = " << (context.isVerboseMode() ? "true" : "false") << std::endl;
    if (context.isVerboseMode()) {
        std::cerr << "\n=== IR Instructions ===" << std::endl;
        for (const auto& instruction : ir.getInstructions()) {
            std::cerr << instruction.getReadableString() << std::endl;
        }
        std::cerr << "=== End IR ===" << std::endl << std::endl;
    }

    IrToObjConverter irConverter;

    try {
        PhaseTimer timer("Code Generation", show_timing);
        irConverter.convert(ir, context.getOutputFile(), context.getInputFile().value(), show_timing);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Code generation failed: " << e.what() << std::endl;
        if (show_timing) {
            auto total_end = std::chrono::high_resolution_clock::now();
            auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start);
            printf("  %-20s: %8.3f ms\n", "TOTAL", total_duration.count() / 1000.0);
            printf("==========================\n\n");
        }
        if (show_perf_stats) {
            StackStringStats::print_stats();
        }
        return 1;
    } catch (...) {
        std::cerr << "ERROR: Code generation failed with unknown exception" << std::endl;
        if (show_timing) {
            auto total_end = std::chrono::high_resolution_clock::now();
            auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start);
            printf("  %-20s: %8.3f ms\n", "TOTAL", total_duration.count() / 1000.0);
            printf("==========================\n\n");
        }
        if (show_perf_stats) {
            StackStringStats::print_stats();
        }
        return 1;
    }

    if (show_timing) {
        auto total_end = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start);
        printf("  %-20s: %8.3f ms\n", "TOTAL", total_duration.count() / 1000.0);
        printf("==========================\n\n");
    }

    if (show_perf_stats) {
        StackStringStats::print_stats();

#ifdef USE_GLOBAL_OPERAND_STORAGE
        printf("\n");  // Add spacing
        GlobalOperandStorage::instance().printStats();
#else
        printf("\nNote: Chunked operand storage is disabled. Enable USE_GLOBAL_OPERAND_STORAGE to see operand stats.\n\n");
#endif

        // Print IR instruction statistics
        ir.printStats();
    }

    return 0;
}
