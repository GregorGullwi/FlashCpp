#pragma once

#include "coffi/coffi.hpp"
#include "CodeViewDebug.h"
#include "AstNodeTypes.h"
#include <string>
#include <array>
#include <chrono>
#include <optional>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>

// Additional COFF relocation types not defined in COFFI
#ifndef IMAGE_REL_AMD64_SECREL
#define IMAGE_REL_AMD64_SECREL          0x000B  // 32 bit offset from base of section containing target
#endif
#ifndef IMAGE_REL_AMD64_SECTION
#define IMAGE_REL_AMD64_SECTION         0x000A  // Section index
#endif

enum class SectionType : unsigned char
{
	TEXT,
	DATA,
	BSS,
	DRECTVE,
	XDATA,
	PDATA,
	DEBUG_S,
	DEBUG_T,
	LLVM_ADDRSIG,

	Count
};

class ObjectFileWriter {
public:
	ObjectFileWriter() {
		std::cerr << "Creating simplified ObjectFileWriter for debugging..." << std::endl;

		coffi_.create(COFFI::COFFI_ARCHITECTURE_PE);
		coffi_.get_header()->set_machine(IMAGE_FILE_MACHINE_AMD64);

		// Set flags for object file (not executable)
		// For x64 object files, we typically set IMAGE_FILE_LARGE_ADDRESS_AWARE
		coffi_.get_header()->set_flags(IMAGE_FILE_LARGE_ADDRESS_AWARE);

		auto now = std::chrono::system_clock::now();
		std::time_t current_time_t = std::chrono::system_clock::to_time_t(now);
		coffi_.get_header()->set_time_data_stamp(static_cast<uint32_t>(current_time_t));

		// Add text section first to match Clang order
		auto section_text = coffi_.add_section(".text");
		section_text->set_flags(IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_CODE | IMAGE_SCN_ALIGN_16BYTES);
		sectiontype_to_index[SectionType::TEXT] = section_text->get_index();
		sectiontype_to_name[SectionType::TEXT] = ".text";

		// Add section symbol for .text
		auto symbol_text_main = coffi_.add_symbol(".text");
		symbol_text_main->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_text_main->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_text_main->set_section_number(section_text->get_index() + 1);
		symbol_text_main->set_value(0);

		// Add auxiliary symbol for .text section (format 5)
		COFFI::auxiliary_symbol_record_5 aux_text = {};
		aux_text.length = 0; // Will be set later when we know the section size
		aux_text.number_of_relocations = 0;
		aux_text.number_of_linenumbers = 0;
		aux_text.check_sum = 0;
		aux_text.number = static_cast<uint16_t>(section_text->get_index() + 1);
		aux_text.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_text;
		std::memcpy(aux_record_text.value, &aux_text, sizeof(aux_text));
		symbol_text_main->get_auxiliary_symbols().push_back(aux_record_text);

		auto section_drectve = add_section(".drectve", IMAGE_SCN_ALIGN_1BYTES | IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE, SectionType::DRECTVE);
		section_drectve->append_data(" /DEFAULTLIB:\"LIBCMT\" /DEFAULTLIB:\"OLDNAMES\""); // MSVC also contains '/DEFAULTLIB:\"OLDNAMES\" ', but it doesn't seem to be needed?
		auto symbol_drectve = coffi_.add_symbol(".drectve");
		symbol_drectve->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_drectve->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_drectve->set_section_number(section_drectve->get_index() + 1);
		symbol_drectve->set_value(0);

		// Add auxiliary symbol for .drectve section
		COFFI::auxiliary_symbol_record_5 aux_drectve = {};
		aux_drectve.length = 0;
		aux_drectve.number_of_relocations = 0;
		aux_drectve.number_of_linenumbers = 0;
		aux_drectve.check_sum = 0;
		aux_drectve.number = static_cast<uint16_t>(section_drectve->get_index() + 1);
		aux_drectve.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_drectve;
		std::memcpy(aux_record_drectve.value, &aux_drectve, sizeof(aux_drectve));
		symbol_drectve->get_auxiliary_symbols().push_back(aux_record_drectve);

		// Add .data section
		auto section_data = add_section(".data", IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_8BYTES, SectionType::DATA);
		auto symbol_data = coffi_.add_symbol(".data");
		symbol_data->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_data->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_data->set_section_number(section_data->get_index() + 1);
		symbol_data->set_value(0);

		// Add auxiliary symbol for .data section
		COFFI::auxiliary_symbol_record_5 aux_data = {};
		aux_data.length = 0;
		aux_data.number_of_relocations = 0;
		aux_data.number_of_linenumbers = 0;
		aux_data.check_sum = 0;
		aux_data.number = static_cast<uint16_t>(section_data->get_index() + 1);
		aux_data.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_data;
		std::memcpy(aux_record_data.value, &aux_data, sizeof(aux_data));
		symbol_data->get_auxiliary_symbols().push_back(aux_record_data);

		// Add .bss section
		auto section_bss = add_section(".bss", IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_ALIGN_8BYTES, SectionType::BSS);
		auto symbol_bss = coffi_.add_symbol(".bss");
		symbol_bss->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_bss->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_bss->set_section_number(section_bss->get_index() + 1);
		symbol_bss->set_value(0);

		// Add auxiliary symbol for .bss section
		COFFI::auxiliary_symbol_record_5 aux_bss = {};
		aux_bss.length = 0;
		aux_bss.number_of_relocations = 0;
		aux_bss.number_of_linenumbers = 0;
		aux_bss.check_sum = 0;
		aux_bss.number = static_cast<uint16_t>(section_bss->get_index() + 1);
		aux_bss.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_bss;
		std::memcpy(aux_record_bss.value, &aux_bss, sizeof(aux_bss));
		symbol_bss->get_auxiliary_symbols().push_back(aux_record_bss);

		// Add debug sections to match Clang order
		auto section_debug_s = coffi_.add_section(".debug$S");
		section_debug_s->set_flags(IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_MEM_DISCARDABLE);
		sectiontype_to_index[SectionType::DEBUG_S] = section_debug_s->get_index();
		sectiontype_to_name[SectionType::DEBUG_S] = ".debug$S";

		// Add section symbol for .debug$S
		auto symbol_debug_s = coffi_.add_symbol(".debug$S");
		symbol_debug_s->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_debug_s->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_debug_s->set_section_number(section_debug_s->get_index() + 1);
		symbol_debug_s->set_value(0);

		// Add auxiliary symbol for .debug$S section
		COFFI::auxiliary_symbol_record_5 aux_debug_s = {};
		aux_debug_s.length = 0;
		aux_debug_s.number_of_relocations = 0;
		aux_debug_s.number_of_linenumbers = 0;
		aux_debug_s.check_sum = 0;
		aux_debug_s.number = static_cast<uint16_t>(section_debug_s->get_index() + 1);
		aux_debug_s.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_debug_s;
		std::memcpy(aux_record_debug_s.value, &aux_debug_s, sizeof(aux_debug_s));
		symbol_debug_s->get_auxiliary_symbols().push_back(aux_record_debug_s);

		auto section_debug_t = coffi_.add_section(".debug$T");
		section_debug_t->set_flags(IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_MEM_DISCARDABLE);
		sectiontype_to_index[SectionType::DEBUG_T] = section_debug_t->get_index();
		sectiontype_to_name[SectionType::DEBUG_T] = ".debug$T";

		// Add section symbol for .debug$T
		auto symbol_debug_t = coffi_.add_symbol(".debug$T");
		symbol_debug_t->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_debug_t->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_debug_t->set_section_number(section_debug_t->get_index() + 1);
		symbol_debug_t->set_value(0);

		// Add auxiliary symbol for .debug$T section
		COFFI::auxiliary_symbol_record_5 aux_debug_t = {};
		aux_debug_t.length = 0;
		aux_debug_t.number_of_relocations = 0;
		aux_debug_t.number_of_linenumbers = 0;
		aux_debug_t.check_sum = 0;
		aux_debug_t.number = static_cast<uint16_t>(section_debug_t->get_index() + 1);
		aux_debug_t.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_debug_t;
		std::memcpy(aux_record_debug_t.value, &aux_debug_t, sizeof(aux_debug_t));
		symbol_debug_t->get_auxiliary_symbols().push_back(aux_record_debug_t);

		// Add .xdata section (exception handling data)
		auto section_xdata = add_section(".xdata", IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES, SectionType::XDATA);
		auto symbol_xdata = coffi_.add_symbol(".xdata");
		symbol_xdata->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_xdata->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_xdata->set_section_number(section_xdata->get_index() + 1);
		symbol_xdata->set_value(0);

		// Add auxiliary symbol for .xdata section
		COFFI::auxiliary_symbol_record_5 aux_xdata = {};
		aux_xdata.length = 0;
		aux_xdata.number_of_relocations = 0;
		aux_xdata.number_of_linenumbers = 0;
		aux_xdata.check_sum = 0;
		aux_xdata.number = static_cast<uint16_t>(section_xdata->get_index() + 1);
		aux_xdata.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_xdata;
		std::memcpy(aux_record_xdata.value, &aux_xdata, sizeof(aux_xdata));
		symbol_xdata->get_auxiliary_symbols().push_back(aux_record_xdata);

		// Add .pdata section (procedure data for exception handling)
		auto section_pdata = add_section(".pdata", IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES, SectionType::PDATA);
		auto symbol_pdata = coffi_.add_symbol(".pdata");
		symbol_pdata->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_pdata->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_pdata->set_section_number(section_pdata->get_index() + 1);
		symbol_pdata->set_value(0);

		// Add auxiliary symbol for .pdata section
		COFFI::auxiliary_symbol_record_5 aux_pdata = {};
		aux_pdata.length = 0;
		aux_pdata.number_of_relocations = 0;
		aux_pdata.number_of_linenumbers = 0;
		aux_pdata.check_sum = 0;
		aux_pdata.number = static_cast<uint16_t>(section_pdata->get_index() + 1);
		aux_pdata.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_pdata;
		std::memcpy(aux_record_pdata.value, &aux_pdata, sizeof(aux_pdata));
		symbol_pdata->get_auxiliary_symbols().push_back(aux_record_pdata);

		// Add .llvm_addrsig section (LLVM address significance table)
		auto section_llvm_addrsig = add_section(".llvm_addrsig", IMAGE_SCN_LNK_REMOVE | IMAGE_SCN_ALIGN_1BYTES, SectionType::LLVM_ADDRSIG);
		auto symbol_llvm_addrsig = coffi_.add_symbol(".llvm_addrsig");
		symbol_llvm_addrsig->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_llvm_addrsig->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_llvm_addrsig->set_section_number(section_llvm_addrsig->get_index() + 1);
		symbol_llvm_addrsig->set_value(0);

		// Add auxiliary symbol for .llvm_addrsig section
		COFFI::auxiliary_symbol_record_5 aux_llvm_addrsig = {};
		aux_llvm_addrsig.length = 0;
		aux_llvm_addrsig.number_of_relocations = 0;
		aux_llvm_addrsig.number_of_linenumbers = 0;
		aux_llvm_addrsig.check_sum = 0;
		aux_llvm_addrsig.number = static_cast<uint16_t>(section_llvm_addrsig->get_index() + 1);
		aux_llvm_addrsig.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_llvm_addrsig;
		std::memcpy(aux_record_llvm_addrsig.value, &aux_llvm_addrsig, sizeof(aux_llvm_addrsig));
		symbol_llvm_addrsig->get_auxiliary_symbols().push_back(aux_record_llvm_addrsig);

		std::cerr << "Simplified ObjectFileWriter created successfully" << std::endl;
	}

	COFFI::section* add_section(const std::string& section_name, int32_t flags, std::optional<SectionType> section_type) {
		COFFI::section* section = coffi_.add_section(section_name);
		section->set_flags(flags);
		if (section_type.has_value()) {
			sectiontype_to_index[*section_type] = section->get_index();
			sectiontype_to_name[*section_type] = section_name;
		}
		return section;
	}

	void write(const std::string& filename) {
		try {
			// Skip debug info for now
			std::cerr << "Starting coffi_.save..." << std::endl;
			std::cerr << "Number of sections: " << coffi_.get_sections().size() << std::endl;
			std::cerr << "Number of symbols: " << coffi_.get_symbols()->size() << std::endl;

			// Print section info
			for (size_t i = 0; i < coffi_.get_sections().size(); ++i) {
				auto section = coffi_.get_sections()[i];
				// Note: COFFI has a bug where section names are not stored correctly, so we use our mapping
				std::string section_name = "unknown";
				for (const auto& [type, name] : sectiontype_to_name) {
					if (sectiontype_to_index[type] == static_cast<int>(i)) {
						section_name = name;
						break;
					}
				}
				std::cerr << "Section " << i << ": '" << section_name << "'"
				         << " size=" << section->get_data_size()
				         << " flags=0x" << std::hex << section->get_flags() << std::dec << std::endl;
			}

			// Print symbol info
			auto symbols = coffi_.get_symbols();
			for (size_t i = 0; i < symbols->size(); ++i) {
				auto symbol = (*symbols)[i];
				std::cerr << "Symbol " << i << ": " << symbol.get_name()
				         << " section=" << symbol.get_section_number()
				         << " value=0x" << std::hex << symbol.get_value() << std::dec << std::endl;
			}

			bool success = coffi_.save(filename);
			if (success) {
				std::cerr << "Object file written successfully!" << std::endl;
			} else {
				std::cerr << "COFFI save failed!" << std::endl;
				throw std::runtime_error("Failed to save object file with both COFFI and manual fallback");
			}
		} catch (const std::exception& e) {
			std::cerr << "Error writing object file: " << e.what() << std::endl;
			throw;
		}
	}

	// C++20 compatible name mangling system
	std::string getMangledName(const std::string& name) const {
		// Check if we have function signature information for this function
		auto it = function_signatures_.find(name);
		if (it != function_signatures_.end()) {
			return generateMangledName(name, it->second);
		}

		return name;  // Default: no mangling
	}

private:
	enum EFunctionCallingConv : unsigned char
	{
		cdecl,
		stdcall,
		fastcall,
	};
	// Function signature information for mangling
	struct FunctionSignature {
		Type return_type = Type::Void;
		std::vector<Type> parameter_types;
		bool is_const = false;
		bool is_static = false;
		EFunctionCallingConv calling_convention = EFunctionCallingConv::cdecl;
		std::string namespace_name;
		std::string class_name;

		FunctionSignature() = default;
		FunctionSignature(Type ret_type, std::vector<Type> params)
			: return_type(ret_type), parameter_types(std::move(params)) {}
	};

	mutable std::unordered_map<std::string, FunctionSignature> function_signatures_;

	// Generate Microsoft Visual C++ mangled name
	std::string generateMangledName(const std::string_view name, const FunctionSignature& sig) const {
		// Special case: main function is never mangled
		if (name == "main") {
			return "main";
		}

		std::string mangled = "?";
		mangled += name;

		// Add class name if this is a member function
		if (!sig.class_name.empty()) {
			mangled += "@";
			mangled += sig.class_name;
		}

		mangled += "@@";

		// Add calling convention and linkage
		if (sig.calling_convention == EFunctionCallingConv::cdecl) {
			mangled += "YA";  // __cdecl
		} else if (sig.calling_convention == EFunctionCallingConv::stdcall) {
			mangled += "YG";  // __stdcall
		} else if (sig.calling_convention == EFunctionCallingConv::fastcall) {
			mangled += "YI";  // __fastcall
		} else {
			mangled += "YA";  // Default to __cdecl
		}

		// Add return type
		mangled += getTypeCode(sig.return_type);

		// Add parameter types
		for (const auto& param_type : sig.parameter_types) {
			mangled += getTypeCode(param_type);
		}

		// End marker
		mangled += "@Z";

		return mangled;
	}

	// Get Microsoft Visual C++ type code for mangling
	std::string_view getTypeCode(Type type) const {
		switch (type) {
			case Type::Void: return "X"sv;
			case Type::Bool: return "_N"sv;
			case Type::Char: return "D"sv;
			case Type::UnsignedChar: return "E"sv;
			case Type::Short: return "F"sv;
			case Type::UnsignedShort: return "G"sv;
			case Type::Int: return "H"sv;
			case Type::UnsignedInt: return "I"sv;
			case Type::Long: return "J"sv;
			case Type::UnsignedLong: return "K"sv;
			case Type::LongLong: return "_J"sv;
			case Type::UnsignedLongLong: return "_K"sv;
			case Type::Float: return "M"sv;
			case Type::Double: return "N"sv;
			case Type::LongDouble: return "O"sv;
			default: return "H"sv;  // Default to int for unknown types
		}
	}

public:
	// Add function signature information for proper mangling
	void addFunctionSignature(const std::string& name, Type return_type, const std::vector<Type>& parameter_types) {
		FunctionSignature sig(return_type, parameter_types);
		function_signatures_[name] = sig;
	}

	// Add function signature information for member functions with class name
	void addFunctionSignature(const std::string& name, Type return_type, const std::vector<Type>& parameter_types, const std::string& class_name) {
		FunctionSignature sig(return_type, parameter_types);
		sig.class_name = class_name;
		function_signatures_[name] = sig;
	}

	void add_function_symbol(const std::string& name, uint32_t section_offset, uint32_t stack_space) {
		std::string symbol_name = getMangledName(name);
		std::cerr << "Adding function symbol: " << symbol_name << " (original: " << name << ") at offset " << section_offset << std::endl;
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		auto symbol_func = coffi_.add_symbol(symbol_name);
		symbol_func->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol_func->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol_func->set_section_number(section_text->get_index() + 1);
		symbol_func->set_value(section_offset);

		// Add function to debug info with length 0 - length will be calculated later
		std::cerr << "DEBUG: Adding function to debug builder: " << name << " at offset " << section_offset << std::endl;
		debug_builder_.addFunction(name, section_offset, 0, stack_space);
		std::cerr << "DEBUG: Function added to debug builder" << std::endl;

		// Exception info is now handled directly in IRConverter finalization logic

		std::cerr << "Function symbol added successfully" << std::endl;
	}

	void add_data(const std::vector<char>& data, SectionType section_type) {
		std::cerr << "Adding " << data.size() << " bytes to section " << static_cast<int>(section_type);
		auto section = coffi_.get_sections()[sectiontype_to_index[section_type]];
		std::cerr << " (current size: " << section->get_data_size() << ")" << std::endl;
		if (section_type == SectionType::TEXT) {
			std::cerr << "Machine code bytes (" << data.size() << " total): ";
			for (size_t i = 0; i < data.size(); ++i) {
				std::cerr << std::hex << std::setfill('0') << std::setw(2) << (static_cast<unsigned char>(data[i]) & 0xFF) << " ";
			}
			std::cerr << std::dec << std::endl;
		}
		section->append_data(data.data(), data.size());
	}

	void add_relocation(uint64_t offset, std::string_view symbol_name) {
		// Get the function symbol using mangled name
		std::string mangled_name = getMangledName(std::string(symbol_name));
		auto* symbol = coffi_.get_symbol(std::string(mangled_name));
		if (!symbol) {
			throw std::runtime_error("Symbol not found: " + std::string(symbol_name));
		}

		auto symbol_index = symbol->get_index();
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		COFFI::rel_entry_generic relocation;
		relocation.virtual_address = offset;
		relocation.symbol_table_index = symbol_index;
		relocation.type = IMAGE_REL_AMD64_REL32;
		section_text->add_relocation_entry(&relocation);
	}

	void add_pdata_relocations(uint32_t pdata_offset, const std::string& function_name, uint32_t xdata_offset) {
		std::cerr << "Adding PDATA relocations for function: " << function_name << " at pdata offset " << pdata_offset << std::endl;

		// Get the function symbol using mangled name
		std::string mangled_name = getMangledName(function_name);
		auto* function_symbol = coffi_.get_symbol(mangled_name);
		if (!function_symbol) {
			throw std::runtime_error("Function symbol not found: " + mangled_name + " (original: " + function_name + ")");
		}

		// Get the .xdata section symbol
		auto* xdata_symbol = coffi_.get_symbol(".xdata");
		if (!xdata_symbol) {
			throw std::runtime_error("XDATA section symbol not found");
		}

		auto pdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::PDATA]];

		// Relocation 1: Function start address (offset 0 in PDATA entry)
		COFFI::rel_entry_generic reloc1;
		reloc1.virtual_address = pdata_offset + 0;
		reloc1.symbol_table_index = function_symbol->get_index();
		reloc1.type = IMAGE_REL_AMD64_ADDR32NB;  // 32-bit address without base
		pdata_section->add_relocation_entry(&reloc1);

		// Relocation 2: Function end address (offset 4 in PDATA entry)
		COFFI::rel_entry_generic reloc2;
		reloc2.virtual_address = pdata_offset + 4;
		reloc2.symbol_table_index = function_symbol->get_index();
		reloc2.type = IMAGE_REL_AMD64_ADDR32NB;  // 32-bit address without base
		pdata_section->add_relocation_entry(&reloc2);

		// Relocation 3: Unwind info address (offset 8 in PDATA entry)
		COFFI::rel_entry_generic reloc3;
		reloc3.virtual_address = pdata_offset + 8;
		reloc3.symbol_table_index = xdata_symbol->get_index();
		reloc3.type = IMAGE_REL_AMD64_ADDR32NB;  // 32-bit address without base
		pdata_section->add_relocation_entry(&reloc3);

		std::cerr << "Added 3 PDATA relocations for function " << function_name << std::endl;
	}

	void add_debug_relocation(uint32_t offset, const std::string& symbol_name, uint32_t relocation_type) {
		std::cerr << "Adding debug relocation at offset " << offset << " for symbol: " << symbol_name
		          << " type: 0x" << std::hex << relocation_type << std::dec << std::endl;

		// Get the symbol (could be function symbol or section symbol)
		auto* symbol = coffi_.get_symbol(symbol_name);
		if (!symbol) {
			// Try mangled name for function symbols
			std::string mangled_name = getMangledName(symbol_name);
			symbol = coffi_.get_symbol(mangled_name);
			if (!symbol) {
				throw std::runtime_error("Debug symbol not found: " + symbol_name + " (mangled: " + mangled_name + ")");
			}
		}

		auto debug_s_section = coffi_.get_sections()[sectiontype_to_index[SectionType::DEBUG_S]];

		// Add relocation to .debug$S section with the specified type
		COFFI::rel_entry_generic reloc;
		reloc.virtual_address = offset;
		reloc.symbol_table_index = symbol->get_index();
		reloc.type = relocation_type;  // Use the specified relocation type
		debug_s_section->add_relocation_entry(&reloc);

		std::cerr << "Added debug relocation for symbol " << symbol_name << " at offset " << offset
		          << " type: 0x" << std::hex << relocation_type << std::dec << std::endl;
	}

	// Debug information methods
	void add_source_file(const std::string& filename) {
		debug_builder_.addSourceFile(filename);
	}

	void set_current_function_for_debug(const std::string& name, uint32_t file_id) {
		debug_builder_.setCurrentFunction(name, file_id);
	}

	void add_line_mapping(uint32_t code_offset, uint32_t line_number) {
		debug_builder_.addLineMapping(code_offset, line_number);
	}

	void add_local_variable(const std::string& name, uint32_t type_index, uint16_t flags,
	                       const std::vector<CodeView::VariableLocation>& locations) {
		debug_builder_.addLocalVariable(name, type_index, flags, locations);
	}

	void add_function_parameter(const std::string& name, uint32_t type_index, int32_t stack_offset) {
		debug_builder_.addFunctionParameter(name, type_index, stack_offset);
	}

	void update_function_length(const std::string& name, uint32_t code_length) {
		debug_builder_.updateFunctionLength(name, code_length);
	}

	void set_function_debug_range(const std::string& name, uint32_t prologue_size, uint32_t epilogue_size) {
		debug_builder_.setFunctionDebugRange(name, prologue_size, epilogue_size);
	}

	void finalize_current_function() {
		debug_builder_.finalizeCurrentFunction();
	}

	void add_function_exception_info(const std::string& function_name, uint32_t function_start, uint32_t function_size) {
		// Check if exception info has already been added for this function
		for (const auto& existing : added_exception_functions_) {
			if (existing == function_name) {
				std::cerr << "Exception info already added for function: " << function_name << " - skipping" << std::endl;
				return;
			}
		}

		std::cerr << "Adding exception info for function: " << function_name << " at offset " << function_start << " size " << function_size << std::endl;
		added_exception_functions_.push_back(function_name);

		// Get current XDATA section size to calculate the offset for this function's unwind info
		auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
		uint32_t xdata_offset = static_cast<uint32_t>(xdata_section->get_data_size());

		// Add XDATA (exception handling unwind information) for this specific function
		// Simple unwind info for functions that use standard prologue/epilogue
		std::vector<char> xdata = {
			0x01,  // Version and flags (version 1, no chained info)
			0x04,  // Size of prolog (4 bytes: push rbp + mov rbp, rsp)
			0x02,  // Count of unwind codes
			0x00,  // Frame register (none)
			0x42,  // Unwind code: UWOP_ALLOC_SMALL (4 bytes for shadow space)
			0x00,  // Unwind code: UWOP_PUSH_NONVOL (push rbp)
			0x00,  // Padding
			0x00   // Padding
		};
		add_data(xdata, SectionType::XDATA);

		// Get current PDATA section size to calculate relocation offsets
		auto pdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::PDATA]];
		uint32_t pdata_offset = static_cast<uint32_t>(pdata_section->get_data_size());

		// Add PDATA (procedure data) for this specific function
		// PDATA entry: [function_start, function_end, unwind_info_address]
		std::vector<char> pdata(12);
		*reinterpret_cast<uint32_t*>(&pdata[0]) = function_start;      // Function start RVA (will be relocated)
		*reinterpret_cast<uint32_t*>(&pdata[4]) = function_start + function_size; // Function end RVA (will be relocated)
		*reinterpret_cast<uint32_t*>(&pdata[8]) = xdata_offset;        // Unwind info RVA (will be relocated)
		add_data(pdata, SectionType::PDATA);

		// Add relocations for PDATA section
		// These relocations are critical for the linker to resolve addresses correctly
		add_pdata_relocations(pdata_offset, function_name, xdata_offset);
	}

	void finalize_debug_info() {
		std::cerr << "finalize_debug_info: Generating debug information..." << std::endl;
		// Exception info is now handled directly in IRConverter finalization logic

		// Finalize the current function before generating debug sections
		debug_builder_.finalizeCurrentFunction();

		// Set the correct text section number for symbol references
		uint16_t text_section_number = static_cast<uint16_t>(sectiontype_to_index[SectionType::TEXT] + 1);
		debug_builder_.setTextSectionNumber(text_section_number);
		std::cerr << "DEBUG: Set text section number to " << text_section_number << std::endl;

		// Generate debug sections
		auto debug_s_data = debug_builder_.generateDebugS();
		auto debug_t_data = debug_builder_.generateDebugT();

		// Add debug relocations
		const auto& debug_relocations = debug_builder_.getDebugRelocations();
		for (const auto& reloc : debug_relocations) {
			add_debug_relocation(reloc.offset, reloc.symbol_name, reloc.relocation_type);
		}
		std::cerr << "DEBUG: Added " << debug_relocations.size() << " debug relocations" << std::endl;

		// Add debug data to sections
		if (!debug_s_data.empty()) {
			add_data(std::vector<char>(debug_s_data.begin(), debug_s_data.end()), SectionType::DEBUG_S);
			std::cerr << "Added " << debug_s_data.size() << " bytes of .debug$S data" << std::endl;
		}
		if (!debug_t_data.empty()) {
			add_data(std::vector<char>(debug_t_data.begin(), debug_t_data.end()), SectionType::DEBUG_T);
			std::cerr << "Added " << debug_t_data.size() << " bytes of .debug$T data" << std::endl;
		}
	}

protected:
	COFFI::coffi coffi_;
	std::unordered_map<SectionType, std::string> sectiontype_to_name;
	std::unordered_map<SectionType, int32_t> sectiontype_to_index;
	CodeView::DebugInfoBuilder debug_builder_;

	// Pending function info for exception handling
	struct PendingFunctionInfo {
		std::string name;
		uint32_t offset;
		uint32_t length;
	};
	std::vector<PendingFunctionInfo> pending_functions_;

	// Track functions that already have exception info to avoid duplicates
	std::vector<std::string> added_exception_functions_;
};
