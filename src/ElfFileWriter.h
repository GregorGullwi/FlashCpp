#pragma once

#include "AstNodeTypes.h"
#include "ObjectFileCommon.h"
#include "NameMangling.h"
#include "ChunkedString.h"
#include "CodeViewDebug.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <fstream>

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
	                        uint32_t stack_space, Linkage linkage = Linkage::None) {
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

		// Add function symbol
		ELFIO::Elf64_Addr value = section_offset;
		ELFIO::Elf_Xword size = 0;  // Size will be updated later
		unsigned char bind = (linkage == Linkage::C || linkage == Linkage::None) ? 
		                     ELFIO::STB_GLOBAL : ELFIO::STB_GLOBAL;  // Global symbols
		unsigned char type = ELFIO::STT_FUNC;  // Function type
		unsigned char other = ELFIO::STV_DEFAULT;  // Default visibility
		ELFIO::Elf_Half section_index = text_section_->get_index();

		accessor->add_symbol(*string_accessor_, mangled_name.data(), value, size, bind, type, other, section_index);

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

	/**
	 * @brief Add a relocation entry (default: PC-relative 32-bit)
	 */
	void add_relocation(uint64_t offset, std::string_view symbol_name) {
		add_relocation(offset, symbol_name, ELFIO::R_X86_64_PC32);
	}

	/**
	 * @brief Add a relocation entry with specified type
	 */
	void add_relocation(uint64_t offset, std::string_view symbol_name, uint32_t relocation_type) {
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
		// Lower 32 bits: relocation type (e.g., R_X86_64_PC32 = 2)
		ELFIO::Elf_Xword rel_info = (static_cast<ELFIO::Elf_Xword>(symbol_index) << 32) | 
		                             (static_cast<ELFIO::Elf_Xword>(relocation_type) & 0xffffffffUL);
		ELFIO::Elf_Sxword addend = -4;  // Standard addend for PC-relative (compensate for instruction size)

		rela_accessor->add_entry(rel_offset, rel_info, addend);
	}

	/**
	 * @brief Add a string literal to .rodata section
	 * @return Symbol name for the string literal (as std::string for compatibility)
	 */
	std::string add_string_literal(std::string_view str_content) {
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
		auto symbol_index = getOrCreateSymbol(symbol_name_sv, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
		                                      rodata->get_index(), offset, processed_str.size());

		if (g_enable_debug_output) {
			std::cerr << "Added string literal '" << processed_str << "' with symbol " 
			          << symbol_name_sv << std::endl;
		}

		return std::string(symbol_name_sv);
	}

	/**
	 * @brief Add a global variable
	 */
	void add_global_variable_data(std::string_view var_name, size_t size_in_bytes,
	                              bool is_initialized, const std::vector<char>& init_data) {
		if (g_enable_debug_output) {
			std::cerr << "Adding global variable: " << var_name 
			          << " size=" << size_in_bytes 
			          << " initialized=" << is_initialized << std::endl;
		}

		ELFIO::section* section;
		if (is_initialized) {
			section = getSectionByName(".data");
			if (!section) {
				throw std::runtime_error(".data section not found");
			}

			uint32_t offset = section->get_size();

			// Add initialized data
			if (!init_data.empty()) {
				section->append_data(init_data.data(), init_data.size());
			} else {
				// Zero-initialized
				std::vector<char> zero_data(size_in_bytes, 0);
				section->append_data(zero_data.data(), zero_data.size());
			}

			// Add symbol
			getOrCreateSymbol(var_name, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
			                 section->get_index(), offset, size_in_bytes);
		} else {
			// Uninitialized - goes in .bss
			section = getSectionByName(".bss");
			if (!section) {
				throw std::runtime_error(".bss section not found");
			}

			uint32_t offset = section->get_size();

			// For .bss, we just increase the size (no actual data)
			section->set_size(section->get_size() + size_in_bytes);

			// Add symbol
			getOrCreateSymbol(var_name, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
			                 section->get_index(), offset, size_in_bytes);
		}
	}

	/**
	 * @brief Add vtable for C++ class
	 * Itanium C++ ABI vtable format: array of function pointers
	 */
	void add_vtable(const std::string& vtable_symbol, 
	               const std::vector<std::string>& function_symbols,
	               const std::string& class_name,
	               const std::vector<std::string>& base_class_names,
	               const std::vector<BaseClassDescriptorInfo>& base_class_info) {
		
		if (g_enable_debug_output) {
			std::cerr << "Adding vtable '" << vtable_symbol << "' for class " << class_name
			          << " with " << function_symbols.size() << " virtual functions" << std::endl;
		}
		
		// Get .rodata section (vtables go in read-only data)
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		
		uint32_t vtable_offset = rodata->get_size();
		
		// Itanium C++ ABI vtable structure:
		// - Offset to top (8 bytes) - always 0 for simple cases
		// - RTTI pointer (8 bytes) - null for now (RTTI deferred to future milestone)
		// - Function pointers (8 bytes each)
		
		std::vector<char> vtable_data;
		
		// Offset to top (8 bytes, value = 0)
		for (int i = 0; i < 8; ++i) {
			vtable_data.push_back(0);
		}
		
		// RTTI pointer (8 bytes, null for now)
		for (int i = 0; i < 8; ++i) {
			vtable_data.push_back(0);
		}
		
		// Function pointers (8 bytes each, will be filled by relocations)
		for (size_t i = 0; i < function_symbols.size(); ++i) {
			for (int j = 0; j < 8; ++j) {
				vtable_data.push_back(0);
			}
		}
		
		// Add vtable data to .rodata
		rodata->append_data(vtable_data.data(), vtable_data.size());
		
		// Add vtable symbol pointing to the function pointer array (skip offset-to-top and RTTI)
		uint32_t symbol_offset = vtable_offset + 16;  // Skip offset-to-top (8) and RTTI (8)
		getOrCreateSymbol(vtable_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
		                  rodata->get_index(), symbol_offset, vtable_data.size() - 16);
		
		// Add relocations for each function pointer
		auto* rela_rodata = getOrCreateRelocationSection(".rodata");
		auto* rela_accessor = getRelocationAccessor(".rela.rodata");
		
		for (size_t i = 0; i < function_symbols.size(); ++i) {
			// Get or create symbol for the function
			auto func_symbol_idx = getOrCreateSymbol(function_symbols[i], ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);
			
			// Add relocation for this function pointer
			uint32_t reloc_offset = vtable_offset + 16 + (i * 8);  // Skip header + i*8 for function pointer
			rela_accessor->add_entry(reloc_offset, func_symbol_idx, ELFIO::R_X86_64_64, 0);
			
			if (g_enable_debug_output) {
				std::cerr << "  Added relocation for function " << function_symbols[i] 
				          << " at offset " << reloc_offset << std::endl;
			}
		}
		
		if (g_enable_debug_output) {
			std::cerr << "Vtable '" << vtable_symbol << "' added at offset " << symbol_offset 
			          << " with " << function_symbols.size() << " function pointers" << std::endl;
		}
	}

	/**
	 * @brief Get mangled name (for now, return as-is - Itanium mangling deferred)
	 */
	std::string getMangledName(std::string_view name) const {
		// MVP: Use C linkage (unmangled names)
		// For MVP, return a copy since IRConverter expects std::string
		// When we implement Itanium mangling, we'll use StringBuilder and store in function_signatures_
		// TODO: Implement Itanium name mangling in future milestone
		return std::string(name);
	}

	/**
	 * @brief Generate mangled name (placeholder)
	 * Returns the mangled name as a string (not string_view) since we may need to create it
	 */
	std::string generateMangledName(std::string_view name, const FunctionSignature& sig) const {
		// MVP: Use C linkage for simplicity - just return a copy
		// TODO: Implement Itanium mangling for C++ which will generate new strings
		return std::string(name);
	}

	/**
	 * @brief Add function signature (for future name mangling)
	 * Returns std::string (not string_view) to ensure stable storage
	 */
	std::string addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                                const std::vector<TypeSpecifierNode>& parameter_types,
	                                Linkage linkage = Linkage::None, bool is_variadic = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		std::string mangled = generateMangledName(name, sig);
		function_signatures_[mangled] = sig;
		return mangled;
	}

	// Overload that accepts pre-computed mangled name (without class_name)
	void addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                          const std::vector<TypeSpecifierNode>& parameter_types,
	                          Linkage linkage, bool is_variadic,
	                          std::string_view mangled_name) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		function_signatures_[std::string(mangled_name)] = sig;
	}

	// Overloads for member functions (compatibility with ObjectFileWriter)
	std::string addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                                const std::vector<TypeSpecifierNode>& parameter_types,
	                                std::string_view class_name, Linkage linkage = Linkage::None,
	                                bool is_variadic = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.class_name = class_name;
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		std::string mangled = generateMangledName(name, sig);
		function_signatures_[mangled] = sig;
		return mangled;
	}

	// Overload that accepts pre-computed mangled name (for function definitions from IR)
	void addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                          const std::vector<TypeSpecifierNode>& parameter_types,
	                          std::string_view class_name, Linkage linkage, bool is_variadic,
	                          std::string_view mangled_name) {
		FunctionSignature sig(return_type, parameter_types);
		sig.class_name = class_name;
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		function_signatures_[std::string(mangled_name)] = sig;
	}

	// Debug info methods (placeholders - DWARF support deferred)
	void add_source_file(const std::string& filename) {
		// Placeholder for DWARF debug info
	}

	void set_current_function_for_debug(const std::string& name, uint32_t file_id) {
		// Placeholder
	}

	void add_line_mapping(uint32_t code_offset, uint32_t line_number) {
		// Placeholder
	}

	void add_local_variable(const std::string& name, uint32_t type_index, uint16_t flags,
	                       const std::vector<CodeView::VariableLocation>& locations) {
		// Placeholder - DWARF debug info implementation deferred
	}

	void add_function_parameter(const std::string& name, uint32_t type_index, int32_t stack_offset) {
		// Placeholder
	}

	void update_function_length(const std::string& name, uint32_t code_length) {
		// Placeholder
	}

	void set_function_debug_range(const std::string& name, uint32_t prologue_size, uint32_t epilogue_size) {
		// Placeholder
	}

	void finalize_current_function() {
		// Placeholder
	}

	void finalize_debug_info() {
		// Placeholder for DWARF generation
		if (g_enable_debug_output) {
			std::cerr << "DWARF debug info not yet implemented" << std::endl;
		}
	}

	// Exception handling (placeholders - not implemented for Linux MVP)
	void add_function_exception_info(std::string_view mangled_name, uint32_t function_start,
	                                 uint32_t function_size, const std::vector<TryBlockInfo>& try_blocks = {},
	                                 const std::vector<UnwindMapEntryInfo>& unwind_map = {}) {
		// Placeholder - exception handling deferred
	}

	// Additional compatibility methods
	void add_text_relocation(uint64_t offset, const std::string& symbol_name, uint32_t relocation_type) {
		add_relocation(offset, symbol_name, relocation_type);
	}

	void add_pdata_relocations(uint32_t pdata_offset, std::string_view mangled_name, uint32_t xdata_offset) {
		// Not needed for ELF - Windows-specific
	}

	void add_xdata_relocation(uint32_t xdata_offset, std::string_view handler_name) {
		// Not needed for ELF - Windows-specific
	}

	void add_debug_relocation(uint32_t offset, const std::string& symbol_name, uint32_t relocation_type) {
		// Placeholder for DWARF relocations
	}

private:
	ELFIO::elfio elf_writer_;  // ELFIO writer instance
	
	// Section pointers for quick access
	ELFIO::section* text_section_ = nullptr;
	ELFIO::section* data_section_ = nullptr;
	ELFIO::section* bss_section_ = nullptr;
	ELFIO::section* rodata_section_ = nullptr;
	ELFIO::section* symtab_section_ = nullptr;
	ELFIO::section* strtab_section_ = nullptr;
	ELFIO::section* rela_text_section_ = nullptr;

	// Symbol table accessor
	std::unique_ptr<ELFIO::symbol_section_accessor> symbol_accessor_;
	
	// String table accessor for symbol names
	std::unique_ptr<ELFIO::string_section_accessor> string_accessor_;
	
	// Relocation accessors (one per section that needs relocations)
	std::unordered_map<std::string, std::unique_ptr<ELFIO::relocation_section_accessor>> rela_accessors_;

	// String literal counter for generating unique names
	uint32_t string_literal_counter_ = 0;

	// Function signatures for name mangling
	std::unordered_map<std::string, FunctionSignature> function_signatures_;

	/**
	 * @brief Create standard ELF sections
	 */
	void createStandardSections() {
		// .text - executable code
		text_section_ = elf_writer_.sections.add(".text");
		text_section_->set_type(ELFIO::SHT_PROGBITS);
		text_section_->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
		text_section_->set_addr_align(16);

		// .data - initialized data
		data_section_ = elf_writer_.sections.add(".data");
		data_section_->set_type(ELFIO::SHT_PROGBITS);
		data_section_->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
		data_section_->set_addr_align(8);

		// .bss - uninitialized data
		bss_section_ = elf_writer_.sections.add(".bss");
		bss_section_->set_type(ELFIO::SHT_NOBITS);
		bss_section_->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
		bss_section_->set_addr_align(8);

		// .rodata - read-only data (constants, strings)
		rodata_section_ = elf_writer_.sections.add(".rodata");
		rodata_section_->set_type(ELFIO::SHT_PROGBITS);
		rodata_section_->set_flags(ELFIO::SHF_ALLOC);
		rodata_section_->set_addr_align(16);

		// .symtab - symbol table
		symtab_section_ = elf_writer_.sections.add(".symtab");
		symtab_section_->set_type(ELFIO::SHT_SYMTAB);
		symtab_section_->set_addr_align(8);
		symtab_section_->set_entry_size(elf_writer_.get_default_entry_size(ELFIO::SHT_SYMTAB));

		// .strtab - string table
		strtab_section_ = elf_writer_.sections.add(".strtab");
		strtab_section_->set_type(ELFIO::SHT_STRTAB);
		strtab_section_->set_addr_align(1);

		// Link symtab to strtab
		symtab_section_->set_link(strtab_section_->get_index());

		// Create symbol accessor
		symbol_accessor_ = std::make_unique<ELFIO::symbol_section_accessor>(
			elf_writer_, symtab_section_
		);

		// Create string accessor for symbol names
		string_accessor_ = std::make_unique<ELFIO::string_section_accessor>(strtab_section_);

		// .rela.text - relocations for .text section
		rela_text_section_ = elf_writer_.sections.add(".rela.text");
		rela_text_section_->set_type(ELFIO::SHT_RELA);
		rela_text_section_->set_info(text_section_->get_index());
		rela_text_section_->set_link(symtab_section_->get_index());
		rela_text_section_->set_addr_align(8);
		rela_text_section_->set_entry_size(elf_writer_.get_default_entry_size(ELFIO::SHT_RELA));

		// Create relocation accessor for .text
		rela_accessors_[".rela.text"] = std::make_unique<ELFIO::relocation_section_accessor>(
			elf_writer_, rela_text_section_
		);

		// .note.GNU-stack - marks stack as non-executable (security feature)
		// This section is empty but must be present to avoid linker warnings
		ELFIO::section* gnu_stack_section = elf_writer_.sections.add(".note.GNU-stack");
		gnu_stack_section->set_type(ELFIO::SHT_PROGBITS);
		gnu_stack_section->set_flags(0);  // No flags = non-executable stack
		gnu_stack_section->set_addr_align(1);
		gnu_stack_section->set_size(0);  // Empty section

		if (g_enable_debug_output) {
			std::cerr << "Created standard ELF sections" << std::endl;
		}
	}

	/**
	 * @brief Get section by name
	 */
	ELFIO::section* getSectionByName(const std::string& name) {
		for (auto& sec : elf_writer_.sections) {
			if (sec->get_name() == name) {
				return sec.get();  // Get raw pointer from unique_ptr
			}
		}
		return nullptr;
	}

	/**
	 * @brief Get section for a given SectionType
	 */
	ELFIO::section* getSectionForType(SectionType type) {
		// Map SectionType enum to ELF sections
		switch (type) {
			case SectionType::TEXT: return text_section_;
			case SectionType::DATA: return data_section_;
			case SectionType::BSS: return bss_section_;
			case SectionType::RDATA: return rodata_section_;
			default: return nullptr;
		}
	}

	/**
	 * @brief Get symbol accessor
	 */
	ELFIO::symbol_section_accessor* getSymbolAccessor() {
		return symbol_accessor_.get();
	}

	/**
	 * @brief Get or create relocation section for a data section
	 */
	ELFIO::section* getOrCreateRelocationSection(const std::string& section_name) {
		std::string rela_name = ".rela" + section_name;
		
		// Check if it already exists
		auto* existing = getSectionByName(rela_name);
		if (existing) {
			return existing;
		}
		
		// Get the target section
		auto* target_section = getSectionByName(section_name);
		if (!target_section) {
			throw std::runtime_error("Target section " + section_name + " not found");
		}
		
		// Create new relocation section
		auto* rela_section = elf_writer_.sections.add(rela_name);
		rela_section->set_type(ELFIO::SHT_RELA);
		rela_section->set_info(target_section->get_index());
		rela_section->set_link(symtab_section_->get_index());
		rela_section->set_addr_align(8);
		rela_section->set_entry_size(elf_writer_.get_default_entry_size(ELFIO::SHT_RELA));
		
		// Create relocation accessor
		rela_accessors_[rela_name] = std::make_unique<ELFIO::relocation_section_accessor>(
			elf_writer_, rela_section
		);
		
		if (g_enable_debug_output) {
			std::cerr << "Created relocation section " << rela_name << std::endl;
		}
		
		return rela_section;
	}

	/**
	 * @brief Get relocation accessor for a section
	 */
	ELFIO::relocation_section_accessor* getRelocationAccessor(const std::string& rela_section_name) {
		auto it = rela_accessors_.find(rela_section_name);
		if (it != rela_accessors_.end()) {
			return it->second.get();
		}
		return nullptr;
	}

	/**
	 * @brief Get or create a symbol in the symbol table
	 * @return Symbol index
	 */
	ELFIO::Elf_Word getOrCreateSymbol(std::string_view name, unsigned char type, unsigned char bind,
	                                   ELFIO::Elf_Half section_index = ELFIO::SHN_UNDEF,
	                                   ELFIO::Elf64_Addr value = 0, ELFIO::Elf_Xword size = 0) {
		// Check if symbol already exists
		auto* accessor = getSymbolAccessor();
		ELFIO::Elf_Xword sym_count = accessor->get_symbols_num();
		
		for (ELFIO::Elf_Xword i = 0; i < sym_count; ++i) {
			std::string sym_name;
			ELFIO::Elf64_Addr sym_value;
			ELFIO::Elf_Xword sym_size;
			unsigned char sym_bind, sym_type;
			ELFIO::Elf_Half sym_section;
			unsigned char sym_other;

			accessor->get_symbol(i, sym_name, sym_value, sym_size, sym_bind, sym_type, sym_section, sym_other);
			
			if (sym_name == name) {
				return i;  // Symbol already exists
			}
		}

		// Symbol doesn't exist, create it
		return accessor->add_symbol(*string_accessor_, name.data(), value, size, bind, type, ELFIO::STV_DEFAULT, section_index);
	}

	/**
	 * @brief Get symbol count
	 */
	size_t getSymbolCount() const {
		if (!symbol_accessor_) return 0;
		return symbol_accessor_->get_symbols_num();
	}

	/**
	 * @brief Process string literal (remove quotes, handle escapes)
	 */
	std::string processStringLiteral(std::string_view str_content) {
		std::string result;
		
		if (str_content.size() >= 2 && str_content.front() == '"' && str_content.back() == '"') {
			// Remove quotes
			std::string_view content(str_content.data() + 1, str_content.size() - 2);
			
			// Process escape sequences
			for (size_t i = 0; i < content.size(); ++i) {
				if (content[i] == '\\' && i + 1 < content.size()) {
					switch (content[i + 1]) {
						case 'n': result += '\n'; ++i; break;
						case 't': result += '\t'; ++i; break;
						case 'r': result += '\r'; ++i; break;
						case '\\': result += '\\'; ++i; break;
						case '"': result += '"'; ++i; break;
						case '0': result += '\0'; ++i; break;
						default: result += content[i]; break;
					}
				} else {
					result += content[i];
				}
			}
		} else {
			result = std::string(str_content);
		}
		
		// Add null terminator
		result += '\0';
		return result;
	}

	/**
	 * @brief Finalize sections before writing
	 */
	void finalizeSections() {
		// Update section sizes and offsets
		// ELFIO handles most of this automatically, but we need to set sh_info for .symtab
		
		// For .symtab, sh_info should point to the first non-local symbol
		// Count all local symbols
		auto* accessor = getSymbolAccessor();
		if (accessor) {
			ELFIO::Elf_Xword sym_count = accessor->get_symbols_num();
			ELFIO::Elf_Xword first_global = sym_count;  // Default to end if no globals
			
			for (ELFIO::Elf_Xword i = 0; i < sym_count; ++i) {
				std::string sym_name;
				ELFIO::Elf64_Addr sym_value;
				ELFIO::Elf_Xword sym_size;
				unsigned char sym_bind, sym_type;
				ELFIO::Elf_Half sym_section;
				unsigned char sym_other;

				accessor->get_symbol(i, sym_name, sym_value, sym_size, sym_bind, sym_type, sym_section, sym_other);
				
				// Check if this is a local symbol (bind == STB_LOCAL)
				if (sym_bind != ELFIO::STB_LOCAL) {
					first_global = i;
					break;
				}
			}
			
			// Set sh_info to index of first global symbol
			symtab_section_->set_info(first_global);
			
			if (g_enable_debug_output) {
				std::cerr << "Symbol table: " << sym_count << " total symbols, first global at index " << first_global << std::endl;
			}
		}
		
		if (g_enable_debug_output) {
			std::cerr << "Finalizing sections:" << std::endl;
			std::cerr << "  .text size: " << text_section_->get_size() << std::endl;
			std::cerr << "  .data size: " << data_section_->get_size() << std::endl;
			std::cerr << "  .bss size: " << bss_section_->get_size() << std::endl;
			std::cerr << "  .rodata size: " << rodata_section_->get_size() << std::endl;
		}
	}

private:
};
