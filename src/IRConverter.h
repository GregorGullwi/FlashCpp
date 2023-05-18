#pragma once

#include <string>
#include <numeric>
#include <vector>
#include <array>
#include <variant>
#include <string_view>

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
			default:
				break;
			}
		}
		finalizeSections();
		writer.write(filename);
	}

private:
	void handleFunctionDecl(const IrInstruction& instruction) {
		std::ostringstream oss;
		oss << "_" << instruction.getOperandAs<std::string_view>(2);
		writer.add_function_symbol(oss.str());
	}

	void handleReturn(const IrInstruction& instruction) {
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

		// ret instruction is a single byte
		std::array<uint8_t, 1> retInst = { 0xC3 };

		// Add instructions to the .text section
		textSectionData.insert(textSectionData.end(), movEaxImmedInst.begin(), movEaxImmedInst.end());
		textSectionData.insert(textSectionData.end(), retInst.begin(), retInst.end());
	}

	void finalizeSections() {
		writer.add_data(textSectionData, SectionType::TEXT);
	}

	TWriterClass writer;
	std::vector<char> textSectionData;
};
