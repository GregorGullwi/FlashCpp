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
#include <algorithm>
#include <numeric>
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

	// Helper: Add a COFF section auxiliary symbol (format 5) to a symbol
	static void addSectionAuxSymbol(COFFI::symbol* sym, COFFI::section* section) {
		COFFI::auxiliary_symbol_record_5 aux = {};
		aux.length = 0;
		aux.number_of_relocations = 0;
		aux.number_of_linenumbers = 0;
		aux.check_sum = 0;
		aux.number = static_cast<uint16_t>(section->get_index() + 1);
		aux.selection = 0;
		COFFI::auxiliary_symbol_record aux_record;
		std::memcpy(aux_record.value, &aux, sizeof(aux));
		sym->get_auxiliary_symbols().push_back(aux_record);
	}

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
		addSectionAuxSymbol(symbol_text_main, section_text);

		auto section_drectve = add_section(".drectve", IMAGE_SCN_ALIGN_1BYTES | IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE, SectionType::DRECTVE);
		section_drectve->append_data(" /DEFAULTLIB:\"LIBCMT\" /DEFAULTLIB:\"OLDNAMES\""); // MSVC also contains '/DEFAULTLIB:\"OLDNAMES\" ', but it doesn't seem to be needed?
		auto symbol_drectve = coffi_.add_symbol(".drectve");
		symbol_drectve->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_drectve->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_drectve->set_section_number(section_drectve->get_index() + 1);
		symbol_drectve->set_value(0);

		// Add auxiliary symbol for .drectve section
		addSectionAuxSymbol(symbol_drectve, section_drectve);

		// Add .data section
		auto section_data = add_section(".data", IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_8BYTES, SectionType::DATA);
		auto symbol_data = coffi_.add_symbol(".data");
		symbol_data->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_data->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_data->set_section_number(section_data->get_index() + 1);
		symbol_data->set_value(0);

		// Add auxiliary symbol for .data section
		addSectionAuxSymbol(symbol_data, section_data);

		// Add .bss section
		auto section_bss = add_section(".bss", IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_ALIGN_8BYTES, SectionType::BSS);
		auto symbol_bss = coffi_.add_symbol(".bss");
		symbol_bss->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_bss->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_bss->set_section_number(section_bss->get_index() + 1);
		symbol_bss->set_value(0);

		// Add auxiliary symbol for .bss section
		addSectionAuxSymbol(symbol_bss, section_bss);

		// Add .rdata section (read-only data for string literals and constants)
		auto section_rdata = add_section(".rdata", IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_16BYTES, SectionType::RDATA);
		auto symbol_rdata = coffi_.add_symbol(".rdata");
		symbol_rdata->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_rdata->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_rdata->set_section_number(section_rdata->get_index() + 1);
		symbol_rdata->set_value(0);

		// Add auxiliary symbol for .rdata section
		addSectionAuxSymbol(symbol_rdata, section_rdata);

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
		addSectionAuxSymbol(symbol_debug_s, section_debug_s);

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
		addSectionAuxSymbol(symbol_debug_t, section_debug_t);

		// Add .xdata section (exception handling data)
		auto section_xdata = add_section(".xdata", IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES, SectionType::XDATA);
		auto symbol_xdata = coffi_.add_symbol(".xdata");
		symbol_xdata->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_xdata->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_xdata->set_section_number(section_xdata->get_index() + 1);
		symbol_xdata->set_value(0);

		// Add auxiliary symbol for .xdata section
		addSectionAuxSymbol(symbol_xdata, section_xdata);

		// Add .pdata section (procedure data for exception handling)
		auto section_pdata = add_section(".pdata", IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES, SectionType::PDATA);
		auto symbol_pdata = coffi_.add_symbol(".pdata");
		symbol_pdata->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_pdata->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_pdata->set_section_number(section_pdata->get_index() + 1);
		symbol_pdata->set_value(0);

		// Add auxiliary symbol for .pdata section
		addSectionAuxSymbol(symbol_pdata, section_pdata);

		// Add .llvm_addrsig section (LLVM address significance table)
		auto section_llvm_addrsig = add_section(".llvm_addrsig", IMAGE_SCN_LNK_REMOVE | IMAGE_SCN_ALIGN_1BYTES, SectionType::LLVM_ADDRSIG);
		auto symbol_llvm_addrsig = coffi_.add_symbol(".llvm_addrsig");
		symbol_llvm_addrsig->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol_llvm_addrsig->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol_llvm_addrsig->set_section_number(section_llvm_addrsig->get_index() + 1);
		symbol_llvm_addrsig->set_value(0);

		// Add auxiliary symbol for .llvm_addrsig section
		addSectionAuxSymbol(symbol_llvm_addrsig, section_llvm_addrsig);

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
	mutable std::unordered_map<std::string, ObjectFileWriter::FunctionSignature, ObjectFileCommon::StringViewHash, std::equal_to<>> function_signatures_;

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


#include "ObjFileWriter_Symbols.h"
#include "ObjFileWriter_Debug.h"
#include "ObjFileWriter_EH.h"
#include "ObjFileWriter_RTTI.h"
