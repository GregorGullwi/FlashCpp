#pragma once

#include "AstNodeTypes.h"
#include "ObjectFileCommon.h"
#include "NameMangling.h"
#include "ChunkedString.h"
#include "CodeViewDebug.h"
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <set>

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
		// Lower 32 bits: relocation type (e.g., R_X86_64_PLT32 = 4, R_X86_64_PC32 = 2)
		ELFIO::Elf_Xword rel_info = (static_cast<ELFIO::Elf_Xword>(symbol_index) << 32) | 
		                             (static_cast<ELFIO::Elf_Xword>(relocation_type) & 0xffffffffUL);
		ELFIO::Elf_Sxword addend = -4;  // Standard addend for PC-relative and PLT relocations (compensate for instruction size)

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
		StringBuilder typeinfo_builder;
		std::string typeinfo_symbol;
		if (rtti_info && rtti_info->itanium_type_info) {
			// Generate typeinfo symbol name: _ZTI + mangled class name
			// For now, use the class name length-prefixed
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
	                          std::string_view mangled_name) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
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
		// Convert string_view to std::string to ensure null-termination for C API
		std::string name_str(name);
		return accessor->add_symbol(*string_accessor_, name_str.c_str(), value, size, bind, type, ELFIO::STV_DEFAULT, section_index);
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
