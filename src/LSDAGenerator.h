#pragma once

#include "DwarfCFI.h"
#include "ObjectFileCommon.h"
#include <vector>
#include <cstdint>
#include <string>

// LSDA (Language Specific Data Area) Generator
// Generates the .gcc_except_table section data for exception handling
//
// The LSDA contains:
// 1. Header with encoding information
// 2. Call site table - maps code regions to exception handlers
// 3. Action table - describes what to do when exception is caught
// 4. Type table - lists type_info pointers for exception type matching
//
// References:
// - Itanium C++ ABI Exception Handling
// - LSB Exception Frames specification

class LSDAGenerator {
public:
	// Information about a single catch handler
	struct CatchHandlerInfo {
		uint32_t type_index;       // Index into type table (0 for catch-all)
		std::string typeinfo_symbol;  // Symbol name of type_info (e.g., "_ZTIi" for int)
		bool is_catch_all;         // True for catch(...)
	};
	
	// Information about a try region and its handlers
	struct TryRegionInfo {
		uint32_t try_start_offset;    // Start of try block (relative to function)
		uint32_t try_length;          // Length of try block
		uint32_t landing_pad_offset;  // Start of catch handler(s)
		std::vector<CatchHandlerInfo> catch_handlers;  // Catch clauses
	};
	
	// Information for generating LSDA for a function
	struct FunctionLSDAInfo {
		std::vector<TryRegionInfo> try_regions;
		std::vector<std::string> type_table;  // Ordered list of type_info symbols
	};
	
	// Result of LSDA generation - includes data and relocation info
	struct LSDAGenerationResult {
		std::vector<uint8_t> data;
		// Type table relocations: offset (within LSDA) and symbol name
		std::vector<std::pair<uint32_t, std::string>> type_table_relocations;
	};
	
	// Generate LSDA binary data for a function
	LSDAGenerationResult generate(const FunctionLSDAInfo& info) {
		LSDAGenerationResult result;
		std::vector<uint8_t>& lsda_data = result.data;
		
		// Build type table first (to know offsets)
		std::vector<uint8_t> type_table_data;
		std::vector<std::pair<uint32_t, std::string>> type_table_relocs;
		encode_type_table(type_table_data, info, type_table_relocs);
		
		// Build action table
		std::vector<uint8_t> action_table_data;
		encode_action_table(action_table_data, info);
		
		// Build call site table
		std::vector<uint8_t> call_site_table_data;
		encode_call_site_table(call_site_table_data, info);
		
		// Now assemble the LSDA header with actual sizes
		encode_header(lsda_data, type_table_data.size(), call_site_table_data.size(), action_table_data.size());
		
		// Append call site table
		DwarfCFI::appendVector(lsda_data, call_site_table_data);
		
		// Append action table
		DwarfCFI::appendVector(lsda_data, action_table_data);
		
		// Compute type table start offset
		uint32_t type_table_start = static_cast<uint32_t>(lsda_data.size());
		
		// Append type table
		DwarfCFI::appendVector(lsda_data, type_table_data);
		
		// Adjust relocation offsets to be relative to LSDA start
		for (const auto& [offset, symbol] : type_table_relocs) {
			result.type_table_relocations.push_back({type_table_start + offset, symbol});
		}
		
		return result;
	}

private:
	// Encode LSDA header
	void encode_header(std::vector<uint8_t>& data, size_t type_table_size, 
	                  size_t call_site_table_size, size_t action_table_size) {
		// LPStart encoding (landing pad base)
		// 0xff = omitted (we use function-relative offsets)
		data.push_back(DwarfCFI::DW_EH_PE_omit);
		
		// TType encoding (type table encoding)
		// Use udata4 (0x03 = absolute 4-byte) to match GCC's encoding.
		// This uses R_X86_64_32 relocations for type_info pointers.
		// GCC generates this encoding for static/non-PIE executables.
		data.push_back(DwarfCFI::DW_EH_PE_udata4);
		
		// TType base offset (offset from this field to the start of the type table)
		// Includes: size of this ULEB itself + call_site_encoding + call_site_table_size_uleb +
		//           call_site_table + action_table
		uint64_t base_without_self = 1 + DwarfCFI::encodeULEB128(call_site_table_size).size() +
		                             call_site_table_size + action_table_size;
		uint64_t ttype_base = base_without_self + DwarfCFI::encodeULEB128(base_without_self).size();
		// If the encoded size changes after adding itself, recompute until stable (for larger tables)
		while (true) {
			auto encoded = DwarfCFI::encodeULEB128(ttype_base);
			uint64_t candidate = base_without_self + encoded.size();
			if (candidate == ttype_base) break;
			ttype_base = candidate;
		}
		DwarfCFI::appendULEB128(data, ttype_base);
		
		// Call site table encoding
		data.push_back(DwarfCFI::DW_EH_PE_uleb128);
		
		// Call site table size
		DwarfCFI::appendULEB128(data, call_site_table_size);
	}
	
	// Encode call site table
	void encode_call_site_table(std::vector<uint8_t>& data, const FunctionLSDAInfo& info) {
		for (const auto& try_region : info.try_regions) {
			// Call site entry:
			// - Start offset (ULEB128)
			// - Length (ULEB128)
			// - Landing pad offset (ULEB128) - 0 if no handler
			// - Action offset (ULEB128) - 1-based index into action table, 0 for no action
			
			DwarfCFI::appendULEB128(data, try_region.try_start_offset);
			DwarfCFI::appendULEB128(data, try_region.try_length);
			DwarfCFI::appendULEB128(data, try_region.landing_pad_offset);
			
			// Action index: 1-based index into action table
			// For now, use 1 for first try block (will be refined)
			DwarfCFI::appendULEB128(data, try_region.catch_handlers.empty() ? 0 : 1);
		}
	}
	
	// Encode action table
	void encode_action_table(std::vector<uint8_t>& data, const FunctionLSDAInfo& info) {
		// Action table entries describe what to do when exception is caught
		// Each entry has:
		// - Type filter (SLEB128) - positive for catch clause, 0 for cleanup, negative for exception spec
		// - Next action (SLEB128) - offset to next action or 0
		//
		// Type filter meanings (Itanium C++ ABI):
		// - Positive N: catch clause, match type at index N (1-based) in type table
		// - Zero: cleanup action (no type matching, always executed during unwind)
		// - Negative: exception specification filter (NOT used for regular catch clauses)
		
		// For now, generate simple action entries for each try region
		for (const auto& try_region : info.try_regions) {
			for (size_t i = 0; i < try_region.catch_handlers.size(); ++i) {
				const auto& handler = try_region.catch_handlers[i];
				
				if (handler.is_catch_all) {
					// Catch-all (catch(...)): type filter = 0 means cleanup
					// Actually, for catch(...), we use type filter that matches any exception
					// The personality routine treats filter 0 as cleanup (not catch-all)
					// For true catch-all, we need a type filter that always matches
					// Looking at GCC output: it uses filter > 0 pointing to a 0 type entry
					// But simpler approach: -1 as exception spec that matches all
					DwarfCFI::appendSLEB128(data, 0);  // 0 = cleanup, will run for any exception
				} else {
					// Find type index in type table (0-based)
					int type_index = find_type_index(info.type_table, handler.typeinfo_symbol);
					if (type_index < 0) {
						// Type not found in type table - treat as catch-all
						// This shouldn't happen but handle gracefully
						DwarfCFI::appendSLEB128(data, 0);
					} else {
						// Type filter is POSITIVE and 1-based for catch clauses
						// So index 0 in type table -> filter 1, index 1 -> filter 2, etc.
						DwarfCFI::appendSLEB128(data, type_index + 1);
					}
				}
				
				// Next action: 0 for last handler, else offset to next
				// For simplicity, chain handlers with offset = size of previous entry
				if (i + 1 < try_region.catch_handlers.size()) {
					// Size of this entry (type_filter + next_action, both SLEB128)
					// Approximate as 2 bytes for now (will be refined)
					DwarfCFI::appendSLEB128(data, 2);
				} else {
					DwarfCFI::appendSLEB128(data, 0);  // Last action
				}
			}
		}
	}
	
	// Encode type table with relocation tracking
	void encode_type_table(std::vector<uint8_t>& data, const FunctionLSDAInfo& info,
	                      std::vector<std::pair<uint32_t, std::string>>& relocations) {
		// Type table contains type_info pointers in reverse order
		// (type filter 1 refers to last entry, 2 to second-to-last, etc.)
		// Using udata4 encoding: each entry is a 4-byte absolute address
		
		for (const auto& typeinfo_symbol : info.type_table) {
			// Record relocation for this type_info pointer
			uint32_t offset = static_cast<uint32_t>(data.size());
			relocations.push_back({offset, typeinfo_symbol});
			
			// Each entry is a 4-byte absolute address (udata4)
			// Placeholder - will be filled by linker via R_X86_64_32 relocation
			for (int i = 0; i < 4; ++i) {
				data.push_back(0);
			}
		}
	}
	
	// Helper: find index of type_info symbol in type table
	int find_type_index(const std::vector<std::string>& type_table, 
	                   const std::string& typeinfo_symbol) const {
		for (size_t i = 0; i < type_table.size(); ++i) {
			if (type_table[i] == typeinfo_symbol) {
				return static_cast<int>(i);
			}
		}
		return -1;  // Not found
	}
	
	// Calculate action table size (approximate)
};
