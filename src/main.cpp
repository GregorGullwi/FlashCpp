#include <iostream>
#include <string>
#include <string_view>
#include <optional>
#include <variant>
#include <map>
#include <vector>
#include "FileTree.h"
#include "FileReader.h"
#include "CompileContext.h"
#include "CommandLineParser.h"

int main(int argc, char *argv[]) {
	CompileContext context;
    CommandLineParser parser(argc, argv, context);

    if (parser.hasOption("h") || parser.hasOption("help")) {
        std::cout << "Help message" << std::endl;
        return 0;
    }

    if (parser.hasOption("o")) {
        auto output_file = parser.optionValue("o");
        if (std::holds_alternative<std::string_view>(output_file))
            context.setOutputFile(std::get<std::string_view>(output_file));
    }

    if (parser.hasFlag("v") || parser.hasFlag("verbose")) {
        context.setVerboseMode(true);
    }

	// Process input file arguments here...
	const auto& inputFileArgs = parser.inputFileArgs();
	if (inputFileArgs.empty()) {
		std::cerr << "No input file specified" << std::endl;
		return 1;
	}
    context.setInputFile(inputFileArgs.front());

    FileTree file_tree;
    FileReader file_reader(context, file_tree);
    if (!file_reader.readFile(context.getInputFile().value())) {
        std::cerr << "Failed to read input file: " << context.getInputFile().value() << std::endl;
        return 1;
    }

    // Use context and file_tree to perform the desired operation
    std::cout << "Output file: " << context.getOutputFile() << std::endl;
    std::cout << "Verbose mode: " << (context.isVerboseMode() ? "enabled" : "disabled") << std::endl;
    std::cout << "Input file: " << context.getInputFile().value() << std::endl;
    std::cout << "Dependencies:" << std::endl;
    for (const auto& dep : context.getDependencies()) {
        std::cout << "- " << dep << std::endl;
    }
}
