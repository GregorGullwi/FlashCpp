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

		// Determine symbol binding based on whether the function is inline
		// Inline functions need STB_WEAK so the linker can discard duplicates
		bool is_inline = false;
		auto it = function_signatures_.find(std::string(mangled_name));
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
		accessor->add_symbol(*string_accessor_, mangled_name_str.c_str(), value, size, bind, type, other, section_index);

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
		auto symbol_index = getOrCreateSymbol(symbol_name_sv, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
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
	void add_global_variable_data(std::string_view var_name, size_t size_in_bytes,
	                              bool is_initialized, std::span<const char> init_data) {
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
	 * @brief Add typeinfo symbol for RTTI (Itanium C++ ABI)
	 * @param typeinfo_symbol Symbol name (e.g., "_ZTI4Base" for typeinfo for Base)
	 * @param typeinfo_data Pointer to the typeinfo structure
	 * @param typeinfo_size Size of the typeinfo structure in bytes
	 */
	void add_typeinfo(std::string_view typeinfo_symbol, const void* typeinfo_data, size_t typeinfo_size) {
		FLASH_LOG_FORMAT(Codegen, Debug, "Adding typeinfo '{}' of size {}", typeinfo_symbol, typeinfo_size);
		
		// Get .rodata section (typeinfo goes in read-only data)
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		
		uint32_t typeinfo_offset = rodata->get_size();
		
		// Add typeinfo data to .rodata
		rodata->append_data(reinterpret_cast<const char*>(typeinfo_data), typeinfo_size);
		
		// Add typeinfo symbol
		getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
		                  rodata->get_index(), typeinfo_offset, typeinfo_size);
	}

	/**
	 * @brief Get or create type_info symbol for a built-in type
	 * @param type The type to get type_info for
	 * @return Symbol name for the type_info (e.g., "_ZTIi" for int)
	 */
	std::string get_or_create_builtin_typeinfo(Type type) {
		// Map types to Itanium C++ ABI mangled type codes
		std::string_view type_code;
		switch (type) {
			case Type::Void: type_code = "v"; break;
			case Type::Bool: type_code = "b"; break;
			case Type::Char: type_code = "c"; break;
			case Type::UnsignedChar: type_code = "h"; break;
			case Type::Short: type_code = "s"; break;
			case Type::UnsignedShort: type_code = "t"; break;
			case Type::Int: type_code = "i"; break;
			case Type::UnsignedInt: type_code = "j"; break;
			case Type::Long: type_code = "l"; break;
			case Type::UnsignedLong: type_code = "m"; break;
			case Type::LongLong: type_code = "x"; break;
			case Type::UnsignedLongLong: type_code = "y"; break;
			case Type::Float: type_code = "f"; break;
			case Type::Double: type_code = "d"; break;
			case Type::LongDouble: type_code = "e"; break;
			default:
				// For non-builtin types, return empty string
				return "";
		}
		
		// Type info symbol: _ZTI + type_code (e.g., _ZTIi for int)
		char typeinfo_symbol_buf[8]; // "_ZTI" + single char + null = max 6 bytes
		std::snprintf(typeinfo_symbol_buf, sizeof(typeinfo_symbol_buf), "_ZTI%.*s", 
		              static_cast<int>(type_code.length()), type_code.data());
		std::string typeinfo_symbol(typeinfo_symbol_buf);
		
		// Check if we've already created this symbol
		static std::set<std::string> created_typeinfos;
		if (created_typeinfos.find(typeinfo_symbol) != created_typeinfos.end()) {
			return typeinfo_symbol;
		}
		
		// For built-in types, we need to create a minimal type_info structure
		// Itanium C++ ABI: type_info for fundamental types is just vtable pointer + name
		struct FundamentalTypeInfo {
			const void* vtable;  // Pointer to __fundamental_type_info vtable (external)
			const char* name;    // Mangled type name
		};
		
		// We'll emit a reference to an external vtable symbol
		// The actual vtable will be provided by the C++ runtime library
		static constexpr std::string_view vtable_symbol = "_ZTVN10__cxxabiv123__fundamental_type_infoE";
		
		// For now, we'll create a placeholder that references the external vtable
		// In a full implementation, we'd emit proper type_info data
		// For the minimal case, we just need the symbol to exist
		
		// Create a minimal type_info: just 16 bytes (vtable ptr + name ptr)
		std::vector<char> typeinfo_data(16, 0);
		
		// Add typeinfo data to .rodata
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		
		uint32_t typeinfo_offset = rodata->get_size();
		rodata->append_data(typeinfo_data.data(), typeinfo_data.size());
		
		// Add typeinfo symbol
		getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK,
		                  rodata->get_index(), typeinfo_offset, typeinfo_data.size());
		
		created_typeinfos.insert(typeinfo_symbol);
		
		FLASH_LOG_FORMAT(Codegen, Debug, "Created built-in typeinfo '{}' for type code '{}'", 
		                 typeinfo_symbol, type_code);
		
		return typeinfo_symbol;
	}

	/**
	 * @brief Get or create type_info symbol for a class type
	 * @param class_name The name of the class
	 * @return Symbol name for the type_info (e.g., "_ZTI7MyClass")
	 */
	std::string get_or_create_class_typeinfo(std::string_view class_name) {
		// Itanium C++ ABI class type_info: _ZTI + length + name
		// Example: class "Foo" -> "_ZTI3Foo"
		StringBuilder builder;
		builder.append("_ZTI").append(class_name.length()).append(class_name);
		std::string typeinfo_symbol(builder.commit());
		
		// Check if we've already created this symbol
		static std::set<std::string> created_class_typeinfos;
		if (created_class_typeinfos.find(typeinfo_symbol) != created_class_typeinfos.end()) {
			return typeinfo_symbol;
		}
		
		// For class types, create a minimal __class_type_info structure
		// This is just vtable pointer + name pointer (16 bytes total)
		char typeinfo_data_buf[16] = {0};
		
		// Add typeinfo data to .rodata
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		
		uint32_t typeinfo_offset = rodata->get_size();
		rodata->append_data(typeinfo_data_buf, sizeof(typeinfo_data_buf));
		
		// Add typeinfo symbol as weak (may be provided by other translation units)
		getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK,
		                  rodata->get_index(), typeinfo_offset, sizeof(typeinfo_data_buf));
		
		created_class_typeinfos.insert(typeinfo_symbol);
		
		FLASH_LOG_FORMAT(Codegen, Debug, "Created class typeinfo '{}' for class '{}'", 
		                 typeinfo_symbol, class_name);
		
		return typeinfo_symbol;
	}

	/**
	 * @brief Add vtable for C++ class
	 * Itanium C++ ABI vtable format: array of function pointers
	 */
	void add_vtable(std::string_view vtable_symbol, 
	               std::span<const std::string_view> function_symbols,
	               std::string_view class_name,
	               std::span<const std::string_view> base_class_names,
	               std::span<const BaseClassDescriptorInfo> base_class_info,
	               const RTTITypeInfo* rtti_info = nullptr) {
		
		FLASH_LOG_FORMAT(Codegen, Debug, "Adding vtable '{}' for class {} with {} virtual functions",
		                 vtable_symbol, class_name, function_symbols.size());
		
		// Get .rodata section (vtables go in read-only data)
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		
		uint32_t vtable_offset = rodata->get_size();
		
		// Itanium C++ ABI vtable structure:
		// - Offset to top (8 bytes) - always 0 for simple cases
		// - RTTI pointer (8 bytes) - pointer to typeinfo structure
		// - Function pointers (8 bytes each)
		
		// First, emit typeinfo if available
		std::string typeinfo_symbol;
		if (rtti_info && rtti_info->itanium_type_info) {
			// Generate typeinfo symbol name: _ZTI + mangled class name
			// For now, use the class name length-prefixed
			StringBuilder typeinfo_builder;
			typeinfo_builder.append("_ZTI").append(class_name.length()).append(class_name);
			typeinfo_symbol = std::string(typeinfo_builder.commit());
			
			// Determine which typeinfo structure to emit based on kind
			if (rtti_info->itanium_kind == RTTITypeInfo::ItaniumTypeInfoKind::ClassTypeInfo) {
				add_typeinfo(typeinfo_symbol, rtti_info->itanium_type_info, sizeof(ItaniumClassTypeInfo));
			} else if (rtti_info->itanium_kind == RTTITypeInfo::ItaniumTypeInfoKind::SIClassTypeInfo) {
				add_typeinfo(typeinfo_symbol, rtti_info->itanium_type_info, sizeof(ItaniumSIClassTypeInfo));
			} else if (rtti_info->itanium_kind == RTTITypeInfo::ItaniumTypeInfoKind::VMIClassTypeInfo) {
				// Variable size - need to calculate
				const ItaniumVMIClassTypeInfo* vmi = static_cast<const ItaniumVMIClassTypeInfo*>(rtti_info->itanium_type_info);
				// Safe calculation - VMI always has at least 1 base class
				size_t vmi_size = sizeof(ItaniumVMIClassTypeInfo);
				if (vmi->base_count > 1) {
					vmi_size += (vmi->base_count - 1) * sizeof(ItaniumBaseClassTypeInfo);
				}
				add_typeinfo(typeinfo_symbol, rtti_info->itanium_type_info, vmi_size);
			}
		}
		
		char vtable_data_buf[8192]; // Stack-based buffer for vtable (reasonable max size)
		size_t vtable_data_size = 0;
		
		auto append_bytes = [&](const void* data, size_t size) {
			if (vtable_data_size + size > sizeof(vtable_data_buf)) {
				throw std::runtime_error("Vtable too large for stack buffer");
			}
			std::memcpy(vtable_data_buf + vtable_data_size, data, size);
			vtable_data_size += size;
		};
		
		// Offset to top (8 bytes, value = 0)
		uint64_t offset_to_top = 0;
		append_bytes(&offset_to_top, 8);
		
		// RTTI pointer (8 bytes, null for now)
		uint64_t rtti_ptr = 0;
		append_bytes(&rtti_ptr, 8);
		
		// Function pointers (8 bytes each, will be filled by relocations)
		uint64_t func_ptr = 0;
		for (size_t i = 0; i < function_symbols.size(); ++i) {
			append_bytes(&func_ptr, 8);
		}
		
		// Add vtable data to .rodata
		rodata->append_data(vtable_data_buf, vtable_data_size);
		
		// Add vtable symbol pointing to the function pointer array (skip offset-to-top and RTTI)
		uint32_t symbol_offset = vtable_offset + 16;  // Skip offset-to-top (8) and RTTI (8)
		getOrCreateSymbol(vtable_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
		                  rodata->get_index(), symbol_offset, vtable_data_size - 16);
		
		// Add relocations for each function pointer
		auto* rela_rodata = getOrCreateRelocationSection(".rodata");
		auto* rela_accessor = getRelocationAccessor(".rela.rodata");
		
		// Add relocation for RTTI pointer if typeinfo was emitted
		if (!typeinfo_symbol.empty()) {
			auto typeinfo_symbol_idx = getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL);
			uint32_t rtti_reloc_offset = vtable_offset + 8;  // Offset to top is first 8 bytes
			rela_accessor->add_entry(rtti_reloc_offset, typeinfo_symbol_idx, ELFIO::R_X86_64_64, 0);
			
			FLASH_LOG_FORMAT(Codegen, Debug, "  Added relocation for typeinfo {} at offset {}", 
			                 typeinfo_symbol, rtti_reloc_offset);
		}
		
		for (size_t i = 0; i < function_symbols.size(); ++i) {
			// Get or create symbol for the function
			auto func_symbol_idx = getOrCreateSymbol(function_symbols[i], ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);
			
			// Add relocation for this function pointer
			uint32_t reloc_offset = vtable_offset + 16 + (i * 8);  // Skip header + i*8 for function pointer
			rela_accessor->add_entry(reloc_offset, func_symbol_idx, ELFIO::R_X86_64_64, 0);
			
			FLASH_LOG_FORMAT(Codegen, Debug, "  Added relocation for function {} at offset {}", 
			                 function_symbols[i], reloc_offset);
		}
		
		FLASH_LOG_FORMAT(Codegen, Debug, "Vtable '{}' added at offset {} with {} bytes",
		                 vtable_symbol, symbol_offset, vtable_data_size);
	}

	// Note: Mangled names are pre-computed by the Parser.
	// All names are passed through as-is.

	/**
	 * @brief Generate mangled name using platform-appropriate mangling
	 * Returns string_view to stable storage in function_signatures_ map
	 */
	std::string_view generateMangledName(std::string_view name, const FunctionSignature& sig) {
		// Check linkage first - extern "C" always uses C linkage (unmangled)
		if (sig.linkage == Linkage::C) {
			// For C linkage, store the name in function_signatures_ to ensure stability
			std::string key(name);
			auto it = function_signatures_.find(key);
			if (it != function_signatures_.end()) {
				return std::string_view(it->first);  // Return view to key in map
			}
			// Store it
			function_signatures_[key] = sig;
			return std::string_view(function_signatures_.find(key)->first);
		}
		
		// Split namespace_name into components for mangling functions
		std::vector<std::string_view> namespace_path;
		if (!sig.namespace_name.empty()) {
			// Split by "::" delimiter
			size_t start = 0;
			size_t end = sig.namespace_name.find("::");
			while (end != std::string::npos) {
				namespace_path.push_back(std::string_view(sig.namespace_name.c_str() + start, end - start));
				start = end + 2;
				end = sig.namespace_name.find("::", start);
			}
			if (start < sig.namespace_name.size()) {
				namespace_path.push_back(std::string_view(sig.namespace_name.c_str() + start));
			}
		}
		
		// Use NameMangling::generateMangledName which handles both MSVC and Itanium
		NameMangling::MangledName mangled = NameMangling::generateMangledName(
			name,
			sig.return_type,
			sig.parameter_types,
			sig.is_variadic,
			sig.class_name,
			namespace_path
		);
		
		// Store in function_signatures_ map to ensure stable storage
		std::string key(mangled.view());
		function_signatures_[key] = sig;
		
		// Return string_view to the key in the map (stable storage)
		return std::string_view(function_signatures_.find(key)->first);
	}

	/**
	 * @brief Add function signature (for future name mangling)
	 * Returns std::string_view to stable storage in function_signatures_ map
	 */
	std::string_view addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                                const std::vector<TypeSpecifierNode>& parameter_types,
	                                Linkage linkage = Linkage::None, bool is_variadic = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		return generateMangledName(name, sig);  // generateMangledName now returns string_view to stable storage
	}

	// Overload that accepts pre-computed mangled name (without class_name)
	void addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                          const std::vector<TypeSpecifierNode>& parameter_types,
	                          Linkage linkage, bool is_variadic,
	                          std::string_view mangled_name, bool is_inline = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		sig.is_inline = is_inline;
		function_signatures_[std::string(mangled_name)] = sig;
	}

	// Overloads for member functions (compatibility with ObjectFileWriter)
	std::string_view addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                                const std::vector<TypeSpecifierNode>& parameter_types,
	                                std::string_view class_name, Linkage linkage = Linkage::None,
	                                bool is_variadic = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.class_name = class_name;
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		return generateMangledName(name, sig);  // generateMangledName now returns string_view to stable storage
	}

	// Overload that accepts pre-computed mangled name (for function definitions from IR)
	void addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                          const std::vector<TypeSpecifierNode>& parameter_types,
	                          std::string_view class_name, Linkage linkage, bool is_variadic,
	                          std::string_view mangled_name, bool is_inline = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.class_name = class_name;
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		sig.is_inline = is_inline;
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

	void update_function_length(const std::string_view manged_name, uint32_t code_length) {
		// Placeholder
	}

	void set_function_debug_range(const std::string_view manged_name, uint32_t prologue_size, uint32_t epilogue_size) {
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

	// CFI instruction tracking for a single function (defined here for use in add_function_exception_info)
	struct CFIInstruction {
		enum Type {
			PUSH_RBP,       // push rbp
			MOV_RSP_RBP,    // mov rbp, rsp
			SUB_RSP,        // sub rsp, imm
			ADD_RSP,        // add rsp, imm
			POP_RBP,        // pop rbp
		};
		Type type;
		uint32_t offset;  // Offset in function where this occurs
		uint32_t value;   // Immediate value (for SUB_RSP/ADD_RSP)
	};

	// Exception handling (placeholders - not implemented for Linux MVP)
	void add_function_exception_info(std::string_view mangled_name, uint32_t function_start,
	                                 uint32_t function_size, const std::vector<TryBlockInfo>& try_blocks = {},
	                                 const std::vector<UnwindMapEntryInfo>& unwind_map = {},
	                                 const std::vector<CFIInstruction>& cfi_instructions = {}) {
		// Add FDE for this function (all functions get an FDE for proper unwinding)
		FDEInfo fde_info;
		fde_info.function_start_offset = function_start;
		fde_info.function_length = function_size;
		fde_info.function_symbol.assign(mangled_name.begin(), mangled_name.end());
		fde_info.has_exception_handling = !try_blocks.empty();
		fde_info.cfi_instructions = cfi_instructions;  // Store CFI instructions
		
		functions_with_fdes_.push_back(fde_info);
		
		// Generate LSDA if function has exception handling
		if (fde_info.has_exception_handling) {
			LSDAGenerator::FunctionLSDAInfo lsda_info;
			
			// Convert each try block to LSDA format
			for (const auto& try_block : try_blocks) {
				LSDAGenerator::TryRegionInfo try_region;
				try_region.try_start_offset = try_block.try_start_offset;
				try_region.try_length = try_block.try_end_offset - try_block.try_start_offset;
				
				// Use first handler's offset as landing pad (they're all at the same location)
				if (!try_block.catch_handlers.empty()) {
					try_region.landing_pad_offset = try_block.catch_handlers[0].handler_offset;
				}
				
				// Convert catch handlers
				for (const auto& handler : try_block.catch_handlers) {
					LSDAGenerator::CatchHandlerInfo catch_info;
					catch_info.type_index = handler.type_index;
					catch_info.is_catch_all = handler.is_catch_all;
					
					// Generate typeinfo symbol name from type name
					// For now, use a simple mapping for built-in types
					if (!handler.is_catch_all && !handler.type_name.empty()) {
						catch_info.typeinfo_symbol = get_typeinfo_symbol(handler.type_name);
						
						// Add to type table if not already present
						if (std::find(lsda_info.type_table.begin(), lsda_info.type_table.end(),
						             catch_info.typeinfo_symbol) == lsda_info.type_table.end()) {
							lsda_info.type_table.push_back(catch_info.typeinfo_symbol);
						}
					}
					
					try_region.catch_handlers.push_back(catch_info);
				}
				
				lsda_info.try_regions.push_back(try_region);
			}
			
			// Use the already-stored function symbol string to avoid creating another copy
			function_lsda_map_[fde_info.function_symbol] = lsda_info;
			
			if (g_enable_debug_output) {
				std::cerr << "Function " << mangled_name << " has " << try_blocks.size() 
				         << " try blocks - will need LSDA" << std::endl;
			}
		}
	}
	
	// Helper: get typeinfo symbol for a type name
	std::string get_typeinfo_symbol(const std::string& type_name) const {
		// Map common type names to their typeinfo symbols (Itanium ABI mangling)
		static const std::unordered_map<std::string, std::string> typeinfo_map = {
			{"int", "_ZTIi"},
			{"char", "_ZTIc"},
			{"short", "_ZTIs"},
			{"long", "_ZTIl"},
			{"long long", "_ZTIx"},
			{"unsigned int", "_ZTIj"},
			{"unsigned char", "_ZTIh"},
			{"unsigned short", "_ZTIt"},
			{"unsigned long", "_ZTIm"},
			{"unsigned long long", "_ZTIy"},
			{"float", "_ZTIf"},
			{"double", "_ZTId"},
			{"long double", "_ZTIe"},
			{"bool", "_ZTIb"},
			{"void", "_ZTIv"},
			{"wchar_t", "_ZTIw"},
			{"char16_t", "_ZTIDs"},
			{"char32_t", "_ZTIDi"},
		};
		
		auto it = typeinfo_map.find(type_name);
		if (it != typeinfo_map.end()) {
			return it->second;
		}
		
		// For class types, generate symbol: _ZTI + mangled class name
		// This is a simplification - real implementation would use full name mangling
		return "_ZTI" + type_name;  // Placeholder
	}

	// Additional compatibility methods
	void add_text_relocation(uint64_t offset, const std::string& symbol_name, uint32_t relocation_type, int64_t addend = -4) {
		add_relocation(offset, symbol_name, relocation_type, addend);
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

	// Exception handling - .eh_frame generation
	// (CFIInstruction defined above, before add_function_exception_info)
	
	// FDE (Frame Description Entry) information for a function
	struct FDEInfo {
		uint32_t function_start_offset;  // Offset in .text section
		uint32_t function_length;        // Length of function code
		std::string function_symbol;     // Symbol name of function
		std::vector<CFIInstruction> cfi_instructions;  // CFI state changes
		
		// LSDA (Language Specific Data Area) info for exception handling
		bool has_exception_handling = false;
		std::string lsda_symbol;  // Symbol pointing to LSDA in .gcc_except_table
		uint32_t lsda_offset = 0;  // Offset in .gcc_except_table where LSDA starts
		
		// Track where in .eh_frame this FDE's fields are located (for relocations)
		uint32_t pc_begin_offset = 0;  // Offset in .eh_frame where PC begin field is
		uint32_t lsda_pointer_offset = 0;  // Offset in .eh_frame where LSDA pointer is
	};
	
	// Track all functions that need FDEs
	std::vector<FDEInfo> functions_with_fdes_;
	
	// Track offset of personality routine pointer in .eh_frame (for relocation)
	uint32_t personality_routine_offset_ = 0;
	
	// Generate Common Information Entry (CIE) for .eh_frame
	void generate_eh_frame_cie(std::vector<uint8_t>& eh_frame_data, bool has_exception_handlers) {
		// CIE header structure (x86-64 System V ABI)
		
		size_t length_offset = eh_frame_data.size();
		
		// Length field (will be filled in at the end)
		for (int i = 0; i < 4; ++i) eh_frame_data.push_back(0);
		
		size_t cie_start = eh_frame_data.size();
		
		// CIE ID (0 for CIE)
		for (int i = 0; i < 4; ++i) eh_frame_data.push_back(0);
		
		// Version
		eh_frame_data.push_back(1);
		
		if (has_exception_handlers) {
			// Augmentation string "zPLR" (null-terminated)
			// z = has augmentation data
			// P = personality routine pointer present
			// L = LSDA encoding present
			// R = FDE encoding present
			eh_frame_data.push_back('z');
			eh_frame_data.push_back('P');
			eh_frame_data.push_back('L');
			eh_frame_data.push_back('R');
			eh_frame_data.push_back(0);
		} else {
			// Augmentation string "zR" (null-terminated) - no exception handling
			// z = has augmentation data
			// R = FDE encoding present
			eh_frame_data.push_back('z');
			eh_frame_data.push_back('R');
			eh_frame_data.push_back(0);
		}
		
		// Code alignment factor (1 for x86-64)
		DwarfCFI::appendULEB128(eh_frame_data, 1);
		
		// Data alignment factor (-8 for x86-64)
		DwarfCFI::appendSLEB128(eh_frame_data, -8);
		
		// Return address register (RIP = 16 on x86-64)
		DwarfCFI::appendULEB128(eh_frame_data, 16);
		
		// Augmentation data length (will be calculated)
		size_t aug_length_offset = eh_frame_data.size();
		eh_frame_data.push_back(0);  // Placeholder, will be updated
		
		size_t aug_data_start = eh_frame_data.size();
		
		if (has_exception_handlers) {
			// P: Personality routine encoding and pointer
			// Encoding: PC-relative signed 4-byte (direct, not indirect for non-PIE)
			// Note: GCC uses indirect (DW_EH_PE_indirect) for PIE/shared libraries,
			// but for static/non-PIE executables, direct addressing is more appropriate
			eh_frame_data.push_back(DwarfCFI::DW_EH_PE_pcrel | DwarfCFI::DW_EH_PE_sdata4);
			// Personality routine pointer (will need relocation to __gxx_personality_v0)
			// Store offset for relocation tracking
			personality_routine_offset_ = static_cast<uint32_t>(eh_frame_data.size());
			for (int i = 0; i < 4; ++i) eh_frame_data.push_back(0);  // Placeholder
			
			// L: LSDA encoding
			eh_frame_data.push_back(DwarfCFI::DW_EH_PE_pcrel | DwarfCFI::DW_EH_PE_sdata4);
		}
		
		// R: FDE encoding (PC-relative signed 4-byte) - always present with 'z'
		eh_frame_data.push_back(DwarfCFI::DW_EH_PE_pcrel | DwarfCFI::DW_EH_PE_sdata4);
		
		// Update augmentation data length
		uint8_t aug_length = static_cast<uint8_t>(eh_frame_data.size() - aug_data_start);
		eh_frame_data[aug_length_offset] = aug_length;
		
		// Initial instructions
		// DW_CFA_def_cfa: RSP (reg 7) + 8
		eh_frame_data.push_back(DwarfCFI::DW_CFA_def_cfa);
		DwarfCFI::appendULEB128(eh_frame_data, 7);  // RSP
		DwarfCFI::appendULEB128(eh_frame_data, 8);  // Offset 8
		
		// DW_CFA_offset: RIP (reg 16) is at CFA-8
		eh_frame_data.push_back(DwarfCFI::DW_CFA_offset | 16);
		DwarfCFI::appendULEB128(eh_frame_data, 1);  // Offset 1 * -8 = -8
		
		// Pad to 8-byte alignment
		while ((eh_frame_data.size() - cie_start + 4) % 8 != 0) {
			eh_frame_data.push_back(DwarfCFI::DW_CFA_nop);
		}
		
		// Fill in length (excluding the length field itself)
		uint32_t cie_length = static_cast<uint32_t>(eh_frame_data.size() - cie_start);
		eh_frame_data[length_offset + 0] = (cie_length >> 0) & 0xff;
		eh_frame_data[length_offset + 1] = (cie_length >> 8) & 0xff;
		eh_frame_data[length_offset + 2] = (cie_length >> 16) & 0xff;
		eh_frame_data[length_offset + 3] = (cie_length >> 24) & 0xff;
	}
	
	// Generate Frame Description Entry (FDE) for a function
	void generate_eh_frame_fde(std::vector<uint8_t>& eh_frame_data, 
	                           uint32_t cie_offset,
	                           FDEInfo& fde_info) {  // Non-const so we can update pc_begin_offset
		// FDE structure
		
		size_t length_offset = eh_frame_data.size();
		
		// Length field (will be filled in at the end)
		for (int i = 0; i < 4; ++i) eh_frame_data.push_back(0);
		
		size_t fde_start = eh_frame_data.size();
		
		// CIE pointer (offset from current location to CIE)
		uint32_t cie_pointer = static_cast<uint32_t>(fde_start - cie_offset);
		eh_frame_data.push_back((cie_pointer >> 0) & 0xff);
		eh_frame_data.push_back((cie_pointer >> 8) & 0xff);
		eh_frame_data.push_back((cie_pointer >> 16) & 0xff);
		eh_frame_data.push_back((cie_pointer >> 24) & 0xff);
		
		// PC begin (will be relocated - placeholder for now)
		// Record offset for relocation
		fde_info.pc_begin_offset = static_cast<uint32_t>(eh_frame_data.size());
		for (int i = 0; i < 4; ++i) eh_frame_data.push_back(0);
		
		// PC range (function length)
		eh_frame_data.push_back((fde_info.function_length >> 0) & 0xff);
		eh_frame_data.push_back((fde_info.function_length >> 8) & 0xff);
		eh_frame_data.push_back((fde_info.function_length >> 16) & 0xff);
		eh_frame_data.push_back((fde_info.function_length >> 24) & 0xff);
		
		// Augmentation data
		if (fde_info.has_exception_handling) {
			// Augmentation data length (4 bytes for LSDA pointer)
			DwarfCFI::appendULEB128(eh_frame_data, 4);
			
			// LSDA pointer (PC-relative to .gcc_except_table)
			// Record offset for relocation
			fde_info.lsda_pointer_offset = static_cast<uint32_t>(eh_frame_data.size());
			fde_info.lsda_symbol = ".gcc_except_table";
			for (int i = 0; i < 4; ++i) eh_frame_data.push_back(0);  // Placeholder for relocation
		} else {
			// No exception handling - augmentation data length is 0
			DwarfCFI::appendULEB128(eh_frame_data, 0);
		}
		
		// Generate CFI instructions based on tracked prologue state
		// The initial state from CIE is: CFA = RSP+8, RIP at CFA-8
		// After push rbp: CFA = RSP+16, RBP at CFA-16
		// After mov rbp, rsp: CFA = RBP+16
		// After sub rsp, N: CFA still = RBP+16 (RBP-based frame)
		
		uint32_t last_offset = 0;
		for (const auto& cfi : fde_info.cfi_instructions) {
			// Emit DW_CFA_advance_loc if offset changed
			if (cfi.offset > last_offset) {
				uint32_t delta = cfi.offset - last_offset;
				if (delta < 64) {
					// DW_CFA_advance_loc: low 6 bits encode delta
					eh_frame_data.push_back(DwarfCFI::DW_CFA_advance_loc | (delta & 0x3f));
				} else if (delta < 256) {
					// DW_CFA_advance_loc1: 1-byte delta
					eh_frame_data.push_back(DwarfCFI::DW_CFA_advance_loc1);
					eh_frame_data.push_back(static_cast<uint8_t>(delta));
				} else {
					// DW_CFA_advance_loc2: 2-byte delta
					eh_frame_data.push_back(DwarfCFI::DW_CFA_advance_loc2);
					eh_frame_data.push_back((delta >> 0) & 0xff);
					eh_frame_data.push_back((delta >> 8) & 0xff);
				}
				last_offset = cfi.offset;
			}
			
			switch (cfi.type) {
			case CFIInstruction::PUSH_RBP:
				// After push rbp: CFA = RSP+16, RBP saved at CFA-16
				// DW_CFA_def_cfa_offset: 16
				eh_frame_data.push_back(DwarfCFI::DW_CFA_def_cfa_offset);
				DwarfCFI::appendULEB128(eh_frame_data, 16);
				// DW_CFA_offset: rbp at CFA-16 (factored by data_align=-8, so offset=2)
				eh_frame_data.push_back(DwarfCFI::DW_CFA_offset | 6);  // RBP = register 6
				DwarfCFI::appendULEB128(eh_frame_data, 2);  // 2 * 8 = 16
				break;
				
			case CFIInstruction::MOV_RSP_RBP:
				// After mov rbp, rsp: CFA = RBP+16 (use frame pointer)
				// DW_CFA_def_cfa_register: rbp
				eh_frame_data.push_back(DwarfCFI::DW_CFA_def_cfa_register);
				DwarfCFI::appendULEB128(eh_frame_data, 6);  // RBP = register 6
				break;
				
			case CFIInstruction::SUB_RSP:
				// After sub rsp, N: CFA still RBP+16 (no change needed when using frame pointer)
				// CFA remains based on RBP, not RSP
				break;
				
			case CFIInstruction::ADD_RSP:
				// Epilogue starts - no CFI change needed until pop rbp
				break;
				
			case CFIInstruction::POP_RBP:
				// After pop rbp: CFA = RSP+8 (back to call site state)
				// DW_CFA_def_cfa: rsp, 8
				eh_frame_data.push_back(DwarfCFI::DW_CFA_def_cfa);
				DwarfCFI::appendULEB128(eh_frame_data, 7);  // RSP = register 7
				DwarfCFI::appendULEB128(eh_frame_data, 8);  // offset 8
				break;
			}
		}
		
		// Pad to 8-byte alignment
		while ((eh_frame_data.size() - fde_start + 4) % 8 != 0) {
			eh_frame_data.push_back(DwarfCFI::DW_CFA_nop);
		}
		
		// Fill in length (excluding the length field itself)
		uint32_t fde_length = static_cast<uint32_t>(eh_frame_data.size() - fde_start);
		eh_frame_data[length_offset + 0] = (fde_length >> 0) & 0xff;
		eh_frame_data[length_offset + 1] = (fde_length >> 8) & 0xff;
		eh_frame_data[length_offset + 2] = (fde_length >> 16) & 0xff;
		eh_frame_data[length_offset + 3] = (fde_length >> 24) & 0xff;
	}
	
	// Generate .eh_frame section
	void generate_eh_frame() {
		if (functions_with_fdes_.empty()) {
			// No functions with exception handling - skip .eh_frame generation
			return;
		}
		
		std::vector<uint8_t> eh_frame_data;
		
		// Check if any function has exception handling
		bool has_exception_handlers = false;
		for (const auto& fde_info : functions_with_fdes_) {
			if (fde_info.has_exception_handling) {
				has_exception_handlers = true;
				break;
			}
		}
		
		// Generate CIE
		uint32_t cie_offset = 0;
		generate_eh_frame_cie(eh_frame_data, has_exception_handlers);
		
		// Generate FDEs for each function
		for (auto& fde_info : functions_with_fdes_) {  // Non-const to update pc_begin_offset
			generate_eh_frame_fde(eh_frame_data, cie_offset, fde_info);
		}
		
		// Create .eh_frame section
		auto* eh_frame_section = elf_writer_.sections.add(".eh_frame");
		eh_frame_section->set_type(ELFIO::SHT_PROGBITS);
		eh_frame_section->set_flags(ELFIO::SHF_ALLOC);
		eh_frame_section->set_addr_align(8);
		eh_frame_section->set_data(reinterpret_cast<const char*>(eh_frame_data.data()),
		                           eh_frame_data.size());
		
		// Create .rela.eh_frame section for relocations
		auto* rela_eh_frame = elf_writer_.sections.add(".rela.eh_frame");
		rela_eh_frame->set_type(ELFIO::SHT_RELA);
		rela_eh_frame->set_flags(ELFIO::SHF_INFO_LINK);
		rela_eh_frame->set_info(eh_frame_section->get_index());
		rela_eh_frame->set_link(symtab_section_->get_index());
		rela_eh_frame->set_addr_align(8);
		rela_eh_frame->set_entry_size(elf_writer_.get_default_entry_size(ELFIO::SHT_RELA));
		
		// Create relocation accessor for .eh_frame
		auto rela_accessor = std::make_unique<ELFIO::relocation_section_accessor>(
			elf_writer_, rela_eh_frame
		);
		
		// Add relocations for each FDE's PC begin field
		for (const auto& fde_info : functions_with_fdes_) {
			// R_X86_64_PC32: PC-relative 32-bit signed (S + A - P)
			// where S = symbol value, A = addend, P = place (offset being relocated)
			rela_accessor->add_entry(fde_info.pc_begin_offset, 
			                        0,  // Symbol index (will be set below)
			                        ELFIO::R_X86_64_PC32,
			                        0);  // Addend (0 for PC-relative to function start)
			
			// Now we need to update the symbol index
			// Find the symbol index for this function
			auto* accessor = getSymbolAccessor();
			if (accessor) {
				ELFIO::Elf_Xword sym_count = accessor->get_symbols_num();
				for (ELFIO::Elf_Xword i = 0; i < sym_count; ++i) {
					std::string sym_name;
					ELFIO::Elf64_Addr sym_value;
					ELFIO::Elf_Xword sym_size;
					unsigned char sym_bind, sym_type;
					ELFIO::Elf_Half sym_section;
					unsigned char sym_other;
					
					accessor->get_symbol(i, sym_name, sym_value, sym_size, sym_bind, 
					                    sym_type, sym_section, sym_other);
					
					if (sym_name == fde_info.function_symbol) {
						// Update the relocation we just added
						ELFIO::Elf64_Addr r_offset;
						ELFIO::Elf_Word r_symbol;  // Use Elf_Word (32-bit)
						unsigned r_type;
						ELFIO::Elf_Sxword r_addend;
						
						// Get the last relocation we added
						ELFIO::Elf_Xword rel_count = rela_accessor->get_entries_num();
						if (rel_count > 0) {
							rela_accessor->get_entry(rel_count - 1, r_offset, r_symbol, 
							                        r_type, r_addend);
							// Update symbol index
							rela_accessor->set_entry(rel_count - 1, r_offset, 
							                        static_cast<ELFIO::Elf_Word>(i), 
							                        r_type, r_addend);
						}
						break;
					}
				}
			}
			
			// Add LSDA relocation if function has exception handling
			if (fde_info.has_exception_handling && fde_info.lsda_pointer_offset > 0) {
				// Add .gcc_except_table section symbol if needed
				// First check if it exists
				ELFIO::Elf_Xword gcc_except_table_sym_index = 0;
				bool found_except_table = false;
				
				ELFIO::Elf_Xword sym_count = accessor->get_symbols_num();
				for (ELFIO::Elf_Xword i = 0; i < sym_count; ++i) {
					std::string sym_name;
					ELFIO::Elf64_Addr sym_value;
					ELFIO::Elf_Xword sym_size;
					unsigned char sym_bind, sym_type;
					ELFIO::Elf_Half sym_section;
					unsigned char sym_other;
					
					accessor->get_symbol(i, sym_name, sym_value, sym_size, sym_bind,
					                    sym_type, sym_section, sym_other);
					
					if (sym_name == ".gcc_except_table") {
						gcc_except_table_sym_index = i;
						found_except_table = true;
						break;
					}
				}
				
				if (!found_except_table) {
					// Add section symbol for .gcc_except_table
					auto* except_section = getSectionByName(".gcc_except_table");
					if (except_section) {
						accessor->add_symbol(*string_accessor_, ".gcc_except_table",
						                    0, 0, ELFIO::STB_LOCAL, ELFIO::STT_SECTION,
						                    except_section->get_index(), ELFIO::STV_DEFAULT);
						gcc_except_table_sym_index = accessor->get_symbols_num() - 1;
					}
				}
				
				// Add relocation for LSDA pointer with addend = lsda_offset
				rela_accessor->add_entry(fde_info.lsda_pointer_offset,
				                        static_cast<ELFIO::Elf_Word>(gcc_except_table_sym_index),
				                        ELFIO::R_X86_64_PC32,
				                        static_cast<ELFIO::Elf_Sxword>(fde_info.lsda_offset));
			}
		}
		
		// Add relocation for personality routine pointer in CIE
		if (personality_routine_offset_ > 0) {
			// Add __gxx_personality_v0 symbol if it doesn't exist
			auto* accessor = getSymbolAccessor();
			if (accessor) {
				// Add as undefined external symbol
				accessor->add_symbol(*string_accessor_, "__gxx_personality_v0", 
				                    0, 0, ELFIO::STB_GLOBAL, ELFIO::STT_NOTYPE, 
				                    0, ELFIO::STV_DEFAULT);
				
				// Find the symbol index we just added
				ELFIO::Elf_Xword sym_count = accessor->get_symbols_num();
				ELFIO::Elf_Xword personality_sym_index = sym_count - 1;
				
				// Add relocation for personality routine
				rela_accessor->add_entry(personality_routine_offset_,
				                        static_cast<ELFIO::Elf_Word>(personality_sym_index),
				                        ELFIO::R_X86_64_PC32,
				                        0);
			}
		}
		
		if (g_enable_debug_output) {
			std::cerr << "Generated .eh_frame section with " << functions_with_fdes_.size() 
			         << " FDEs (" << eh_frame_data.size() << " bytes)" << std::endl;
		}
	}
	
	// Map to store LSDA info for each function
	std::unordered_map<std::string, LSDAGenerator::FunctionLSDAInfo> function_lsda_map_;
	
	// Generate .gcc_except_table section
	void generate_gcc_except_table() {
		if (function_lsda_map_.empty()) {
			// No functions with exception handling
			return;
		}
		
		std::vector<uint8_t> gcc_except_table_data;
		LSDAGenerator generator;
		
		// Generate LSDA for each function and track offsets
		for (const auto& [func_name, lsda_info] : function_lsda_map_) {
			// Find the corresponding FDE to update its LSDA offset
			for (auto& fde_info : functions_with_fdes_) {
				if (fde_info.function_symbol == func_name) {
					fde_info.lsda_offset = static_cast<uint32_t>(gcc_except_table_data.size());
					break;
				}
			}
			
			auto lsda_data = generator.generate(lsda_info);
			gcc_except_table_data.insert(gcc_except_table_data.end(), 
			                            lsda_data.begin(), lsda_data.end());
		}
		
		// Create .gcc_except_table section
		auto* except_section = elf_writer_.sections.add(".gcc_except_table");
		except_section->set_type(ELFIO::SHT_PROGBITS);
		except_section->set_flags(ELFIO::SHF_ALLOC);
		except_section->set_addr_align(4);
		except_section->set_data(reinterpret_cast<const char*>(gcc_except_table_data.data()),
		                        gcc_except_table_data.size());
		
		// Add a DATA symbol for the .gcc_except_table section
		// Use STB_WEAK instead of STB_LOCAL so it doesn't break symbol ordering
		// (WEAK symbols are treated like GLOBAL for ordering purposes)
		auto* accessor = getSymbolAccessor();
		if (accessor && string_accessor_) {
			accessor->add_symbol(*string_accessor_, ".gcc_except_table",
			                    0, gcc_except_table_data.size(), 
			                    ELFIO::STB_WEAK, ELFIO::STT_OBJECT,
			                    except_section->get_index(), ELFIO::STV_HIDDEN);
		}
		
		// TODO: Create .rela.gcc_except_table for type_info relocations
		
		if (g_enable_debug_output) {
			std::cerr << "Generated .gcc_except_table section with " 
			         << function_lsda_map_.size() << " LSDAs (" 
			         << gcc_except_table_data.size() << " bytes)" << std::endl;
		}
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
				// Symbol exists - check if we need to update it
				// If the existing symbol is undefined (SHN_UNDEF) and we're providing a definition,
				// we need to update it
				if (sym_section == ELFIO::SHN_UNDEF && section_index != ELFIO::SHN_UNDEF) {
					// Update the existing undefined symbol with the definition
					// ELFIO doesn't provide a direct update method, so we need to work around it
					// by modifying the symbol table data directly
					ELFIO::section* symtab_sec = getSectionByName(".symtab");
					if (symtab_sec) {
						// Calculate offset to this symbol's entry
						size_t entry_size = symtab_sec->get_entry_size();
						size_t offset = i * entry_size;
						
						// Get mutable access to symbol table data
						char* data = const_cast<char*>(symtab_sec->get_data());
						
						// Symbol table entry layout (64-bit):
						// Offset | Size | Field
						// -------|------|-------
						// 0      | 4    | st_name (string table index)
						// 4      | 1    | st_info (bind << 4 | type)
						// 5      | 1    | st_other
						// 6      | 2    | st_shndx (section index)
						// 8      | 8    | st_value
						// 16     | 8    | st_size
						
						// Update st_info (bind and type)
						data[offset + 4] = (bind << 4) | (type & 0x0F);
						
						// Update st_shndx (section index)
						*reinterpret_cast<ELFIO::Elf_Half*>(&data[offset + 6]) = section_index;
						
						// Update st_value
						*reinterpret_cast<ELFIO::Elf64_Addr*>(&data[offset + 8]) = value;
						
						// Update st_size
						*reinterpret_cast<ELFIO::Elf_Xword*>(&data[offset + 16]) = size;
					}
				}
				return i;  // Return existing symbol index
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
		// Generate exception handling tables if needed
		// Generate .gcc_except_table first so LSDA offsets are known
		generate_gcc_except_table();
		// Then generate .eh_frame with LSDA pointers
		generate_eh_frame();
		
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
