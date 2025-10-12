#include <iostream>
#include <string>
#include <string_view>
#include <optional>
#include <variant>
#include <map>
#include <vector>
#include <filesystem>

#include "FileTree.h"
#include "FileReader.h"
#include "CompileContext.h"
#include "CommandLineParser.h"
#include "Lexer.h"
#include "Parser.h"
// #include "LibClangIRGenerator.h"  // Disabled for now due to LLVM dependency
#include "CodeGen.h"

int main(int argc, char *argv[]) {
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

    // Process input file arguments here...
    const auto& inputFileArgs = argsparser.inputFileArgs();
    if (inputFileArgs.empty()) {
            std::cerr << "No input file specified" << std::endl;
            return 1;
    }
    std::filesystem::path inputFilePath(inputFileArgs.front());
    inputFilePath = std::filesystem::absolute(inputFilePath);
    context.setInputFile(inputFilePath.string());

    // If no -o option was specified but we have a second argument, use it as output file
    if (!argsparser.hasOption("o") && inputFileArgs.size() >= 2) {
        context.setOutputFile(inputFileArgs[1]);
    }

    // If no output file was specified, generate default output filename
    if (context.getOutputFile().empty()) {
        std::filesystem::path outputPath = inputFilePath;
        outputPath.replace_extension(".obj");
        context.setOutputFile(outputPath.string());
    }

    // Add the directory of the input source file as an implicit include directory
    std::filesystem::path inputDirPath = inputFilePath.parent_path();
    context.addIncludeDir(inputDirPath.string());

    FileTree file_tree;
    FileReader file_reader(context, file_tree);
    if (!file_reader.readFile(context.getInputFile().value())) {
        std::cerr << "Failed to read input file: " << context.getInputFile().value() << std::endl;
        return 1;
    }

    if (context.isVerboseMode() && !context.isPreprocessorOnlyMode()) {
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
    Parser parser(lexer);
    auto parse_result = parser.parse();

    if (parse_result.is_error()) {
        std::cerr << "Error: " << parse_result.error_message() << std::endl;
        return 1;
    }

    const auto& ast = parser.get_nodes();

    AstToIr converter;
    for (auto& node_handle : ast) {
        converter.visit(node_handle);
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

    irConverter.convert(ir, context.getOutputFile(), context.getInputFile().value());

    return 0;
}
