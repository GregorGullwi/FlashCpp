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

/*
	+ ---------------- +
	| Parameter 2	| [rbp + 24] < -Positive offsets
	| Parameter 1	| [rbp + 16] < -Positive offsets
	| Return Address| [rbp + 8]
	| Saved RBP		| [rbp + 0] < -RBP points here
	| Local Var 1	| [rbp - 8] < -Negative offsets
	| Local Var 2	| [rbp - 16] < -Negative offsets
	| Temp Var 1	| [rbp - 24] < -Negative offsets
	+ ---------------- +
*/

// Maximum possible size for 'mov destination_register, [rbp + offset]' instruction:
// REX (1 byte) + Opcode (1 byte) + ModR/M (1 byte) + SIB (1 byte) + Disp32 (4 bytes) = 8 bytes
constexpr size_t MAX_MOV_INSTRUCTION_SIZE = 8;

struct OpCodeWithSize {
	std::array<uint8_t, MAX_MOV_INSTRUCTION_SIZE> op_codes;
	size_t size_in_bytes = 0;
};

/**
 * @brief Generates x86-64 binary opcodes for 'mov destination_register, [rbp + offset]'.
 *
 * This function creates the byte sequence for moving a 64-bit value from a
 * frame-relative address (RBP + offset) into a general-purpose 64-bit register.
 * It handles REX prefixes, ModR/M, and 8-bit/32-bit displacements.
 * The generated opcodes are placed into a stack-allocated `std::array` within
 * the returned `OpCodeWithSize` struct.
 *
 * @param destinationRegister The target register (e.g., RAX, RCX, R8).
 * @param offset The signed byte offset from RBP. Negative for locals, positive for parameters.
 * @return An `OpCodeWithSize` struct containing a stack-allocated `std::array`
 *         of `uint8_t` and the actual number of bytes generated.
 */
OpCodeWithSize generateMovFromFrame(X64Register destinationRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0; // Initialize count to 0

	// Use a pointer to fill the array sequentially.
	uint8_t* current_byte_ptr = result.op_codes.data();

	// --- REX Prefix (0x40 | W | R | X | B) ---
	uint8_t rex_prefix = 0x48; // Base: REX.W = 0100_1000b

	// If destination register is R8-R15 (enum values >= 8), set REX.R bit.
	if (static_cast<uint8_t>(destinationRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 2); // Set R bit (0b0100)
	}
	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// --- Opcode for MOV r64, r/m64 ---
	*current_byte_ptr++ = 0x8B;
	result.size_in_bytes++;

	// --- ModR/M byte (Mod | Reg | R/M) ---
	uint8_t modrm_byte;
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(destinationRegister) & 0x07;

	uint8_t mod_field;
	if (offset == 0) {
		mod_field = 0x01; // Mod = 01b (8-bit displacement) - RBP always needs displacement even for 0
	}
	else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // Mod = 01b (8-bit displacement)
	}
	else {
		mod_field = 0x02; // Mod = 10b (32-bit displacement)
	}

	// RBP encoding is 0x05, no SIB needed for RBP-relative addressing
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05; // 0x05 for RBP
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset == 0 || (offset >= -128 && offset <= 127)) {
		// 8-bit signed displacement (even for offset 0 with RBP)
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes++;
	}
	else {
		// 32-bit signed displacement (little-endian format)
		*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
		result.size_in_bytes += 4;
	}

	return result;
}

/**
 * @brief Generates x86-64 binary opcodes for 'mov [rbp + offset], source_register'.
 *
 * This function creates the byte sequence for moving a 64-bit value from a
 * general-purpose 64-bit register to a frame-relative address (RBP + offset).
 * It handles REX prefixes, ModR/M, and 8-bit/32-bit displacements.
 * The generated opcodes are placed into a stack-allocated `std::array` within
 * the returned `OpCodeWithSize` struct.
 *
 * @param sourceRegister The source register (e.g., RAX, RCX, R8).
 * @param offset The signed byte offset from RBP. Negative for locals, positive for parameters.
 * @return An `OpCodeWithSize` struct containing a stack-allocated `std::array`
 *         of `uint8_t` and the actual number of bytes generated.
 */
OpCodeWithSize generateMovToFrame(X64Register sourceRegister, int32_t offset) {
	OpCodeWithSize result;
	result.size_in_bytes = 0;

	uint8_t* current_byte_ptr = result.op_codes.data();

	// --- REX Prefix (0x40 | W | R | X | B) ---
	uint8_t rex_prefix = 0x48; // Base: REX.W = 0100_1000b

	// If source register (Reg field) is R8-R15, set REX.R bit.
	if (static_cast<uint8_t>(sourceRegister) >= static_cast<uint8_t>(X64Register::R8)) {
		rex_prefix |= (1 << 2); // Set R bit (0b0100)
	}
	*current_byte_ptr++ = rex_prefix;
	result.size_in_bytes++;

	// --- Opcode for MOV r/m64, r64 ---
	*current_byte_ptr++ = 0x89;
	result.size_in_bytes++;

	// --- ModR/M byte (Mod | Reg | R/M) ---
	uint8_t modrm_byte;
	uint8_t reg_encoding_lower_3_bits = static_cast<uint8_t>(sourceRegister) & 0x07;

	uint8_t mod_field;
	if (offset == 0) {
		mod_field = 0x01; // Mod = 01b (8-bit displacement) - RBP always needs displacement even for 0
	} else if (offset >= -128 && offset <= 127) {
		mod_field = 0x01; // Mod = 01b (8-bit displacement)
	} else {
		mod_field = 0x02; // Mod = 10b (32-bit displacement)
	}

	// RBP encoding is 0x05, no SIB needed for RBP-relative addressing
	modrm_byte = (mod_field << 6) | (reg_encoding_lower_3_bits << 3) | 0x05; // 0x05 for RBP
	*current_byte_ptr++ = modrm_byte;
	result.size_in_bytes++;

	// --- Displacement ---
	if (offset == 0 || (offset >= -128 && offset <= 127)) {
		// 8-bit signed displacement (even for offset 0 with RBP)
		*current_byte_ptr++ = static_cast<uint8_t>(offset);
		result.size_in_bytes++;
	} else {
		// 32-bit signed displacement (little-endian format)
		*current_byte_ptr++ = static_cast<uint8_t>(offset & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 8) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 16) & 0xFF);
		*current_byte_ptr++ = static_cast<uint8_t>((offset >> 24) & 0xFF);
		result.size_in_bytes += 4;
	}

	return result;
}

// Win64 calling convention register mapping
constexpr std::array<X64Register, 4> INT_PARAM_REGS = {
	X64Register::RCX,  // First integer/pointer argument
	X64Register::RDX,  // Second integer/pointer argument
	X64Register::R8,   // Third integer/pointer argument
	X64Register::R9    // Fourth integer/pointer argument
};

constexpr std::array<X64Register, 4> FLOAT_PARAM_REGS = {
	X64Register::XMM0, // First floating point argument
	X64Register::XMM1, // Second floating point argument
	X64Register::XMM2, // Third floating point argument
	X64Register::XMM3  // Fourth floating point argument
};

std::optional<TempVar> getTempVarFromOffset(int32_t stackVariableOffset) {
	// For RBP-relative addressing, temporary variables have negative offsets
	// TempVar 0 is at offset -8, TempVar 1 is at offset -16, etc.
	if (stackVariableOffset < 0 && (stackVariableOffset % 8) == 0) {
		size_t index = static_cast<size_t>((-stackVariableOffset / 8) - 1);
		return TempVar(index);
	}

	return std::nullopt;
}

int32_t getStackOffsetFromTempVar(TempVar tempVar) {
	// For RBP-relative addressing, temporary variables use negative offsets
	// TempVar 0 is at [rbp-8], TempVar 1 is at [rbp-16], etc.
	return -static_cast<int32_t>((tempVar.index + 1) * 8);
}

struct RegisterAllocator
{
	static constexpr uint8_t REGISTER_COUNT = static_cast<uint8_t>(X64Register::Count) - 1;
	struct AllocatedRegister {
		X64Register reg = X64Register::Count;
		bool isAllocated = false;
		bool isDirty = false;	// Does the stack variable need to be updated on a flush
		int32_t stackVariableOffset = INT_MIN;
	};
	std::array<AllocatedRegister, REGISTER_COUNT> registers;

	RegisterAllocator() {
		for (size_t i = 0; i < REGISTER_COUNT; ++i) {
			registers[i].reg = static_cast<X64Register>(i);
		}
		registers[static_cast<int>(X64Register::RSP)].isAllocated = true;	// assume RSP is always allocated
		registers[static_cast<int>(X64Register::RBP)].isAllocated = true;	// assume RBP is always allocated
	}

	void reset() {
		for (auto& reg : registers) {
			reg = AllocatedRegister{ .reg = reg.reg };
		}
		registers[static_cast<int>(X64Register::RSP)].isAllocated = true;	// assume RSP is always allocated
		registers[static_cast<int>(X64Register::RBP)].isAllocated = true;	// assume RBP is always allocated
	}

	template<typename Func>
	void flushAllDirtyRegisters(Func func) {
		for (auto& reg : registers) {
			if (reg.isDirty) {
				func(reg.reg, reg.stackVariableOffset);
				reg.isDirty = false;
			}
		}
	}

	void flushSingleDirtyRegister(X64Register reg) {
		registers[static_cast<int>(reg)].isDirty = false;
	}

	const AllocatedRegister& allocate() {
		for (auto& reg : registers) {
			if (!reg.isAllocated) {
				reg.isAllocated = true;
				return reg;
			}
		}
		throw std::runtime_error("No registers available");
	}

	void allocateSpecific(X64Register reg, int32_t stackVariableOffset) {
		assert(!registers[static_cast<int>(reg)].isAllocated);
		registers[static_cast<int>(reg)].isAllocated = true;
		registers[static_cast<int>(reg)].stackVariableOffset = stackVariableOffset;
	}

	void release(X64Register reg) {
		registers[static_cast<int>(reg)] = AllocatedRegister{ .reg = reg };
	}

	bool is_allocated(X64Register reg) const {
		return registers[static_cast<size_t>(reg)].isAllocated;
	}

	void mark_reg_dirty(X64Register reg) {
		assert(registers[static_cast<int>(reg)].isAllocated);
		registers[static_cast<int>(reg)].isDirty = true;
	}

	std::optional<X64Register> tryGetStackVariableRegister(int32_t stackVariableOffset) const {
		for (auto& reg : registers) {
			if (reg.stackVariableOffset == stackVariableOffset) {
				return reg.reg;
			}
		}
		return std::nullopt;
	}

	void set_stack_variable_offset(X64Register reg, int32_t stackVariableOffset) {
		assert(registers[static_cast<int>(reg)].isAllocated);
		registers[static_cast<int>(reg)].stackVariableOffset = stackVariableOffset;
		registers[static_cast<int>(reg)].isDirty = true;
	}

	OpCodeWithSize get_reg_reg_move_op_code(X64Register dst_reg, X64Register src_reg, size_t size_in_bytes) {
		OpCodeWithSize result;
		/*if (dst_reg == src_reg) {	// removed for now, since this is an optimization
			return result;
		}*/

		assert(size_in_bytes >= 1 && size_in_bytes <= 8);

		// For 64-bit moves, we need the REX prefix (0x48)
		if (size_in_bytes == 8) {
			result.op_codes[0] = 0x48;
			result.op_codes[1] = 0x89;  // MOV r64, r64
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg, R/M=dst_reg
			result.op_codes[2] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
			result.size_in_bytes = 3;
		}
		// For 32-bit moves, we don't need REX prefix
		else if (size_in_bytes == 4) {
			result.op_codes[0] = 0x89;  // MOV r32, r32
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg, R/M=dst_reg
			result.op_codes[1] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
			result.size_in_bytes = 2;
		}
		// For 16-bit moves, we need the 66 prefix
		else if (size_in_bytes == 2) {
			result.op_codes[0] = 0x66;
			result.op_codes[1] = 0x89;  // MOV r16, r16
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg, R/M=dst_reg
			result.op_codes[2] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
			result.size_in_bytes = 3;
		}
		// For 8-bit moves, we need special handling for high registers
		else if (size_in_bytes == 1) {
			result.op_codes[0] = 0x88;  // MOV r8, r8
			// ModR/M: Mod=11 (register-to-register), Reg=src_reg, R/M=dst_reg
			result.op_codes[1] = 0xC0 + (static_cast<uint8_t>(src_reg) << 3) + static_cast<uint8_t>(dst_reg);
			result.size_in_bytes = 2;
		}

		return result;
	}
};

struct RegCode {
	uint8_t code;
	bool use_rex;
};

RegCode getWin64RegCode(int paramNumber) {
	switch (paramNumber) {
		case 0: return {0x4C, false};  // RCX
		case 1: return {0x54, false};  // RDX
		case 2: return {0x44, true};   // R8
		case 3: return {0x4C, true};   // R9
		default: return {0, false};
	}
}

template<class TWriterClass = ObjectFileWriter>
class IrToObjConverter {
public:
	IrToObjConverter() = default;

	void convert(const Ir& ir, const std::string_view filename, const std::string_view source_filename = "") {
		// Group instructions by function for stack space calculation
		groupInstructionsByFunction(ir);

		for (const auto& instruction : ir.getInstructions()) {
			// Add line mapping for debug information if line number is available
			if (instruction.getLineNumber() > 0) {
				std::cerr << "DEBUG: Adding line mapping for line " << instruction.getLineNumber()
				         << " (opcode: " << static_cast<int>(instruction.getOpcode()) << ")" << std::endl;
				addLineMapping(instruction.getLineNumber());
			}

			switch (instruction.getOpcode()) {
			case IrOpcode::FunctionDecl:
				handleFunctionDecl(instruction);
				break;
			case IrOpcode::VariableDecl:
				handleVariableDecl(instruction);
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
			case IrOpcode::Add:
				handleAdd(instruction);
				break;
			case IrOpcode::Subtract:
				handleSubtract(instruction);
				break;
			case IrOpcode::Multiply:
				handleMultiply(instruction);
				break;
			case IrOpcode::Divide:
				handleDivide(instruction);
				break;
			case IrOpcode::UnsignedDivide:
				handleUnsignedDivide(instruction);
				break;
			case IrOpcode::ShiftLeft:
				handleShiftLeft(instruction);
				break;
			case IrOpcode::ShiftRight:
				handleShiftRight(instruction);
				break;
			case IrOpcode::UnsignedShiftRight:
				handleUnsignedShiftRight(instruction);
				break;
			case IrOpcode::BitwiseAnd:
				handleBitwiseAnd(instruction);
				break;
			case IrOpcode::BitwiseOr:
				handleBitwiseOr(instruction);
				break;
			case IrOpcode::BitwiseXor:
				handleBitwiseXor(instruction);
				break;
			case IrOpcode::Modulo:
				handleModulo(instruction);
				break;
			case IrOpcode::FloatAdd:
				handleFloatAdd(instruction);
				break;
			case IrOpcode::FloatSubtract:
				handleFloatSubtract(instruction);
				break;
			case IrOpcode::FloatMultiply:
				handleFloatMultiply(instruction);
				break;
			case IrOpcode::FloatDivide:
				handleFloatDivide(instruction);
				break;
			case IrOpcode::Equal:
				handleEqual(instruction);
				break;
			case IrOpcode::NotEqual:
				handleNotEqual(instruction);
				break;
			case IrOpcode::LessThan:
				handleLessThan(instruction);
				break;
			case IrOpcode::LessEqual:
				handleLessEqual(instruction);
				break;
			case IrOpcode::GreaterThan:
				handleGreaterThan(instruction);
				break;
			case IrOpcode::GreaterEqual:
				handleGreaterEqual(instruction);
				break;
			case IrOpcode::UnsignedLessThan:
				handleUnsignedLessThan(instruction);
				break;
			case IrOpcode::UnsignedLessEqual:
				handleUnsignedLessEqual(instruction);
				break;
			case IrOpcode::UnsignedGreaterThan:
				handleUnsignedGreaterThan(instruction);
				break;
			case IrOpcode::UnsignedGreaterEqual:
				handleUnsignedGreaterEqual(instruction);
				break;
			case IrOpcode::FloatEqual:
				handleFloatEqual(instruction);
				break;
			case IrOpcode::FloatNotEqual:
				handleFloatNotEqual(instruction);
				break;
			case IrOpcode::FloatLessThan:
				handleFloatLessThan(instruction);
				break;
			case IrOpcode::FloatLessEqual:
				handleFloatLessEqual(instruction);
				break;
			case IrOpcode::FloatGreaterThan:
				handleFloatGreaterThan(instruction);
				break;
			case IrOpcode::FloatGreaterEqual:
				handleFloatGreaterEqual(instruction);
				break;
			case IrOpcode::LogicalAnd:
				handleLogicalAnd(instruction);
				break;
			case IrOpcode::LogicalOr:
				handleLogicalOr(instruction);
				break;
			case IrOpcode::SignExtend:
				handleSignExtend(instruction);
				break;
			case IrOpcode::ZeroExtend:
				handleZeroExtend(instruction);
				break;
			case IrOpcode::Truncate:
				handleTruncate(instruction);
				break;
			default:
				assert(false && "Not implemented yet");
				break;
			}
		}

		// Use the provided source filename, or fall back to a default if not provided
		std::string actual_source_file = source_filename.empty() ? "test_debug.cpp" : std::string(source_filename);
		std::cerr << "Adding source file to debug info: '" << actual_source_file << "'" << std::endl;
		writer.add_source_file(actual_source_file);

		finalizeSections();
		writer.write(std::string(filename));
	}

private:
	// Shared arithmetic operation context
	struct ArithmeticOperationContext {
		Type type = Type::Void;
		int size_in_bits{};
		const IrOperand& result_operand;
		X64Register result_physical_reg;
		X64Register rhs_physical_reg;
	};

	// Setup and load operands for arithmetic operations - validates operands, extracts common data, and loads into registers
	ArithmeticOperationContext setupAndLoadArithmeticOperation(const IrInstruction& instruction, const char* operation_name) {
		assert(instruction.getOperandCount() == 7 &&
			   (std::string(operation_name) + " instruction must have exactly 7 operands: result_var, LHS_type,LHS_size,LHS_val,RHS_type,RHS_size,RHS_val").c_str());

		// Get type and size (from LHS)
		ArithmeticOperationContext ctx = {
			.type = instruction.getOperandAs<Type>(1),
			.size_in_bits = instruction.getOperandAs<int>(2),
			.result_operand = instruction.getOperand(0),
		};

		// For now, we only support integer operations
		if (!is_integer_type(ctx.type)) {
			assert(false && (std::string("Only integer ") + operation_name + " is supported").c_str());
		}

		ctx.result_physical_reg = X64Register::Count;
		if (instruction.isOperandType<std::string_view>(3)) {
			auto lhs_var_op = instruction.getOperandAs<std::string_view>(3);
			auto lhs_var_id = variable_scopes.back().identifier_offset.find(lhs_var_op);
			if (lhs_var_id != variable_scopes.back().identifier_offset.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(lhs_var_id->second); var_reg.has_value()) {
					ctx.result_physical_reg = var_reg.value();	// value is already in a register, we can use it without a move!
				}
				else {
					assert(variable_scopes.back().current_stack_offset <= lhs_var_id->second);

					ctx.result_physical_reg = regAlloc.allocate().reg;
					auto mov_opcodes = generateMovFromFrame(ctx.result_physical_reg, lhs_var_id->second);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
				}
			}
			else {
				assert(false && "Missing variable name"); // TODO: Error handling
			}
		}
		else if (instruction.isOperandType<TempVar>(3)) {
			auto lhs_var_op = instruction.getOperandAs<TempVar>(3);
			auto lhs_stack_var_addr = getStackOffsetFromTempVar(lhs_var_op);
			if (auto lhs_reg = regAlloc.tryGetStackVariableRegister(lhs_stack_var_addr); lhs_reg.has_value()) {
				ctx.result_physical_reg = lhs_reg.value();
			}
			else {
				assert(variable_scopes.back().current_stack_offset <= lhs_stack_var_addr);

				ctx.result_physical_reg = regAlloc.allocate().reg;
				auto mov_opcodes = generateMovFromFrame(ctx.result_physical_reg, lhs_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(ctx.result_physical_reg);
			}
		}

		ctx.rhs_physical_reg = X64Register::Count;
		if (instruction.isOperandType<std::string_view>(6)) {
			auto rhs_var_op = instruction.getOperandAs<std::string_view>(6);
			auto rhs_var_id = variable_scopes.back().identifier_offset.find(rhs_var_op);
			if (rhs_var_id != variable_scopes.back().identifier_offset.end()) {
				if (auto var_reg = regAlloc.tryGetStackVariableRegister(rhs_var_id->second); var_reg.has_value()) {
					ctx.rhs_physical_reg = var_reg.value();	// value is already in a register, we can use it without a move!
				}
				else {
					assert(variable_scopes.back().current_stack_offset <= rhs_var_id->second);

					ctx.rhs_physical_reg = regAlloc.allocate().reg;
					auto mov_opcodes = generateMovFromFrame(ctx.rhs_physical_reg, rhs_var_id->second);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
				}
			}
			else {
				assert(false && "Missing variable name"); // TODO: Error handling
			}
		}
		else if (instruction.isOperandType<TempVar>(6)) {
			auto rhs_var_op = instruction.getOperandAs<TempVar>(6);
			auto rhs_stack_var_addr = getStackOffsetFromTempVar(rhs_var_op);
			if (auto rhs_reg = regAlloc.tryGetStackVariableRegister(rhs_stack_var_addr); rhs_reg.has_value()) {
				ctx.rhs_physical_reg = rhs_reg.value();
			}
			else {
				assert(variable_scopes.back().current_stack_offset <= rhs_stack_var_addr);

				ctx.rhs_physical_reg = regAlloc.allocate().reg;
				auto mov_opcodes = generateMovFromFrame(ctx.rhs_physical_reg, rhs_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
				regAlloc.flushSingleDirtyRegister(ctx.rhs_physical_reg);
			}
		}

		if (std::holds_alternative<TempVar>(ctx.result_operand)) {
			const TempVar temp_var = std::get<TempVar>(ctx.result_operand);
			const int32_t stack_offset = getStackOffsetFromTempVar(temp_var);
			variable_scopes.back().identifier_offset[temp_var.name()] = stack_offset;
			regAlloc.set_stack_variable_offset(ctx.result_physical_reg, stack_offset);
		}

		return ctx;
	}

	// Store the result of arithmetic operations to the appropriate destination
	void storeArithmeticResult(const ArithmeticOperationContext& ctx, X64Register source_reg = X64Register::Count) {
		// Use the result register by default, or the specified source register (e.g., RAX for division)
		X64Register actual_source_reg = (source_reg == X64Register::Count) ? ctx.result_physical_reg : source_reg;

		// Determine the final destination of the result (register or memory)
		if (std::holds_alternative<std::string_view>(ctx.result_operand)) {
			// If the result is a named variable, find its stack offset
			int final_result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(ctx.result_operand)];

			// Store the computed result from actual_source_reg to memory
			auto store_opcodes = generateMovToFrame(actual_source_reg, final_result_offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		}
		else if (std::holds_alternative<TempVar>(ctx.result_operand)) {
			auto res_var_op = std::get<TempVar>(ctx.result_operand);
			auto res_stack_var_addr = getStackOffsetFromTempVar(res_var_op);
			if (auto res_reg = regAlloc.tryGetStackVariableRegister(res_stack_var_addr); res_reg.has_value()) {
				if (res_reg != actual_source_reg) {
					auto moveFromRax = regAlloc.get_reg_reg_move_op_code(res_reg.value(), actual_source_reg, ctx.size_in_bits / 8);
					textSectionData.insert(textSectionData.end(), moveFromRax.op_codes.begin(), moveFromRax.op_codes.begin() + moveFromRax.size_in_bytes);
				}
			}
			else {
				assert(variable_scopes.back().current_stack_offset <= res_stack_var_addr);

				auto mov_opcodes = generateMovToFrame(actual_source_reg, res_stack_var_addr);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
			}

		}
		else {
			assert(false && "Unhandled destination type");
		}

		if (source_reg != X64Register::Count) {
			regAlloc.release(source_reg);
		}
	}

	// Group IR instructions by function for analysis
	void groupInstructionsByFunction(const Ir& ir) {
		function_instructions.clear();
		std::string_view current_func_name;

		for (const auto& instruction : ir.getInstructions()) {
			if (instruction.getOpcode() == IrOpcode::FunctionDecl) {
				current_func_name = instruction.getOperandAs<std::string_view>(2);
				function_instructions[current_func_name] = std::vector<IrInstruction>();
			} else if (!current_func_name.empty()) {
				function_instructions[current_func_name].push_back(instruction);
			}
		}
	}

	// Calculate the total stack space needed for a function by analyzing its IR instructions
	int32_t calculateFunctionStackSpace(std::string_view func_name) {
		auto it = function_instructions.find(func_name);
		if (it == function_instructions.end()) {
			return 0; // No instructions found for this function
		}

		// Find the maximum TempVar index used in this function
		int32_t max_temp_var_index = -1;
		int32_t shadow_stack_space = 0;

		for (const auto& instruction : it->second) {
			// Look for TempVar operands in the instruction
			shadow_stack_space |= (0x20 * !(instruction.getOpcode() != IrOpcode::FunctionCall));

			for (size_t i = 0; i < instruction.getOperandCount(); ++i) {
				if (instruction.isOperandType<TempVar>(i)) {
					auto temp_var = instruction.getOperandAs<TempVar>(i);
					max_temp_var_index = std::max(max_temp_var_index, static_cast<int32_t>(temp_var.index));
				}
			}
		}

		// if we are a leaf function (don't call other functions), we can get by with just register if we don't have more than 8 * 64 bytes of values to store
		//if (shadow_stack_space == 0 && max_temp_var_index <= 8) {
			//return 0;
		//}

		// Calculate space needed: (max_index + 1) * 8 bytes per variable
		int32_t local_space = (max_temp_var_index + 1) * 8;
		return local_space + shadow_stack_space; // for now, always assume that we need to allocate stack space for a function call
	}

	// Helper function to allocate stack space for a temporary variable
	int allocateStackSlotForTempVar(int32_t index) {
		StackVariableScope& current_scope = variable_scopes.back();
		auto temp_var_name = TempVar(index).name();	// TODO: rework this so it doesn't require strings
		auto it = current_scope.identifier_offset.find(temp_var_name);
		if (it != current_scope.identifier_offset.end()) {
			return it->second; // Already allocated
		}

		constexpr int temp_var_size = 8; // 8 bytes for 64-bit value

		// With RBP-relative addressing, local variables use NEGATIVE offsets
		// Move to next slot (going more negative)
		current_scope.current_stack_offset -= temp_var_size;
		int stack_offset = current_scope.current_stack_offset;
		current_scope.identifier_offset[temp_var_name] = stack_offset;

		return stack_offset;
	}

	void flushAllDirtyRegisters()
	{
		regAlloc.flushAllDirtyRegisters([this](X64Register reg, int32_t stackVariableOffset)
			{
				auto tempVarIndex = getTempVarFromOffset(stackVariableOffset);

				if (tempVarIndex.has_value()) {
					// For RBP-relative addressing with negative offsets, we need to ensure
					// the stack space has been allocated. Since we're using negative offsets,
					// we check if current_stack_offset is greater than (less negative than) stackVariableOffset
					if (variable_scopes.back().current_stack_offset > stackVariableOffset) {
						// Update current_stack_offset to reflect the actual stack usage
						variable_scopes.back().current_stack_offset = stackVariableOffset;
					}

					// Store the computed result from register to stack
					auto store_opcodes = generateMovToFrame(reg, stackVariableOffset);
					textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
				}
			});
	}

	void handleFunctionCall(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 2 && "Function call must have at least 2 ope rands (result, function name)");

		flushAllDirtyRegisters();

		// Get result variable and function name
		auto result_var = instruction.getOperand(0);
		auto funcName = instruction.getOperandAs<std::string_view>(1);

		// Get result offset
		int result_offset = 0;
		if (std::holds_alternative<TempVar>(result_var)) {
			const TempVar temp_var = std::get<TempVar>(result_var);
			result_offset = getStackOffsetFromTempVar(temp_var);
			variable_scopes.back().identifier_offset[temp_var.name()] = result_offset;
		} else {
			result_offset = variable_scopes.back().identifier_offset[std::get<std::string_view>(result_var)];
		}

		// Process arguments (if any)
		const size_t first_arg_index = 2; // Start after result and function name
		const size_t arg_stride = 3; // type, size, value
		const size_t num_args = (instruction.getOperandCount() - first_arg_index) / arg_stride;
		for (size_t i = 0; i < num_args; ++i) {
			size_t argIndex = first_arg_index + i * arg_stride;
			Type argType = instruction.getOperandAs<Type>(argIndex);
			// int argSize = instruction.getOperandAs<int>(argIndex + 1); // unused for now
			auto argValue = instruction.getOperand(argIndex + 2); // could be immediate or variable

			if (!is_integer_type(argType)) {
				assert(false && "Only integer arguments are supported");
				return;
			}

			// Determine the target register for the argument
			X64Register target_reg = INT_PARAM_REGS[i];

			if (std::holds_alternative<unsigned long long>(argValue)) {
				// Construct the REX prefix based on target_reg for destination (Reg field).
				// REX.W is always needed for 64-bit operations (0x48).
				// REX.R (bit 2) is set if the target_reg is R8-R15 (as it appears in the Reg field of ModR/M or is directly encoded).
				uint8_t rex_prefix = 0x48;
				if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					rex_prefix |= (1 << 2); // Set REX.R
				}
				// Push the REX prefix. It might be modified later for REX.B or REX.X if needed.
				textSectionData.push_back(rex_prefix);

				// Immediate value
				// MOV r64, imm64 (REX.W/B + Opcode B8+rd)
				if (static_cast<uint8_t>(target_reg) >= static_cast<uint8_t>(X64Register::R8)) {
					textSectionData.back() |= (1 << 0); // Set REX.B bit on the already pushed REX prefix (for rd in opcode)
				}
				textSectionData.push_back(0xB8 + (static_cast<uint8_t>(target_reg) & 0x07)); // Opcode B8 + lower 3 bits of target_reg
				unsigned long long value = std::get<unsigned long long>(argValue);
				for (size_t j = 0; j < 8; ++j) {
					textSectionData.push_back(static_cast<uint8_t>(value & 0xFF));
					value >>= 8;
				}
			} else if (std::holds_alternative<std::string_view>(argValue)) {
				// Local variable - use RBP-relative addressing consistently
				std::string_view var_name = std::get<std::string_view>(argValue);
				int var_offset = variable_scopes.back().identifier_offset[var_name];

				if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
					if (reg_var.value() != target_reg) {
						auto movResultToRax = regAlloc.get_reg_reg_move_op_code(target_reg, reg_var.value(), 4);	// TODO: reg size
						textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
					}
				}
				else {
					assert(variable_scopes.back().current_stack_offset <= var_offset);
					// Load from stack using RBP-relative addressing
					auto load_opcodes = generateMovFromFrame(target_reg, var_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(target_reg);
				}
			} else if (std::holds_alternative<TempVar>(argValue)) {
				// Temporary variable value (stored on stack)
				auto src_reg_temp = std::get<TempVar>(argValue);
				const std::string_view temp_var_name = src_reg_temp.name();

				// Find the stack offset for this temporary variable
				const StackVariableScope& current_scope = variable_scopes.back();
				auto it = current_scope.identifier_offset.find(temp_var_name);
				if (it != current_scope.identifier_offset.end()) {
					int var_offset = it->second;

					if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
						if (reg_var.value() != target_reg) {
							auto movResultToRax = regAlloc.get_reg_reg_move_op_code(target_reg, reg_var.value(), 4);	// TODO: reg size
							textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
						}
					}
					else {
					// Use the existing generateMovFromFrame function for consistency
					auto mov_opcodes = generateMovFromFrame(target_reg, var_offset);
					textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
						regAlloc.flushSingleDirtyRegister(target_reg);
					}
				} else {
					// Temporary variable not found in stack. This indicates a problem with our
					// TempVar management. Let's allocate a stack slot for it now and assume
					// the value is currently in RAX (from the most recent operation).

					// Allocate stack slot for this TempVar
					int stack_offset = allocateStackSlotForTempVar(src_reg_temp.index);

					// Store RAX to the newly allocated stack slot first
					auto store_opcodes = generateMovToFrame(X64Register::RAX, stack_offset);
					textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);

					// Now load from stack to target register
					auto load_opcodes = generateMovFromFrame(target_reg, stack_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(target_reg);
				}
			} else {
				assert(false && "Unsupported argument value type");
			}
		}

		// call [function name] instruction is 5 bytes
		auto function_name = instruction.getOperandAs<std::string_view>(1);
		std::array<uint8_t, 5> callInst = { 0xE8, 0, 0, 0, 0 };
		textSectionData.insert(textSectionData.end(), callInst.begin(), callInst.end());

		writer.add_relocation(textSectionData.size() - 4, function_name);
		
		regAlloc.reset();
		regAlloc.allocateSpecific(X64Register::RAX, result_offset);
		regAlloc.mark_reg_dirty(X64Register::RAX);
	}

	void handleVariableDecl(const IrInstruction& instruction) {
		auto var_type = instruction.getOperandAs<Type>(0);
		auto var_size = instruction.getOperandAs<int>(1);
		auto var_name = instruction.getOperandAs<std::string_view>(2);

		// Allocate space on the stack for the variable
		// For now, just track the variable in our local variable map
		// This will be enhanced when we add proper local variable debug info

		// Add debug information for the local variable
		if (!current_function_name_.empty()) {
			uint32_t code_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;

			// For now, use a simple type index (1 for int, 2 for float, etc.)
			uint32_t type_index = 1; // Default to int type
			switch (var_type) {
				case Type::Int: type_index = 1; break;
				case Type::Float: type_index = 2; break;
				case Type::Double: type_index = 3; break;
				case Type::Char: type_index = 4; break;
				case Type::Bool: type_index = 5; break;
				default: type_index = 1; break;
			}

			// Assume variable is valid for the entire function (simplified)
			uint32_t stack_offset = 8; // Simplified stack offset
			uint32_t start_offset = code_offset;
			uint32_t end_offset = code_offset + 100; // Simplified end offset

			writer.add_local_variable(std::string(var_name), type_index,
			                         stack_offset, start_offset, end_offset);
		}
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

		// Track function for debug information
		current_function_name_ = std::string(func_name);
		current_function_offset_ = func_offset;

		// Set up debug information for this function
		// For now, use file ID 0 (first source file)
		writer.set_current_function_for_debug(std::string(func_name), 0);

		// Create a new function scope
		regAlloc.reset();

		int32_t total_stack_space = calculateFunctionStackSpace(func_name);

		if (total_stack_space > 0)
		{
			// Ensure stack alignment to 16 bytes
			total_stack_space = (total_stack_space + 15) & -16;

			// MSVC-style prologue: push rbp; mov rbp, rsp; sub rsp, total_stack_space
			textSectionData.push_back(0x55); // push rbp
			textSectionData.push_back(0x48); textSectionData.push_back(0x8B); textSectionData.push_back(0xEC); // mov rbp, rsp

			// Generate stack allocation instruction
			if (total_stack_space <= 127) {
				// Use 8-bit immediate: sub rsp, imm8
				textSectionData.push_back(0x48); textSectionData.push_back(0x83); textSectionData.push_back(0xEC);
				textSectionData.push_back(static_cast<uint8_t>(total_stack_space));
			} else {
				// Use 32-bit immediate: sub rsp, imm32
				textSectionData.push_back(0x48); textSectionData.push_back(0x81); textSectionData.push_back(0xEC);
				for (int i = 0; i < 4; ++i) {
					textSectionData.push_back(static_cast<uint8_t>((total_stack_space >> (8 * i)) & 0xFF));
				}
			}
		}

		variable_scopes.emplace_back();
		// For RBP-relative addressing, we start with negative offset after total allocated space
		variable_scopes.back().current_stack_offset = -total_stack_space;

		// Handle parameters
		size_t paramIndex = 3;  // Start after return type, size, and function name
		while (paramIndex + 2 < instruction.getOperandCount()) {  // Need at least type, size, and name
			auto param_type = instruction.getOperandAs<Type>(paramIndex);
			auto param_size = instruction.getOperandAs<int>(paramIndex + 1);

			// Store parameter at [rbp+10h] for first parameter, [rbp+18h] for second, etc.
			int paramNumber = paramIndex / 3 - 1;
			int offset = 16 + (paramNumber * 8);

			auto param_name = instruction.getOperandAs<std::string_view>(paramIndex + 2);
			variable_scopes.back().identifier_offset[param_name] = offset;

			// Add parameter to debug information
			uint32_t param_type_index = 1; // Default to int type
			switch (param_type) {
				case Type::Int: param_type_index = 1; break;
				case Type::Float: param_type_index = 2; break;
				case Type::Double: param_type_index = 3; break;
				case Type::Char: param_type_index = 4; break;
				case Type::Bool: param_type_index = 5; break;
				default: param_type_index = 1; break;
			}
			writer.add_function_parameter(std::string(param_name), param_type_index, static_cast<uint32_t>(offset));

			if (paramNumber < INT_PARAM_REGS.size()) {
				X64Register src_reg = INT_PARAM_REGS[paramNumber];
				regAlloc.allocateSpecific(src_reg, offset);

				// Generate the MOV [rbp+offset], reg instruction
				auto mov_opcodes = generateMovToFrame(src_reg, offset);
				textSectionData.insert(textSectionData.end(), mov_opcodes.op_codes.begin(), mov_opcodes.op_codes.begin() + mov_opcodes.size_in_bytes);
			}

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
			// Handle temporary variable (stored on stack)
			auto size_it_bits = instruction.getOperandAs<int>(1);
			auto return_var = instruction.getOperandAs<TempVar>(2);

			// Load the temporary variable from stack to RAX
			auto temp_var_name = return_var.name();
			const StackVariableScope& current_scope = variable_scopes.back();
			auto it = current_scope.identifier_offset.find(temp_var_name);
			if (it != current_scope.identifier_offset.end()) {
				int var_offset = it->second;
				if (auto reg_var = regAlloc.tryGetStackVariableRegister(var_offset); reg_var.has_value()) {
					if (reg_var.value() != X64Register::RAX) {
						auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, reg_var.value(), 4);	// TODO: reg size
						textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);
					}
				}
				else {
					assert(variable_scopes.back().current_stack_offset <= var_offset);
					// Load from stack using RBP-relative addressing
					auto load_opcodes = generateMovFromFrame(X64Register::RAX, var_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(X64Register::RAX);
				}
			} else {
				// Temporary variable not found in stack. This can happen in a few scenarios:
				// 1. Function call result is immediately returned (value still in RAX)
				// 2. TempVar was never actually stored (shouldn't happen with our current logic)
				// 3. There's a bug in our TempVar indexing

				// For now, we'll assume the value is still in RAX from the most recent operation
				// This is a reasonable assumption for simple cases like "return function_call()"
				// where the function call result goes directly to the return without intermediate operations

				// In a more sophisticated compiler, we'd track register liveness and know
				// whether the value is still in RAX or needs to be loaded from somewhere else
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
				int var_offset = it->second;
				if (auto stackRegister = regAlloc.tryGetStackVariableRegister(var_offset); stackRegister.has_value()) {
					if (stackRegister.value() != X64Register::RAX) {	// Value is already in register, move if it's not in RAX already
						regAlloc.get_reg_reg_move_op_code(X64Register::RAX, stackRegister.value(), 4);	// TODO: What size should we use here?
					}
				}
				else {
					assert(current_scope.current_stack_offset < var_offset);
					// Load from stack using RBP-relative addressing
					auto load_opcodes = generateMovFromFrame(X64Register::RAX, var_offset);
					textSectionData.insert(textSectionData.end(), load_opcodes.op_codes.begin(), load_opcodes.op_codes.begin() + load_opcodes.size_in_bytes);
					regAlloc.flushSingleDirtyRegister(X64Register::RAX);
				}
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

		// Add debug information for the completed function
		if (!current_function_name_.empty()) {
			uint32_t function_length = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			writer.add_function_debug_info(current_function_name_, current_function_offset_, function_length);

			// Temporarily disable exception handling information to test if that's the issue
			// writer.add_function_exception_info(current_function_name_, current_function_offset_, function_length);

			current_function_name_.clear();
			current_function_offset_ = 0;
		}

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
		// With RBP-relative addressing, local variables use NEGATIVE offsets
		StackVariableScope& current_scope = variable_scopes.back();
		current_scope.current_stack_offset -= sizeInBytes;  // Move to next slot (going more negative)
		int stack_offset = current_scope.current_stack_offset;
		current_scope.identifier_offset[instruction.getOperandAs<std::string_view>(2)] = stack_offset;
	}

	void handleStore(const IrInstruction& instruction) {
		assert(instruction.getOperandCount() >= 4);  // type, size, dest, src

		auto var_name = instruction.getOperandAs<std::string_view>(2);
		auto src_reg = static_cast<X64Register>(instruction.getOperandAs<int>(3));

		// Find the variable's stack offset
		const StackVariableScope& current_scope = variable_scopes.back();
		auto it = current_scope.identifier_offset.find(var_name);
		if (it != current_scope.identifier_offset.end()) {
			// Store to stack using RBP-relative addressing
			int32_t offset = it->second;
			auto store_opcodes = generateMovToFrame(src_reg, offset);
			textSectionData.insert(textSectionData.end(), store_opcodes.op_codes.begin(), store_opcodes.op_codes.begin() + store_opcodes.size_in_bytes);
		}
	}

	void handleAdd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "addition");

		// Perform the addition operation
		std::array<uint8_t, 3> addInst = { 0x48, 0x01, 0xC0 }; // add r/m64, r64. 0x01 is opcode, 0xC0 is ModR/M for reg, reg
		addInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), addInst.begin(), addInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleSubtract(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "subtraction");

		// Perform the subtraction operation
		std::array<uint8_t, 3> subInst = { 0x48, 0x29, 0xC0 }; // sub r/m64, r64. 0x29 is opcode, 0xC0 is ModR/M for reg, reg
		subInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), subInst.begin(), subInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "multiplication");

		// Perform the multiplication operation
		std::array<uint8_t, 4> mulInst = { 0x48, 0x0F, 0xAF, 0x00 }; // REX.W (0x48) + Opcode (0x0F 0xAF) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=result_physical_reg, R/M=rhs_physical_reg
		mulInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), mulInst.begin(), mulInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleDivide(const IrInstruction& instruction) {
		flushAllDirtyRegisters();	// we do this so that RDX is free to use

		regAlloc.release(X64Register::RAX);
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);

		regAlloc.release(X64Register::RDX);
		regAlloc.allocateSpecific(X64Register::RDX, INT_MIN);

		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "division");

		// Division requires special handling: dividend must be in RAX
		// Move result_physical_reg to RAX (dividend must be in RAX for idiv)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// Sign extend RAX into RDX:RAX (CQO for 64-bit)
		if (ctx.size_in_bits == 64) {
			// CQO - sign extend RAX into RDX:RAX (fills RDX with 0 or -1)
			std::array<uint8_t, 2> cqoInst = { 0x48, 0x99 }; // REX.W + CQO
			textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end());
		} else {
			// CDQ - sign extend EAX into EDX:EAX (for 32-bit)
			std::array<uint8_t, 1> cdqInst = { 0x99 };
			textSectionData.insert(textSectionData.end(), cdqInst.begin(), cdqInst.end());
		}

	    // idiv rhs_physical_reg
		uint8_t rex = 0x40; // Base REX prefix
		if (ctx.size_in_bits == 64) {
			rex |= 0x08; // Set REX.W for 64-bit operation
		}

		// Check if we need REX.B for the divisor register
		if (static_cast<uint8_t>(ctx.rhs_physical_reg) >= static_cast<uint8_t>(X64Register::R8)) {
			rex |= 0x01; // Set REX.B
		}

		std::array<uint8_t, 3> divInst = {
			rex,
			0xF7,  // Opcode for IDIV
			static_cast<uint8_t>(0xF8 + (static_cast<uint8_t>(ctx.rhs_physical_reg) & 0x07))  // ModR/M: 11 111 reg (opcode extension 7 for IDIV)
		};
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());

		// Store the result from RAX (quotient) to the appropriate destination
		storeArithmeticResult(ctx, X64Register::RAX);

		regAlloc.release(X64Register::RDX);
	}

	void handleShiftLeft(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift left");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the shift left operation: shl r/m64, cl
		std::array<uint8_t, 3> shlInst = { 0x48, 0xD3, 0x00 }; // REX.W (0x48) + Opcode (0xD3) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=4 (opcode extension for shl), R/M=result_physical_reg
		shlInst[2] = 0xC0 + (0x04 << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), shlInst.begin(), shlInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleShiftRight(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "shift right");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the shift right operation: sar r/m64, cl (arithmetic right shift)
		// Note: Using SAR (arithmetic) instead of SHR (logical) to preserve sign for signed integers
		std::array<uint8_t, 3> sarInst = { 0x48, 0xD3, 0x00 }; // REX.W (0x48) + Opcode (0xD3) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=7 (opcode extension for sar), R/M=result_physical_reg
		sarInst[2] = 0xC0 + (0x07 << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), sarInst.begin(), sarInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedDivide(const IrInstruction& instruction) {
		regAlloc.allocateSpecific(X64Register::RAX, INT_MIN);
		regAlloc.allocateSpecific(X64Register::RDX, INT_MIN);

		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned division");

		// Division requires special handling: dividend must be in RAX
		// Move result_physical_reg to RAX (dividend must be in RAX for div)
		auto movResultToRax = regAlloc.get_reg_reg_move_op_code(X64Register::RAX, ctx.result_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movResultToRax.op_codes.begin(), movResultToRax.op_codes.begin() + movResultToRax.size_in_bytes);

		// xor edx, edx - clear upper 32 bits of dividend for unsigned division
		std::array<uint8_t, 2> xorEdxInst = { 0x31, 0xD2 };
		textSectionData.insert(textSectionData.end(), xorEdxInst.begin(), xorEdxInst.end());

		// div rhs_physical_reg (unsigned division)
		std::array<uint8_t, 3> divInst = { 0x48, 0xF7, 0x00 }; // REX.W (0x48) + Opcode (0xF7) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=6 (opcode extension for div), R/M=rhs_physical_reg
		divInst[2] = 0xC0 + (0x06 << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), divInst.begin(), divInst.end());

		// Store the result from RAX (quotient) to the appropriate destination
		storeArithmeticResult(ctx, X64Register::RAX);
	}

	void handleUnsignedShiftRight(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned shift right");

		// Shift operations require the shift count to be in CL (lower 8 bits of RCX)
		// Move rhs_physical_reg to RCX
		auto movRhsToCx = regAlloc.get_reg_reg_move_op_code(X64Register::RCX, ctx.rhs_physical_reg, ctx.size_in_bits / 8);
		textSectionData.insert(textSectionData.end(), movRhsToCx.op_codes.begin(), movRhsToCx.op_codes.begin() + movRhsToCx.size_in_bytes);

		// Perform the unsigned shift right operation: shr r/m64, cl (logical right shift)
		// Note: Using SHR (logical) instead of SAR (arithmetic) for unsigned integers
		std::array<uint8_t, 3> shrInst = { 0x48, 0xD3, 0x00 }; // REX.W (0x48) + Opcode (0xD3) + ModR/M (initially 0x00)
		// ModR/M: Mod=11 (register-to-register), Reg=5 (opcode extension for shr), R/M=result_physical_reg
		shrInst[2] = 0xC0 + (0x05 << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), shrInst.begin(), shrInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseAnd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise AND");

		// Perform the bitwise AND operation
		std::array<uint8_t, 3> andInst = { 0x48, 0x21, 0xC0 }; // and r/m64, r64. 0x21 is opcode, 0xC0 is ModR/M for reg, reg
		andInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), andInst.begin(), andInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseOr(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise OR");

		// Perform the bitwise OR operation
		std::array<uint8_t, 3> orInst = { 0x48, 0x09, 0xC0 }; // or r/m64, r64. 0x09 is opcode, 0xC0 is ModR/M for reg, reg
		orInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), orInst.begin(), orInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleBitwiseXor(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "bitwise XOR");

		// Perform the bitwise XOR operation
		std::array<uint8_t, 3> xorInst = { 0x48, 0x31, 0xC0 }; // xor r/m64, r64. 0x31 is opcode, 0xC0 is ModR/M for reg, reg
		xorInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), xorInst.begin(), xorInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleModulo(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "modulo");

		// For x86-64, modulo is implemented using division
		// idiv instruction computes both quotient (RAX) and remainder (RDX)
		// We need the remainder in RDX

		// Move dividend to RAX, divisor to a register
		// Sign extend RAX to RDX:RAX for signed division
		std::array<uint8_t, 3> cqoInst = { 0x48, 0x99, 0x00 }; // cqo (sign extend RAX to RDX:RAX)
		textSectionData.insert(textSectionData.end(), cqoInst.begin(), cqoInst.end() - 1);

		// Perform signed division: idiv r/m64
		std::array<uint8_t, 3> idivInst = { 0x48, 0xF7, 0xF8 }; // idiv r64
		idivInst[2] = 0xF8 + static_cast<uint8_t>(ctx.rhs_physical_reg);
		textSectionData.insert(textSectionData.end(), idivInst.begin(), idivInst.end());

		// Move remainder from RDX to result register
		if (ctx.result_physical_reg != X64Register::RDX) {
			std::array<uint8_t, 3> movInst = { 0x48, 0x89, 0xD0 }; // mov r64, rdx
			movInst[2] = 0xD0 + static_cast<uint8_t>(ctx.result_physical_reg);
			textSectionData.insert(textSectionData.end(), movInst.begin(), movInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on zero flag: sete r8
		std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, 0xC0 }; // sete r8
		seteInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleNotEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "not equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on zero flag: setne r8
		std::array<uint8_t, 3> setneInst = { 0x0F, 0x95, 0xC0 }; // setne r8
		setneInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setneInst.begin(), setneInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLessThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "less than comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on sign flag: setl r8 (signed less than)
		std::array<uint8_t, 3> setlInst = { 0x0F, 0x9C, 0xC0 }; // setl r8
		setlInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setlInst.begin(), setlInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLessEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "less than or equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setle r8 (signed less than or equal)
		std::array<uint8_t, 3> setleInst = { 0x0F, 0x9E, 0xC0 }; // setle r8
		setleInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setleInst.begin(), setleInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleGreaterThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "greater than comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setg r8 (signed greater than)
		std::array<uint8_t, 3> setgInst = { 0x0F, 0x9F, 0xC0 }; // setg r8
		setgInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setgInst.begin(), setgInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleGreaterEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "greater than or equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setge r8 (signed greater than or equal)
		std::array<uint8_t, 3> setgeInst = { 0x0F, 0x9D, 0xC0 }; // setge r8
		setgeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setgeInst.begin(), setgeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedLessThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned less than comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on carry flag: setb r8 (unsigned less than)
		std::array<uint8_t, 3> setbInst = { 0x0F, 0x92, 0xC0 }; // setb r8
		setbInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setbInst.begin(), setbInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedLessEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned less than or equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setbe r8 (unsigned less than or equal)
		std::array<uint8_t, 3> setbeInst = { 0x0F, 0x96, 0xC0 }; // setbe r8
		setbeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setbeInst.begin(), setbeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedGreaterThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned greater than comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: seta r8 (unsigned greater than)
		std::array<uint8_t, 3> setaInst = { 0x0F, 0x97, 0xC0 }; // seta r8
		setaInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setaInst.begin(), setaInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleUnsignedGreaterEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "unsigned greater than or equal comparison");

		// Compare operands: cmp r/m64, r64
		std::array<uint8_t, 3> cmpInst = { 0x48, 0x39, 0xC0 }; // cmp r/m64, r64
		cmpInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), cmpInst.begin(), cmpInst.end());

		// Set result based on flags: setae r8 (unsigned greater than or equal)
		std::array<uint8_t, 3> setaeInst = { 0x0F, 0x93, 0xC0 }; // setae r8
		setaeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setaeInst.begin(), setaeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalAnd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "logical AND");

		// For logical AND, we need to implement short-circuit evaluation
		// For now, implement as bitwise AND on boolean values
		std::array<uint8_t, 3> andInst = { 0x48, 0x21, 0xC0 }; // and r/m64, r64
		andInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), andInst.begin(), andInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleLogicalOr(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "logical OR");

		// For logical OR, we need to implement short-circuit evaluation
		// For now, implement as bitwise OR on boolean values
		std::array<uint8_t, 3> orInst = { 0x48, 0x09, 0xC0 }; // or r/m64, r64
		orInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.rhs_physical_reg) << 3) + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), orInst.begin(), orInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatAdd(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point addition");

		// Use SSE addss (scalar single-precision) or addsd (scalar double-precision)
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// addss xmm_dst, xmm_src (F3 0F 58 /r)
			std::array<uint8_t, 4> addssInst = { 0xF3, 0x0F, 0x58, 0xC0 };
			addssInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), addssInst.begin(), addssInst.end());
		} else if (type == Type::Double) {
			// addsd xmm_dst, xmm_src (F2 0F 58 /r)
			std::array<uint8_t, 4> addsdInst = { 0xF2, 0x0F, 0x58, 0xC0 };
			addsdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), addsdInst.begin(), addsdInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatSubtract(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point subtraction");

		// Use SSE subss (scalar single-precision) or subsd (scalar double-precision)
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// subss xmm_dst, xmm_src (F3 0F 5C /r)
			std::array<uint8_t, 4> subssInst = { 0xF3, 0x0F, 0x5C, 0xC0 };
			subssInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), subssInst.begin(), subssInst.end());
		} else if (type == Type::Double) {
			// subsd xmm_dst, xmm_src (F2 0F 5C /r)
			std::array<uint8_t, 4> subsdInst = { 0xF2, 0x0F, 0x5C, 0xC0 };
			subsdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), subsdInst.begin(), subsdInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatMultiply(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point multiplication");

		// Use SSE mulss (scalar single-precision) or mulsd (scalar double-precision)
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// mulss xmm_dst, xmm_src (F3 0F 59 /r)
			std::array<uint8_t, 4> mulssInst = { 0xF3, 0x0F, 0x59, 0xC0 };
			mulssInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), mulssInst.begin(), mulssInst.end());
		} else if (type == Type::Double) {
			// mulsd xmm_dst, xmm_src (F2 0F 59 /r)
			std::array<uint8_t, 4> mulsdInst = { 0xF2, 0x0F, 0x59, 0xC0 };
			mulsdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), mulsdInst.begin(), mulsdInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatDivide(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point division");

		// Use SSE divss (scalar single-precision) or divsd (scalar double-precision)
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// divss xmm_dst, xmm_src (F3 0F 5E /r)
			std::array<uint8_t, 4> divssInst = { 0xF3, 0x0F, 0x5E, 0xC0 };
			divssInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), divssInst.begin(), divssInst.end());
		} else if (type == Type::Double) {
			// divsd xmm_dst, xmm_src (F2 0F 5E /r)
			std::array<uint8_t, 4> divsdInst = { 0xF2, 0x0F, 0x5E, 0xC0 };
			divsdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), divsdInst.begin(), divsdInst.end());
		}

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on zero flag: sete r8
		std::array<uint8_t, 3> seteInst = { 0x0F, 0x94, 0xC0 };
		seteInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), seteInst.begin(), seteInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatNotEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point not equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on zero flag: setne r8
		std::array<uint8_t, 3> setneInst = { 0x0F, 0x95, 0xC0 };
		setneInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setneInst.begin(), setneInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatLessThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on carry flag: setb r8 (below = less than for floating-point)
		std::array<uint8_t, 3> setbInst = { 0x0F, 0x92, 0xC0 };
		setbInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setbInst.begin(), setbInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatLessEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point less than or equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on flags: setbe r8 (below or equal)
		std::array<uint8_t, 3> setbeInst = { 0x0F, 0x96, 0xC0 };
		setbeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setbeInst.begin(), setbeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatGreaterThan(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on flags: seta r8 (above = greater than for floating-point)
		std::array<uint8_t, 3> setaInst = { 0x0F, 0x97, 0xC0 };
		setaInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setaInst.begin(), setaInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleFloatGreaterEqual(const IrInstruction& instruction) {
		// Setup and load operands
		auto ctx = setupAndLoadArithmeticOperation(instruction, "floating-point greater than or equal comparison");

		// Use SSE comiss/comisd for comparison
		Type type = instruction.getOperandAs<Type>(1);
		if (type == Type::Float) {
			// comiss xmm1, xmm2 (0F 2F /r)
			std::array<uint8_t, 3> comissInst = { 0x0F, 0x2F, 0xC0 };
			comissInst[2] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comissInst.begin(), comissInst.end());
		} else if (type == Type::Double) {
			// comisd xmm1, xmm2 (66 0F 2F /r)
			std::array<uint8_t, 4> comisdInst = { 0x66, 0x0F, 0x2F, 0xC0 };
			comisdInst[3] = 0xC0 + (static_cast<uint8_t>(ctx.result_physical_reg) << 3) + static_cast<uint8_t>(ctx.rhs_physical_reg);
			textSectionData.insert(textSectionData.end(), comisdInst.begin(), comisdInst.end());
		}

		// Set result based on flags: setae r8 (above or equal)
		std::array<uint8_t, 3> setaeInst = { 0x0F, 0x93, 0xC0 };
		setaeInst[2] = 0xC0 + static_cast<uint8_t>(ctx.result_physical_reg);
		textSectionData.insert(textSectionData.end(), setaeInst.begin(), setaeInst.end());

		// Store the result to the appropriate destination
		storeArithmeticResult(ctx);
	}

	void handleSignExtend(const IrInstruction& instruction) {
		// Sign extension: movsx dest, src
		// For now, implement a simple version that handles common cases

		// Get operands: result_var, from_type, from_size, value, to_size
		Type fromType = instruction.getOperandAs<Type>(1);
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// For simplicity, assume we're extending to a register
		// In a full implementation, we'd need to handle different size combinations
		// This is a placeholder that would need proper x86-64 movsx instructions

		// TODO: Implement proper sign extension instructions based on sizes
		// movsx r64, r32 (32->64), movsx r32, r16 (16->32), movsx r32, r8 (8->32), etc.
	}

	void handleZeroExtend(const IrInstruction& instruction) {
		// Zero extension: movzx dest, src
		// For now, implement a simple version that handles common cases

		// Get operands: result_var, from_type, from_size, value, to_size
		Type fromType = instruction.getOperandAs<Type>(1);
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// For simplicity, assume we're extending to a register
		// In a full implementation, we'd need to handle different size combinations
		// This is a placeholder that would need proper x86-64 movzx instructions

		// TODO: Implement proper zero extension instructions based on sizes
		// movzx r64, r32 (32->64), movzx r32, r16 (16->32), movzx r32, r8 (8->32), etc.
	}

	void handleTruncate(const IrInstruction& instruction) {
		// Truncation: just use the lower bits
		// For now, implement a simple version that handles common cases

		// Get operands: result_var, from_type, from_size, value, to_size
		Type fromType = instruction.getOperandAs<Type>(1);
		int fromSize = instruction.getOperandAs<int>(2);
		int toSize = instruction.getOperandAs<int>(4);

		// For simplicity, assume we're truncating in a register
		// In a full implementation, we'd need to handle different size combinations
		// This is a placeholder that would need proper x86-64 instructions

		// TODO: Implement proper truncation (often just using smaller register names)
		// e.g., using eax instead of rax for 32-bit, ax for 16-bit, al for 8-bit
	}

	void finalizeSections() {
		writer.add_data(textSectionData, SectionType::TEXT);

		// Finalize debug information
		writer.finalize_debug_info();
	}

	// Debug information tracking
	void addLineMapping(uint32_t line_number) {
		if (!current_function_name_.empty()) {
			uint32_t code_offset = static_cast<uint32_t>(textSectionData.size()) - current_function_offset_;
			std::cerr << "DEBUG: Line mapping added - function: " << current_function_name_
			         << ", offset: " << code_offset << ", line: " << line_number << std::endl;
			writer.add_line_mapping(code_offset, line_number);
		} else {
			std::cerr << "DEBUG: Line mapping SKIPPED (no current function) - line: " << line_number << std::endl;
		}
	}

	TWriterClass writer;
	std::vector<char> textSectionData;
	std::unordered_map<std::string_view, uint32_t> functionSymbols;
	std::unordered_map<std::string_view, std::vector<IrInstruction>> function_instructions;

	RegisterAllocator regAlloc;

	// Debug information tracking
	std::string current_function_name_;
	uint32_t current_function_offset_ = 0;

	struct StackVariableScope
	{
		int current_stack_offset = 0; // For RBP-relative: starts at 0, goes negative for locals
		std::unordered_map<std::string_view, int> identifier_offset;
	};
	std::vector<StackVariableScope> variable_scopes;
};
