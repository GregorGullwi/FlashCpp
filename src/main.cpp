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
#include "LibClangIRGenerator.h"

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

    // Add the directory of the input source file as an implicit include directory
    std::filesystem::path inputDirPath = inputFilePath.parent_path();
    context.addIncludeDir(inputDirPath.string());

    FileTree file_tree;
    FileReader file_reader(context, file_tree);
    if (!file_reader.readFile(context.getInputFile().value())) {
        std::cerr << "Failed to read input file: " << context.getInputFile().value() << std::endl;
        return 1;
    }
    Lexer lexer(file_reader.get_result());
    Parser parser(lexer);
    parser.parse();
    parser.generate_coff("output.o");

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
}
