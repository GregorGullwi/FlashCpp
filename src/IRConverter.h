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

// Define the calling convention for x64 Windows
enum class Win64CallingConvention : uint8_t {
    RCX = static_cast<uint8_t>(X64Register::RCX),  // First integer/pointer argument
    RDX = static_cast<uint8_t>(X64Register::RDX),  // Second integer/pointer argument
    R8 = static_cast<uint8_t>(X64Register::R8),    // Third integer/pointer argument
    R9 = static_cast<uint8_t>(X64Register::R9),    // Fourth integer/pointer argument
    XMM0 = static_cast<uint8_t>(X64Register::XMM0), // First floating point argument
    XMM1 = static_cast<uint8_t>(X64Register::XMM1), // Second floating point argument
    XMM2 = static_cast<uint8_t>(X64Register::XMM2), // Third floating point argument
    XMM3 = static_cast<uint8_t>(X64Register::XMM3), // Fourth floating point argument
    FirstArgument = RCX,
    SecondArgument = RDX,
    ThirdArgument = R8,
    FourthArgument = R9,
    Count = 8
};

struct RegisterAllocator
{
	static constexpr uint8_t REGISTER_COUNT = static_cast<uint8_t>(X64Register::Count);
	struct AllocatedRegister {
		X64Register reg;
		bool isAllocated = false;
	};
	std::array<AllocatedRegister, REGISTER_COUNT> registers;

	RegisterAllocator() {
		for (size_t i = 0; i < REGISTER_COUNT; ++i) {
			registers[i].reg = static_cast<X64Register>(i);
		}
		registers[0].isAllocated = true;	// assume RAX is always allocated
	}

	void reset() {
		for (auto& reg : registers) {
			reg.isAllocated = false;
		}
	}

	X64Register allocate() {
		for (auto& reg : registers) {
			if (!reg.isAllocated) {
				reg.isAllocated = true;
				return reg.reg;
			}
		}
		throw std::runtime_error("No registers available");
	}

	bool is_allocated(X64Register reg) const {
		return registers[static_cast<size_t>(reg)].isAllocated;
	}

	void reserve_arguments(size_t count) {
		for (size_t i = 0; i < count && i < static_cast<size_t>(Win64CallingConvention::Count); ++i) {
			registers[static_cast<size_t>(Win64CallingConvention::FirstArgument) + i].isAllocated = true;
		}
	}

	struct OpCodeWithSize {
		std::array<uint8_t, 4> op_codes;
		size_t size_in_bytes = 0;
	};
	OpCodeWithSize get_reg_reg_move_op_code(X64Register dst_reg, X64Register src_reg, size_t size_in_bytes) {
		OpCodeWithSize result;
		/*if (dst_reg == src_reg) {	// removed for now, since this is an optimization
			return result;
		}*/

		assert(size_in_bytes >= 1 && size_in_bytes <= 4);

		static std::array<std::array<uint8_t, 4>, 4> opcodeLookup = {
			std::array<uint8_t, 4>{0x88, 0x00, 0x00, 0x00}, // MOV byte ptr [RAX], AL
			{0x89, 0x00, 0x00, 0x00}, // MOV word ptr [RAX], AX
			{0x89, 0x00, 0x00, 0x00}, // MOV dword ptr [RAX], EAX
			{0x48, 0x89, 0x00, 0x00}  // MOV qword ptr [RAX], RAX
		};

		// Find the opcode in the lookup table based on the size
		auto opcodeIt = opcodeLookup[size_in_bytes - 1];
		result.op_codes = opcodeIt;
		result.size_in_bytes = 2 + (size_in_bytes / 64);
		result.op_codes[result.size_in_bytes - 2] = 0xC0 + static_cast<uint8_t>(dst_reg);
		result.op_codes[result.size_in_bytes - 1] = 0xC0 + static_cast<uint8_t>(src_reg);

		return result;
	}
};

template<class TWriterClass = ObjectFileWriter>
class IrToObjConverter {
public:
	IrToObjConverter() = default;

	void convert(const Ir& ir, const std::string& filename) {
		// add int 3
		textSectionData.push_back(static_cast<char>(0xcc));

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
			case IrOpcode::StackAlloc:
				handleStackAlloc(instruction);
				break;
			case IrOpcode::Store:
				handleStore(instruction);
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
		// Function call should have at least 2 operands (return var and function name)
		assert(instruction.getOperandCount() >= 2);

		// First, handle any arguments by moving them to the correct registers
		// In x64 calling convention, first 4 args go to RCX, RDX, R8, R9
		size_t argIndex = 2;  // Start after return var and function name
		while (argIndex < instruction.getOperandCount()) {
			X64Register argReg;
			switch (argIndex - 2) {
			case 0: argReg = X64Register::RCX; break;
			case 1: argReg = X64Register::RDX; break;
			case 2: argReg = X64Register::R8; break;
			case 3: argReg = X64Register::R9; break;
			default:
				assert(false && "Too many arguments");
				return;
			}

			// Get the argument value
			if (instruction.isOperandType<Type>(argIndex)) {
				Type type = instruction.getOperandAs<Type>(argIndex);
				int size = instruction.getOperandAs<int>(argIndex + 1);
				unsigned long long value = instruction.getOperandAs<unsigned long long>(argIndex + 2);

				// Handle type operand
				switch (type) {
				case Type::Int:
					// For int type, we expect a 32-bit value
					if (size != 32) {
						assert(false && "Int type must be 32 bits");
						return;
					}
					{
						// mov reg, immediate
						std::array<uint8_t, 5> movInst = { 0xB8, 0, 0, 0, 0 };
						movInst[0] = 0xB8 + static_cast<uint8_t>(argReg);  // Adjust opcode for target register
						for (size_t i = 0; i < 4; ++i) {
							movInst[i + 1] = (value >> (8 * i)) & 0xFF;
						}
						textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
					}
					break;
				case Type::Char:
					// For char type, we expect an 8-bit value
					if (size != 8) {
						assert(false && "Char type must be 8 bits");
						return;
					}
					{
						// mov reg, immediate (8-bit)
						std::array<uint8_t, 3> movInst = { 0xB0, 0, 0 };
						movInst[0] = 0xB0 + static_cast<uint8_t>(argReg);  // Adjust opcode for target register
						movInst[1] = value & 0xFF;  // Only use lowest byte
						textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.begin() + 2);
					}
					break;
				case Type::Void:
					assert(false && "Void type not allowed as argument");
					return;
				default:
					assert(false && "Unsupported type");
					return;
				}
			}
			argIndex += 3;  // Skip type, size, and value operands
		}

		// call [function name] instruction is 5 bytes
		auto function_name = instruction.getOperandAs<std::string_view>(1);
		std::array<uint32_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		writer.add_relocation(textSectionData.size() - 4, function_name);
	}

	void handleFunctionDecl(const IrInstruction& instruction) {
		auto func_name = instruction.getOperandAs<std::string_view>(2);
		
		// align the function to 16 bytes
		static constexpr char nop = static_cast<char>(0x90);
		const uint32_t nop_count = 16 - (textSectionData.size() % 16);
		if (nop_count < 16)
			textSectionData.insert(textSectionData.end(), nop_count, nop);
		uint32_t func_offset = static_cast<uint32_t>(textSectionData.size());
	
		writer.add_function_symbol(std::string(func_name), func_offset);
		functionSymbols[func_name] = func_offset;
	
		// reset function register allocations
		regAlloc.reset();
		regAlloc.reserve_arguments(instruction.getOperandCount() - 2);
	
		// MSVC-style prologue: push rbp; mov rbp, rsp; sub rsp, 0x20 (32 bytes shadow space)
		textSectionData.push_back(0x55); // push rbp
		textSectionData.push_back(0x48); textSectionData.push_back(0x8B); textSectionData.push_back(0xEC); // mov rbp, rsp
		textSectionData.push_back(0x48); textSectionData.push_back(0x83); textSectionData.push_back(0xEC); textSectionData.push_back(0x20); // sub rsp, 0x20
	
		variable_scopes.emplace_back();
		
		// Handle parameters
		size_t paramIndex = 3;  // Start after return type, size, and function name
		while (paramIndex + 2 < instruction.getOperandCount()) {  // Need at least type, size, and name
			auto param_type = instruction.getOperandAs<Type>(paramIndex);
			auto param_size = instruction.getOperandAs<int>(paramIndex + 1);
			auto param_name = instruction.getOperandAs<std::string_view>(paramIndex + 2);
			
			// Store parameter at [rsp+8] for first parameter, [rsp+16] for second, etc.
			int offset = 8 + (paramIndex - 3) * 8;
			textSectionData.push_back(0x48); textSectionData.push_back(0x89); textSectionData.push_back(0x4C); textSectionData.push_back(0x24); textSectionData.push_back(offset); // mov [rsp+offset], rcx
			
			variable_scopes.back().identifier_offset[param_name] = offset;
			paramIndex += 3;  // Skip type, size, and name
		}
	}

	void handleReturn(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 3);
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
			// Do register allocation
			auto size_it_bits = instruction.getOperandAs<int>(1);
			auto return_var = instruction.getOperandAs<TempVar>(2);
			
			// For function call results, we need to move from the result register to RAX
			if (return_var.index > 0) {  // If this is a function call result
				// mov eax, eax (no-op since function result is already in eax)
				// We don't need to do anything since the function call result is already in eax
			} else {
				// For other cases, allocate a register and move the value
				auto src_reg = regAlloc.allocate();
				auto dst_reg = X64Register::RAX;	// all return values are stored in RAX
				if (src_reg != dst_reg) {
					auto op_codes = regAlloc.get_reg_reg_move_op_code(dst_reg, src_reg, size_it_bits / 8);
					if (op_codes.size_in_bytes > 0)
					{
						textSectionData.insert(textSectionData.end(), op_codes.op_codes.begin(), op_codes.op_codes.begin() + op_codes.size_in_bytes);
					}
				}
			}
		}
		else if (instruction.isOperandType<std::string_view>(2)) {
			// Handle local variable access
			auto var_name = instruction.getOperandAs<std::string_view>(2);
			auto size_in_bits = instruction.getOperandAs<int>(1);

			// Find the variable's stack offset
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(var_name);
			if (it != current_scope.identifier_offset.end()) {
				// mov eax, [rsp+8]
				std::array<uint8_t, 5> movInst = { 0x48, 0x8B, 0x44, 0x24, 0x08 };
				textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
			}
		}

		// Function epilogue
		// mov rsp, rbp (restore stack pointer)
		textSectionData.push_back(0x48);
		textSectionData.push_back(0x89);
		textSectionData.push_back(0xEC);

		// pop rbp (restore caller's base pointer)
		textSectionData.push_back(0x5D);

		// ret (return to caller)
		textSectionData.push_back(0xC3);

		variable_scopes.pop_back();
	}

	void handleStackAlloc(const IrInstruction& instruction) {
		// Get the size of the allocation
		auto sizeInBytes = instruction.getOperandAs<int>(1) / 8;

		// Ensure the stack remains aligned to 16 bytes
		sizeInBytes = (sizeInBytes + 15) & -16;

		// Generate the opcode for `sub rsp, imm32`
		std::array<uint8_t, 7> subRspInst = { 0x48, 0x81, 0xEC };
		std::memcpy(subRspInst.data() + 3, &sizeInBytes, sizeof(sizeInBytes));

		// Add the instruction to the .text section
		textSectionData.insert(textSectionData.end(), subRspInst.begin(), subRspInst.end());

		// Add the identifier and its stack offset to the current scope
		StackVariableScope& current_scope = variable_scopes.back();
		current_scope.identifier_offset[instruction.getOperandAs<std::string_view>(2)] = current_scope.current_stack_offset;
		current_scope.current_stack_offset += sizeInBytes;
	}

	void handleStore(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 4);  // type, size, dest, src

		auto var_name = instruction.getOperandAs<std::string_view>(2);
		auto src_reg = static_cast<X64Register>(instruction.getOperandAs<int>(3));

		// Find the variable's stack offset
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(var_name);
		if (it != current_scope.identifier_offset.end()) {
			// mov [rbp + offset], reg
			std::array<uint8_t, 7> movInst = { 0x48, 0x89, 0x85, 0, 0, 0, 0 };
			int32_t offset = it->second;
			std::memcpy(movInst.data() + 3, &offset, sizeof(offset));
			movInst[1] = 0x89;  // mov [mem], reg
			movInst[2] = 0x85;  // [rbp + disp32]
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}
	}

	void finalizeSections() {
		writer.add_data(textSectionData, SectionType::TEXT);
	}

	TWriterClass writer;
	std::vector<char> textSectionData;
	std::unordered_map<std::string_view, uint32_t> functionSymbols;
	RegisterAllocator regAlloc;

	struct StackVariableScope
	{
		int current_stack_offset = 0;
		std::unordered_map<std::string_view, int> identifier_offset;
	};
	std::vector<StackVariableScope> variable_scopes;
};
