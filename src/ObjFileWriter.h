#pragma once

#if defined(__clang__)
// Clang compiler
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif

#include "CodeViewDebug.h"
#include "AstNodeTypes.h"
#include "ObjectFileCommon.h"
#include "NameMangling.h"
#include <string>
#include <string_view>
#include <span>
#include <array>
#include <chrono>
#include <optional>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>
#include "coffi/coffi.hpp"

extern bool g_enable_debug_output;

// Additional COFF relocation types not defined in COFFI
#ifndef IMAGE_REL_AMD64_SECREL
#define IMAGE_REL_AMD64_SECREL          0x000B  // 32 bit offset from base of section containing target
#endif
#ifndef IMAGE_REL_AMD64_SECTION
#define IMAGE_REL_AMD64_SECTION         0x000A  // Section index
#endif
#ifndef IMAGE_REL_AMD64_ADDR64
#define IMAGE_REL_AMD64_ADDR64          0x0001  // 64-bit absolute address
#endif
#ifndef IMAGE_REL_AMD64_REL32
#define IMAGE_REL_AMD64_REL32           0x0004	// 32-bit relative address from byte following reloc
#endif
#ifndef IMAGE_REL_AMD64_ADDR32NB
#define IMAGE_REL_AMD64_ADDR32NB        0x0003 // 32-bit address w/o image base (RVA).
#endif

enum class SectionType : unsigned char
{
	TEXT,
	DATA,
	BSS,
	RDATA,
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
	// Constants for RTTI and exception handling
	static constexpr size_t POINTER_SIZE = 8;  // 64-bit pointers on x64
	
	// Use shared structures from ObjectFileCommon
	using FunctionSignature = ObjectFileCommon::FunctionSignature;
	using CatchHandlerInfo = ObjectFileCommon::CatchHandlerInfo;
	using UnwindMapEntryInfo = ObjectFileCommon::UnwindMapEntryInfo;
	using TryBlockInfo = ObjectFileCommon::TryBlockInfo;
	using SehExceptHandlerInfo = ObjectFileCommon::SehExceptHandlerInfo;
	using SehFinallyHandlerInfo = ObjectFileCommon::SehFinallyHandlerInfo;
	using SehTryBlockInfo = ObjectFileCommon::SehTryBlockInfo;
	using BaseClassDescriptorInfo = ObjectFileCommon::BaseClassDescriptorInfo;

	ObjectFileWriter() {
		if (g_enable_debug_output) std::cerr << "Creating simplified ObjectFileWriter for debugging..." << std::endl;

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

		// Add .rdata section (read-only data for string literals and constants)
		auto section_rdata = add_section(".rdata", IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_16BYTES, SectionType::RDATA);
		auto symbol_rdata = coffi_.add_symbol(".rdata");
		symbol_rdata->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_rdata->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_rdata->set_section_number(section_rdata->get_index() + 1);
		symbol_rdata->set_value(0);

		// Add auxiliary symbol for .rdata section
		COFFI::auxiliary_symbol_record_5 aux_rdata = {};
		aux_rdata.length = 0;
		aux_rdata.number_of_relocations = 0;
		aux_rdata.number_of_linenumbers = 0;
		aux_rdata.check_sum = 0;
		aux_rdata.number = static_cast<uint16_t>(section_rdata->get_index() + 1);
		aux_rdata.selection = 0;

		COFFI::auxiliary_symbol_record aux_record_rdata;
		std::memcpy(aux_record_rdata.value, &aux_rdata, sizeof(aux_rdata));
		symbol_rdata->get_auxiliary_symbols().push_back(aux_record_rdata);

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

		if (g_enable_debug_output) std::cerr << "Simplified ObjectFileWriter created successfully" << std::endl;
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
			if (g_enable_debug_output) std::cerr << "Starting coffi_.save..." << std::endl;
			if (g_enable_debug_output) std::cerr << "Number of sections: " << coffi_.get_sections().get_count() << std::endl;
			if (g_enable_debug_output) std::cerr << "Number of symbols: " << coffi_.get_symbols()->size() << std::endl;

			// Print section info
			for (size_t i = 0; i < coffi_.get_sections().get_count(); ++i) {
				auto section = coffi_.get_sections()[i];
				// Note: COFFI has a bug where section names are not stored correctly, so we use our mapping
				std::string section_name = "unknown";
				for (const auto& [type, name] : sectiontype_to_name) {
					if (sectiontype_to_index[type] == static_cast<int>(i)) {
						section_name = name;
						break;
					}
				}
				auto data_size = section->get_data_size();
				if (g_enable_debug_output) std::cerr << "Section " << i << ": '" << section_name << "'"
				         << " size=" << data_size
				         << " flags=0x" << std::hex << section->get_flags() << std::dec
				         << " reloc_count=" << section->get_reloc_count()
				         << " reloc_offset=" << section->get_reloc_offset();
				
				// For .data section (index 2), check data
				if (section_name == ".data") {
					if (g_enable_debug_output) std::cerr << " <<< DATA SECTION";
				}
				// For .pdata section, check relocations  
				if (section_name == ".pdata") {
					if (g_enable_debug_output) std::cerr << " <<< PDATA SECTION";
				}
				if (g_enable_debug_output) std::cerr << std::endl;
			}

			// Print symbol info
			auto symbols = coffi_.get_symbols();
			for (size_t i = 0; i < symbols->size(); ++i) {
				auto symbol = (*symbols)[i];
				if (g_enable_debug_output) std::cerr << "Symbol " << i << ": " << symbol.get_name()
				         << " section=" << symbol.get_section_number()
				         << " value=0x" << std::hex << symbol.get_value() << std::dec << std::endl;
			}

			bool success = coffi_.save(filename);
			if (g_enable_debug_output) std::cerr << "COFFI save returned: " << (success ? "true" : "FALSE") << std::endl;
			
			// Verify the file was written correctly by checking size
			std::ifstream check_file(filename, std::ios::binary | std::ios::ate);
			if (check_file.is_open()) {
				auto file_size = check_file.tellg();
				if (g_enable_debug_output) std::cerr << "Written file size: " << file_size << " bytes\n";
				check_file.close();
			}
			
			if (success) {
				if (g_enable_debug_output) std::cerr << "Object file written successfully!" << std::endl;
			} else {
				if (g_enable_debug_output) std::cerr << "COFFI save failed!" << std::endl;
				throw std::runtime_error("Failed to save object file with both COFFI and manual fallback");
			}
		} catch (const std::exception& e) {
			if (g_enable_debug_output) std::cerr << "Error writing object file: " << e.what() << std::endl;
			throw;
		}
	}

	// Note: Mangled names are pre-computed by the Parser.
	// Functions with C linkage use their plain names.
	// All names are passed through as-is to the symbol table.

private:
	// Map from mangled name to function signature
	mutable std::unordered_map<std::string, ObjectFileWriter::FunctionSignature> function_signatures_;

	// Get Microsoft Visual C++ type code for mangling (with pointer support)
	std::string getTypeCode(const TypeSpecifierNode& type_node) const {
		std::string code;
		NameMangling::appendTypeCode(code, type_node);
		return code;
	}

public:
	// Generate Microsoft Visual C++ mangled name
	std::string generateMangledName(std::string_view name, const FunctionSignature& sig) const {
		// Special case: main function is never mangled
		if (name == "main") {
			return "main";
		}

		// C linkage functions are not mangled
		if (sig.linkage == Linkage::C) {
			return std::string(name);
		}

		// Calculate approximate size and reserve to avoid reallocations
		size_t estimated_size = 1 + name.size() + 2 + 2 + 2;  // '?' + name + "@@" + calling_conv + return_type
		if (!sig.class_name.empty()) {
			estimated_size += 1 + sig.class_name.size();  // '@' + class_name
		}
		estimated_size += sig.parameter_types.size() * 3;  // Rough estimate for param types
		estimated_size += 2;  // "@Z"

		std::string mangled;
		mangled.reserve(estimated_size);

		mangled += '?';
		mangled += name;

		// Check if this is a destructor (starts with ~)
		if (name.size() > 1 && name[0] == '~') {
			// Delegate to NameMangling implementation which handles MSVC destructor logic correctly
			// (??1ClassName@@QAE@XZ)
			if (!sig.class_name.empty()) {
				// Verify it matches class name to be safe
				std::string_view class_short_name = sig.class_name;
				size_t last_colon = class_short_name.rfind("::");
				if (last_colon != std::string_view::npos) {
					class_short_name = class_short_name.substr(last_colon + 2);
				}
				
				if (name.substr(1) == class_short_name) {
					// It is a destructor
					return std::string(NameMangling::generateMangledNameForDestructor(
						StringTable::getOrInternStringHandle(sig.class_name)
					));
				}
			}
		}

		// Check if this is a constructor (name matches class name)
		if (!sig.class_name.empty()) {
			std::string_view class_short_name = sig.class_name;
			size_t last_colon = class_short_name.rfind("::");
			if (last_colon != std::string_view::npos) {
				class_short_name = class_short_name.substr(last_colon + 2);
			}
				
			if (name == class_short_name) {
				// It is a constructor
				return std::string(NameMangling::generateMangledNameForConstructor(
					sig.class_name,
					sig.parameter_types
				));
			}
		}

		// Add class name if this is a member function
		// For nested classes (e.g., "Outer::Inner"), reverse the order and use @ separators
		// Example: "Outer::Inner" becomes "@Inner@Outer"
		if (!sig.class_name.empty()) {
			std::vector<std::string_view> class_parts;
			std::string_view remaining = sig.class_name;
			size_t pos;
			
			// Split class_name by "::" and collect parts
			while ((pos = remaining.find("::")) != std::string_view::npos) {
				class_parts.push_back(remaining.substr(0, pos));
				remaining = remaining.substr(pos + 2);
			}
			class_parts.push_back(remaining);  // Add the last part
			
			// Reverse and append with @ separators (innermost to outermost)
			for (auto it = class_parts.rbegin(); it != class_parts.rend(); ++it) {
				mangled += '@';
				mangled += *it;
			}
		}

		mangled += "@@";

		// Calling convention - Y for __cdecl (non-member), Q for __thiscall (member)
		if (!sig.class_name.empty()) {
			mangled += "Q";  // Member function
			if (sig.is_const) {
				mangled += "E";  // const member function
			} else {
				mangled += "A";  // non-const member function
			}
		} else {
			mangled += "YA";  // Non-member function with __cdecl
		}

		// Return type
		mangled += getTypeCode(sig.return_type);

		// Parameter types
		for (const auto& param : sig.parameter_types) {
			mangled += getTypeCode(param);
		}

		if (sig.is_variadic) {
			mangled += "Z";  // ... ellipsis parameter
		} else {
			mangled += "@Z";  // End of parameter list (no ellipsis)
		}

		if (g_enable_debug_output) std::cerr << "DEBUG generateMangledName: " << name << " -> " << mangled << "\n";
		return mangled;
	}

	// Add function signature information for proper mangling
	// Returns the mangled name for the function
	std::string addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, Linkage linkage = Linkage::None, bool is_variadic = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		// Generate the mangled name and use it as the key
		std::string mangled_name = generateMangledName(name, sig);
		function_signatures_[mangled_name] = sig;
		return mangled_name;
	}
	
	// Overload that accepts pre-computed mangled name (for function definitions from IR)
	void addFunctionSignature([[maybe_unused]] std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, Linkage linkage, bool is_variadic, std::string_view mangled_name, bool is_inline = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		sig.is_inline = is_inline;
		function_signatures_[std::string(mangled_name)] = sig;
	}

	// Add function signature information for member functions with class name
	// Returns the mangled name for the function
	std::string addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, std::string_view class_name, Linkage linkage = Linkage::None, bool is_variadic = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.class_name = class_name;
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		// Generate the mangled name and use it as the key
		std::string mangled_name = generateMangledName(name, sig);
		function_signatures_[mangled_name] = sig;
		return mangled_name;
	}
	
	// Overload that accepts pre-computed mangled name (for member function definitions from IR)
	void addFunctionSignature([[maybe_unused]] std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, std::string_view class_name, Linkage linkage, bool is_variadic, std::string_view mangled_name, bool is_inline = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.class_name = class_name;
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		sig.is_inline = is_inline;
		function_signatures_[std::string(mangled_name)] = sig;
	}

	void add_function_symbol(std::string_view mangled_name, uint32_t section_offset, uint32_t stack_space, Linkage linkage = Linkage::None) {
		if (g_enable_debug_output) std::cerr << "Adding function symbol: " << mangled_name << " at offset " << section_offset << " with linkage " << static_cast<int>(linkage) << std::endl;
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		auto symbol_func = coffi_.add_symbol(mangled_name);
		symbol_func->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol_func->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol_func->set_section_number(section_text->get_index() + 1);
		symbol_func->set_value(section_offset);

		// Handle dllexport - add export directive
		if (linkage == Linkage::DllExport) {
			auto section_drectve = coffi_.get_sections()[sectiontype_to_index[SectionType::DRECTVE]];
			std::string export_directive = std::string(" /EXPORT:") + std::string(mangled_name);
			if (g_enable_debug_output) std::cerr << "Adding export directive: " << export_directive << std::endl;
			section_drectve->append_data(export_directive.c_str(), export_directive.size());
		}

		// Extract unmangled name for debug info
		// Mangled names start with '?' followed by the function name up to '@@'
		std::string unmangled_name(mangled_name);
		if (!mangled_name.empty() && mangled_name[0] == '?') {
			size_t end_pos = mangled_name.find("@@");
			if (end_pos != std::string_view::npos) {
				unmangled_name = std::string(mangled_name.substr(1, end_pos - 1));
			}
		}

		// Add function to debug info with length 0 - length will be calculated later
		if (g_enable_debug_output) std::cerr << "DEBUG: Adding function to debug builder: " << unmangled_name << " (mangled: " << mangled_name << ") at offset " << section_offset << "\n";
		debug_builder_.addFunction(unmangled_name, std::string(mangled_name), section_offset, 0, stack_space);
		if (g_enable_debug_output) std::cerr << "DEBUG: Function added to debug builder \n";

		// Exception info is now handled directly in IRConverter finalization logic

		if (g_enable_debug_output) std::cerr << "Function symbol added successfully" << std::endl;
	}

	void add_data(std::span<const uint8_t> data, SectionType section_type) {
		add_data(std::span<const char>(reinterpret_cast<const char*>(data.data()), data.size()), section_type);
	}

	void add_data(std::span<const char> data, SectionType section_type) {
		int section_index = sectiontype_to_index[section_type];
		if (g_enable_debug_output) std::cerr << "Adding " << data.size() << " bytes to section " << static_cast<int>(section_type) << " (index=" << section_index << ")";
		auto section = coffi_.get_sections()[section_index];
		uint32_t size_before = section->get_data_size();
		if (g_enable_debug_output) std::cerr << " (current size: " << size_before << ")" << std::endl;
		if (section_type == SectionType::TEXT) {
			if (g_enable_debug_output) std::cerr << "Machine code bytes (" << data.size() << " total): ";
			for (size_t i = 0; i < data.size(); ++i) {
				if (g_enable_debug_output) std::cerr << std::hex << std::setfill('0') << std::setw(2) << (static_cast<unsigned char>(data[i]) & 0xFF) << " ";
			}
			if (g_enable_debug_output) std::cerr << std::dec << std::endl;
		}
		section->append_data(data.data(), data.size());
		uint32_t size_after = section->get_data_size();
		uint32_t size_increase = size_after - size_before;
		if (g_enable_debug_output) std::cerr << "DEBUG: Section " << section_index << " size after append: " << size_after 
		          << " (increased by " << size_increase << ", expected " << data.size() << ")" << std::endl;
		if (size_increase != data.size()) {
			if (g_enable_debug_output) std::cerr << "WARNING: Size increase mismatch! Expected " << data.size() << " but got " << size_increase << std::endl;
		}
	}

	void add_relocation(uint64_t offset, std::string_view symbol_name) {
		add_relocation(offset, symbol_name, IMAGE_REL_AMD64_REL32);
	}

	void add_relocation(uint64_t offset, std::string_view symbol_name, uint32_t relocation_type) {
		// Get the function symbol (name already mangled by Parser)
		std::string symbol_str(symbol_name);
		auto* symbol = coffi_.get_symbol(symbol_str);
		if (!symbol) {
			// Symbol not found - add it as an external symbol (for C library functions like puts, printf, etc.)

			// Add external symbol with:
			// - section number 0 (undefined/external)
			// - storage class IMAGE_SYM_CLASS_EXTERNAL
			// - value 0
			// - type 0x20 (function)
			symbol = coffi_.add_symbol(symbol_str);
			symbol->set_value(0);
			symbol->set_section_number(0);  // 0 = undefined/external symbol
			symbol->set_type(0x20);  // 0x20 = function type
			symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		}

		auto symbol_index = symbol->get_index();
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		COFFI::rel_entry_generic relocation;
		relocation.virtual_address = offset;
		relocation.symbol_table_index = symbol_index;
		relocation.type = relocation_type;
		section_text->add_relocation_entry(&relocation);
	}

	// Add a relocation to the .text section with a custom relocation type
	void add_text_relocation(uint64_t offset, const std::string& symbol_name, uint32_t relocation_type, [[maybe_unused]] int64_t addend = -4) {
		// For COFF format, addend is not used (it's a REL format, not RELA)
		// The addend is encoded in the instruction itself
		// Look up the symbol (could be a global variable, function, etc.)
		auto* symbol = coffi_.get_symbol(symbol_name);
		if (!symbol) {
			// Symbol not found
			if (true) {
				if (g_enable_debug_output) std::cerr << "Warning: Symbol not found for relocation: " << symbol_name << std::endl;
				return;
			}
		}

		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		COFFI::rel_entry_generic relocation;
		relocation.virtual_address = offset;
		relocation.symbol_table_index = symbol->get_index();
		relocation.type = relocation_type;
		section_text->add_relocation_entry(&relocation);

		if (g_enable_debug_output) std::cerr << "Added text relocation at offset " << offset << " for symbol " << symbol_name
		          << " type: 0x" << std::hex << relocation_type << std::dec << std::endl;
	}

	void add_pdata_relocations(uint32_t pdata_offset, std::string_view mangled_name, [[maybe_unused]] uint32_t xdata_offset) {
		if (g_enable_debug_output) std::cerr << "Adding PDATA relocations for function: " << mangled_name << " at pdata offset " << pdata_offset << std::endl;

		// Use the .text section symbol (value=0) for BeginAddress/EndAddress relocations.
		// The pdata data already contains absolute .text offsets as addends, so:
		//   result = text_RVA + 0 + addend = text_RVA + addend = correct
		// Using the function symbol would double-count: text_RVA + func_start + func_start
		auto* text_symbol = coffi_.get_symbol(".text");
		if (!text_symbol) {
			throw std::runtime_error("Text section symbol not found");
		}

		// Get the .xdata section symbol
		auto* xdata_symbol = coffi_.get_symbol(".xdata");
		if (!xdata_symbol) {
			throw std::runtime_error("XDATA section symbol not found");
		}

		auto pdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::PDATA]];

		// Relocation 1: Function start address (offset 0 in PDATA entry)
		// Addend in data = function_start (absolute .text offset)
		COFFI::rel_entry_generic reloc1;
		reloc1.virtual_address = pdata_offset + 0;
		reloc1.symbol_table_index = text_symbol->get_index();
		reloc1.type = IMAGE_REL_AMD64_ADDR32NB;
		pdata_section->add_relocation_entry(&reloc1);

		// Relocation 2: Function end address (offset 4 in PDATA entry)
		// Addend in data = function_start + function_size (absolute .text offset)
		COFFI::rel_entry_generic reloc2;
		reloc2.virtual_address = pdata_offset + 4;
		reloc2.symbol_table_index = text_symbol->get_index();
		reloc2.type = IMAGE_REL_AMD64_ADDR32NB;
		pdata_section->add_relocation_entry(&reloc2);

		// Relocation 3: Unwind info address (offset 8 in PDATA entry)
		COFFI::rel_entry_generic reloc3;
		reloc3.virtual_address = pdata_offset + 8;
		reloc3.symbol_table_index = xdata_symbol->get_index();
		reloc3.type = IMAGE_REL_AMD64_ADDR32NB;
		pdata_section->add_relocation_entry(&reloc3);

		if (g_enable_debug_output) std::cerr << "Added 3 PDATA relocations for function " << mangled_name << std::endl;
	}

	void add_xdata_relocation(uint32_t xdata_offset, std::string_view handler_name) {
		if (g_enable_debug_output) std::cerr << "Adding XDATA relocation at offset " << xdata_offset << " for handler: " << handler_name << std::endl;

		// Get or create the exception handler symbol
		auto* handler_symbol = coffi_.get_symbol(handler_name);
		if (!handler_symbol) {
			// Add external symbol for the C++ exception handler
			handler_symbol = coffi_.add_symbol(handler_name);
			handler_symbol->set_value(0);
			handler_symbol->set_section_number(0);  // 0 = undefined/external symbol
			handler_symbol->set_type(0x20);  // 0x20 = function type
			handler_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			if (g_enable_debug_output) std::cerr << "Created external symbol for exception handler: " << handler_name << std::endl;
		}

		auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];

		// Add relocation for the exception handler RVA in XDATA
		COFFI::rel_entry_generic reloc;
		reloc.virtual_address = xdata_offset;
		reloc.symbol_table_index = handler_symbol->get_index();
		reloc.type = IMAGE_REL_AMD64_ADDR32NB;  // 32-bit address without base
		xdata_section->add_relocation_entry(&reloc);

		if (g_enable_debug_output) std::cerr << "Added XDATA relocation for handler " << handler_name << " at offset " << xdata_offset << std::endl;
	}

	void add_rdata_relocation(uint32_t rdata_offset, std::string_view symbol_name, uint32_t relocation_type = IMAGE_REL_AMD64_ADDR32NB) {
		auto* target_symbol = coffi_.get_symbol(symbol_name);
		if (!target_symbol) {
			target_symbol = coffi_.add_symbol(symbol_name);
			target_symbol->set_value(0);
			target_symbol->set_section_number(0);
			target_symbol->set_type(0x20);
			target_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		}

		auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];

		COFFI::rel_entry_generic reloc;
		reloc.virtual_address = rdata_offset;
		reloc.symbol_table_index = target_symbol->get_index();
		reloc.type = relocation_type;
		rdata_section->add_relocation_entry(&reloc);
	}

	// Simple type name mangling for exception type descriptors
	// Converts C++ type names to MSVC-style mangled names
	std::string mangleTypeName(const std::string& type_name) const {
		// Simple mapping for built-in types
		// MSVC type codes: H=int, I=unsigned int, D=char, E=unsigned char, etc.
		if (type_name == "int") return "H@";
		if (type_name == "unsigned int") return "I@";
		if (type_name == "char") return "D@";
		if (type_name == "unsigned char") return "E@";
		if (type_name == "short") return "F@";
		if (type_name == "unsigned short") return "G@";
		if (type_name == "long") return "J@";
		if (type_name == "unsigned long") return "K@";
		if (type_name == "long long") return "_J@";
		if (type_name == "unsigned long long") return "_K@";
		if (type_name == "float") return "M@";
		if (type_name == "double") return "N@";
		if (type_name == "long double") return "O@";
		if (type_name == "bool") return "_N@";
		if (type_name == "void") return "X@";
		
		// For class/struct types, use the name directly with @ suffix
		// This is a simplified approach - full MSVC would encode nested namespaces, templates, etc.
		// Format: V<name>@@ for struct/class
		return "V" + type_name + "@@";
	}

	// Returns (type descriptor symbol name, type descriptor runtime name string)
	// for use in MSVC exception metadata.
	std::pair<std::string, std::string> getMsvcTypeDescriptorInfo(const std::string& type_name) const {
		// Built-ins use canonical MSVC RTTI descriptor naming with @8 suffix
		// and runtime type name strings with leading dot (e.g., ".H" for int).
		if (type_name == "int") {
			return {"??_R0H@8", ".H"};
		}

		// Fallback to existing simplified naming for non-builtins.
		std::string mangled_type_name = mangleTypeName(type_name);
		return {"??_R0" + mangled_type_name, mangled_type_name};
	}

	std::string get_or_create_exception_throw_info(const std::string& type_name, size_t type_size = 0) {
		if (type_name.empty() || type_name == "void") {
			return std::string();
		}

		// Keep canonical, known-good path for int.
		if (type_name == "int") {
			return get_or_create_builtin_throwinfo(Type::Int);
		}

		auto cached_it = throw_info_symbols_.find(type_name);
		if (cached_it != throw_info_symbols_.end()) {
			return cached_it->second;
		}

		auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];
		if (!rdata_section) {
			return std::string();
		}

		auto [type_desc_symbol, type_runtime_name] = getMsvcTypeDescriptorInfo(type_name);

		auto* type_desc_sym = coffi_.get_symbol(type_desc_symbol);
		if (!type_desc_sym) {
			uint32_t type_desc_offset = static_cast<uint32_t>(rdata_section->get_data_size());

			std::vector<char> type_desc_data;
			type_desc_data.resize(POINTER_SIZE * 2, 0);
			for (char c : type_runtime_name) type_desc_data.push_back(c);
			type_desc_data.push_back(0);

			add_data(type_desc_data, SectionType::RDATA);

			type_desc_sym = coffi_.add_symbol(type_desc_symbol);
			type_desc_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			type_desc_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			type_desc_sym->set_section_number(rdata_section->get_index() + 1);
			type_desc_sym->set_value(type_desc_offset);

			// vftable pointer at offset 0 -> type_info::vftable
			add_rdata_relocation(type_desc_offset, "??_7type_info@@6B@", IMAGE_REL_AMD64_ADDR64);
		}

		const std::string mangled_type_name = mangleTypeName(type_name);
		const std::string catchable_type_symbol = "$flash$ct$" + mangled_type_name;
		const std::string catchable_array_symbol = "$flash$cta$" + mangled_type_name;
		const std::string throw_info_symbol = "$flash$ti$" + mangled_type_name;

		auto* catchable_type_sym = coffi_.get_symbol(catchable_type_symbol);
		if (!catchable_type_sym) {
			uint32_t catchable_type_offset = static_cast<uint32_t>(rdata_section->get_data_size());
			uint32_t throw_size = static_cast<uint32_t>(type_size == 0 ? 8 : type_size);

			std::vector<char> catchable_type_data;
			catchable_type_data.reserve(28);
			auto append_u32 = [&catchable_type_data](uint32_t v) {
				catchable_type_data.push_back(static_cast<char>(v & 0xFF));
				catchable_type_data.push_back(static_cast<char>((v >> 8) & 0xFF));
				catchable_type_data.push_back(static_cast<char>((v >> 16) & 0xFF));
				catchable_type_data.push_back(static_cast<char>((v >> 24) & 0xFF));
			};

			append_u32(0);              // properties
			append_u32(0);              // pType (relocated)
			append_u32(0);              // thisDisplacement.mdisp
			append_u32(0xFFFFFFFFu);    // thisDisplacement.pdisp
			append_u32(0);              // thisDisplacement.vdisp
			append_u32(throw_size);     // sizeOrOffset
			append_u32(0);              // copyFunction

			add_data(catchable_type_data, SectionType::RDATA);

			catchable_type_sym = coffi_.add_symbol(catchable_type_symbol);
			catchable_type_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			catchable_type_sym->set_storage_class(IMAGE_SYM_CLASS_STATIC);
			catchable_type_sym->set_section_number(rdata_section->get_index() + 1);
			catchable_type_sym->set_value(catchable_type_offset);

			add_rdata_relocation(catchable_type_offset + 4, type_desc_symbol, IMAGE_REL_AMD64_ADDR32NB);
		}

		auto* catchable_array_sym = coffi_.get_symbol(catchable_array_symbol);
		if (!catchable_array_sym) {
			uint32_t catchable_array_offset = static_cast<uint32_t>(rdata_section->get_data_size());
			std::vector<char> catchable_array_data(0x0C, 0);
			catchable_array_data[0] = 1; // nCatchableTypes
			add_data(catchable_array_data, SectionType::RDATA);

			catchable_array_sym = coffi_.add_symbol(catchable_array_symbol);
			catchable_array_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			catchable_array_sym->set_storage_class(IMAGE_SYM_CLASS_STATIC);
			catchable_array_sym->set_section_number(rdata_section->get_index() + 1);
			catchable_array_sym->set_value(catchable_array_offset);

			add_rdata_relocation(catchable_array_offset + 4, catchable_type_symbol, IMAGE_REL_AMD64_ADDR32NB);
		}

		auto* throw_info_sym = coffi_.get_symbol(throw_info_symbol);
		if (!throw_info_sym) {
			uint32_t throw_info_offset = static_cast<uint32_t>(rdata_section->get_data_size());
			std::vector<char> throw_info_data(0x1C, 0);
			add_data(throw_info_data, SectionType::RDATA);

			throw_info_sym = coffi_.add_symbol(throw_info_symbol);
			throw_info_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			throw_info_sym->set_storage_class(IMAGE_SYM_CLASS_STATIC);
			throw_info_sym->set_section_number(rdata_section->get_index() + 1);
			throw_info_sym->set_value(throw_info_offset);

			add_rdata_relocation(throw_info_offset + 12, catchable_array_symbol, IMAGE_REL_AMD64_ADDR32NB);
		}

		throw_info_symbols_[type_name] = throw_info_symbol;
		return throw_info_symbol;
	}

	void add_debug_relocation(uint32_t offset, const std::string& symbol_name, uint32_t relocation_type) {
		if (g_enable_debug_output) std::cerr << "Adding debug relocation at offset " << offset << " for symbol: " << symbol_name
		          << " type: 0x" << std::hex << relocation_type << std::dec << std::endl;

		// Get the symbol (could be function symbol or section symbol)
		auto* symbol = coffi_.get_symbol(symbol_name);
		if (!symbol) {
			// Symbol not found
			if (true) {
				throw std::runtime_error("Debug symbol not found: " + symbol_name);
			}
		}

		auto debug_s_section = coffi_.get_sections()[sectiontype_to_index[SectionType::DEBUG_S]];

		// Add relocation to .debug$S section with the specified type
		COFFI::rel_entry_generic reloc;
		reloc.virtual_address = offset;
		reloc.symbol_table_index = symbol->get_index();
		reloc.type = relocation_type;  // Use the specified relocation type
		debug_s_section->add_relocation_entry(&reloc);

		if (g_enable_debug_output) std::cerr << "Added debug relocation for symbol " << symbol_name << " at offset " << offset
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

	void update_function_length(const std::string_view manged_name, uint32_t code_length) {
		debug_builder_.updateFunctionLength(manged_name, code_length);
	}

	void set_function_debug_range(const std::string_view manged_name, uint32_t prologue_size, uint32_t epilogue_size) {
		debug_builder_.setFunctionDebugRange(manged_name, prologue_size, epilogue_size);
	}

	void finalize_current_function() {
		debug_builder_.finalizeCurrentFunction();
	}

	void add_function_exception_info(std::string_view mangled_name, uint32_t function_start, uint32_t function_size, const std::vector<TryBlockInfo>& try_blocks = {}, const std::vector<UnwindMapEntryInfo>& unwind_map = {}, const std::vector<SehTryBlockInfo>& seh_try_blocks = {}, uint32_t stack_frame_size = 0) {
		// Check if exception info has already been added for this function
		for (const auto& existing : added_exception_functions_) {
			if (existing == mangled_name) {
				if (g_enable_debug_output) std::cerr << "Exception info already added for function: " << mangled_name << " - skipping" << std::endl;
				return;
			}
		}

		if (g_enable_debug_output) std::cerr << "Adding exception info for function: " << mangled_name << " at offset " << function_start << " size " << function_size << std::endl;
		added_exception_functions_.push_back(std::string(mangled_name));

		// Get current XDATA section size to calculate the offset for this function's unwind info
		auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
		uint32_t xdata_offset = static_cast<uint32_t>(xdata_section->get_data_size());

		// Determine if this is SEH or C++ exception handling
		bool is_seh = !seh_try_blocks.empty();
		bool is_cpp = !try_blocks.empty();
		uint32_t cpp_funcinfo_local_offset = 0;
		bool has_cpp_funcinfo_local_offset = false;
		StringBuilder cppx_sym_sb;
		cppx_sym_sb.append("$cppxdata$").append(mangled_name);
		std::string cppx_sym_name(cppx_sym_sb.commit());

		if (is_seh && is_cpp) {
			FLASH_LOG(Codegen, Warning, "Function has both SEH and C++ exception handling - using SEH");
		}

		// Add XDATA (exception handling unwind information) for this specific function
		// Windows x64 UNWIND_INFO structure:
		// - BYTE Version:3, Flags:5
		// - BYTE SizeOfProlog
		// - BYTE CountOfCodes
		// - BYTE FrameRegister:4, FrameOffset:4
		// - UNWIND_CODE UnwindCode[CountOfCodes] (aligned to DWORD)
		// - Optional: ExceptionHandler RVA (if UNW_FLAG_EHANDLER/UNW_FLAG_UHANDLER is set)
		// - Optional: Exception-specific data

		// Determine flags based on exception type
		uint8_t unwind_flags = 0x00;
		if (is_seh) {
			// SEH needs both UNW_FLAG_EHANDLER (0x01) and UNW_FLAG_UHANDLER (0x02)
			// EHANDLER triggers __C_specific_handler during the dispatch phase (__except filters)
			// UHANDLER triggers it during the unwind phase (__finally handlers)
			unwind_flags = 0x03;  // UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER
		} else if (is_cpp) {
			// For C++ EH with __CxxFrameHandler3, use both dispatch and unwind handler flags.
			// This matches MSVC FH3 objects where catch dispatch and unwind pass both route through
			// the language-specific handler.
			unwind_flags = 0x03;  // UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER
		}

		// Build unwind codes array dynamically based on actual prologue:
		//   Offset 0:  push rbp           (1 byte)
		//   Offset 1:  mov rbp, rsp       (3 bytes)
		//   Offset 4:  sub rsp, imm32     (7 bytes)
		// Total prologue size: 11 bytes
		//
		// Unwind codes are listed in REVERSE order of prologue operations:
		// Each UNWIND_CODE is 2 bytes: [offset_in_prolog, (info << 4) | operation]
		//   UWOP_PUSH_NONVOL = 0, UWOP_ALLOC_LARGE = 1, UWOP_ALLOC_SMALL = 2, UWOP_SET_FPREG = 3

		std::vector<uint8_t> unwind_codes;
		uint8_t prolog_size = 11;  // push rbp(1) + mov rbp,rsp(3) + sub rsp,imm32(7)

		if (stack_frame_size > 0) {
			if (stack_frame_size <= 128) {
				// UWOP_ALLOC_SMALL: allocation size = (info + 1) * 8, info = size/8 - 1
				uint8_t info = static_cast<uint8_t>(stack_frame_size / 8 - 1);
				unwind_codes.push_back(prolog_size);  // offset at end of sub rsp instruction
				unwind_codes.push_back(static_cast<uint8_t>((info << 4) | 0x02));  // UWOP_ALLOC_SMALL
			} else {
				// UWOP_ALLOC_LARGE with 16-bit operand (info=0): size/8 in next slot
				unwind_codes.push_back(prolog_size);
				unwind_codes.push_back(0x01);  // UWOP_ALLOC_LARGE, info=0
				uint16_t size_in_8bytes = static_cast<uint16_t>(stack_frame_size / 8);
				unwind_codes.push_back(static_cast<uint8_t>(size_in_8bytes & 0xFF));
				unwind_codes.push_back(static_cast<uint8_t>((size_in_8bytes >> 8) & 0xFF));
			}
		}

		// UWOP_SET_FPREG at offset 4 (after mov rbp, rsp)
		unwind_codes.push_back(0x04);  // offset in prolog
		unwind_codes.push_back(0x03);  // UWOP_SET_FPREG, info=0

		// UWOP_PUSH_NONVOL(RBP) at offset 1 (after push rbp)
		unwind_codes.push_back(0x01);  // offset in prolog
		unwind_codes.push_back(static_cast<uint8_t>(0x05 << 4 | 0x00));  // info=5 (RBP), UWOP_PUSH_NONVOL

		// Pad to DWORD alignment (even number of unwind code slots)
		if (unwind_codes.size() % 4 != 0) {
			while (unwind_codes.size() % 4 != 0) {
				unwind_codes.push_back(0x00);
			}
		}

		uint8_t count_of_codes = static_cast<uint8_t>(unwind_codes.size() / 2);
		// Adjust count_of_codes to actual number of UNWIND_CODE entries (excluding padding)
		// For UWOP_ALLOC_SMALL: 1 slot; UWOP_ALLOC_LARGE(info=0): 2 slots; SET_FPREG: 1 slot; PUSH_NONVOL: 1 slot
		if (stack_frame_size > 0) {
			if (stack_frame_size <= 128) {
				count_of_codes = 3;  // ALLOC_SMALL(1) + SET_FPREG(1) + PUSH_NONVOL(1)
			} else {
				count_of_codes = 4;  // ALLOC_LARGE(2) + SET_FPREG(1) + PUSH_NONVOL(1)
			}
		} else {
			count_of_codes = 2;  // SET_FPREG(1) + PUSH_NONVOL(1)
		}

		std::vector<char> xdata = {
			static_cast<char>(0x01 | (unwind_flags << 3)),  // Version 1, Flags
			static_cast<char>(prolog_size),                  // Size of prolog
			static_cast<char>(count_of_codes),               // Count of unwind codes
			static_cast<char>(0x05)                          // Frame register = RBP (register 5), offset = 0
		};
		for (auto b : unwind_codes) {
			xdata.push_back(static_cast<char>(b));
		}
		
		// Add placeholder for exception handler RVA (4 bytes)
		// This will point to __C_specific_handler (SEH) or __CxxFrameHandler3 (C++)
		// We'll add a relocation for this
		uint32_t handler_rva_offset = static_cast<uint32_t>(xdata.size());
		xdata.push_back(0x00);
		xdata.push_back(0x00);
		xdata.push_back(0x00);
		xdata.push_back(0x00);

		// For C++ EH, __CxxFrameHandler3 expects language-specific data to begin with
		// a 32-bit image-relative pointer to FuncInfo.
		uint32_t cpp_funcinfo_rva_field_offset = 0;
		bool has_cpp_funcinfo_rva_field = false;
		if (is_cpp) {
			cpp_funcinfo_rva_field_offset = static_cast<uint32_t>(xdata.size());
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			has_cpp_funcinfo_rva_field = true;
		}

		// Generate SEH-specific exception data
		// Track scope table entry offsets for relocations
		struct ScopeTableReloc {
			uint32_t begin_offset;    // Offset of BeginAddress field within xdata
			uint32_t end_offset;      // Offset of EndAddress field within xdata
			uint32_t handler_offset;  // Offset of HandlerAddress field within xdata
			uint32_t jump_offset;     // Offset of JumpTarget field within xdata
			bool needs_handler_reloc; // True if HandlerAddress needs a relocation (RVA, not constant)
			bool needs_jump_reloc;    // True if JumpTarget needs a relocation (non-zero RVA)
		};
		std::vector<ScopeTableReloc> scope_relocs;
		// C++ EH relocation tracking (for __CxxFrameHandler3 metadata)
		std::vector<uint32_t> cpp_xdata_rva_field_offsets; // fields that point within .xdata
		std::vector<uint32_t> cpp_text_rva_field_offsets;  // fields that point into .text

		if (is_seh) {
			// SEH uses a scope table instead of FuncInfo
			// Scope table format:
			//   DWORD Count (number of scope entries)
			//   SCOPE_TABLE_ENTRY Entries[Count]
			//
			// Each SCOPE_TABLE_ENTRY:
			//   DWORD BeginAddress (image-relative RVA of try block start)
			//   DWORD EndAddress (image-relative RVA of try block end)
			//   DWORD HandlerAddress (RVA of filter funclet, or constant filter value for __except)
			//   DWORD JumpTarget (image-relative RVA of __except handler, or 0 for __finally)

			FLASH_LOG_FORMAT(Codegen, Debug, "Generating SEH scope table with {} entries", seh_try_blocks.size());

			// Count - number of scope table entries
			uint32_t scope_count = static_cast<uint32_t>(seh_try_blocks.size());
			xdata.push_back(static_cast<char>(scope_count & 0xFF));
			xdata.push_back(static_cast<char>((scope_count >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((scope_count >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((scope_count >> 24) & 0xFF));

			// Generate scope table entries
			for (const auto& seh_block : seh_try_blocks) {
				ScopeTableReloc reloc_info;

				// BeginAddress - absolute .text offset (relocation against .text section symbol with value=0)
				uint32_t begin_address = function_start + seh_block.try_start_offset;
				reloc_info.begin_offset = static_cast<uint32_t>(xdata.size());
				xdata.push_back(static_cast<char>(begin_address & 0xFF));
				xdata.push_back(static_cast<char>((begin_address >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((begin_address >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((begin_address >> 24) & 0xFF));

				// EndAddress - absolute .text offset
				uint32_t end_address = function_start + seh_block.try_end_offset;
				reloc_info.end_offset = static_cast<uint32_t>(xdata.size());
				xdata.push_back(static_cast<char>(end_address & 0xFF));
				xdata.push_back(static_cast<char>((end_address >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((end_address >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((end_address >> 24) & 0xFF));

				// HandlerAddress - RVA of handler (or constant filter value for __except with constant filter)
				uint32_t handler_address;
				uint32_t jump_target;
				reloc_info.needs_handler_reloc = false;
				reloc_info.needs_jump_reloc = false;

				if (seh_block.has_except_handler) {
					if (seh_block.except_handler.is_constant_filter) {
						handler_address = static_cast<uint32_t>(static_cast<int32_t>(seh_block.except_handler.constant_filter_value));
						jump_target = function_start + seh_block.except_handler.handler_offset;
						reloc_info.needs_jump_reloc = true;
						FLASH_LOG_FORMAT(Codegen, Debug, "SEH __except: constant filter={}, jump_target={:x}",
						                 seh_block.except_handler.constant_filter_value, jump_target);
					} else {
						// Non-constant filter: handler_address = RVA of filter funclet
						handler_address = function_start + seh_block.except_handler.filter_funclet_offset;
						reloc_info.needs_handler_reloc = true;
						jump_target = function_start + seh_block.except_handler.handler_offset;
						reloc_info.needs_jump_reloc = true;
						FLASH_LOG_FORMAT(Codegen, Debug, "SEH __except: filter funclet at offset {:x}, jump_target={:x}",
						                 seh_block.except_handler.filter_funclet_offset, jump_target);
					}
				} else if (seh_block.has_finally_handler) {
					handler_address = function_start + seh_block.finally_handler.handler_offset;
					reloc_info.needs_handler_reloc = true;
					jump_target = 0;  // JumpTarget = 0 identifies __finally (termination handler) entries
				} else {
					handler_address = 0;  // No handler (shouldn't happen)
					jump_target = 0;
				}
				reloc_info.handler_offset = static_cast<uint32_t>(xdata.size());
				xdata.push_back(static_cast<char>(handler_address & 0xFF));
				xdata.push_back(static_cast<char>((handler_address >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((handler_address >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((handler_address >> 24) & 0xFF));

				reloc_info.jump_offset = static_cast<uint32_t>(xdata.size());
				xdata.push_back(static_cast<char>(jump_target & 0xFF));
				xdata.push_back(static_cast<char>((jump_target >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((jump_target >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((jump_target >> 24) & 0xFF));

				scope_relocs.push_back(reloc_info);

				FLASH_LOG_FORMAT(Codegen, Debug, "SEH scope: begin={} end={} handler={} type={}",
				                 seh_block.try_start_offset, seh_block.try_end_offset,
				                 (seh_block.has_except_handler ? seh_block.except_handler.handler_offset : seh_block.finally_handler.handler_offset),
				                 (seh_block.has_except_handler ? "__except" : "__finally"));
			}
		}
		// Add FuncInfo structure for C++ exception handling
		// This contains information about try blocks and catch handlers
		// FuncInfo structure (simplified):
		//   DWORD magicNumber (0x19930520 or 0x19930521 for x64)
		//   int maxState
		//   DWORD pUnwindMap (RVA)
		//   DWORD nTryBlocks
		//   DWORD pTryBlockMap (RVA)
		//   DWORD nIPMapEntries
		//   DWORD pIPToStateMap (RVA)
		//   ... (other fields for EH4)

		if (is_cpp && !try_blocks.empty()) {
			[[maybe_unused]] uint32_t funcinfo_offset = static_cast<uint32_t>(xdata.size());
			cpp_funcinfo_local_offset = funcinfo_offset;
			has_cpp_funcinfo_local_offset = true;
			if (has_cpp_funcinfo_rva_field) {
				uint32_t funcinfo_rva = xdata_offset + funcinfo_offset;
				xdata[cpp_funcinfo_rva_field_offset + 0] = static_cast<char>(funcinfo_rva & 0xFF);
				xdata[cpp_funcinfo_rva_field_offset + 1] = static_cast<char>((funcinfo_rva >> 8) & 0xFF);
				xdata[cpp_funcinfo_rva_field_offset + 2] = static_cast<char>((funcinfo_rva >> 16) & 0xFF);
				xdata[cpp_funcinfo_rva_field_offset + 3] = static_cast<char>((funcinfo_rva >> 24) & 0xFF);
			}

			struct CatchStateBinding {
				const CatchHandlerInfo* handler;
				int32_t catch_state;
			};

			struct TryStateLayout {
				int32_t try_low;
				int32_t try_high;
				int32_t catch_high;
				std::vector<CatchStateBinding> catches;
			};

			std::vector<TryStateLayout> try_state_layout;
			try_state_layout.reserve(try_blocks.size());

			int32_t next_state = 0;
			for (const auto& try_block : try_blocks) {
				TryStateLayout layout;
				layout.try_low = next_state++;
				layout.try_high = layout.try_low;
				layout.catch_high = layout.try_high;
				layout.catches.reserve(try_block.catch_handlers.size());

				for (const auto& handler : try_block.catch_handlers) {
					int32_t catch_state = next_state++;
					layout.catches.push_back(CatchStateBinding{&handler, catch_state});
					layout.catch_high = catch_state;
				}

				try_state_layout.push_back(std::move(layout));
			}
			
			// Magic number for modern FuncInfo used with __CxxFrameHandler3/__CxxFrameHandler4.
			uint32_t magic = 0x19930522;
			xdata.push_back(static_cast<char>(magic & 0xFF));
			xdata.push_back(static_cast<char>((magic >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((magic >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((magic >> 24) & 0xFF));
			
			// maxState - state count used by FH3 state machine.
			uint32_t max_state = static_cast<uint32_t>(next_state);
			if (!unwind_map.empty() && unwind_map.size() > max_state) {
				max_state = static_cast<uint32_t>(unwind_map.size());
			}
			xdata.push_back(static_cast<char>(max_state & 0xFF));
			xdata.push_back(static_cast<char>((max_state >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((max_state >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((max_state >> 24) & 0xFF));

			// pUnwindMap - patch after map emission
			uint32_t p_unwind_map_field_offset = static_cast<uint32_t>(xdata.size());
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);

			// nTryBlocks - number of try blocks
			uint32_t num_try_blocks = static_cast<uint32_t>(try_blocks.size());
			xdata.push_back(static_cast<char>(num_try_blocks & 0xFF));
			xdata.push_back(static_cast<char>((num_try_blocks >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((num_try_blocks >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((num_try_blocks >> 24) & 0xFF));

			// pTryBlockMap - patch after map emission
			uint32_t p_try_block_map_field_offset = static_cast<uint32_t>(xdata.size());
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);

			// nIPMapEntries - patch after map emission
			uint32_t n_ip_map_entries_field_offset = static_cast<uint32_t>(xdata.size());
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);

			// pIPToStateMap - patch after map emission
			uint32_t p_ip_to_state_map_field_offset = static_cast<uint32_t>(xdata.size());
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);

			// dispUnwindHelp - frame-relative helper slot used by FH3 runtime.
			// Empirically MSVC places this in caller stack space near the top of frame.
			uint32_t disp_unwind_help = 8;
			if (stack_frame_size >= 0x20) {
				disp_unwind_help = stack_frame_size - 0x20;
			}
			xdata.push_back(static_cast<char>(disp_unwind_help & 0xFF));
			xdata.push_back(static_cast<char>((disp_unwind_help >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((disp_unwind_help >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((disp_unwind_help >> 24) & 0xFF));

			// pESTypeList - dynamic exception specification type list (unused)
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);

			// EHFlags (bit 0 set for /EHs semantics)
			uint32_t eh_flags = 0x1;
			xdata.push_back(static_cast<char>(eh_flags & 0xFF));
			xdata.push_back(static_cast<char>((eh_flags >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((eh_flags >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((eh_flags >> 24) & 0xFF));

			auto patch_u32 = [&xdata](uint32_t field_offset, uint32_t value) {
				xdata[field_offset + 0] = static_cast<char>(value & 0xFF);
				xdata[field_offset + 1] = static_cast<char>((value >> 8) & 0xFF);
				xdata[field_offset + 2] = static_cast<char>((value >> 16) & 0xFF);
				xdata[field_offset + 3] = static_cast<char>((value >> 24) & 0xFF);
			};

			uint32_t unwind_map_offset = 0;
			if (!unwind_map.empty()) {
				unwind_map_offset = xdata_offset + static_cast<uint32_t>(xdata.size());
				patch_u32(p_unwind_map_field_offset, unwind_map_offset);
				cpp_xdata_rva_field_offsets.push_back(p_unwind_map_field_offset);
			}
			
			// Now add UnwindMap entries (if present)
			// Each UnwindMapEntry:
			//   int toState (state to transition to, -1 = end of unwind chain)
			//   DWORD action (RVA to cleanup/destructor function, or 0 for no action)
			if (!unwind_map.empty()) {
				for (const auto& unwind_entry : unwind_map) {
					// toState
					int32_t to_state = unwind_entry.to_state;
					xdata.push_back(static_cast<char>(to_state & 0xFF));
					xdata.push_back(static_cast<char>((to_state >> 8) & 0xFF));
					xdata.push_back(static_cast<char>((to_state >> 16) & 0xFF));
					xdata.push_back(static_cast<char>((to_state >> 24) & 0xFF));
					
					// action - RVA to destructor/cleanup function
					// For now, we'll use 0 (no action) since we don't have destructor function addresses yet
					// TODO: Add relocation for destructor function when we have the mangled name
					uint32_t action_rva = 0;
					if (!unwind_entry.action.empty()) {
						// We would need to add a relocation here for the destructor function
						// For now, just set to 0 and add TODO comment
						// action_rva will be patched via relocation
					}
					xdata.push_back(static_cast<char>(action_rva & 0xFF));
					xdata.push_back(static_cast<char>((action_rva >> 8) & 0xFF));
					xdata.push_back(static_cast<char>((action_rva >> 16) & 0xFF));
					xdata.push_back(static_cast<char>((action_rva >> 24) & 0xFF));
				}
			}

			uint32_t tryblock_map_offset = xdata_offset + static_cast<uint32_t>(xdata.size());
			patch_u32(p_try_block_map_field_offset, tryblock_map_offset);
			cpp_xdata_rva_field_offsets.push_back(p_try_block_map_field_offset);
			
			// Now add TryBlockMap entries
			// Each TryBlockMapEntry:
			//   int tryLow (state number)
			//   int tryHigh (state number)
			//   int catchHigh (state number after try)
			//   int nCatches
			//   DWORD pHandlerArray (RVA)
			
			uint32_t handler_array_base = tryblock_map_offset + (num_try_blocks * 20); // 20 bytes per TryBlockMapEntry
			
			for (size_t i = 0; i < try_blocks.size(); ++i) {
				const auto& try_block = try_blocks[i];
				const auto& state_layout = try_state_layout[i];
				
				// tryLow (state when entering try block)
				uint32_t try_low = static_cast<uint32_t>(state_layout.try_low);
				xdata.push_back(static_cast<char>(try_low & 0xFF));
				xdata.push_back(static_cast<char>((try_low >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((try_low >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((try_low >> 24) & 0xFF));
				
				// tryHigh (inclusive state range for the try body)
				uint32_t try_high = static_cast<uint32_t>(state_layout.try_high);
				xdata.push_back(static_cast<char>(try_high & 0xFF));
				xdata.push_back(static_cast<char>((try_high >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((try_high >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((try_high >> 24) & 0xFF));
				
				// catchHigh (highest state owned by this try + catch funclets)
				uint32_t catch_high = static_cast<uint32_t>(state_layout.catch_high);
				xdata.push_back(static_cast<char>(catch_high & 0xFF));
				xdata.push_back(static_cast<char>((catch_high >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((catch_high >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((catch_high >> 24) & 0xFF));
				
				// nCatches
				uint32_t num_catches = static_cast<uint32_t>(try_block.catch_handlers.size());
				xdata.push_back(static_cast<char>(num_catches & 0xFF));
				xdata.push_back(static_cast<char>((num_catches >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((num_catches >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((num_catches >> 24) & 0xFF));
				
				// pHandlerArray - RVA to handler array for this try block
				uint32_t handler_array_offset = handler_array_base;
				uint32_t p_handler_array_field_offset = static_cast<uint32_t>(xdata.size());
				xdata.push_back(static_cast<char>(handler_array_offset & 0xFF));
				xdata.push_back(static_cast<char>((handler_array_offset >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((handler_array_offset >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((handler_array_offset >> 24) & 0xFF));
				cpp_xdata_rva_field_offsets.push_back(p_handler_array_field_offset);
				
				handler_array_base += num_catches * 16; // 16 bytes per HandlerType entry
			}
			
			// Now add HandlerType arrays for each try block
			// Each HandlerType:
			//   DWORD adjectives (0x01 = const, 0x08 = reference, 0 = by-value)
			//   DWORD pType (RVA to type descriptor, 0 for catch-all)
			//   int catchObjOffset (frame offset of catch parameter, negative)
			//   DWORD addressOfHandler (RVA of catch handler code)
			
			// First, generate type descriptors for all unique exception types
			// Use class member to track across multiple function calls
			
			for (const auto& try_block : try_blocks) {
				for (const auto& handler : try_block.catch_handlers) {
					if (!handler.is_catch_all && !handler.type_name.empty()) {
						// Mangle the type name to get the symbol name
						auto [type_desc_symbol, type_desc_runtime_name] = getMsvcTypeDescriptorInfo(handler.type_name);
						
						// Check if we've already created a descriptor for this type
						// by checking both the class member map and if the symbol already exists
						if (type_descriptor_offsets_.find(handler.type_name) == type_descriptor_offsets_.end()) {
							// Check if the symbol already exists (could have been created elsewhere)
							auto* existing_symbol = coffi_.get_symbol(type_desc_symbol);
							if (existing_symbol) {
								// Symbol already exists, just record its offset for later use
								type_descriptor_offsets_[handler.type_name] = existing_symbol->get_value();
								if (g_enable_debug_output) std::cerr << "  Type descriptor '" << type_desc_symbol 
								          << "' already exists for exception type '" << handler.type_name << "'" << std::endl;
							} else {
								// Symbol doesn't exist, create it
								// Validate that RDATA section exists
								auto rdata_section_it = sectiontype_to_index.find(SectionType::RDATA);
								if (rdata_section_it == sectiontype_to_index.end()) {
									if (g_enable_debug_output) std::cerr << "ERROR: RDATA section not found for type descriptor generation" << std::endl;
									continue;
								}
								
								// Generate type descriptor in .rdata section
								auto rdata_section = coffi_.get_sections()[rdata_section_it->second];
								uint32_t type_desc_offset = static_cast<uint32_t>(rdata_section->get_data_size());
								
								// Create type descriptor data
								// Format: vtable_ptr (8 bytes) + spare (8 bytes) + mangled_name (null-terminated)
								std::vector<char> type_desc_data;
								
								// vtable pointer (POINTER_SIZE bytes) - null for exception types
								for (size_t i = 0; i < POINTER_SIZE; ++i) type_desc_data.push_back(0);
								
								// spare pointer (POINTER_SIZE bytes) - null
								for (size_t i = 0; i < POINTER_SIZE; ++i) type_desc_data.push_back(0);
								
								// mangled name (null-terminated)
								for (char c : type_desc_runtime_name) type_desc_data.push_back(c);
								type_desc_data.push_back(0);
								
								// Add to .rdata section
								add_data(type_desc_data, SectionType::RDATA);
								
								// Create symbol for the type descriptor
								auto type_desc_sym = coffi_.add_symbol(type_desc_symbol);
								type_desc_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
								type_desc_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
								type_desc_sym->set_section_number(rdata_section->get_index() + 1);
								type_desc_sym->set_value(type_desc_offset);
								
								if (g_enable_debug_output) std::cerr << "  Created type descriptor '" << type_desc_symbol << "' for exception type '" 
								          << handler.type_name << "' at offset " << type_desc_offset << std::endl;
								
								type_descriptor_offsets_[handler.type_name] = type_desc_offset;
							}
						}
					}
				}
			}
			
			// Now generate HandlerType entries with proper pType references
			auto ensure_catch_symbol = [this, function_start](std::string_view parent_mangled_name, uint32_t funclet_entry_offset, size_t handler_index) -> std::string {
				StringBuilder sb;
				sb.append("$catch$").append(parent_mangled_name).append("$").append(handler_index);
				std::string catch_symbol_name(sb.commit());

				auto* existing = coffi_.get_symbol(catch_symbol_name);
				if (existing) {
					return catch_symbol_name;
				}

				auto text_section = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
				auto* catch_symbol = coffi_.add_symbol(catch_symbol_name);
				catch_symbol->set_type(0x20);  // function symbol
				catch_symbol->set_storage_class(IMAGE_SYM_CLASS_STATIC);
				catch_symbol->set_section_number(text_section->get_index() + 1);
				catch_symbol->set_value(function_start + funclet_entry_offset);

				return catch_symbol_name;
			};

			size_t handler_index = 0;
			for (size_t try_index = 0; try_index < try_blocks.size(); ++try_index) {
				const auto& state_layout = try_state_layout[try_index];
				for (const auto& catch_binding : state_layout.catches) {
					const auto& handler = *catch_binding.handler;
					// adjectives - MSVC exception handler flags
					// 0x01 = const
					// 0x08 = reference (lvalue reference &)
					// 0x10 = rvalue reference (&&)
					uint32_t adjectives = 0;
					if (handler.is_const) {
						adjectives |= 0x01;
					}
					if (handler.is_reference) {
						adjectives |= 0x08;
					}
					if (handler.is_rvalue_reference) {
						adjectives |= 0x10;
					}
					
					xdata.push_back(static_cast<char>(adjectives & 0xFF));
					xdata.push_back(static_cast<char>((adjectives >> 8) & 0xFF));
					xdata.push_back(static_cast<char>((adjectives >> 16) & 0xFF));
					xdata.push_back(static_cast<char>((adjectives >> 24) & 0xFF));
					
					// pType - RVA to type descriptor (0 for catch-all)
					uint32_t ptype_offset = static_cast<uint32_t>(xdata.size());
					// Add placeholder for pType (4 bytes)
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					
					if (!handler.is_catch_all && !handler.type_name.empty()) {
						// Type-specific catch - add relocation for pType to point to the type descriptor
						auto type_desc_info = getMsvcTypeDescriptorInfo(handler.type_name);
						const std::string& type_desc_symbol = type_desc_info.first;
						add_xdata_relocation(xdata_offset + ptype_offset, type_desc_symbol);
						
						if (g_enable_debug_output) std::cerr << "  Added pType relocation for handler " << handler_index 
						          << " to type descriptor '" << type_desc_symbol << "'" << std::endl;
					}
					// For catch(...), pType remains 0 (no relocation needed)
					
					// catchObjOffset (dispCatchObj).
					// For current FH3 path, keep this as 0 to avoid writing into an invalid
					// establisher-frame slot. Catch variable materialization is handled in codegen.
					int32_t catch_offset = 0;
					xdata.push_back(static_cast<char>(catch_offset & 0xFF));
					xdata.push_back(static_cast<char>((catch_offset >> 8) & 0xFF));
					xdata.push_back(static_cast<char>((catch_offset >> 16) & 0xFF));
					xdata.push_back(static_cast<char>((catch_offset >> 24) & 0xFF));
					
					// addressOfHandler - RVA of catch handler entry.
					// Use a dedicated catch symbol to mirror MSVC's handler map relocation style.
					uint32_t funclet_entry_offset = handler.funclet_entry_offset != 0 ? handler.funclet_entry_offset : handler.handler_offset;
					std::string catch_symbol_name = ensure_catch_symbol(mangled_name, funclet_entry_offset, handler_index);
					uint32_t address_of_handler_field_offset = static_cast<uint32_t>(xdata.size());
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					add_xdata_relocation(xdata_offset + address_of_handler_field_offset, catch_symbol_name);
					
					handler_index++;
				}
			}

			// Build a funclet-aware IP-to-state map so __CxxFrameHandler3 can resolve
			// active try states and active catch funclet states.
			struct IpStateEntry {
				uint32_t ip_rva;
				int32_t state;
			};

			auto resolve_funclet_start = [](const CatchHandlerInfo& handler) -> uint32_t {
				return handler.funclet_entry_offset != 0 ? handler.funclet_entry_offset : handler.handler_offset;
			};

			auto resolve_funclet_end = [function_size, &resolve_funclet_start](
				const CatchHandlerInfo& handler,
				const CatchHandlerInfo* next_handler) -> uint32_t {
				uint32_t start = resolve_funclet_start(handler);
				uint32_t end = handler.funclet_end_offset != 0 ? handler.funclet_end_offset : handler.handler_end_offset;
				if (end == 0 && next_handler != nullptr) {
					end = resolve_funclet_start(*next_handler);
				}
				if (end == 0 || end > function_size) {
					end = function_size;
				}
				if (end <= start) {
					return start;
				}
				return end;
			};

			std::vector<IpStateEntry> ip_to_state_entries;
			ip_to_state_entries.reserve(try_blocks.size() * 6 + 2);
			ip_to_state_entries.push_back({function_start, -1});

			for (size_t i = 0; i < try_blocks.size(); ++i) {
				const auto& tb = try_blocks[i];
				const auto& state_layout = try_state_layout[i];
				const int32_t try_low_state = state_layout.try_low;

				ip_to_state_entries.push_back({function_start + tb.try_start_offset, try_low_state});
				ip_to_state_entries.push_back({function_start + tb.try_end_offset, -1});

				for (size_t j = 0; j < state_layout.catches.size(); ++j) {
					const auto& binding = state_layout.catches[j];
					const CatchHandlerInfo& handler = *binding.handler;
					const CatchHandlerInfo* next_handler = (j + 1 < tb.catch_handlers.size()) ? &tb.catch_handlers[j + 1] : nullptr;

					uint32_t funclet_start = resolve_funclet_start(handler);
					uint32_t funclet_end = resolve_funclet_end(handler, next_handler);
					if (funclet_start < function_size && funclet_end > funclet_start) {
						ip_to_state_entries.push_back({function_start + funclet_start, binding.catch_state});
						ip_to_state_entries.push_back({function_start + funclet_end, -1});
					}
				}
			}

			// Sentinel state at function end.
			ip_to_state_entries.push_back({function_start + function_size, -1});

			std::sort(ip_to_state_entries.begin(), ip_to_state_entries.end(), [](const IpStateEntry& a, const IpStateEntry& b) {
				if (a.ip_rva != b.ip_rva) {
					return a.ip_rva < b.ip_rva;
				}
				return a.state < b.state;
			});

			// Deduplicate equal IP entries by keeping the last state for that address.
			std::vector<IpStateEntry> compact_ip_to_state;
			compact_ip_to_state.reserve(ip_to_state_entries.size());
			for (const auto& entry : ip_to_state_entries) {
				if (!compact_ip_to_state.empty() && compact_ip_to_state.back().ip_rva == entry.ip_rva) {
					compact_ip_to_state.back().state = entry.state;
				} else {
					compact_ip_to_state.push_back(entry);
				}
			}

			uint32_t ip_to_state_map_offset = xdata_offset + static_cast<uint32_t>(xdata.size());
			patch_u32(n_ip_map_entries_field_offset, static_cast<uint32_t>(compact_ip_to_state.size()));
			patch_u32(p_ip_to_state_map_field_offset, ip_to_state_map_offset);
			cpp_xdata_rva_field_offsets.push_back(p_ip_to_state_map_field_offset);

			for (const auto& entry : compact_ip_to_state) {
				uint32_t ip_field_offset = static_cast<uint32_t>(xdata.size());
				xdata.push_back(static_cast<char>(entry.ip_rva & 0xFF));
				xdata.push_back(static_cast<char>((entry.ip_rva >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((entry.ip_rva >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((entry.ip_rva >> 24) & 0xFF));
				cpp_text_rva_field_offsets.push_back(ip_field_offset);

				xdata.push_back(static_cast<char>(entry.state & 0xFF));
				xdata.push_back(static_cast<char>((entry.state >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((entry.state >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((entry.state >> 24) & 0xFF));
			}
		}
		
		// Mirror FuncInfo into .rdata and repoint UNWIND language-specific data pointer.
		if (is_cpp && has_cpp_funcinfo_rva_field && has_cpp_funcinfo_local_offset) {
			auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];
			uint32_t cppxdata_rva = static_cast<uint32_t>(rdata_section->get_data_size());

			// FuncInfo has 10 DWORD fields (40 bytes).
			constexpr uint32_t kFuncInfoSize = 40;
			if (cpp_funcinfo_local_offset + kFuncInfoSize <= xdata.size()) {
				std::vector<char> cppxdata_blob(
					xdata.begin() + static_cast<std::ptrdiff_t>(cpp_funcinfo_local_offset),
					xdata.begin() + static_cast<std::ptrdiff_t>(cpp_funcinfo_local_offset + kFuncInfoSize));
				add_data(cppxdata_blob, SectionType::RDATA);

				auto* cppx_sym = coffi_.get_symbol(cppx_sym_name);
				if (!cppx_sym) {
					cppx_sym = coffi_.add_symbol(cppx_sym_name);
					cppx_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
					cppx_sym->set_storage_class(IMAGE_SYM_CLASS_STATIC);
					cppx_sym->set_section_number(rdata_section->get_index() + 1);
					cppx_sym->set_value(cppxdata_rva);
				}

				// Repoint UNWIND language-specific pointer to $cppxdata$ symbol in .rdata.
				xdata[cpp_funcinfo_rva_field_offset + 0] = static_cast<char>(cppxdata_rva & 0xFF);
				xdata[cpp_funcinfo_rva_field_offset + 1] = static_cast<char>((cppxdata_rva >> 8) & 0xFF);
				xdata[cpp_funcinfo_rva_field_offset + 2] = static_cast<char>((cppxdata_rva >> 16) & 0xFF);
				xdata[cpp_funcinfo_rva_field_offset + 3] = static_cast<char>((cppxdata_rva >> 24) & 0xFF);

				// Ensure FuncInfo internal map pointers in .rdata are image-relative via relocations.
				// Offsets within FuncInfo: pUnwindMap=+8, pTryBlockMap=+16, pIPToStateMap=+24.
				add_rdata_relocation(cppxdata_rva + 8, ".xdata", IMAGE_REL_AMD64_ADDR32NB);
				add_rdata_relocation(cppxdata_rva + 16, ".xdata", IMAGE_REL_AMD64_ADDR32NB);
				add_rdata_relocation(cppxdata_rva + 24, ".xdata", IMAGE_REL_AMD64_ADDR32NB);
			}
		}

		// Add the XDATA to the section
		add_data(xdata, SectionType::XDATA);

		// Add relocation for the exception handler RVA
		// Point to __C_specific_handler (SEH) or __CxxFrameHandler3 (C++)
		if (is_seh) {
			add_xdata_relocation(xdata_offset + handler_rva_offset, "__C_specific_handler");
			FLASH_LOG(Codegen, Debug, "Added relocation to __C_specific_handler for SEH");

			// Add IMAGE_REL_AMD64_ADDR32NB relocations for scope table entries
			// These relocations are against the .text section symbol (value=0) so the linker computes:
			//   result = text_RVA + 0 + addend = text_RVA + addend
			// The addend in data is the absolute .text offset (function_start + offset_within_func)
			auto* text_symbol = coffi_.get_symbol(".text");
			if (text_symbol) {
				auto xdata_sec = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
				for (const auto& sr : scope_relocs) {
					// BeginAddress relocation
					COFFI::rel_entry_generic reloc_begin;
					reloc_begin.virtual_address = xdata_offset + sr.begin_offset;
					reloc_begin.symbol_table_index = text_symbol->get_index();
					reloc_begin.type = IMAGE_REL_AMD64_ADDR32NB;
					xdata_sec->add_relocation_entry(&reloc_begin);

					// EndAddress relocation
					COFFI::rel_entry_generic reloc_end;
					reloc_end.virtual_address = xdata_offset + sr.end_offset;
					reloc_end.symbol_table_index = text_symbol->get_index();
					reloc_end.type = IMAGE_REL_AMD64_ADDR32NB;
					xdata_sec->add_relocation_entry(&reloc_end);

					// HandlerAddress relocation (only for __finally handlers that need RVA)
					if (sr.needs_handler_reloc) {
						COFFI::rel_entry_generic reloc_handler;
						reloc_handler.virtual_address = xdata_offset + sr.handler_offset;
						reloc_handler.symbol_table_index = text_symbol->get_index();
						reloc_handler.type = IMAGE_REL_AMD64_ADDR32NB;
						xdata_sec->add_relocation_entry(&reloc_handler);
					}

					// JumpTarget relocation (for __except handlers)
					if (sr.needs_jump_reloc) {
						COFFI::rel_entry_generic reloc_jump;
						reloc_jump.virtual_address = xdata_offset + sr.jump_offset;
						reloc_jump.symbol_table_index = text_symbol->get_index();
						reloc_jump.type = IMAGE_REL_AMD64_ADDR32NB;
						xdata_sec->add_relocation_entry(&reloc_jump);
					}
				}
				FLASH_LOG_FORMAT(Codegen, Debug, "Added {} scope table relocations for SEH", scope_relocs.size());
			}
		} else if (is_cpp) {
			add_xdata_relocation(xdata_offset + handler_rva_offset, "__CxxFrameHandler3");
			FLASH_LOG(Codegen, Debug, "Added relocation to __CxxFrameHandler3 for C++");
			if (has_cpp_funcinfo_rva_field) {
				add_xdata_relocation(xdata_offset + cpp_funcinfo_rva_field_offset, cppx_sym_name);
			}

			// Add IMAGE_REL_AMD64_ADDR32NB relocations for C++ EH metadata RVAs.
			// These fields are image-relative RVAs and must be fixed by the linker.
			auto* xdata_symbol = coffi_.get_symbol(".xdata");
			auto* text_symbol = coffi_.get_symbol(".text");
			auto xdata_sec = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];

			if (xdata_symbol) {
				for (uint32_t field_off : cpp_xdata_rva_field_offsets) {
					COFFI::rel_entry_generic reloc;
					reloc.virtual_address = xdata_offset + field_off;
					reloc.symbol_table_index = xdata_symbol->get_index();
					reloc.type = IMAGE_REL_AMD64_ADDR32NB;
					xdata_sec->add_relocation_entry(&reloc);
				}
			}

			if (text_symbol) {
				for (uint32_t field_off : cpp_text_rva_field_offsets) {
					COFFI::rel_entry_generic reloc;
					reloc.virtual_address = xdata_offset + field_off;
					reloc.symbol_table_index = text_symbol->get_index();
					reloc.type = IMAGE_REL_AMD64_ADDR32NB;
					xdata_sec->add_relocation_entry(&reloc);
				}
			}
		}

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
		add_pdata_relocations(pdata_offset, mangled_name, xdata_offset);

		// Canonical catch funclet emission for C++ EH.
		// Emit dedicated UNWIND_INFO + PDATA entries for each concrete catch funclet range.
		if (is_cpp) {
			auto resolve_funclet_start = [](const CatchHandlerInfo& handler) -> uint32_t {
				return handler.funclet_entry_offset != 0 ? handler.funclet_entry_offset : handler.handler_offset;
			};

			auto resolve_funclet_end = [function_size, &resolve_funclet_start](
				const CatchHandlerInfo& handler,
				const CatchHandlerInfo* next_handler) -> uint32_t {
				uint32_t start = resolve_funclet_start(handler);
				uint32_t end = handler.funclet_end_offset != 0 ? handler.funclet_end_offset : handler.handler_end_offset;
				if (end == 0 && next_handler != nullptr) {
					end = resolve_funclet_start(*next_handler);
				}
				if (end == 0 || end > function_size) {
					end = function_size;
				}
				if (end <= start) {
					return start;
				}
				return end;
			};

			for (const auto& tb : try_blocks) {
				for (size_t i = 0; i < tb.catch_handlers.size(); ++i) {
					const auto& handler = tb.catch_handlers[i];
					const CatchHandlerInfo* next_handler = (i + 1 < tb.catch_handlers.size()) ? &tb.catch_handlers[i + 1] : nullptr;

					uint32_t handler_start_rel = resolve_funclet_start(handler);
					uint32_t handler_end_rel = resolve_funclet_end(handler, next_handler);

					if (handler_end_rel <= handler_start_rel || handler_end_rel > function_size) {
						continue;
					}

					// Catch funclet UNWIND_INFO uses FH3 and the same FuncInfo blob as the parent.
					std::vector<char> catch_xdata = {
						static_cast<char>(0x01 | (0x03 << 3)), // Version=1, EHANDLER|UHANDLER
						static_cast<char>(0x00),
						static_cast<char>(0x00),
						static_cast<char>(0x00),
						static_cast<char>(0x00), static_cast<char>(0x00), static_cast<char>(0x00), static_cast<char>(0x00), // handler RVA
						static_cast<char>(0x00), static_cast<char>(0x00), static_cast<char>(0x00), static_cast<char>(0x00)  // FuncInfo RVA
					};

					auto xdata_section_curr = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
					uint32_t catch_xdata_offset = static_cast<uint32_t>(xdata_section_curr->get_data_size());
					add_data(catch_xdata, SectionType::XDATA);
					add_xdata_relocation(catch_xdata_offset + 4, "__CxxFrameHandler3");
					add_xdata_relocation(catch_xdata_offset + 8, cppx_sym_name);

					auto pdata_section_curr = coffi_.get_sections()[sectiontype_to_index[SectionType::PDATA]];
					uint32_t catch_pdata_offset = static_cast<uint32_t>(pdata_section_curr->get_data_size());

					std::vector<char> catch_pdata(12);
					*reinterpret_cast<uint32_t*>(&catch_pdata[0]) = function_start + handler_start_rel;
					*reinterpret_cast<uint32_t*>(&catch_pdata[4]) = function_start + handler_end_rel;
					*reinterpret_cast<uint32_t*>(&catch_pdata[8]) = catch_xdata_offset;
					add_data(catch_pdata, SectionType::PDATA);

					add_pdata_relocations(catch_pdata_offset, mangled_name, catch_xdata_offset);
				}
			}
		}
	}

	void finalize_debug_info() {
		if (g_enable_debug_output) std::cerr << "finalize_debug_info: Generating debug information..." << std::endl;
		// Exception info is now handled directly in IRConverter finalization logic

		// Finalize the current function before generating debug sections
		debug_builder_.finalizeCurrentFunction();

		// Set the correct text section number for symbol references
		uint16_t text_section_number = static_cast<uint16_t>(sectiontype_to_index[SectionType::TEXT] + 1);
		debug_builder_.setTextSectionNumber(text_section_number);
		if (g_enable_debug_output) std::cerr << "DEBUG: Set text section number to " << text_section_number << "\n";

		// Generate debug sections
		auto debug_s_data = debug_builder_.generateDebugS();
		auto debug_t_data = debug_builder_.generateDebugT();

		// Add debug relocations
		const auto& debug_relocations = debug_builder_.getDebugRelocations();
		for (const auto& reloc : debug_relocations) {
			add_debug_relocation(reloc.offset, reloc.symbol_name, reloc.relocation_type);
		}
		if (g_enable_debug_output) std::cerr << "DEBUG: Added " << debug_relocations.size() << " debug relocations\n";

		// Add debug data to sections
		if (!debug_s_data.empty()) {
			add_data(std::vector<char>(debug_s_data.begin(), debug_s_data.end()), SectionType::DEBUG_S);
			if (g_enable_debug_output) std::cerr << "Added " << debug_s_data.size() << " bytes of .debug$S data" << std::endl;
		}
		if (!debug_t_data.empty()) {
			add_data(std::vector<char>(debug_t_data.begin(), debug_t_data.end()), SectionType::DEBUG_T);
			if (g_enable_debug_output) std::cerr << "Added " << debug_t_data.size() << " bytes of .debug$T data" << std::endl;
		}
	}

	// Add a string literal to the .rdata section and return its symbol name
	std::string_view add_string_literal(std::string_view str_content) {
		// Generate a unique symbol name for this string literal
		std::string_view symbol_name = StringBuilder().append(".str."sv).append(string_literal_counter_++).commit();

		// Get current offset in .rdata section
		auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];
		uint32_t offset = rdata_section->get_data_size();

		// Process the string: remove quotes and handle escape sequences
		// Reuse buffer and clear it (capacity is retained)
		string_literal_buffer_.clear();
		string_literal_buffer_.reserve(str_content.size() + 1);
		
		if (str_content.size() >= 2 && str_content.front() == '"' && str_content.back() == '"') {
			// Remove quotes
			std::string_view content(str_content.data() + 1, str_content.size() - 2);

			// Process escape sequences
			for (size_t i = 0; i < content.size(); ++i) {
				if (content[i] == '\\' && i + 1 < content.size()) {
					switch (content[i + 1]) {
						case 'n': string_literal_buffer_ += '\n'; ++i; break;
						case 't': string_literal_buffer_ += '\t'; ++i; break;
						case 'r': string_literal_buffer_ += '\r'; ++i; break;
						case '\\': string_literal_buffer_ += '\\'; ++i; break;
						case '"': string_literal_buffer_ += '"'; ++i; break;
						case '0': string_literal_buffer_ += '\0'; ++i; break;
						default: string_literal_buffer_ += content[i]; break;
					}
				} else {
					string_literal_buffer_ += content[i];
				}
			}
		} else {
			// Copy the raw string content
			string_literal_buffer_.append(str_content);
		}

		// Add null terminator
		string_literal_buffer_ += '\0';

		// Add the string data to .rdata section (span constructed from string)
		add_data(std::span(string_literal_buffer_), SectionType::RDATA);

		// Add a symbol for this string literal
		auto symbol = coffi_.add_symbol(symbol_name);
		symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol->set_section_number(rdata_section->get_index() + 1);
		symbol->set_value(offset);

		if (g_enable_debug_output) std::cerr << "Added string literal '" << string_literal_buffer_.substr(0, string_literal_buffer_.size() - 1)
		          << "' at offset " << offset << " with symbol " << symbol_name << std::endl;

		return symbol_name;
	}

	// Add a global variable with raw initialization data
	void add_global_variable_data(std::string_view var_name, size_t size_in_bytes, 
	                              bool is_initialized, std::span<const char> init_data) {
		SectionType section_type = is_initialized ? SectionType::DATA : SectionType::BSS;
		auto section = coffi_.get_sections()[sectiontype_to_index[section_type]];
		uint32_t offset = static_cast<uint32_t>(section->get_data_size());

		if (g_enable_debug_output) std::cerr << "DEBUG: add_global_variable_data - var_name=" << var_name 
			<< " size=" << size_in_bytes << " is_initialized=" << is_initialized << "\n";

		if (is_initialized && !init_data.empty()) {
			// Add initialized data to .data section
			add_data(init_data, SectionType::DATA);
		} else {
			// For .bss or uninitialized, use zero-filled data
			std::vector<char> zero_data(size_in_bytes, 0);
			add_data(zero_data, is_initialized ? SectionType::DATA : SectionType::BSS);
		}

		// Add a symbol for this global variable
		auto symbol = coffi_.add_symbol(var_name);
		symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);  // Global variables are external
		symbol->set_section_number(section->get_index() + 1);
		symbol->set_value(offset);

		if (g_enable_debug_output) std::cerr << "Added global variable '" << var_name << "' at offset " << offset
		          << " in " << (is_initialized ? ".data" : ".bss") << " section (size: " << size_in_bytes << " bytes)" << std::endl;
	}

	// Add a vtable to .rdata section with RTTI support
	// vtable_symbol: mangled vtable symbol name (e.g., "??_7Base@@6B@")
	// function_symbols: span of mangled function names in vtable order
	// class_name: name of the class for RTTI
	// base_class_names: span of base class names for RTTI (legacy)
	// base_class_info: detailed base class information for proper RTTI
	void add_vtable(std::string_view vtable_symbol, std::span<const std::string_view> function_symbols,
	                std::string_view class_name, std::span<const std::string_view> base_class_names,
	                std::span<const BaseClassDescriptorInfo> base_class_info,
	                [[maybe_unused]] const RTTITypeInfo* rtti_info = nullptr) {
		auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];
		
		if (g_enable_debug_output) std::cerr << "DEBUG: add_vtable - vtable_symbol=" << vtable_symbol 
		          << " class=" << class_name
		          << " with " << function_symbols.size() << " entries"
		          << " and " << base_class_names.size() << " base classes" << std::endl;

		// Step 1: Emit MSVC RTTI data structures for this class
		// MSVC uses a multi-component RTTI format:
		//   ??_R0 - Type Descriptor
		//   ??_R1 - Base Class Descriptor(s)
		//   ??_R2 - Base Class Array
		//   ??_R3 - Class Hierarchy Descriptor
		//   ??_R4 - Complete Object Locator
		
		// MSVC class name mangling: .?AV<name>@@
		// Note: This is a simplified mangling for classes. Full MSVC mangling would handle
		// templates, namespaces, and other complex types. For basic classes, this format works.
		std::string mangled_class_name = std::string(".?AV") + std::string(class_name) + "@@";
		
		// ??_R0 - Type Descriptor (16 bytes header + mangled name)
		uint32_t type_desc_offset = static_cast<uint32_t>(rdata_section->get_data_size());
		std::string type_desc_symbol = "??_R0" + mangled_class_name;
		
		std::vector<char> type_desc_data;
		// vtable pointer (8 bytes) - null
		for (int i = 0; i < 8; ++i) type_desc_data.push_back(0);
		// spare pointer (8 bytes) - null
		for (int i = 0; i < 8; ++i) type_desc_data.push_back(0);
		// mangled name (null-terminated)
		for (char c : mangled_class_name) type_desc_data.push_back(c);
		type_desc_data.push_back(0);
		
		add_data(type_desc_data, SectionType::RDATA);
		auto type_desc_sym = coffi_.add_symbol(type_desc_symbol);
		type_desc_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		type_desc_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		type_desc_sym->set_section_number(rdata_section->get_index() + 1);
		type_desc_sym->set_value(type_desc_offset);
		uint32_t type_desc_symbol_index = type_desc_sym->get_index();
		
		if (g_enable_debug_output) std::cerr << "  Added ??_R0 Type Descriptor '" << type_desc_symbol << "' at offset " 
		          << type_desc_offset << std::endl;
		
		// ??_R1 - Base Class Descriptors (one for self + one per base)
		std::vector<uint32_t> bcd_offsets;
		std::vector<uint32_t> bcd_symbol_indices;
		
		// Self descriptor
		uint32_t self_bcd_offset = static_cast<uint32_t>(rdata_section->get_data_size());
		std::string self_bcd_symbol = "??_R1" + mangled_class_name + "8";  // "8" suffix for self
		std::vector<char> self_bcd_data;
		
		// type_descriptor pointer (8 bytes) - will add relocation
		for (int i = 0; i < 8; ++i) self_bcd_data.push_back(0);
		// num_contained_bases (4 bytes)
		uint32_t num_contained = static_cast<uint32_t>(base_class_names.size());
		for (int i = 0; i < 4; ++i) self_bcd_data.push_back((num_contained >> (i * 8)) & 0xFF);
		// mdisp (4 bytes) - 0 for self
		for (int i = 0; i < 4; ++i) self_bcd_data.push_back(0);
		// pdisp (4 bytes) - -1 for non-virtual
		for (int i = 0; i < 4; ++i) self_bcd_data.push_back(0xFF);
		// vdisp (4 bytes) - 0
		for (int i = 0; i < 4; ++i) self_bcd_data.push_back(0);
		// attributes (4 bytes) - 0 for self
		for (int i = 0; i < 4; ++i) self_bcd_data.push_back(0);
		
		add_data(self_bcd_data, SectionType::RDATA);
		auto self_bcd_sym = coffi_.add_symbol(self_bcd_symbol);
		self_bcd_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		self_bcd_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		self_bcd_sym->set_section_number(rdata_section->get_index() + 1);
		self_bcd_sym->set_value(self_bcd_offset);
		
		// Add relocation for type_descriptor pointer in self BCD
		COFFI::rel_entry_generic self_bcd_reloc;
		self_bcd_reloc.virtual_address = self_bcd_offset;
		self_bcd_reloc.symbol_table_index = type_desc_symbol_index;
		self_bcd_reloc.type = IMAGE_REL_AMD64_ADDR64;
		rdata_section->add_relocation_entry(&self_bcd_reloc);
		
		bcd_offsets.push_back(self_bcd_offset);
		bcd_symbol_indices.push_back(self_bcd_sym->get_index());
		
		if (g_enable_debug_output) std::cerr << "  Added ??_R1 self BCD '" << self_bcd_symbol << "' at offset " 
		          << self_bcd_offset << std::endl;
		
		// Base class descriptors
		for (size_t i = 0; i < base_class_info.size(); ++i) {
			const auto& bci = base_class_info[i];
			std::string base_mangled = ".?AV" + bci.name + "@@";
			std::string base_type_desc_symbol = "??_R0" + base_mangled;
			
			uint32_t base_bcd_offset = static_cast<uint32_t>(rdata_section->get_data_size());
			std::string base_bcd_symbol = "??_R1" + mangled_class_name + "0" + base_mangled;
			std::vector<char> base_bcd_data;
			
			// type_descriptor pointer (8 bytes) - will add relocation
			for (int j = 0; j < 8; ++j) base_bcd_data.push_back(0);
			
			// num_contained_bases (4 bytes) - actual value from base class info
			uint32_t base_num_contained = bci.num_contained_bases;
			for (int j = 0; j < 4; ++j) base_bcd_data.push_back((base_num_contained >> (j * 8)) & 0xFF);
			
			// mdisp (4 bytes) - offset of base in derived class
			uint32_t mdisp = bci.offset;
			for (int j = 0; j < 4; ++j) base_bcd_data.push_back((mdisp >> (j * 8)) & 0xFF);
			
			// pdisp (4 bytes) - vbtable displacement
			// -1 for non-virtual bases (not applicable)
			// 0+ for virtual bases (offset into vbtable)
			int32_t pdisp = bci.is_virtual ? 0 : -1;
			for (int j = 0; j < 4; ++j) base_bcd_data.push_back((pdisp >> (j * 8)) & 0xFF);
			
			// vdisp (4 bytes) - displacement inside vbtable (0 for simplicity)
			for (int j = 0; j < 4; ++j) base_bcd_data.push_back(0);
			
			// attributes (4 bytes) - flags
			// Bit 0: virtual base (1 if virtual, 0 if non-virtual)
			uint32_t attributes = bci.is_virtual ? 1 : 0;
			for (int j = 0; j < 4; ++j) base_bcd_data.push_back((attributes >> (j * 8)) & 0xFF);
			
			add_data(base_bcd_data, SectionType::RDATA);
			auto base_bcd_sym = coffi_.add_symbol(base_bcd_symbol);
			base_bcd_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			base_bcd_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			base_bcd_sym->set_section_number(rdata_section->get_index() + 1);
			base_bcd_sym->set_value(base_bcd_offset);
			
			// Add relocation for type_descriptor pointer in base BCD
			uint32_t base_type_desc_index = get_or_create_symbol_index(base_type_desc_symbol);
			COFFI::rel_entry_generic base_bcd_reloc;
			base_bcd_reloc.virtual_address = base_bcd_offset;
			base_bcd_reloc.symbol_table_index = base_type_desc_index;
			base_bcd_reloc.type = IMAGE_REL_AMD64_ADDR64;
			rdata_section->add_relocation_entry(&base_bcd_reloc);
			
			bcd_offsets.push_back(base_bcd_offset);
			bcd_symbol_indices.push_back(base_bcd_sym->get_index());
			
			if (g_enable_debug_output) std::cerr << "  Added ??_R1 base BCD for " << bci.name << std::endl;
		}
		
		// ??_R2 - Base Class Array (pointers to all BCDs)
		uint32_t bca_offset = static_cast<uint32_t>(rdata_section->get_data_size());
		std::string bca_symbol = "??_R2" + mangled_class_name + "8";
		std::vector<char> bca_data;
		
		// Array of pointers to BCDs
		for (size_t i = 0; i < bcd_offsets.size(); ++i) {
			for (int j = 0; j < 8; ++j) bca_data.push_back(0);
		}
		
		add_data(bca_data, SectionType::RDATA);
		auto bca_sym = coffi_.add_symbol(bca_symbol);
		bca_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		bca_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		bca_sym->set_section_number(rdata_section->get_index() + 1);
		bca_sym->set_value(bca_offset);
		uint32_t bca_symbol_index = bca_sym->get_index();
		
		// Add relocations for BCD pointers in BCA
		for (size_t i = 0; i < bcd_offsets.size(); ++i) {
			COFFI::rel_entry_generic bca_reloc;
			bca_reloc.virtual_address = bca_offset + static_cast<uint32_t>(i * 8);
			bca_reloc.symbol_table_index = bcd_symbol_indices[i];
			bca_reloc.type = IMAGE_REL_AMD64_ADDR64;
			rdata_section->add_relocation_entry(&bca_reloc);
		}
		
		if (g_enable_debug_output) std::cerr << "  Added ??_R2 Base Class Array '" << bca_symbol << "' at offset " 
		          << bca_offset << std::endl;
		
		// ??_R3 - Class Hierarchy Descriptor
		uint32_t chd_offset = static_cast<uint32_t>(rdata_section->get_data_size());
		std::string chd_symbol = "??_R3" + mangled_class_name + "8";
		std::vector<char> chd_data;
		
		// signature (4 bytes) - 0
		for (int i = 0; i < 4; ++i) chd_data.push_back(0);
		// attributes (4 bytes) - 0 (can be extended for multiple/virtual inheritance)
		for (int i = 0; i < 4; ++i) chd_data.push_back(0);
		// num_base_classes (4 bytes) - total including self
		uint32_t total_bases = static_cast<uint32_t>(bcd_offsets.size());
		for (int i = 0; i < 4; ++i) chd_data.push_back((total_bases >> (i * 8)) & 0xFF);
		// base_class_array pointer (8 bytes) - will add relocation
		for (int i = 0; i < 8; ++i) chd_data.push_back(0);
		
		add_data(chd_data, SectionType::RDATA);
		auto chd_sym = coffi_.add_symbol(chd_symbol);
		chd_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		chd_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		chd_sym->set_section_number(rdata_section->get_index() + 1);
		chd_sym->set_value(chd_offset);
		uint32_t chd_symbol_index = chd_sym->get_index();
		
		// Add relocation for base_class_array pointer in CHD
		COFFI::rel_entry_generic chd_reloc;
		chd_reloc.virtual_address = chd_offset + 12;  // After signature + attributes + num_base_classes
		chd_reloc.symbol_table_index = bca_symbol_index;
		chd_reloc.type = IMAGE_REL_AMD64_ADDR64;
		rdata_section->add_relocation_entry(&chd_reloc);
		
		if (g_enable_debug_output) std::cerr << "  Added ??_R3 Class Hierarchy Descriptor '" << chd_symbol << "' at offset " 
		          << chd_offset << std::endl;
		
		// ??_R4 - Complete Object Locator
		uint32_t col_offset = static_cast<uint32_t>(rdata_section->get_data_size());
		std::string col_symbol = "??_R4" + mangled_class_name + "6B@";  // "6B@" suffix for COL
		std::vector<char> col_data;
		
		// signature (4 bytes) - 1 for 64-bit
		col_data.push_back(1);
		for (int i = 1; i < 4; ++i) col_data.push_back(0);
		// offset (4 bytes) - 0 for primary vtable
		for (int i = 0; i < 4; ++i) col_data.push_back(0);
		// cd_offset (4 bytes) - 0
		for (int i = 0; i < 4; ++i) col_data.push_back(0);
		// type_descriptor pointer (8 bytes) - relocation added at offset+12
		for (int i = 0; i < 8; ++i) col_data.push_back(0);
		// hierarchy pointer (8 bytes) - relocation added at offset+20
		for (int i = 0; i < 8; ++i) col_data.push_back(0);
		
		add_data(col_data, SectionType::RDATA);
		auto col_sym = coffi_.add_symbol(col_symbol);
		col_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		col_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		col_sym->set_section_number(rdata_section->get_index() + 1);
		col_sym->set_value(col_offset);
		uint32_t col_symbol_index = col_sym->get_index();
		
		// Add relocations for type_descriptor and hierarchy pointers in COL
		COFFI::rel_entry_generic col_type_reloc;
		col_type_reloc.virtual_address = col_offset + 12;  // After signature + offset + cd_offset
		col_type_reloc.symbol_table_index = type_desc_symbol_index;
		col_type_reloc.type = IMAGE_REL_AMD64_ADDR64;
		rdata_section->add_relocation_entry(&col_type_reloc);
		
		COFFI::rel_entry_generic col_hier_reloc;
		col_hier_reloc.virtual_address = col_offset + 20;  // After type_descriptor pointer
		col_hier_reloc.symbol_table_index = chd_symbol_index;
		col_hier_reloc.type = IMAGE_REL_AMD64_ADDR64;
		rdata_section->add_relocation_entry(&col_hier_reloc);
		
		if (g_enable_debug_output) std::cerr << "  Added ??_R4 Complete Object Locator '" << col_symbol << "' at offset " 
		          << col_offset << std::endl;
		
		// Step 2: Emit vtable structure
		// Layout: [COL pointer (8 bytes), function pointers...]
		uint32_t vtable_offset = static_cast<uint32_t>(rdata_section->get_data_size());
		
		// Add COL pointer + function pointers
		size_t vtable_size = (1 + function_symbols.size()) * 8;  // 1 COL ptr + N function ptrs
		std::vector<char> vtable_data(vtable_size, 0);
		
		// Add the vtable data to .rdata section
		add_data(vtable_data, SectionType::RDATA);
		
		// Add relocation for COL (Complete Object Locator) pointer at vtable[0] (before actual vtable)
		{
			uint32_t col_reloc_offset = vtable_offset;
			
			if (g_enable_debug_output) std::cerr << "  DEBUG: Creating COL relocation at offset " << col_reloc_offset 
			          << " pointing to symbol '" << col_symbol << "' (file index " << col_symbol_index << ")" << std::endl;
			
			COFFI::rel_entry_generic relocation;
			relocation.virtual_address = col_reloc_offset;
			relocation.symbol_table_index = col_symbol_index;
			relocation.type = IMAGE_REL_AMD64_ADDR64;
			
			rdata_section->add_relocation_entry(&relocation);
			
			if (g_enable_debug_output) std::cerr << "  Added COL pointer relocation at vtable[-1]" << std::endl;
		}
		
		// Step 3: Add a symbol for vtable (points to first virtual function, AFTER RTTI pointer)
		uint32_t vtable_symbol_offset = vtable_offset + 8;  // Skip RTTI pointer
		auto symbol = coffi_.add_symbol(std::string(vtable_symbol));
		symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);  // Vtables are external
		symbol->set_section_number(rdata_section->get_index() + 1);
		symbol->set_value(vtable_symbol_offset);
		
		// Add relocations for each function pointer in the vtable
		for (size_t i = 0; i < function_symbols.size(); ++i) {
			if (function_symbols[i].empty()) {
				// Skip empty entries (pure virtual functions might be empty initially)
				continue;
			}
			
			uint32_t reloc_offset = vtable_offset + 8 + static_cast<uint32_t>(i * 8);  // +8 to skip RTTI ptr
			
			// Get the symbol index (COFFI handles aux entries automatically)
			uint32_t func_symbol_index = get_or_create_symbol_index(std::string(function_symbols[i]));
			
			COFFI::rel_entry_generic relocation;
			relocation.virtual_address = reloc_offset;
			relocation.symbol_table_index = func_symbol_index;
			relocation.type = IMAGE_REL_AMD64_ADDR64;  // 64-bit absolute address
			
			rdata_section->add_relocation_entry(&relocation);
			
			if (g_enable_debug_output) std::cerr << "  Added relocation for vtable[" << i << "] -> " << function_symbols[i] 
			          << " at offset " << reloc_offset << " (file index " << func_symbol_index << ")" << std::endl;
		}

		if (g_enable_debug_output) std::cerr << "Added vtable '" << vtable_symbol << "' at offset " << vtable_symbol_offset
		          << " in .rdata section (total size with RTTI: " << vtable_size << " bytes)" << std::endl;
	}

	// Get or create MSVC _ThrowInfo metadata symbol for a built-in thrown type.
	// Current implementation provides concrete metadata for int (Type::Int), which
	// is enough to make basic throw/catch(int) and noexcept(int throw) flows work.
	//
	// Emitted layout mirrors MSVC x64 objects:
	//   _TI1H            (ThrowInfo, 0x1C bytes)
	//   _CTA1H           (CatchableTypeArray, 0x0C bytes)
	//   _CT??_R0H@84     (CatchableType, 0x24 bytes)
	//   ??_R0H@8         (RTTI Type Descriptor, created on-demand if missing)
	std::string get_or_create_builtin_throwinfo(Type type) {
		if (type != Type::Int) {
			return std::string();
		}

		const std::string throw_info_symbol = "_TI1H";
		auto* existing_throw_info = coffi_.get_symbol(throw_info_symbol);
		if (existing_throw_info) {
			return throw_info_symbol;
		}

		auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];

		// Ensure RTTI type descriptor for int exists: ??_R0H@8
		const std::string type_desc_symbol_name = "??_R0H@8";
		auto* type_desc_symbol = coffi_.get_symbol(type_desc_symbol_name);
		if (!type_desc_symbol) {
			uint32_t type_desc_offset = static_cast<uint32_t>(rdata_section->get_data_size());

			std::vector<char> type_desc_data;
			// vftable pointer (8 bytes) - relocated to type_info vftable
			type_desc_data.resize(16, 0);
			// Mangled built-in type name for int
			type_desc_data.push_back('.');
			type_desc_data.push_back('H');
			type_desc_data.push_back(0);

			add_data(type_desc_data, SectionType::RDATA);

			type_desc_symbol = coffi_.add_symbol(type_desc_symbol_name);
			type_desc_symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			type_desc_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			type_desc_symbol->set_section_number(rdata_section->get_index() + 1);
			type_desc_symbol->set_value(type_desc_offset);

			// Relocate vftable pointer to type_info::vftable
			auto* type_info_vftable = coffi_.get_symbol("??_7type_info@@6B@");
			if (!type_info_vftable) {
				type_info_vftable = coffi_.add_symbol("??_7type_info@@6B@");
				type_info_vftable->set_value(0);
				type_info_vftable->set_section_number(0);
				type_info_vftable->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
				type_info_vftable->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			}

			COFFI::rel_entry_generic td_vft_reloc;
			td_vft_reloc.virtual_address = type_desc_offset;
			td_vft_reloc.symbol_table_index = type_info_vftable->get_index();
			td_vft_reloc.type = IMAGE_REL_AMD64_ADDR64;
			rdata_section->add_relocation_entry(&td_vft_reloc);
		}

		// Emit CatchableType: _CT??_R0H@84 (0x24 bytes)
		const std::string catchable_type_symbol_name = "_CT??_R0H@84";
		auto* catchable_type_symbol = coffi_.get_symbol(catchable_type_symbol_name);
		if (!catchable_type_symbol) {
			uint32_t ct_offset = static_cast<uint32_t>(rdata_section->get_data_size());
			std::vector<char> ct_data(0x24, 0);
			// properties = 1 (simple by-value scalar)
			ct_data[0] = 0x01;
			// thisDisplacement.pdisp = -1
			ct_data[0x0C] = static_cast<char>(0xFF);
			ct_data[0x0D] = static_cast<char>(0xFF);
			ct_data[0x0E] = static_cast<char>(0xFF);
			ct_data[0x0F] = static_cast<char>(0xFF);
			// sizeOrOffset = 4 (sizeof(int))
			ct_data[0x14] = 0x04;

			add_data(ct_data, SectionType::RDATA);

			catchable_type_symbol = coffi_.add_symbol(catchable_type_symbol_name);
			catchable_type_symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			catchable_type_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			catchable_type_symbol->set_section_number(rdata_section->get_index() + 1);
			catchable_type_symbol->set_value(ct_offset);

			// pType -> ??_R0H@8 (image-relative)
			COFFI::rel_entry_generic ct_type_reloc;
			ct_type_reloc.virtual_address = ct_offset + 0x04;
			ct_type_reloc.symbol_table_index = type_desc_symbol->get_index();
			ct_type_reloc.type = IMAGE_REL_AMD64_ADDR32NB;
			rdata_section->add_relocation_entry(&ct_type_reloc);
		}

		// Emit CatchableTypeArray: _CTA1H (0x0C bytes)
		const std::string cta_symbol_name = "_CTA1H";
		auto* cta_symbol = coffi_.get_symbol(cta_symbol_name);
		if (!cta_symbol) {
			uint32_t cta_offset = static_cast<uint32_t>(rdata_section->get_data_size());
			std::vector<char> cta_data(0x0C, 0);
			// nCatchableTypes = 1
			cta_data[0] = 0x01;
			add_data(cta_data, SectionType::RDATA);

			cta_symbol = coffi_.add_symbol(cta_symbol_name);
			cta_symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			cta_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			cta_symbol->set_section_number(rdata_section->get_index() + 1);
			cta_symbol->set_value(cta_offset);

			COFFI::rel_entry_generic cta_reloc;
			cta_reloc.virtual_address = cta_offset + 0x04;
			cta_reloc.symbol_table_index = catchable_type_symbol->get_index();
			cta_reloc.type = IMAGE_REL_AMD64_ADDR32NB;
			rdata_section->add_relocation_entry(&cta_reloc);
		}

		// Emit ThrowInfo: _TI1H (0x1C bytes), with pCatchableTypeArray at +0x0C
		uint32_t ti_offset = static_cast<uint32_t>(rdata_section->get_data_size());
		std::vector<char> ti_data(0x1C, 0);
		add_data(ti_data, SectionType::RDATA);

		auto* ti_symbol = coffi_.add_symbol(throw_info_symbol);
		ti_symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		ti_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		ti_symbol->set_section_number(rdata_section->get_index() + 1);
		ti_symbol->set_value(ti_offset);

		COFFI::rel_entry_generic ti_reloc;
		ti_reloc.virtual_address = ti_offset + 0x0C;
		ti_reloc.symbol_table_index = cta_symbol->get_index();
		ti_reloc.type = IMAGE_REL_AMD64_ADDR32NB;
		rdata_section->add_relocation_entry(&ti_reloc);

		if (g_enable_debug_output) std::cerr << "Created builtin throw metadata symbol: " << throw_info_symbol << std::endl;
		return throw_info_symbol;
	}

	// Helper: get or create symbol index for a function name
	uint32_t get_or_create_symbol_index(const std::string& symbol_name) {
		// First, check if symbol already exists
		auto symbols = coffi_.get_symbols();
		for (size_t i = 0; i < symbols->size(); ++i) {
			if ((*symbols)[i].get_name() == symbol_name) {
				if (g_enable_debug_output) std::cerr << "    DEBUG get_or_create_symbol_index: Found existing symbol '" << symbol_name 
				          << "' at array index " << i << ", file index " << (*symbols)[i].get_index() << std::endl;
				return (*symbols)[i].get_index();
			}
		}
		
		// Symbol doesn't exist, create it as an external reference
		if (g_enable_debug_output) std::cerr << "    DEBUG get_or_create_symbol_index: Creating new symbol '" << symbol_name << "'" << std::endl;
		auto symbol = coffi_.add_symbol(symbol_name);
		symbol->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol->set_section_number(0);  // External reference
		symbol->set_value(0);
		
		// Return the index from COFFI (which includes aux entries)
		uint32_t file_index = symbol->get_index();
		if (g_enable_debug_output) std::cerr << "    DEBUG get_or_create_symbol_index: Created new symbol at file index " << file_index 
		          << " for '" << symbol_name << "'" << std::endl;
		return file_index;
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

	// Track type descriptors that have been created to avoid duplicates across functions
	// Maps type name to its offset in .rdata section
	std::unordered_map<std::string, uint32_t> type_descriptor_offsets_;

	// Track generated throw-info symbols by type name
	std::unordered_map<std::string, std::string> throw_info_symbols_;

	// Counter for generating unique string literal symbols
	uint64_t string_literal_counter_ = 0;
	
	// Thread-local reusable buffer for string literal processing to avoid repeated allocations
	inline static thread_local std::string string_literal_buffer_;
};
