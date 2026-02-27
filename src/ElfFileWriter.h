#pragma once

#include "AstNodeTypes.h"
#include "ObjectFileCommon.h"
#include "NameMangling.h"
#include "ChunkedString.h"
#include "CodeViewDebug.h"
#include "DwarfCFI.h"
#include "LSDAGenerator.h"
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <set>
#include <algorithm>

// ELFIO headers - header-only ELF library
#include "elfio/elfio.hpp"

extern bool g_enable_debug_output;

// SectionType is defined in ObjFileWriter.h
// We use the same enum for consistency between COFF and ELF

/**
 * @brief ELF object file writer for Linux target
 * 
 * This class generates ELF (Executable and Linkable Format) object files
 * for linking on Linux systems. It provides an interface compatible with
 * ObjectFileWriter to enable template-based code generation.
 * 
 * Design philosophy: Keep it simple and C-like, minimal inheritance.
 * Duck-typed interface compatibility with ObjectFileWriter via templates.
 */
class ElfFileWriter {
public:
	// Pointer size for 64-bit ELF
	static constexpr size_t POINTER_SIZE = 8;

	// Use shared structures from ObjectFileCommon
	using FunctionSignature = ObjectFileCommon::FunctionSignature;
	using CatchHandlerInfo = ObjectFileCommon::CatchHandlerInfo;
	using UnwindMapEntryInfo = ObjectFileCommon::UnwindMapEntryInfo;
	using TryBlockInfo = ObjectFileCommon::TryBlockInfo;
	using BaseClassDescriptorInfo = ObjectFileCommon::BaseClassDescriptorInfo;

	/**
	 * @brief Constructor - initializes ELF file structure
	 */
	ElfFileWriter() {
		if (g_enable_debug_output) {
			std::cerr << "Creating ElfFileWriter for Linux target..." << std::endl;
		}

		// Create ELF file with 64-bit little-endian format
		elf_writer_.create(ELFIO::ELFCLASS64, ELFIO::ELFDATA2LSB);

		// Set file header attributes
		elf_writer_.set_type(ELFIO::ET_REL);  // Relocatable object file
		elf_writer_.set_machine(ELFIO::EM_X86_64);  // x86-64 architecture
		elf_writer_.set_os_abi(ELFIO::ELFOSABI_NONE);  // No extensions or unspecified

		// Create standard ELF sections
		createStandardSections();

		if (g_enable_debug_output) {
			std::cerr << "ElfFileWriter initialized successfully" << std::endl;
		}
	}

	/**
	 * @brief Write ELF file to disk
	 */
	void write(const std::string& filename) {
		if (g_enable_debug_output) {
			std::cerr << "Writing ELF file: " << filename << std::endl;
			std::cerr << "  Sections: " << elf_writer_.sections.size() << std::endl;
			std::cerr << "  Symbols: " << getSymbolCount() << std::endl;
		}

		// Finalize sections before writing
		finalizeSections();

		// Save to file
		if (!elf_writer_.save(filename)) {
			throw std::runtime_error("Failed to write ELF file: " + filename);
		}

		if (g_enable_debug_output) {
			std::cerr << "ELF file written successfully" << std::endl;
		}
	}

	/**
	 * @brief Add a function symbol to the symbol table
	 */
	void add_function_symbol(std::string_view mangled_name, uint32_t section_offset, 
	                        [[maybe_unused]] uint32_t stack_space, [[maybe_unused]] Linkage linkage = Linkage::None) {
		if (g_enable_debug_output) {
			std::cerr << "Adding function symbol: " << mangled_name 
			          << " at offset " << section_offset << std::endl;
		}

		// Get symbol accessor
		auto sym_sec = getSectionByName(".symtab");
		if (!sym_sec) {
			throw std::runtime_error("Symbol table not found");
		}

		auto* accessor = getSymbolAccessor();
		if (!accessor) {
			throw std::runtime_error("Failed to get symbol accessor");
		}

		// Determine symbol binding based on whether the function is inline
		// Inline functions need STB_WEAK so the linker can discard duplicates
		bool is_inline = false;
		auto it = function_signatures_.find(mangled_name);
		if (it != function_signatures_.end()) {
			is_inline = it->second.is_inline;
		}

		// Add function symbol
		ELFIO::Elf64_Addr value = section_offset;
		ELFIO::Elf_Xword size = 0;  // Size will be updated later
		unsigned char bind = is_inline ? ELFIO::STB_WEAK : ELFIO::STB_GLOBAL;
		unsigned char type = ELFIO::STT_FUNC;  // Function type
		unsigned char other = ELFIO::STV_DEFAULT;  // Default visibility
		ELFIO::Elf_Half section_index = text_section_->get_index();

		// Convert string_view to std::string to ensure null-termination for C API
		std::string mangled_name_str(mangled_name);
		ELFIO::Elf_Word sym_idx = accessor->add_symbol(*string_accessor_, mangled_name_str.c_str(), value, size, bind, type, other, section_index);
		// Keep symbol cache in sync for O(1) lookups in finalize phase
		symbol_index_cache_[mangled_name_str] = sym_idx;

		// Track for debug info
		debug_pending_text_offset_ = section_offset;

		if (g_enable_debug_output) {
			std::cerr << "Function symbol added successfully" << std::endl;
		}
	}

	/**
	 * @brief Add data to a section
	 */
	void add_data(const std::vector<char>& data, SectionType section_type) {
		auto* section = getSectionForType(section_type);
		if (!section) {
			throw std::runtime_error("Section not found for type");
		}

		if (g_enable_debug_output) {
			std::cerr << "Adding " << data.size() << " bytes to section " 
			          << section->get_name() << std::endl;
		}

		// Append data to section
		section->append_data(data.data(), data.size());
	}

	void add_data(const std::vector<uint8_t>& data, SectionType section_type) {
		auto* section = getSectionForType(section_type);
		if (!section) {
			throw std::runtime_error("Section not found for type");
		}
		if (g_enable_debug_output) {
			std::cerr << "Adding " << data.size() << " bytes to section "
			          << section->get_name() << std::endl;
		}
		section->append_data(reinterpret_cast<const char*>(data.data()), data.size());
	}

	/**
	 * @brief Add a relocation entry (default: PLT32 for function calls)
	 * 
	 * Uses R_X86_64_PLT32 by default, which works for both external and internal
	 * function calls and is required for PIE (Position Independent Executable) linking.
	 */
	void add_relocation(uint64_t offset, std::string_view symbol_name) {
		add_relocation(offset, symbol_name, ELFIO::R_X86_64_PLT32);
	}

	/**
	 * @brief Add a relocation entry with specified type
	 */
	void add_relocation(uint64_t offset, std::string_view symbol_name, uint32_t relocation_type, int64_t addend = -4) {
		if (g_enable_debug_output) {
			std::cerr << "Adding relocation at offset " << offset 
			          << " for symbol " << symbol_name 
			          << " type " << relocation_type << std::endl;
		}

		// Get or create symbol
		auto symbol_index = getOrCreateSymbol(symbol_name, ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);

		// Add relocation to .rela.text section
		auto* rela_text = getSectionByName(".rela.text");
		if (!rela_text) {
			throw std::runtime_error("Relocation section .rela.text not found");
		}

		auto* rela_accessor = getRelocationAccessor(".rela.text");
		if (!rela_accessor) {
			throw std::runtime_error("Failed to get relocation accessor");
		}

		// Add relocation entry
		// For RELA format: offset, symbol, type, addend
		ELFIO::Elf64_Addr rel_offset = offset;
		
		// Construct r_info field for x86-64 ELF relocation
		// Format: r_info = (symbol_index << 32) | (relocation_type & 0xffffffff)
		// This matches the ELF64_R_INFO macro but we inline it to avoid macro namespace issues
		// Upper 32 bits: symbol table index
		// Lower 32 bits: relocation type (e.g., R_X86_64_PLT32 = 4, R_X86_64_PC32 = 2)
		ELFIO::Elf_Xword rel_info = (static_cast<ELFIO::Elf_Xword>(symbol_index) << 32) | 
		                             (static_cast<ELFIO::Elf_Xword>(relocation_type) & 0xffffffffUL);

		rela_accessor->add_entry(rel_offset, rel_info, addend);
	}

	/**
	 * @brief Add a string literal to .rodata section
	 * @return Symbol name for the string literal (as string_view to stable storage)
	 */
	std::string_view add_string_literal(std::string_view str_content) {
		// Generate unique symbol name using StringBuilder
		StringBuilder builder;
		builder.append(".L.str.");
		builder.append(static_cast<uint64_t>(string_literal_counter_++));
		std::string_view symbol_name_sv = builder.commit();

		// Get current offset in .rodata
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		uint32_t offset = rodata->get_size();

		// Process string (remove quotes, handle escapes)
		std::string processed_str = processStringLiteral(str_content);

		// Add to .rodata
		std::vector<char> str_data(processed_str.begin(), processed_str.end());
		rodata->append_data(str_data.data(), str_data.size());

		// Add symbol immediately as GLOBAL to work with relocation flow
		// String literals use unique .L.str.N names (per-translation-unit counter)
		// No collision risk across object files
		// NOTE: Ideally these would be LOCAL, but deferred symbol approach requires
		// complex relocation handling. This pragmatic solution works correctly.
		[[maybe_unused]] auto symbol_index = getOrCreateSymbol(symbol_name_sv, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
		                                      rodata->get_index(), offset, processed_str.size());

		if (g_enable_debug_output) {
			std::cerr << "Added string literal '" << processed_str << "' with symbol " 
			          << symbol_name_sv << std::endl;
		}

		return symbol_name_sv;  // Return string_view to stable storage in ChunkedStringAllocator
	}

	/**
	 * @brief Add a global variable
	 */

#include "ElfFileWriter_GlobalRTTI.h"
#include "ElfFileWriter_FuncSig.h"
#include "ElfFileWriter_EH.h"
