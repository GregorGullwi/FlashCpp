#pragma once

#include "DwarfCFI.h"
#include "ObjectFileCommon.h"
#include <vector>
#include <cstdint>
#include <cassert>
#include <string>
#include <iostream>
#include <iomanip>

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
//
// MULTIPLE CATCH HANDLER SUPPORT:
// ===============================
// The action table generates chained entries for multiple catch handlers within a
// single try block.  Each action entry has a next_offset field that points to the
// next handler in the chain, allowing the personality routine to try each handler
// in sequence.
//
// Landing pads use a unified entry point with selector-based dispatch (RDX set by
// the personality routine).  See EXCEPTION_HANDLING.md § "Multi-Handler Landing
// Pad Dispatch" for the full layout.

class LSDAGenerator {
public:
 // Information about a single catch handler
	struct CatchHandlerInfo {
		uint32_t type_index;		 // Index into type table (0 for catch-all)
		std::string typeinfo_symbol;	 // Symbol name of type_info (e.g., "_ZTIi" for int)
		bool is_catch_all;		   // True for catch(...)
	};

 // Information about a try region and its handlers
	struct TryRegionInfo {
		uint32_t try_start_offset;	   // Start of try block (relative to function)
		uint32_t try_length;			 // Length of try block
		uint32_t landing_pad_offset;	 // Start of catch handler(s)
		std::vector<CatchHandlerInfo> catch_handlers;  // Catch clauses
		bool has_cleanup = false;	  // True if the action chain should end in a cleanup entry (type_filter=0)
	};

 // Information for generating LSDA for a function
	struct FunctionLSDAInfo {
		std::vector<TryRegionInfo> try_regions;
		std::vector<std::string> type_table;	 // Ordered list of type_info symbols
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

	// Make a mutable copy so we can add NULL type table entries for catch-all handlers.
	// In the Itanium C++ ABI, catch(...) requires a NULL entry in the type table
	// with a positive type_filter pointing to it. type_filter=0 means "cleanup" which
	// does NOT catch exceptions during the search phase.
		FunctionLSDAInfo effective_info = info;
		for (const auto& region : effective_info.try_regions) {
			for (const auto& handler : region.catch_handlers) {
				if (handler.is_catch_all) {
		// Add empty string sentinel for NULL type_info entry (catch-all)
					if (std::find(effective_info.type_table.begin(), effective_info.type_table.end(), "") == effective_info.type_table.end()) {
						effective_info.type_table.push_back("");
					}
				}
			}
		}

	// Build type table first (to know offsets)
		std::vector<uint8_t> type_table_data;
		std::vector<std::pair<uint32_t, std::string>> type_table_relocs;
		encode_type_table(type_table_data, effective_info, type_table_relocs);

	// Build action table with per-region action offsets
		std::vector<uint8_t> action_table_data;
		std::vector<uint32_t> region_action_offsets;
		encode_action_table(action_table_data, effective_info, region_action_offsets);

	// Build call site table using per-region action offsets
		std::vector<uint8_t> call_site_table_data;
		encode_call_site_table(call_site_table_data, effective_info, region_action_offsets);

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
	// Use indirect|pcrel|sdata4 (0x9b) to match GCC/Clang
	// 0x9b = DW_EH_PE_indirect (0x80) | DW_EH_PE_pcrel (0x10) | DW_EH_PE_sdata4 (0x0b)
	// This means type table entries are PC-relative pointers to .data entries
	// that contain the actual type_info addresses (via R_X86_64_64 relocations)
		data.push_back(0x9b);

	// TType base offset (offset from end of this field to end of type table)
	// This value points to the END of the type table because type_info
	// pointers are read in REVERSE order (type filter 1 is at end-4, 2 at end-8, etc.)
	// The offset is calculated from the byte AFTER this ULEB128 field.
	//
	// After the TType base ULEB128, we have:
	// - Call site encoding (1 byte)
	// - Call site table size (ULEB128, variable length)
	// - Call site table data
	// - Action table data
	// - Type table data
	//
	// We need to calculate the size of the call site table size ULEB128 first
		auto cs_size_uleb = DwarfCFI::encodeULEB128(call_site_table_size);
		size_t cs_size_uleb_len = cs_size_uleb.size();

	// Total offset from after TType base to end of type table
		uint64_t ttype_base = 1 + cs_size_uleb_len + call_site_table_size + action_table_size + type_table_size;
		DwarfCFI::appendULEB128(data, ttype_base);

	// Call site table encoding
		data.push_back(DwarfCFI::DW_EH_PE_uleb128);

	// Call site table size
		DwarfCFI::appendULEB128(data, call_site_table_size);
	}

 // Encode call site table with per-region action offsets
	void encode_call_site_table(std::vector<uint8_t>& data, const FunctionLSDAInfo& info,
								const std::vector<uint32_t>& region_action_offsets) {
		size_t handler_region_idx = 0;
		for (const auto& try_region : info.try_regions) {
	// Call site entry:
	// - Start offset (ULEB128)
	// - Length (ULEB128)
	// - Landing pad offset (ULEB128) - 0 if no handler
	// - Action offset (ULEB128) - 1-based byte offset into action table, 0 for no action

			uint32_t action = 0;
			if (!try_region.catch_handlers.empty()) {
				assert(handler_region_idx < region_action_offsets.size() && "action offset mismatch");
				action = region_action_offsets[handler_region_idx] + 1;	// 1-based
				handler_region_idx++;
			}

			if (g_enable_debug_output) {
				std::cerr << "[LSDA] Call site: start=" << try_region.try_start_offset
						  << " len=" << try_region.try_length
						  << " lpad=" << try_region.landing_pad_offset
						  << " action=" << action << std::endl;
			}

			DwarfCFI::appendULEB128(data, try_region.try_start_offset);
			DwarfCFI::appendULEB128(data, try_region.try_length);
			DwarfCFI::appendULEB128(data, try_region.landing_pad_offset);
			DwarfCFI::appendULEB128(data, action);
		}
	}

 // Encode action table with per-region action offset tracking
	void encode_action_table(std::vector<uint8_t>& data, const FunctionLSDAInfo& info,
							 std::vector<uint32_t>& region_action_offsets) {
	// Action table entries describe what to do when exception is caught
	// Each entry has:
	// - Type filter (SLEB128) - positive for catch clause, 0 for cleanup, negative for exception spec
	// - Next action (SLEB128) - offset to next action or 0
	//
	// Type filter meanings (Itanium C++ ABI):
	// - Positive N: catch clause, match type at index N (1-based) in type table
	// - Zero: cleanup action (no type matching, always executed during unwind)
	// - Negative: exception specification filter (NOT used for regular catch clauses)
	//
	// For multiple catch handlers in a single try block, actions are chained:
	// - Each action's next_offset is a signed byte offset from the end of the next_offset field
	//   to the start of the next action's type_filter field
	// - The last action in the chain has next_offset = 0
	// - The personality routine tries each handler in sequence until a match is found

	// Process each try region
		for (const auto& try_region : info.try_regions) {
			if (try_region.catch_handlers.empty()) {
				continue;  // No handlers, no action entry needed
			}

	// Record the byte offset where this region's first action entry starts
			region_action_offsets.push_back(static_cast<uint32_t>(data.size()));

	// Generate action entries for all catch handlers in this try region
	// We need to encode them and track positions to calculate offsets
			std::vector<std::vector<uint8_t>> action_entries;

	// First pass: encode all type filters
			for (size_t handler_idx = 0; handler_idx < try_region.catch_handlers.size(); ++handler_idx) {
				const auto& handler = try_region.catch_handlers[handler_idx];

				std::vector<uint8_t> type_filter_bytes;

	// Generate type filter
				if (handler.is_catch_all) {
		// Catch-all uses a NULL entry in the type table with a POSITIVE type_filter.
		// type_filter=0 means "cleanup" which does NOT catch during search phase.
		// A positive filter pointing to a NULL type table entry = catch-all.
					int catch_all_index = find_type_index(info.type_table, "");
					assert(catch_all_index >= 0 && "catch-all handler requires NULL entry in type table");
					int filter = static_cast<int>(info.type_table.size()) - catch_all_index;
					DwarfCFI::appendSLEB128(type_filter_bytes, filter);
				} else {
		// Find type index in type table (0-based)
					int type_index = find_type_index(info.type_table, handler.typeinfo_symbol);
					if (type_index < 0) {
						DwarfCFI::appendSLEB128(type_filter_bytes, 0);
					} else {
		// Type filter maps to type table entries read in REVERSE order.
		// Filter N refers to entry at (type_table_end - N * entry_size).
		// So filter = type_table_size - type_index.
						int filter = static_cast<int>(info.type_table.size()) - type_index;
						if (g_enable_debug_output) {
							std::cerr << "[DEBUG] Action table: handler_idx=" << handler_idx
									  << " type_index=" << type_index
									  << " filter=" << filter << std::endl;
						}
						DwarfCFI::appendSLEB128(type_filter_bytes, filter);
					}
				}

				action_entries.push_back(type_filter_bytes);
			}

	// Second pass: encode and write all entries with correct next_offset values.
	// If append_cleanup is true, a final cleanup record (type_filter=0, next=0)
	// is emitted after the typed catch entries. The last typed catch entry's
	// next_offset points to this cleanup record (next_offset=1 = 1 byte ahead,
	// since both type_filter and next_offset encode as single-byte SLEB128).
	// This causes __gxx_personality_v0 to enter the landing pad during phase-2
	// even when no typed catch handler matches (cleanup is always "handled").
			bool append_cleanup = try_region.has_cleanup && !action_entries.empty();
	// If appending cleanup, total_entries includes the cleanup record so the last
	// typed entry's is_last flag is false and it gets next_offset=1 instead of 0.
			size_t total_entries = action_entries.size() + (append_cleanup ? 1 : 0);
			for (size_t i = 0; i < action_entries.size(); ++i) {
				const auto& type_filter_bytes = action_entries[i];
				bool is_last = (i == total_entries - 1);

	// Write type filter
				DwarfCFI::appendVector(data, type_filter_bytes);

	// Calculate next_offset
	// The next_offset is a signed byte offset from the end of this next_offset field
	// to the start of the next action's type_filter field.
	// For sequentially laid-out actions, this is typically 1 byte when both
	// type_filter and next_offset encode as 1 byte each in SLEB128.
				int64_t next_offset = 0;
				if (!is_last) {
		// Simplified approach: assume next entry immediately follows
		// For typical cases where type_filter and next_offset are 1 byte each,
		// the offset is 1 byte from the end of current next_offset to the
		// start of next type_filter.
					next_offset = 1;

					if (g_enable_debug_output) {
						std::cerr << "[DEBUG] Action chaining: entry " << i
								  << " -> entry " << (i + 1)
								  << " next_offset=" << next_offset << std::endl;
					}
				}

				DwarfCFI::appendSLEB128(data, next_offset);
			}
	// Append cleanup action entry when required (type_filter=0, next=0)
			if (append_cleanup) {
				DwarfCFI::appendSLEB128(data, static_cast<int64_t>(0));	// type_filter=0 → cleanup
				DwarfCFI::appendSLEB128(data, static_cast<int64_t>(0));	// next=0 → end of chain
			}
		}

		if (g_enable_debug_output) {
			std::cerr << "[DEBUG] Action table size: " << data.size() << " bytes" << std::endl;
		}
	}

 // Encode type table with relocation tracking
	void encode_type_table(std::vector<uint8_t>& data, const FunctionLSDAInfo& info,
						   std::vector<std::pair<uint32_t, std::string>>& relocations) {
	// Type table contains type_info pointers in reverse order
	// (type filter 1 refers to last entry, 2 to second-to-last, etc.)
	//
	// Using pcrel|sdata4|indirect (0x9b) encoding:
	// Each entry is a 4-byte PC-relative signed offset that points to a GOT-like
	// entry containing the actual type_info address.
	// The relocation type should be R_X86_64_PC32.
	//
	// NOTE: Type table is read from the END backwards. Type filter 1 is at
	// base_offset - 4, filter 2 at base_offset - 8, etc.

	// Iterate in FORWARD order (they will be accessed in reverse via negative indices)
		for (const auto& typeinfo_symbol : info.type_table) {
			if (typeinfo_symbol.empty()) {
	// NULL entry for catch-all: no relocation, just 4 zero bytes
				for (int i = 0; i < 4; ++i) {
					data.push_back(0);
				}
			} else {
	// Record relocation for this type_info pointer
				uint32_t offset = static_cast<uint32_t>(data.size());
				relocations.push_back({offset, typeinfo_symbol});

	// Each entry is a 4-byte PC-relative pointer (sdata4)
	// Placeholder - will be filled by linker via R_X86_64_PC32 relocation
				for (int i = 0; i < 4; ++i) {
					data.push_back(0);
				}
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
