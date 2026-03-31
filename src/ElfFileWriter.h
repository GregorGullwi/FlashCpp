#pragma once

#include "AstNodeTypes.h"
#include "ObjectFileCommon.h"
#include "NameMangling.h"
#include "StringBuilder.h"
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
		elf_writer_.set_type(ELFIO::ET_REL);	 // Relocatable object file
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
	 * @brief Add a data section relocation (R_X86_64_64) for a global pointer/reference
	 *        initialized with the address of another symbol.
	 * @param var_name  The global variable whose .data slot should receive the address
	 * @param target_name  The symbol whose absolute address is stored
	 */
	void add_data_relocation(std::string_view var_name, std::string_view target_name) {
		// Find the .data section
		auto* data_section = getSectionByName(".data");
		if (!data_section)
			return;

		// Find the variable's offset in .data by looking up its symbol
		ELFIO::Elf64_Addr var_offset = 0;
		bool found = false;
		auto* sym_section = getSectionByName(".symtab");
		if (sym_section) {
			ELFIO::symbol_section_accessor sym_accessor(elf_writer_, sym_section);
			for (ELFIO::Elf_Xword i = 0; i < sym_accessor.get_symbols_num(); ++i) {
				std::string name;
				ELFIO::Elf64_Addr value;
				ELFIO::Elf_Xword size;
				unsigned char bind, type, other;
				ELFIO::Elf_Half section_index;
				sym_accessor.get_symbol(i, name, value, size, bind, type, section_index, other);
				if (name == var_name && section_index == data_section->get_index()) {
					var_offset = value;
					found = true;
					break;
				}
			}
		}
		if (!found)
			return;

		// Get or create the target symbol (may be in .data, .bss, or external)
		auto target_index = getOrCreateSymbol(target_name, ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);

		// Get or create .rela.data section
		auto* rela_data = getSectionByName(".rela.data");
		if (!rela_data) {
			rela_data = elf_writer_.sections.add(".rela.data");
			rela_data->set_type(ELFIO::SHT_RELA);
			rela_data->set_flags(ELFIO::SHF_INFO_LINK);
			rela_data->set_info(data_section->get_index());
			rela_data->set_link(getSectionByName(".symtab")->get_index());
			rela_data->set_addr_align(8);
			rela_data->set_entry_size(sizeof(ELFIO::Elf64_Rela));
		}

		// Add R_X86_64_64 relocation: absolute 64-bit address
		ELFIO::relocation_section_accessor rela_accessor(elf_writer_, rela_data);
		ELFIO::Elf_Xword rel_info = (static_cast<ELFIO::Elf_Xword>(target_index) << 32) |
									(static_cast<ELFIO::Elf_Xword>(ELFIO::R_X86_64_64) & 0xffffffffUL);
		rela_accessor.add_entry(var_offset, rel_info, 0);

		if (g_enable_debug_output) {
			std::cerr << "Added data relocation: " << var_name << " -> " << target_name
					  << " at offset " << var_offset << std::endl;
		}
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

	// --- Method declarations (ElfFileWriter_GlobalRTTI.cpp) ---
	void add_global_variable_data(std::string_view var_name, size_t size_in_bytes, bool is_initialized, std::span<const char> init_data, bool is_rodata = false);
	void add_typeinfo(std::string_view typeinfo_symbol, const void* typeinfo_data, size_t typeinfo_size);
	std::string get_or_create_builtin_typeinfo(TypeCategory cat);
	std::string get_or_create_class_typeinfo(std::string_view class_name);
	std::string get_or_create_class_typeinfo(const StructTypeInfo* struct_info);
	void add_vtable(std::string_view vtable_symbol, std::span<const std::string_view> function_symbols, std::string_view class_name, std::span<const std::string_view> base_class_names, std::span<const BaseClassDescriptorInfo> base_class_info, const RTTITypeInfo* rtti_info = nullptr);
	std::string_view generateMangledName(std::string_view name, const FunctionSignature& sig);
	std::string_view addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, Linkage linkage = Linkage::None, bool is_variadic = false);

	// --- Method declarations (ElfFileWriter_FuncSig.cpp) ---
	void addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, Linkage linkage, bool is_variadic, std::string_view mangled_name, bool is_inline = false);
	std::string_view addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, std::string_view class_name, Linkage linkage = Linkage::None, bool is_variadic = false);
	void addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, std::string_view class_name, Linkage linkage, bool is_variadic, std::string_view mangled_name, bool is_inline = false);
	void add_source_file(const std::string& filename);
	void set_current_function_for_debug(const std::string& name, uint32_t file_id);
	void add_line_mapping(uint32_t code_offset, uint32_t line_number);
	void add_local_variable(const std::string& name, uint32_t type_index, uint16_t flags, const std::vector<CodeView::VariableLocation>& locations);
	void add_function_parameter(const std::string& name, uint32_t type_index, int32_t stack_offset);
	void update_function_length(const std::string_view mangled_name, uint32_t code_length);
	void set_function_debug_range(const std::string_view manged_name, uint32_t prologue_size, uint32_t epilogue_size);
	void finalize_current_function();
	void finalize_debug_info();

	// --- Nested types (ElfFileWriter_FuncSig/EH) ---
	struct CFIInstruction {
		enum Type {
			PUSH_RBP,
			MOV_RSP_RBP,
			SUB_RSP,
			ADD_RSP,
			POP_RBP,
			REMEMBER_STATE,
			RESTORE_STATE,
		};
		Type type;
		uint32_t offset;
		uint32_t value;
	};

	struct FDEInfo {
		uint32_t function_start_offset;
		uint32_t function_length;
		std::string function_symbol;
		std::vector<CFIInstruction> cfi_instructions;
		bool has_exception_handling = false;
		std::string lsda_symbol;
		uint32_t lsda_offset = 0;
		uint32_t pc_begin_offset = 0;
		uint32_t lsda_pointer_offset = 0;
	};

	// Cleanup landing pad info for function-level stack unwinding (ELF Phase 2)
	struct CleanupBlockInfo {
		uint32_t region_start;   // Start of code region that needs cleanup (usually 0)
		uint32_t region_end;	 // End of region = start of the cleanup LP itself
		uint32_t cleanup_lp;	 // Offset of the cleanup landing pad
	};

	// --- Method declarations (ElfFileWriter_EH.cpp) ---
	void add_function_exception_info(std::string_view mangled_name, uint32_t function_start, uint32_t function_size, const std::vector<TryBlockInfo>& try_blocks = {}, const std::vector<UnwindMapEntryInfo>& unwind_map = {}, const std::vector<CFIInstruction>& cfi_instructions = {}, const std::vector<CleanupBlockInfo>& cleanup_blocks = {});
	std::string get_typeinfo_symbol(const std::string& type_name) const;
	void add_text_relocation(uint64_t offset, const std::string& symbol_name, uint32_t relocation_type, int64_t addend = -4);
	void add_pdata_relocations(uint32_t pdata_offset, std::string_view mangled_name, uint32_t xdata_offset);
	void add_xdata_relocation(uint32_t xdata_offset, std::string_view handler_name);
	void add_debug_relocation(uint32_t offset, const std::string& symbol_name, uint32_t relocation_type);

private:
	// --- Private method declarations (ElfFileWriter_EH.cpp) ---
	void generate_eh_frame_cie(std::vector<uint8_t>& eh_frame_data, bool has_exception_handlers);
	void generate_eh_frame_fde(std::vector<uint8_t>& eh_frame_data, uint32_t cie_offset, FDEInfo& fde_info, bool cie_has_lsda);
	void generate_eh_frame();
	void generate_gcc_except_table();
	void createStandardSections();
	ELFIO::section* getSectionByName(const std::string& name);
	ELFIO::section* getSectionForType(SectionType type);
	ELFIO::symbol_section_accessor* getSymbolAccessor();
	ELFIO::section* getOrCreateRelocationSection(const std::string& section_name);
	ELFIO::relocation_section_accessor* getRelocationAccessor(const std::string& rela_section_name);
	ELFIO::Elf_Word getOrCreateSymbol(std::string_view name, unsigned char type, unsigned char bind, ELFIO::Elf_Half section_index = ELFIO::SHN_UNDEF, ELFIO::Elf64_Addr value = 0, ELFIO::Elf_Xword size = 0);
	size_t getSymbolCount() const;
	std::string processStringLiteral(std::string_view str_content);
	void finalizeSections();
	void buildSymbolIndexCache();
	ELFIO::Elf_Word lookupSymbolIndex(const std::string& name);
	void updateSymbolSize(const std::string& name, uint32_t size);
	static int dwarfRegFromCodeViewReg(uint16_t cv);
	static void dw_u8(std::vector<uint8_t>& b, uint8_t v);
	static void dw_u16(std::vector<uint8_t>& b, uint16_t v);
	static void dw_u32(std::vector<uint8_t>& b, uint32_t v);
	static void dw_u64(std::vector<uint8_t>& b, uint64_t v);
	static void dw_uleb(std::vector<uint8_t>& b, uint64_t v);
	static void dw_sleb(std::vector<uint8_t>& b, int64_t v);
	static void dw_patch32(std::vector<uint8_t>& b, uint32_t off, uint32_t v);
	static std::vector<uint8_t> dwarfFbregExpr(int32_t rbp_off);
	static std::vector<uint8_t> dwarfRegExpr(int dwarf_reg);
	ELFIO::Elf_Word dwarfSymbolIndex(const std::string& name);
	void generateDwarfSections();

	// --- Data members ---
	ELFIO::elfio elf_writer_;
	ELFIO::section* text_section_ = nullptr;
	ELFIO::section* data_section_ = nullptr;
	ELFIO::section* bss_section_ = nullptr;
	ELFIO::section* rodata_section_ = nullptr;
	ELFIO::section* symtab_section_ = nullptr;
	ELFIO::section* strtab_section_ = nullptr;
	ELFIO::section* rela_text_section_ = nullptr;
	std::unique_ptr<ELFIO::symbol_section_accessor> symbol_accessor_;
	std::unique_ptr<ELFIO::string_section_accessor> string_accessor_;
	std::unordered_map<std::string, std::unique_ptr<ELFIO::relocation_section_accessor>, ObjectFileCommon::StringViewHash, std::equal_to<>> rela_accessors_;
	std::unordered_map<std::string, ELFIO::section*, ObjectFileCommon::StringViewHash, std::equal_to<>> section_name_cache_;
	uint32_t string_literal_counter_ = 0;
	std::unordered_map<std::string, FunctionSignature, ObjectFileCommon::StringViewHash, std::equal_to<>> function_signatures_;
	std::unordered_set<std::string> added_exception_functions_;
	std::unordered_map<std::string, ELFIO::Elf_Word, ObjectFileCommon::StringViewHash, std::equal_to<>> symbol_index_cache_;
	bool symbol_index_cache_dirty_ = true;
	std::set<std::string> created_class_typeinfos_;	// tracks emitted _ZTI symbols within this ElfFileWriter instance

	// DWARF4 debug info data
	struct DebugLineEntry {
		uint32_t code_offset;
		uint32_t line_number;
	};
	struct DebugVarInfo {
		std::string name;
		bool is_parameter = false;
		bool is_register = false;
		int dwarf_reg = -1;
		int32_t stack_off = 0;
		uint32_t cv_type_index = 0x74;
	};
	struct DebugFuncInfo {
		std::string name;
		uint32_t text_offset = 0;
		uint32_t length = 0;
		uint32_t file_id = 0;
		std::vector<DebugLineEntry> lines;
		std::vector<DebugVarInfo> vars;
	};

	std::vector<std::string> debug_source_files_;
	std::vector<DebugFuncInfo> debug_functions_;
	DebugFuncInfo debug_current_func_;
	bool debug_has_current_ = false;
	uint32_t debug_pending_text_offset_ = 0;

	// EH data members
	std::vector<FDEInfo> functions_with_fdes_;
	uint32_t personality_routine_offset_ = 0;
	std::unordered_map<std::string, LSDAGenerator::FunctionLSDAInfo, ObjectFileCommon::StringViewHash, std::equal_to<>> function_lsda_map_;
};
