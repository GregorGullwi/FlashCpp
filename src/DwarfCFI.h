#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>

// DWARF Call Frame Information (CFI) encoding utilities
// Used for generating .eh_frame section data for exception handling
// 
// References:
// - DWARF 4 Standard Section 7.6 (Variable Length Data)
// - LSB Exception Handling Supplement
// - Itanium C++ ABI Exception Handling

namespace DwarfCFI {

// DW_EH_PE_* encoding constants for pointer encoding
// These specify how pointers and values are encoded in DWARF data
enum DW_EH_PE : uint8_t {
	// Value format (low 4 bits)
	DW_EH_PE_absptr   = 0x00,  // Absolute pointer (native pointer size)
	DW_EH_PE_omit     = 0xff,  // Value is omitted
	DW_EH_PE_uleb128  = 0x01,  // Unsigned LEB128
	DW_EH_PE_udata2   = 0x02,  // Unsigned 2-byte
	DW_EH_PE_udata4   = 0x03,  // Unsigned 4-byte
	DW_EH_PE_udata8   = 0x04,  // Unsigned 8-byte
	DW_EH_PE_sleb128  = 0x09,  // Signed LEB128
	DW_EH_PE_sdata2   = 0x0a,  // Signed 2-byte
	DW_EH_PE_sdata4   = 0x0b,  // Signed 4-byte
	DW_EH_PE_sdata8   = 0x0c,  // Signed 8-byte
	
	// Application mode (high 4 bits)
	DW_EH_PE_pcrel    = 0x10,  // PC-relative (relative to current position)
	DW_EH_PE_textrel  = 0x20,  // Text section relative
	DW_EH_PE_datarel  = 0x30,  // Data section relative
	DW_EH_PE_funcrel  = 0x40,  // Function start relative
	DW_EH_PE_aligned  = 0x50,  // Aligned pointer
	
	// Modifier
	DW_EH_PE_indirect = 0x80,  // Indirect (dereference) pointer
};

// DW_CFA_* opcodes for Call Frame Address instructions
// These describe how to unwind the stack frame
enum DW_CFA : uint8_t {
	// Row creation instructions
	DW_CFA_nop                = 0x00,
	DW_CFA_set_loc            = 0x01,  // Set location (ULEB128 or encoded)
	DW_CFA_advance_loc1       = 0x02,  // Advance location by 1-byte delta
	DW_CFA_advance_loc2       = 0x03,  // Advance location by 2-byte delta
	DW_CFA_advance_loc4       = 0x04,  // Advance location by 4-byte delta
	
	// CFA definition instructions
	DW_CFA_def_cfa            = 0x0c,  // Define CFA as register + offset
	DW_CFA_def_cfa_register   = 0x0d,  // Define CFA register (offset unchanged)
	DW_CFA_def_cfa_offset     = 0x0e,  // Define CFA offset (register unchanged)
	DW_CFA_def_cfa_expression = 0x0f,  // CFA is computed by expression
	
	// Register save/restore instructions
	DW_CFA_undefined          = 0x07,  // Register is undefined
	DW_CFA_same_value         = 0x08,  // Register has same value as caller
	DW_CFA_register           = 0x09,  // Register saved in another register
	DW_CFA_remember_state     = 0x0a,  // Push CFA state on stack
	DW_CFA_restore_state      = 0x0b,  // Pop CFA state from stack
	DW_CFA_offset_extended    = 0x05,  // Register saved at CFA + offset
	DW_CFA_restore_extended   = 0x06,  // Restore register to initial state
	DW_CFA_val_offset         = 0x14,  // Register value is CFA + offset
	
	// High 2 bits encode instruction, low 6 bits encode operand
	DW_CFA_advance_loc        = 0x40,  // Low 6 bits = delta (0-63)
	DW_CFA_offset             = 0x80,  // Low 6 bits = register number
	DW_CFA_restore            = 0xc0,  // Low 6 bits = register number
};

// x86-64 DWARF register numbers
enum DwarfRegister : uint8_t {
	DW_REG_RAX = 0,
	DW_REG_RDX = 1,
	DW_REG_RCX = 2,
	DW_REG_RBX = 3,
	DW_REG_RSI = 4,
	DW_REG_RDI = 5,
	DW_REG_RBP = 6,
	DW_REG_RSP = 7,
	DW_REG_R8  = 8,
	DW_REG_R9  = 9,
	DW_REG_R10 = 10,
	DW_REG_R11 = 11,
	DW_REG_R12 = 12,
	DW_REG_R13 = 13,
	DW_REG_R14 = 14,
	DW_REG_R15 = 15,
	DW_REG_RIP = 16,  // Return address (x86-64 specific)
};

// Encode an unsigned value as LEB128 (Little Endian Base 128)
// Each byte encodes 7 bits of data; high bit set means more bytes follow
inline std::vector<uint8_t> encodeULEB128(uint64_t value) {
	std::vector<uint8_t> result;
	do {
		uint8_t byte = value & 0x7f;  // Take lower 7 bits
		value >>= 7;
		if (value != 0) {
			byte |= 0x80;  // Set high bit if more bytes to come
		}
		result.push_back(byte);
	} while (value != 0);
	return result;
}

// Encode a signed value as LEB128
// Similar to ULEB128 but handles sign extension
inline std::vector<uint8_t> encodeSLEB128(int64_t value) {
	std::vector<uint8_t> result;
	bool more = true;
	while (more) {
		uint8_t byte = value & 0x7f;  // Take lower 7 bits
		value >>= 7;
		
		// Check if we need more bytes
		// If value is 0 or -1, and sign bit of byte matches, we're done
		if ((value == 0 && (byte & 0x40) == 0) ||
		    (value == -1 && (byte & 0x40) != 0)) {
			more = false;
		} else {
			byte |= 0x80;  // Set high bit if more bytes to come
		}
		result.push_back(byte);
	}
	return result;
}

// Encode a pointer value based on the specified encoding type
// This is used in DWARF exception handling tables
inline std::vector<uint8_t> encodePointer(uint64_t value, uint8_t encoding) {
	std::vector<uint8_t> result;
	
	// Special case: omit (0xff)
	if (encoding == DW_EH_PE_omit) {
		return result;  // Empty result
	}
	
	// Extract format from low 4 bits
	uint8_t format = encoding & 0x0f;
	
	switch (format) {
		case DW_EH_PE_absptr:
			// Absolute pointer - 8 bytes on x86-64
			for (int i = 0; i < 8; ++i) {
				result.push_back((value >> (i * 8)) & 0xff);
			}
			break;
			
		case DW_EH_PE_uleb128:
			result = encodeULEB128(value);
			break;
			
		case DW_EH_PE_udata2:
			result.push_back(value & 0xff);
			result.push_back((value >> 8) & 0xff);
			break;
			
		case DW_EH_PE_udata4:
			for (int i = 0; i < 4; ++i) {
				result.push_back((value >> (i * 8)) & 0xff);
			}
			break;
			
		case DW_EH_PE_udata8:
			for (int i = 0; i < 8; ++i) {
				result.push_back((value >> (i * 8)) & 0xff);
			}
			break;
			
		case DW_EH_PE_sleb128:
			result = encodeSLEB128(static_cast<int64_t>(value));
			break;
			
		case DW_EH_PE_sdata2: {
			int16_t svalue = static_cast<int16_t>(value);
			result.push_back(svalue & 0xff);
			result.push_back((svalue >> 8) & 0xff);
			break;
		}
			
		case DW_EH_PE_sdata4: {
			int32_t svalue = static_cast<int32_t>(value);
			for (int i = 0; i < 4; ++i) {
				result.push_back((svalue >> (i * 8)) & 0xff);
			}
			break;
		}
			
		case DW_EH_PE_sdata8: {
			int64_t svalue = static_cast<int64_t>(value);
			for (int i = 0; i < 8; ++i) {
				result.push_back((svalue >> (i * 8)) & 0xff);
			}
			break;
		}
			
		default:
			throw std::runtime_error("Unsupported pointer encoding format");
	}
	
	return result;
}

// Helper to append a vector to another
template<typename T>
inline void appendVector(std::vector<T>& dest, const std::vector<T>& src) {
	dest.insert(dest.end(), src.begin(), src.end());
}

// Helper to encode and append ULEB128
inline void appendULEB128(std::vector<uint8_t>& dest, uint64_t value) {
	auto encoded = encodeULEB128(value);
	appendVector(dest, encoded);
}

// Helper to encode and append SLEB128
inline void appendSLEB128(std::vector<uint8_t>& dest, int64_t value) {
	auto encoded = encodeSLEB128(value);
	appendVector(dest, encoded);
}

} // namespace DwarfCFI
