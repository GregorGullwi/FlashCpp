#pragma once

#include <string>
#include <numeric>
#include <vector>
#include <array>
#include <variant>
#include <string_view>
#include <assert.h>
#include <unordered_map>

#include "IRTypes.h"
#include "ObjFileWriter.h"

template<class TWriterClass = ObjectFileWriter>
class IrToObjConverter {
public:
	IrToObjConverter() = default;

	void convert(const Ir& ir, const std::string& filename) {
		for (const auto& instruction : ir.getInstructions()) {
			switch (instruction.getOpcode()) {
			case IrOpcode::FunctionDecl:
				handleFunctionDecl(instruction);
				break;
			case IrOpcode::Return:
				handleReturn(instruction);
				break;
			case IrOpcode::FunctionCall:
				handleFunctionCall(instruction);
				break;
			default:
				assert(false && "Not implemented yet");
				break;
			}
		}
		finalizeSections();
		writer.write(filename);
	}

private:
	void handleFunctionCall(const IrInstruction& instruction) {	
		// call [function name] instruction is 5 bytes
		auto function_name = instruction.getOperandAs<std::string_view>(1);
		std::array<uint32_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		auto symbol_addr = functionSymbols.find(function_name);
		if (symbol_addr == functionSymbols.end()) {
			throw std::runtime_error("Function symbol not found");
		}

		// Calculate the relative address of the function
		int64_t relative_addr = symbol_addr->second - textSectionData.size() - callInst.size();
		for (size_t i = 0; i < 4; ++i) {
			callInst[i + 1] = (relative_addr >> (8 * i)) & 0xFF;
		}

		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		writer.add_relocation(textSectionData.size() - 4, function_name);
	}

	void handleFunctionDecl(const IrInstruction& instruction) {
		auto func_name = instruction.getOperandAs<std::string_view>(2);
		writer.add_function_symbol(std::string(func_name));
		functionSymbols[func_name] = textSectionData.size();
	}

	void handleReturn(const IrInstruction& instruction) {
		if (instruction.isOperandType<unsigned long long>(2)) {
			unsigned long long returnValue = instruction.getOperandAs<unsigned long long>(2);
			if (returnValue > std::numeric_limits<uint32_t>::max()) {
				throw std::runtime_error("Return value exceeds 32-bit limit");
			}

			// mov eax, immediate instruction has a fixed size of 5 bytes
			std::array<uint8_t, 5> movEaxImmedInst = { 0xB8, 0, 0, 0, 0 };

			// Fill in the return value
			for (size_t i = 0; i < 4; ++i) {
				movEaxImmedInst[i + 1] = (returnValue >> (8 * i)) & 0xFF;
			}

			textSectionData.insert(textSectionData.end(), movEaxImmedInst.begin(), movEaxImmedInst.end());
		}
		else if (instruction.isOperandType<TempVar>(2)) {
			// ?
		}

		// ret instruction is a single byte
		std::array<uint8_t, 1> retInst = { 0xC3 };

		// Add instructions to the .text section
		textSectionData.insert(textSectionData.end(), retInst.begin(), retInst.end());
	}

	void finalizeSections() {
		writer.add_data(textSectionData, SectionType::TEXT);
	}

	TWriterClass writer;
	std::vector<char> textSectionData;
	std::unordered_map<std::string_view, uint32_t> functionSymbols;
};
