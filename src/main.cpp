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

    bool show_perf_stats = argsparser.hasFlag("perf-stats") || argsparser.hasFlag("stats");
    bool show_timing = argsparser.hasFlag("time") || argsparser.hasFlag("timing") || show_perf_stats;

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

    Lexer lexer(file_reader.get_result());

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

    AstToIr converter(gSymbolTable, context);
    {
        PhaseTimer timer("AST to IR", show_timing);
        for (auto& node_handle : ast) {
            converter.visit(node_handle);
        }
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
    }

    return 0;
}
