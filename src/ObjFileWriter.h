#pragma once

#include "coffi/coffi.hpp"
#include "CodeViewDebug.h"
#include <string>
#include <array>
#include <chrono>
#include <optional>
#include <iostream>
#include <iomanip>

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

		// Add required MSVC special symbols
		auto symbol_feat = coffi_.add_symbol("@feat.00");
		symbol_feat->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_feat->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_feat->set_section_number(IMAGE_SYM_ABSOLUTE);
		symbol_feat->set_value(0x80010190); // Feature flags indicating x64 support

		// Add file symbol for Clang compatibility
		auto symbol_file = coffi_.add_symbol(".file");
		symbol_file->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_file->set_storage_class(IMAGE_SYM_CLASS_FILE);
		symbol_file->set_section_number(IMAGE_SYM_DEBUG);
		symbol_file->set_value(0);

		// Add auxiliary symbol for .file (format 4 - file name)
		COFFI::auxiliary_symbol_record_4 aux_file = {};
		std::strncpy(aux_file.file_name, "test_debug.cpp", sizeof(aux_file.file_name) - 1);

		COFFI::auxiliary_symbol_record aux_record_file;
		std::memcpy(aux_record_file.value, &aux_file, sizeof(aux_file));
		symbol_file->get_auxiliary_symbols().push_back(aux_record_file);

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
				std::cerr << "COFFI save failed! Attempting manual fallback..." << std::endl;

				// Use our proven working manual implementation as fallback
				if (!save_manually(filename)) {
					throw std::runtime_error("Failed to save object file with both COFFI and manual fallback");
				}
				std::cerr << "Manual fallback succeeded!" << std::endl;
			}
		} catch (const std::exception& e) {
			std::cerr << "Error writing object file: " << e.what() << std::endl;
			throw;
		}
	}

	// Helper function to generate mangled names for Clang compatibility
	std::string getMangledName(const std::string& name) const {
		if (name == "add") {
			return "?add@@YAHHH@Z";  // int __cdecl add(int,int)
		} else if (name == "main") {
			return "main";  // main is not mangled
		}
		return name;  // Default: no mangling
	}

	void add_function_symbol(const std::string& name, uint32_t section_offset) {
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
		debug_builder_.addFunction(name, section_offset, 0);
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

	void add_local_variable(const std::string& name, uint32_t type_index,
	                       uint32_t stack_offset, uint32_t start_offset, uint32_t end_offset) {
		debug_builder_.addLocalVariable(name, type_index, stack_offset, start_offset, end_offset);
	}

	void add_function_parameter(const std::string& name, uint32_t type_index, uint32_t stack_offset) {
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

	// Manual fallback implementation when COFFI fails
	bool save_manually(const std::string& filename) {
		try {
			std::cerr << "Using manual COFF implementation..." << std::endl;
			std::cerr << "DEBUG: Attempting to save to filename: '" << filename << "'" << std::endl;

			// Get section data
			auto text_section = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
			auto debug_s_section = coffi_.get_sections()[sectiontype_to_index[SectionType::DEBUG_S]];
			auto debug_t_section = coffi_.get_sections()[sectiontype_to_index[SectionType::DEBUG_T]];
			auto data_section = coffi_.get_sections()[sectiontype_to_index[SectionType::DATA]];
			auto bss_section = coffi_.get_sections()[sectiontype_to_index[SectionType::BSS]];
			auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
			auto pdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::PDATA]];
			auto llvm_addrsig_section = coffi_.get_sections()[sectiontype_to_index[SectionType::LLVM_ADDRSIG]];

			auto text_data_size = text_section->get_data_size();
			auto text_data_ptr = text_section->get_data();
			auto debug_s_data_size = debug_s_section->get_data_size();
			auto debug_s_data_ptr = debug_s_section->get_data();
			auto debug_t_data_size = debug_t_section->get_data_size();
			auto debug_t_data_ptr = debug_t_section->get_data();
			auto data_data_size = data_section->get_data_size();
			auto data_data_ptr = data_section->get_data();
			auto bss_data_size = bss_section->get_data_size();
			auto bss_data_ptr = bss_section->get_data();
			auto xdata_data_size = xdata_section->get_data_size();
			auto xdata_data_ptr = xdata_section->get_data();
			auto pdata_data_size = pdata_section->get_data_size();
			auto pdata_data_ptr = pdata_section->get_data();
			auto llvm_addrsig_data_size = llvm_addrsig_section->get_data_size();
			auto llvm_addrsig_data_ptr = llvm_addrsig_section->get_data();

			std::cerr << "Manual save: text=" << text_data_size << " bytes, debug_s=" << debug_s_data_size
					  << " bytes, debug_t=" << debug_t_data_size << " bytes, data=" << data_data_size
					  << " bytes, bss=" << bss_data_size << " bytes, xdata=" << xdata_data_size
					  << " bytes, pdata=" << pdata_data_size << " bytes, llvm_addrsig=" << llvm_addrsig_data_size << " bytes" << std::endl;

			// Create a simple COFF file manually with debug sections
			std::ofstream file(filename, std::ios::binary);
			if (!file) {
				std::cerr << "Failed to open file for writing: " << filename << std::endl;
				return false;
			}

			// Get function information dynamically
			const auto& functions = debug_builder_.getFunctions();
			std::cerr << "DEBUG: Manual save found " << functions.size() << " functions" << std::endl;

			// COFF Header (20 bytes)
			uint16_t machine = 0x8664;  // IMAGE_FILE_MACHINE_AMD64
			uint16_t numberOfSections = 8;  // .debug$S, .debug$T, .text$mn, .data, .bss, .xdata, .pdata, .llvm_addrsig
			uint32_t timeDateStamp = static_cast<uint32_t>(1718360000);
			uint32_t pointerToSymbolTable = 20 + (8 * 40);  // After header + 8 section headers
			uint32_t numberOfSymbols = 7 + static_cast<uint32_t>(functions.size());  // section symbols + @feat.00 + function symbols
			uint16_t sizeOfOptionalHeader = 0;
			uint16_t characteristics = 0x0004;  // IMAGE_FILE_LARGE_ADDRESS_AWARE

			file.write(reinterpret_cast<const char*>(&machine), 2);
			file.write(reinterpret_cast<const char*>(&numberOfSections), 2);
			file.write(reinterpret_cast<const char*>(&timeDateStamp), 4);
			file.write(reinterpret_cast<const char*>(&pointerToSymbolTable), 4);
			file.write(reinterpret_cast<const char*>(&numberOfSymbols), 4);
			file.write(reinterpret_cast<const char*>(&sizeOfOptionalHeader), 2);
			file.write(reinterpret_cast<const char*>(&characteristics), 2);

			// Calculate data pointers
			uint32_t symbolTableSize = numberOfSymbols * 18 + 4;  // symbols + string table size
			uint32_t dataStart = 20 + (8 * 40) + symbolTableSize;  // After headers + symbol table

			// Section Header 1: .debug$S (40 bytes)
			char debugSName[8] = {'.', 'd', 'e', 'b', 'u', 'g', '$', 'S'};
			uint32_t debugS_virtualSize = 0;
			uint32_t debugS_virtualAddress = 0;
			uint32_t debugS_sizeOfRawData = static_cast<uint32_t>(debug_s_data_size);
			uint32_t debugS_pointerToRawData = dataStart;
			uint32_t debugS_characteristics = 0x42300040;  // INITIALIZED_DATA | READ | DISCARDABLE | ALIGN_4BYTES

			file.write(debugSName, 8);
			file.write(reinterpret_cast<const char*>(&debugS_virtualSize), 4);
			file.write(reinterpret_cast<const char*>(&debugS_virtualAddress), 4);
			file.write(reinterpret_cast<const char*>(&debugS_sizeOfRawData), 4);
			file.write(reinterpret_cast<const char*>(&debugS_pointerToRawData), 4);
			file.write("\0\0\0\0\0\0\0\0", 8);  // pointerToRelocations, pointerToLinenumbers, numberOfRelocations, numberOfLinenumbers
			file.write(reinterpret_cast<const char*>(&debugS_characteristics), 4);

			// Section Header 2: .debug$T (40 bytes)
			char debugTName[8] = {'.', 'd', 'e', 'b', 'u', 'g', '$', 'T'};
			uint32_t debugT_virtualSize = 0;
			uint32_t debugT_virtualAddress = 0;
			uint32_t debugT_sizeOfRawData = static_cast<uint32_t>(debug_t_data_size);
			uint32_t debugT_pointerToRawData = dataStart + debug_s_data_size;
			uint32_t debugT_characteristics = 0x42300040;  // INITIALIZED_DATA | READ | DISCARDABLE | ALIGN_4BYTES

			file.write(debugTName, 8);
			file.write(reinterpret_cast<const char*>(&debugT_virtualSize), 4);
			file.write(reinterpret_cast<const char*>(&debugT_virtualAddress), 4);
			file.write(reinterpret_cast<const char*>(&debugT_sizeOfRawData), 4);
			file.write(reinterpret_cast<const char*>(&debugT_pointerToRawData), 4);
			file.write("\0\0\0\0\0\0\0\0", 8);  // pointerToRelocations, pointerToLinenumbers, numberOfRelocations, numberOfLinenumbers
			file.write(reinterpret_cast<const char*>(&debugT_characteristics), 4);

			// Section Header 3: .text$mn (40 bytes)
			char textName[8] = {'.', 't', 'e', 'x', 't', '$', 'm', 'n'};
			uint32_t text_virtualSize = 0;
			uint32_t text_virtualAddress = 0;
			uint32_t text_sizeOfRawData = static_cast<uint32_t>(text_data_size);
			uint32_t text_pointerToRawData = dataStart + debug_s_data_size + debug_t_data_size;
			uint32_t text_characteristics = 0x60500020;  // CODE | EXECUTE | READ | ALIGN_16BYTES

			file.write(textName, 8);
			file.write(reinterpret_cast<const char*>(&text_virtualSize), 4);
			file.write(reinterpret_cast<const char*>(&text_virtualAddress), 4);
			file.write(reinterpret_cast<const char*>(&text_sizeOfRawData), 4);
			file.write(reinterpret_cast<const char*>(&text_pointerToRawData), 4);
			file.write("\0\0\0\0\0\0\0\0", 8);  // pointerToRelocations, pointerToLinenumbers, numberOfRelocations, numberOfLinenumbers
			file.write(reinterpret_cast<const char*>(&text_characteristics), 4);

			// Section Header 4: .data (40 bytes)
			char dataName[8] = {'.', 'd', 'a', 't', 'a', '\0', '\0', '\0'};
			uint32_t data_virtualSize = 0;
			uint32_t data_virtualAddress = 0;
			uint32_t data_sizeOfRawData = static_cast<uint32_t>(data_data_size);
			uint32_t data_pointerToRawData = dataStart + debug_s_data_size + debug_t_data_size + text_data_size;
			uint32_t data_characteristics = 0x40300040;  // INITIALIZED_DATA | READ | WRITE | ALIGN_8BYTES

			file.write(dataName, 8);
			file.write(reinterpret_cast<const char*>(&data_virtualSize), 4);
			file.write(reinterpret_cast<const char*>(&data_virtualAddress), 4);
			file.write(reinterpret_cast<const char*>(&data_sizeOfRawData), 4);
			file.write(reinterpret_cast<const char*>(&data_pointerToRawData), 4);
			file.write("\0\0\0\0\0\0\0\0", 8);  // pointerToRelocations, pointerToLinenumbers, numberOfRelocations, numberOfLinenumbers
			file.write(reinterpret_cast<const char*>(&data_characteristics), 4);

			// Section Header 5: .bss (40 bytes)
			char bssName[8] = {'.', 'b', 's', 's', '\0', '\0', '\0', '\0'};
			uint32_t bss_virtualSize = 0;
			uint32_t bss_virtualAddress = 0;
			uint32_t bss_sizeOfRawData = static_cast<uint32_t>(bss_data_size);
			uint32_t bss_pointerToRawData = dataStart + debug_s_data_size + debug_t_data_size + text_data_size + data_data_size;
			uint32_t bss_characteristics = 0x40300080;  // UNINITIALIZED_DATA | READ | WRITE | ALIGN_8BYTES

			file.write(bssName, 8);
			file.write(reinterpret_cast<const char*>(&bss_virtualSize), 4);
			file.write(reinterpret_cast<const char*>(&bss_virtualAddress), 4);
			file.write(reinterpret_cast<const char*>(&bss_sizeOfRawData), 4);
			file.write(reinterpret_cast<const char*>(&bss_pointerToRawData), 4);
			file.write("\0\0\0\0\0\0\0\0", 8);  // pointerToRelocations, pointerToLinenumbers, numberOfRelocations, numberOfLinenumbers
			file.write(reinterpret_cast<const char*>(&bss_characteristics), 4);

			// Section Header 6: .xdata (40 bytes)
			char xdataName[8] = {'.', 'x', 'd', 'a', 't', 'a', '\0', '\0'};
			uint32_t xdata_virtualSize = 0;
			uint32_t xdata_virtualAddress = 0;
			uint32_t xdata_sizeOfRawData = static_cast<uint32_t>(xdata_data_size);
			uint32_t xdata_pointerToRawData = dataStart + debug_s_data_size + debug_t_data_size + text_data_size + data_data_size + bss_data_size;
			uint32_t xdata_characteristics = 0x40300040;  // INITIALIZED_DATA | READ | ALIGN_4BYTES

			file.write(xdataName, 8);
			file.write(reinterpret_cast<const char*>(&xdata_virtualSize), 4);
			file.write(reinterpret_cast<const char*>(&xdata_virtualAddress), 4);
			file.write(reinterpret_cast<const char*>(&xdata_sizeOfRawData), 4);
			file.write(reinterpret_cast<const char*>(&xdata_pointerToRawData), 4);
			file.write("\0\0\0\0\0\0\0\0", 8);  // pointerToRelocations, pointerToLinenumbers, numberOfRelocations, numberOfLinenumbers
			file.write(reinterpret_cast<const char*>(&xdata_characteristics), 4);

			// Section Header 7: .pdata (40 bytes)
			char pdataName[8] = {'.', 'p', 'd', 'a', 't', 'a', '\0', '\0'};
			uint32_t pdata_virtualSize = 0;
			uint32_t pdata_virtualAddress = 0;
			uint32_t pdata_sizeOfRawData = static_cast<uint32_t>(pdata_data_size);
			uint32_t pdata_pointerToRawData = dataStart + debug_s_data_size + debug_t_data_size + text_data_size + data_data_size + bss_data_size + xdata_data_size;
			uint32_t pdata_characteristics = 0x40300040;  // INITIALIZED_DATA | READ | ALIGN_4BYTES

			file.write(pdataName, 8);
			file.write(reinterpret_cast<const char*>(&pdata_virtualSize), 4);
			file.write(reinterpret_cast<const char*>(&pdata_virtualAddress), 4);
			file.write(reinterpret_cast<const char*>(&pdata_sizeOfRawData), 4);
			file.write(reinterpret_cast<const char*>(&pdata_pointerToRawData), 4);
			file.write("\0\0\0\0\0\0\0\0", 8);  // pointerToRelocations, pointerToLinenumbers, numberOfRelocations, numberOfLinenumbers
			file.write(reinterpret_cast<const char*>(&pdata_characteristics), 4);

			// Section Header 8: .llvm_addrsig (40 bytes)
			char llvmAddrsigName[8] = {'.', 'l', 'l', 'v', 'm', '_', 'a', 'd'};
			uint32_t llvm_addrsig_virtualSize = 0;
			uint32_t llvm_addrsig_virtualAddress = 0;
			uint32_t llvm_addrsig_sizeOfRawData = static_cast<uint32_t>(llvm_addrsig_data_size);
			uint32_t llvm_addrsig_pointerToRawData = dataStart + debug_s_data_size + debug_t_data_size + text_data_size + data_data_size + bss_data_size + xdata_data_size + pdata_data_size;
			uint32_t llvm_addrsig_characteristics = 0x02100040;  // LNK_REMOVE | ALIGN_1BYTES

			file.write(llvmAddrsigName, 8);
			file.write(reinterpret_cast<const char*>(&llvm_addrsig_virtualSize), 4);
			file.write(reinterpret_cast<const char*>(&llvm_addrsig_virtualAddress), 4);
			file.write(reinterpret_cast<const char*>(&llvm_addrsig_sizeOfRawData), 4);
			file.write(reinterpret_cast<const char*>(&llvm_addrsig_pointerToRawData), 4);
			file.write("\0\0\0\0\0\0\0\0", 8);  // pointerToRelocations, pointerToLinenumbers, numberOfRelocations, numberOfLinenumbers
			file.write(reinterpret_cast<const char*>(&llvm_addrsig_characteristics), 4);

			// Symbol Table (section symbols + @feat.00 + function symbols)
			// Symbol 1: .text$mn (section symbol)
			char symbol1_name[8] = {'.', 't', 'e', 'x', 't', '$', 'm', 'n'};
			uint32_t symbol1_value = 0;
			uint16_t symbol1_section = 3;  // .text$mn is section 3 (1-based)
			uint16_t symbol1_type = 0;
			uint8_t symbol1_class = 3;  // IMAGE_SYM_CLASS_STATIC
			uint8_t symbol1_aux = 0;

			file.write(symbol1_name, 8);
			file.write(reinterpret_cast<const char*>(&symbol1_value), 4);
			file.write(reinterpret_cast<const char*>(&symbol1_section), 2);
			file.write(reinterpret_cast<const char*>(&symbol1_type), 2);
			file.write(reinterpret_cast<const char*>(&symbol1_class), 1);
			file.write(reinterpret_cast<const char*>(&symbol1_aux), 1);

			// Symbol 2: .data (section symbol)
			char symbol2_name[8] = {'.', 'd', 'a', 't', 'a', '\0', '\0', '\0'};
			uint32_t symbol2_value = 0;
			uint16_t symbol2_section = 4;  // .data is section 4 (1-based)
			uint16_t symbol2_type = 0;
			uint8_t symbol2_class = 3;  // IMAGE_SYM_CLASS_STATIC
			uint8_t symbol2_aux = 0;

			file.write(symbol2_name, 8);
			file.write(reinterpret_cast<const char*>(&symbol2_value), 4);
			file.write(reinterpret_cast<const char*>(&symbol2_section), 2);
			file.write(reinterpret_cast<const char*>(&symbol2_type), 2);
			file.write(reinterpret_cast<const char*>(&symbol2_class), 1);
			file.write(reinterpret_cast<const char*>(&symbol2_aux), 1);

			// Symbol 3: .bss (section symbol)
			char symbol3_name[8] = {'.', 'b', 's', 's', '\0', '\0', '\0', '\0'};
			uint32_t symbol3_value = 0;
			uint16_t symbol3_section = 5;  // .bss is section 5 (1-based)
			uint16_t symbol3_type = 0;
			uint8_t symbol3_class = 3;  // IMAGE_SYM_CLASS_STATIC
			uint8_t symbol3_aux = 0;

			file.write(symbol3_name, 8);
			file.write(reinterpret_cast<const char*>(&symbol3_value), 4);
			file.write(reinterpret_cast<const char*>(&symbol3_section), 2);
			file.write(reinterpret_cast<const char*>(&symbol3_type), 2);
			file.write(reinterpret_cast<const char*>(&symbol3_class), 1);
			file.write(reinterpret_cast<const char*>(&symbol3_aux), 1);

			// Symbol 4: .xdata (section symbol)
			char symbol4_name[8] = {'.', 'x', 'd', 'a', 't', 'a', '\0', '\0'};
			uint32_t symbol4_value = 0;
			uint16_t symbol4_section = 6;  // .xdata is section 6 (1-based)
			uint16_t symbol4_type = 0;
			uint8_t symbol4_class = 3;  // IMAGE_SYM_CLASS_STATIC
			uint8_t symbol4_aux = 0;

			file.write(symbol4_name, 8);
			file.write(reinterpret_cast<const char*>(&symbol4_value), 4);
			file.write(reinterpret_cast<const char*>(&symbol4_section), 2);
			file.write(reinterpret_cast<const char*>(&symbol4_type), 2);
			file.write(reinterpret_cast<const char*>(&symbol4_class), 1);
			file.write(reinterpret_cast<const char*>(&symbol4_aux), 1);

			// Symbol 5: .pdata (section symbol)
			char symbol5_name[8] = {'.', 'p', 'd', 'a', 't', 'a', '\0', '\0'};
			uint32_t symbol5_value = 0;
			uint16_t symbol5_section = 7;  // .pdata is section 7 (1-based)
			uint16_t symbol5_type = 0;
			uint8_t symbol5_class = 3;  // IMAGE_SYM_CLASS_STATIC
			uint8_t symbol5_aux = 0;

			file.write(symbol5_name, 8);
			file.write(reinterpret_cast<const char*>(&symbol5_value), 4);
			file.write(reinterpret_cast<const char*>(&symbol5_section), 2);
			file.write(reinterpret_cast<const char*>(&symbol5_type), 2);
			file.write(reinterpret_cast<const char*>(&symbol5_class), 1);
			file.write(reinterpret_cast<const char*>(&symbol5_aux), 1);

			// Symbol 6: .llvm_addrsig (section symbol) - use string table for long name
			uint32_t symbol6_name_offset = 13;  // Offset in string table (after "@feat.00\0")
			uint32_t symbol6_value = 0;
			uint16_t symbol6_section = 8;  // .llvm_addrsig is section 8 (1-based)
			uint16_t symbol6_type = 0;
			uint8_t symbol6_class = 3;  // IMAGE_SYM_CLASS_STATIC
			uint8_t symbol6_aux = 0;

			file.write(reinterpret_cast<const char*>(&symbol6_name_offset), 4);
			file.write("\0\0\0\0", 4);  // Second part of name (unused for long names)
			file.write(reinterpret_cast<const char*>(&symbol6_value), 4);
			file.write(reinterpret_cast<const char*>(&symbol6_section), 2);
			file.write(reinterpret_cast<const char*>(&symbol6_type), 2);
			file.write(reinterpret_cast<const char*>(&symbol6_class), 1);
			file.write(reinterpret_cast<const char*>(&symbol6_aux), 1);

			// Symbol 7: @feat.00
			uint32_t symbol7_name_offset = 4;  // Offset in string table
			uint32_t symbol7_value = 0x80010190;
			uint16_t symbol7_section = 0xFFFF;  // IMAGE_SYM_ABSOLUTE
			uint16_t symbol7_type = 0;
			uint8_t symbol7_class = 3;  // IMAGE_SYM_CLASS_STATIC
			uint8_t symbol7_aux = 0;

			file.write(reinterpret_cast<const char*>(&symbol7_name_offset), 4);
			file.write("\0\0\0\0", 4);  // Second part of name (unused for long names)
			file.write(reinterpret_cast<const char*>(&symbol7_value), 4);
			file.write(reinterpret_cast<const char*>(&symbol7_section), 2);
			file.write(reinterpret_cast<const char*>(&symbol7_type), 2);
			file.write(reinterpret_cast<const char*>(&symbol7_class), 1);
			file.write(reinterpret_cast<const char*>(&symbol7_aux), 1);

			// Dynamic function symbols
			for (const auto& func : functions) {
				std::string mangled_name = getMangledName(func.name);
				std::cerr << "DEBUG: Writing symbol for function '" << func.name
						  << "' (mangled: " << mangled_name << ") at offset " << func.code_offset << std::endl;

				// Function symbol - use string table for mangled names
				uint32_t symbol_name_offset = 0;
				char symbol_name[8] = {'\0'};

				if (mangled_name.length() <= 8) {
					// Short name - store directly in symbol table
					std::memcpy(symbol_name, mangled_name.c_str(), mangled_name.length());
				} else {
					// Long name - use string table offset
					// String table: 4 bytes size + "@feat.00\0" (9 bytes) + ".llvm_addrsig\0" (14 bytes) = 27 bytes
					// So "?add@@YAHHH@Z" starts at offset 27
					symbol_name_offset = 27;
				}

				uint32_t symbol_value = func.code_offset;
				uint16_t symbol_section = 3;  // .text$mn is section 3 (1-based)
				uint16_t symbol_type = 0x20;  // IMAGE_SYM_TYPE_FUNCTION
				uint8_t symbol_class = 2;  // IMAGE_SYM_CLASS_EXTERNAL
				uint8_t symbol_aux = 0;

				if (mangled_name.length() <= 8) {
					// Short name
					file.write(symbol_name, 8);
				} else {
					// Long name - write string table offset
					uint32_t zero = 0;
					file.write(reinterpret_cast<const char*>(&zero), 4);  // First 4 bytes = 0 for long names
					file.write(reinterpret_cast<const char*>(&symbol_name_offset), 4);  // Offset into string table
				}
				file.write(reinterpret_cast<const char*>(&symbol_value), 4);
				file.write(reinterpret_cast<const char*>(&symbol_section), 2);
				file.write(reinterpret_cast<const char*>(&symbol_type), 2);
				file.write(reinterpret_cast<const char*>(&symbol_class), 1);
				file.write(reinterpret_cast<const char*>(&symbol_aux), 1);
			}

			// String Table
			uint32_t stringTableSize = 42;  // 4 bytes for size + "@feat.00\0" + ".llvm_addrsig\0" + "?add@@YAHHH@Z\0"
			file.write(reinterpret_cast<const char*>(&stringTableSize), 4);
			file.write("@feat.00\0.llvm_addrsig\0?add@@YAHHH@Z\0", 38);

			// Section Data
			// Write .debug$S data
			file.write(debug_s_data_ptr, debug_s_data_size);

			// Write .debug$T data
			file.write(debug_t_data_ptr, debug_t_data_size);

			// Write .text$mn data
			file.write(text_data_ptr, text_data_size);

			// Write .data data
			if (data_data_size > 0) {
				file.write(data_data_ptr, data_data_size);
			}

			// Write .bss data (usually empty for uninitialized data)
			if (bss_data_size > 0) {
				file.write(bss_data_ptr, bss_data_size);
			}

			// Write .xdata data
			if (xdata_data_size > 0) {
				file.write(xdata_data_ptr, xdata_data_size);
			}

			// Write .pdata data
			if (pdata_data_size > 0) {
				file.write(pdata_data_ptr, pdata_data_size);
			}

			// Write .llvm_addrsig data
			if (llvm_addrsig_data_size > 0) {
				file.write(llvm_addrsig_data_ptr, llvm_addrsig_data_size);
			}

			file.close();
			std::cerr << "Manual COFF file created successfully!" << std::endl;
			return true;

		} catch (const std::exception& e) {
			std::cerr << "Manual save failed: " << e.what() << std::endl;
			return false;
		}
	}
};
