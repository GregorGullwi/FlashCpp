#pragma once

#if defined(__clang__)
// Clang compiler
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif

#include "coffi/coffi.hpp"
#include "CodeViewDebug.h"
#include "AstNodeTypes.h"
#include "NameMangling.h"
#include <string>
#include <string_view>
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
	
	enum EFunctionCallingConv : unsigned char
	{
		cc_cdecl,
		cc_stdcall,
		cc_fastcall,
	};
	// Function signature information for mangling
	struct FunctionSignature {
		TypeSpecifierNode return_type;
		std::vector<TypeSpecifierNode> parameter_types;
		bool is_const = false;
		bool is_static = false;
		bool is_variadic = false;  // True if function has ... ellipsis parameter
		EFunctionCallingConv calling_convention = EFunctionCallingConv::cc_cdecl;
		std::string namespace_name;
		std::string class_name;
		Linkage linkage = Linkage::None;  // C vs C++ linkage

		FunctionSignature() = default;
		FunctionSignature(const TypeSpecifierNode& ret_type, std::vector<TypeSpecifierNode> params)
			: return_type(ret_type), parameter_types(std::move(params)) {}
	};

	// Exception handling information for a catch handler
	struct CatchHandlerInfo {
		uint32_t type_index;      // Type to catch (0 for catch-all)
		uint32_t handler_offset;  // Code offset of catch handler relative to function start
		bool is_catch_all;        // True for catch(...)
		std::string type_name;    // Name of the caught type (empty for catch-all or when type_index is 0)
		bool is_const;            // True if caught by const
		bool is_reference;        // True if caught by lvalue reference
		bool is_rvalue_reference; // True if caught by rvalue reference
	};

	// Exception handling information for a try block
	struct TryBlockInfo {
		uint32_t try_start_offset;  // Code offset where try block starts
		uint32_t try_end_offset;    // Code offset where try block ends
		std::vector<CatchHandlerInfo> catch_handlers;
	};

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
			std::cerr << "Number of sections: " << coffi_.get_sections().get_count() << std::endl;
			std::cerr << "Number of symbols: " << coffi_.get_symbols()->size() << std::endl;

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
				std::cerr << "Section " << i << ": '" << section_name << "'"
				         << " size=" << data_size
				         << " flags=0x" << std::hex << section->get_flags() << std::dec
				         << " reloc_count=" << section->get_reloc_count()
				         << " reloc_offset=" << section->get_reloc_offset();
				
				// For .data section (index 2), check data
				if (section_name == ".data") {
					std::cerr << " <<< DATA SECTION";
				}
				// For .pdata section, check relocations  
				if (section_name == ".pdata") {
					std::cerr << " <<< PDATA SECTION";
				}
				std::cerr << std::endl;
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
			std::cerr << "COFFI save returned: " << (success ? "true" : "FALSE") << std::endl;
			
			// Verify the file was written correctly by checking size
			std::ifstream check_file(filename, std::ios::binary | std::ios::ate);
			if (check_file.is_open()) {
				auto file_size = check_file.tellg();
				std::cerr << "Written file size: " << file_size << " bytes\n";
				check_file.close();
			}
			
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
	std::string getMangledName(std::string_view name) const {
		// Check if the name is already a mangled name (starts with '?')
		if (!name.empty() && name[0] == '?') {
			// Already mangled, return as-is
			return std::string(name);
		}

		// Check if this is a member function call (ClassName::FunctionName format)
		// For nested classes, use the LAST :: to split (e.g., "Outer::Inner::get" -> class="Outer::Inner", func="get")
		size_t scope_pos = name.rfind("::");
		if (scope_pos != std::string_view::npos) {
			// Split into class name and function name using string_view (no allocation)
			std::string_view class_name = name.substr(0, scope_pos);
			std::string_view func_name = name.substr(scope_pos + 2);

			// Convert class_name to mangled format for nested classes
			// "Outer::Inner" -> "Inner@Outer" (reverse order with @ separators)
			std::string mangled_class;
			{
				std::vector<std::string_view> class_parts;
				std::string_view remaining = class_name;
				size_t pos;
				
				while ((pos = remaining.find("::")) != std::string_view::npos) {
					class_parts.push_back(remaining.substr(0, pos));
					remaining = remaining.substr(pos + 2);
				}
				class_parts.push_back(remaining);
				
				// Reverse and append with @ separators
				for (auto it = class_parts.rbegin(); it != class_parts.rend(); ++it) {
					if (!mangled_class.empty()) mangled_class += '@';
					mangled_class += *it;
				}
			}

			// Search for a mangled name that matches this member function
			// The function name in the mangled name might include the class prefix (e.g., "Container::Container")
			// So we need to search for both patterns:
			// 1. ?FunctionName@MangledClassName@@... (func_name without class prefix)
			// 2. ?ClassName::FunctionName@MangledClassName@@... (func_name with class prefix, for constructors)
			for (const auto& [mangled, sig] : function_signatures_) {
				if (mangled[0] != '?') continue;
				
				// Pattern 1: ?FunctionName@MangledClassName@@...
				// Check: mangled starts with "?<func_name>@<mangled_class>@@"
				size_t expected_class_start = 1 + func_name.size() + 1;  // '?' + func_name + '@'
				size_t expected_separator = expected_class_start + mangled_class.size();
				if (mangled.size() > expected_separator + 1 &&
				    mangled.substr(1, func_name.size()) == func_name &&
				    mangled[1 + func_name.size()] == '@' &&
				    mangled.substr(expected_class_start, mangled_class.size()) == mangled_class &&
				    mangled.substr(expected_separator, 2) == "@@") {  // Ensure class name ends with @@
					std::cerr << "DEBUG: getMangledName found match (pattern 1) for " << name << " -> " << mangled << "\n";
					return mangled;
				}
				
				// Pattern 2: ?ClassName::FunctionName@MangledClassName@@... (for constructors/destructors)
				std::string full_func_name = std::string(class_name) + "::" + std::string(func_name);
				expected_class_start = 1 + full_func_name.size() + 1;
				expected_separator = expected_class_start + mangled_class.size();
				if (mangled.size() > expected_separator + 1 &&
				    mangled.substr(1, full_func_name.size()) == full_func_name &&
				    mangled[1 + full_func_name.size()] == '@' &&
				    mangled.substr(expected_class_start, mangled_class.size()) == mangled_class &&
				    mangled.substr(expected_separator, 2) == "@@") {  // Ensure class name ends with @@
					std::cerr << "DEBUG: getMangledName found match (pattern 2) for " << name << " -> " << mangled << "\n";
					return mangled;
				}
			}

			// If not found in function_signatures_, generate a basic mangled name
			std::cerr << "DEBUG: getMangledName fallback for " << name << " (func=" << func_name << " class=" << class_name << ")\n";
			std::cerr << "DEBUG: function_signatures_ has " << function_signatures_.size() << " entries:\n";
			for (const auto& [mangled, sig] : function_signatures_) {
				std::cerr << "  " << mangled << "\n";
			}

			// If not found in function_signatures_, generate a basic mangled name
			// This handles forward references to functions that haven't been processed yet
			// Format: ?FunctionName@MangledClassName@@QAH@Z (member function, int return, no params)
			// For constructors (FunctionName == ClassName), use QAX (void return)
			// Q indicates __thiscall (member function with implicit this pointer)
			// This is a simplified mangling - the actual signature will be added later
			// Reserve space to avoid reallocations
			std::string mangled;
			mangled.reserve(1 + func_name.size() + 1 + mangled_class.size() + 7);
			mangled += '?';
			mangled += func_name;
			mangled += '@';
			mangled += mangled_class;
			
			// Check if this is a constructor (function name == innermost class name)
			std::string_view innermost_class = class_name;
			size_t last_colon = class_name.rfind("::");
			if (last_colon != std::string_view::npos) {
				innermost_class = class_name.substr(last_colon + 2);
			}
			if (func_name == innermost_class) {
				mangled += "@@QAX@Z";  // __thiscall void return, no params
			} else {
				mangled += "@@QAH@Z";  // __thiscall int return, no params (default for member functions)
			}
			return mangled;
		}

		// For overloaded functions, we can't determine which overload to use
		// without signature information. The caller should pass the mangled name directly.
		// For now, we'll search for any mangled name that starts with the function name.
		// This is a temporary solution - ideally the IR should include the mangled name.
		for (const auto& [mangled, sig] : function_signatures_) {
			// Check if this mangled name corresponds to the function name
			// Mangled names start with '?' followed by the function name
			if (mangled.size() > name.size() + 1 &&
			    mangled[0] == '?' &&
			    mangled.substr(1, name.size()) == name) {
				// Found a match - return this mangled name
				// Note: This returns the LAST added overload, which should be the current one
				return mangled;
			}
		}

		return std::string(name);  // Default: no mangling
	}

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

		std::cerr << "DEBUG generateMangledName: " << name << " -> " << mangled << "\n";
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
	void addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, Linkage linkage, bool is_variadic, std::string_view mangled_name) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
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
	void addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& parameter_types, std::string_view class_name, Linkage linkage, bool is_variadic, std::string_view mangled_name) {
		FunctionSignature sig(return_type, parameter_types);
		sig.class_name = class_name;
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		function_signatures_[std::string(mangled_name)] = sig;
	}

	void add_function_symbol(std::string_view mangled_name, uint32_t section_offset, uint32_t stack_space, Linkage linkage = Linkage::None) {
		std::cerr << "Adding function symbol: " << mangled_name << " at offset " << section_offset << " with linkage " << static_cast<int>(linkage) << std::endl;
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
			std::cerr << "Adding export directive: " << export_directive << std::endl;
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
		std::cerr << "DEBUG: Adding function to debug builder: " << unmangled_name << " (mangled: " << mangled_name << ") at offset " << section_offset << "\n";
		debug_builder_.addFunction(unmangled_name, std::string(mangled_name), section_offset, 0, stack_space);
		std::cerr << "DEBUG: Function added to debug builder \n";

		// Exception info is now handled directly in IRConverter finalization logic

		std::cerr << "Function symbol added successfully" << std::endl;
	}

	void add_data(const std::vector<char>& data, SectionType section_type) {
		int section_index = sectiontype_to_index[section_type];
		std::cerr << "Adding " << data.size() << " bytes to section " << static_cast<int>(section_type) << " (index=" << section_index << ")";
		auto section = coffi_.get_sections()[section_index];
		uint32_t size_before = section->get_data_size();
		std::cerr << " (current size: " << size_before << ")" << std::endl;
		if (section_type == SectionType::TEXT) {
			std::cerr << "Machine code bytes (" << data.size() << " total): ";
			for (size_t i = 0; i < data.size(); ++i) {
				std::cerr << std::hex << std::setfill('0') << std::setw(2) << (static_cast<unsigned char>(data[i]) & 0xFF) << " ";
			}
			std::cerr << std::dec << std::endl;
		}
		section->append_data(data.data(), data.size());
		uint32_t size_after = section->get_data_size();
		uint32_t size_increase = size_after - size_before;
		std::cerr << "DEBUG: Section " << section_index << " size after append: " << size_after 
		          << " (increased by " << size_increase << ", expected " << data.size() << ")" << std::endl;
		if (size_increase != data.size()) {
			std::cerr << "WARNING: Size increase mismatch! Expected " << data.size() << " but got " << size_increase << std::endl;
		}
	}

	void add_relocation(uint64_t offset, std::string_view symbol_name) {
		add_relocation(offset, symbol_name, IMAGE_REL_AMD64_REL32);
	}

	void add_relocation(uint64_t offset, std::string_view symbol_name, uint32_t relocation_type) {
		// Get the function symbol using mangled name
		std::string mangled_name = getMangledName(symbol_name);
		auto* symbol = coffi_.get_symbol(mangled_name);
		if (!symbol) {
			// Symbol not found - add it as an external symbol (for C library functions like puts, printf, etc.)

			// Add external symbol with:
			// - section number 0 (undefined/external)
			// - storage class IMAGE_SYM_CLASS_EXTERNAL
			// - value 0
			// - type 0x20 (function)
			symbol = coffi_.add_symbol(mangled_name);
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
	void add_text_relocation(uint64_t offset, const std::string& symbol_name, uint32_t relocation_type) {
		// Look up the symbol (could be a global variable, function, etc.)
		auto* symbol = coffi_.get_symbol(symbol_name);
		if (!symbol) {
			// Try mangled name
			std::string mangled_name = getMangledName(symbol_name);
			symbol = coffi_.get_symbol(mangled_name);
			if (!symbol) {
				std::cerr << "Warning: Symbol not found for relocation: " << symbol_name << std::endl;
				return;
			}
		}

		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		COFFI::rel_entry_generic relocation;
		relocation.virtual_address = offset;
		relocation.symbol_table_index = symbol->get_index();
		relocation.type = relocation_type;
		section_text->add_relocation_entry(&relocation);

		std::cerr << "Added text relocation at offset " << offset << " for symbol " << symbol_name
		          << " type: 0x" << std::hex << relocation_type << std::dec << std::endl;
	}

	void add_pdata_relocations(uint32_t pdata_offset, std::string_view mangled_name, uint32_t xdata_offset) {
		std::cerr << "Adding PDATA relocations for function: " << mangled_name << " at pdata offset " << pdata_offset << std::endl;

		// Get the function symbol using mangled name
		auto* function_symbol = coffi_.get_symbol(mangled_name);
		if (!function_symbol) {
			throw std::runtime_error(std::string("Function symbol not found: ") + std::string(mangled_name));
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

		std::cerr << "Added 3 PDATA relocations for function " << mangled_name << std::endl;
	}

	void add_xdata_relocation(uint32_t xdata_offset, const std::string& handler_name) {
		std::cerr << "Adding XDATA relocation at offset " << xdata_offset << " for handler: " << handler_name << std::endl;

		// Get or create the exception handler symbol
		auto* handler_symbol = coffi_.get_symbol(handler_name);
		if (!handler_symbol) {
			// Add external symbol for the C++ exception handler
			handler_symbol = coffi_.add_symbol(handler_name);
			handler_symbol->set_value(0);
			handler_symbol->set_section_number(0);  // 0 = undefined/external symbol
			handler_symbol->set_type(0x20);  // 0x20 = function type
			handler_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			std::cerr << "Created external symbol for exception handler: " << handler_name << std::endl;
		}

		auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];

		// Add relocation for the exception handler RVA in XDATA
		COFFI::rel_entry_generic reloc;
		reloc.virtual_address = xdata_offset;
		reloc.symbol_table_index = handler_symbol->get_index();
		reloc.type = IMAGE_REL_AMD64_ADDR32NB;  // 32-bit address without base
		xdata_section->add_relocation_entry(&reloc);

		std::cerr << "Added XDATA relocation for handler " << handler_name << " at offset " << xdata_offset << std::endl;
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

	void add_function_exception_info(std::string_view mangled_name, uint32_t function_start, uint32_t function_size, const std::vector<TryBlockInfo>& try_blocks = {}) {
		// Check if exception info has already been added for this function
		for (const auto& existing : added_exception_functions_) {
			if (existing == mangled_name) {
				std::cerr << "Exception info already added for function: " << mangled_name << " - skipping" << std::endl;
				return;
			}
		}

		std::cerr << "Adding exception info for function: " << mangled_name << " at offset " << function_start << " size " << function_size << std::endl;
		added_exception_functions_.push_back(std::string(mangled_name));

		// Get current XDATA section size to calculate the offset for this function's unwind info
		auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
		uint32_t xdata_offset = static_cast<uint32_t>(xdata_section->get_data_size());

		// Add XDATA (exception handling unwind information) for this specific function
		// For C++ exception handling, we need to include an exception handler
		// Windows x64 UNWIND_INFO structure:
		// - BYTE Version:3, Flags:5
		// - BYTE SizeOfProlog
		// - BYTE CountOfCodes
		// - BYTE FrameRegister:4, FrameOffset:4
		// - UNWIND_CODE UnwindCode[CountOfCodes] (aligned to DWORD)
		// - Optional: ExceptionHandler RVA (if UNW_FLAG_EHANDLER is set)
		// - Optional: Exception-specific data
		
		std::vector<char> xdata = {
			0x09,  // Version 1, Flags 0x08 (UNW_FLAG_EHANDLER - exception handler present)
			0x04,  // Size of prolog (4 bytes: push rbp + mov rbp, rsp)
			0x02,  // Count of unwind codes
			0x00,  // Frame register (none)
			0x42,  // Unwind code: UWOP_ALLOC_SMALL at offset 4
			0x00,  // Unwind code: UWOP_PUSH_NONVOL (push rbp) at offset 0
			0x00,  // Padding
			0x00   // Padding
		};
		
		// Add placeholder for exception handler RVA (4 bytes)
		// This will point to __CxxFrameHandler3 or __CxxFrameHandler4
		// We'll add a relocation for this
		uint32_t handler_rva_offset = static_cast<uint32_t>(xdata.size());
		xdata.push_back(0x00);
		xdata.push_back(0x00);
		xdata.push_back(0x00);
		xdata.push_back(0x00);
		
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
		
		if (!try_blocks.empty()) {
			uint32_t funcinfo_offset = static_cast<uint32_t>(xdata.size());
			
			// Magic number for FuncInfo4 (0x19930522 = EH4 with continuation addresses)
			// Using 0x19930521 = EH3 style which is simpler
			uint32_t magic = 0x19930521;
			xdata.push_back(static_cast<char>(magic & 0xFF));
			xdata.push_back(static_cast<char>((magic >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((magic >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((magic >> 24) & 0xFF));
			
			// maxState - maximum state number (accounts for tryLow, tryHigh, catchHigh per try block)
			uint32_t max_state = static_cast<uint32_t>(try_blocks.size() * 2);
			xdata.push_back(static_cast<char>(max_state & 0xFF));
			xdata.push_back(static_cast<char>((max_state >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((max_state >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((max_state >> 24) & 0xFF));
			
			// pUnwindMap - RVA to unwind map (0 for now - no destructors to unwind)
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
			
			// pTryBlockMap - RVA to try block map (will be right after FuncInfo)
			uint32_t tryblock_map_offset = xdata_offset + static_cast<uint32_t>(xdata.size()) + 8; // +8 for remaining FuncInfo fields
			xdata.push_back(static_cast<char>(tryblock_map_offset & 0xFF));
			xdata.push_back(static_cast<char>((tryblock_map_offset >> 8) & 0xFF));
			xdata.push_back(static_cast<char>((tryblock_map_offset >> 16) & 0xFF));
			xdata.push_back(static_cast<char>((tryblock_map_offset >> 24) & 0xFF));
			
			// nIPMapEntries - number of IP-to-state map entries (0 for simple implementation)
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			
			// pIPToStateMap - RVA to IP-to-state map (0 for simple implementation)
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			xdata.push_back(0x00);
			
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
				
				// tryLow (state when entering try block)
				uint32_t try_low = static_cast<uint32_t>(i);
				xdata.push_back(static_cast<char>(try_low & 0xFF));
				xdata.push_back(static_cast<char>((try_low >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((try_low >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((try_low >> 24) & 0xFF));
				
				// tryHigh (state when exiting try block - should be different from tryLow)
				uint32_t try_high = static_cast<uint32_t>(i) + 1;
				xdata.push_back(static_cast<char>(try_high & 0xFF));
				xdata.push_back(static_cast<char>((try_high >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((try_high >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((try_high >> 24) & 0xFF));
				
				// catchHigh (state after handling exception)
				uint32_t catch_high = static_cast<uint32_t>(i) + 2;
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
				xdata.push_back(static_cast<char>(handler_array_offset & 0xFF));
				xdata.push_back(static_cast<char>((handler_array_offset >> 8) & 0xFF));
				xdata.push_back(static_cast<char>((handler_array_offset >> 16) & 0xFF));
				xdata.push_back(static_cast<char>((handler_array_offset >> 24) & 0xFF));
				
				handler_array_base += num_catches * 16; // 16 bytes per HandlerType entry
			}
			
			// Now add HandlerType arrays for each try block
			// Each HandlerType:
			//   DWORD adjectives (0x01 = const, 0x08 = reference, 0 = by-value)
			//   DWORD pType (RVA to type descriptor, 0 for catch-all)
			//   int catchObjOffset (frame offset of catch parameter, negative)
			//   DWORD addressOfHandler (RVA of catch handler code)
			
			// First, generate type descriptors for all unique exception types
			std::unordered_map<std::string, uint32_t> type_descriptor_offsets;
			
			for (const auto& try_block : try_blocks) {
				for (const auto& handler : try_block.catch_handlers) {
					if (!handler.is_catch_all && !handler.type_name.empty()) {
						// Check if we've already created a descriptor for this type
						if (type_descriptor_offsets.find(handler.type_name) == type_descriptor_offsets.end()) {
							// Validate that RDATA section exists
							auto rdata_section_it = sectiontype_to_index.find(SectionType::RDATA);
							if (rdata_section_it == sectiontype_to_index.end()) {
								std::cerr << "ERROR: RDATA section not found for type descriptor generation" << std::endl;
								continue;
							}
							
							// Generate type descriptor in .rdata section
							auto rdata_section = coffi_.get_sections()[rdata_section_it->second];
							uint32_t type_desc_offset = static_cast<uint32_t>(rdata_section->get_data_size());
							
							// Mangle the type name for the type descriptor symbol
							std::string mangled_type_name = mangleTypeName(handler.type_name);
							std::string type_desc_symbol = "??_R0" + mangled_type_name;
							
							// Create type descriptor data
							// Format: vtable_ptr (8 bytes) + spare (8 bytes) + mangled_name (null-terminated)
							std::vector<char> type_desc_data;
							
							// vtable pointer (POINTER_SIZE bytes) - null for exception types
							for (size_t i = 0; i < POINTER_SIZE; ++i) type_desc_data.push_back(0);
							
							// spare pointer (POINTER_SIZE bytes) - null
							for (size_t i = 0; i < POINTER_SIZE; ++i) type_desc_data.push_back(0);
							
							// mangled name (null-terminated)
							for (char c : mangled_type_name) type_desc_data.push_back(c);
							type_desc_data.push_back(0);
							
							// Add to .rdata section
							add_data(type_desc_data, SectionType::RDATA);
							
							// Create symbol for the type descriptor
							auto type_desc_sym = coffi_.add_symbol(type_desc_symbol);
							type_desc_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
							type_desc_sym->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
							type_desc_sym->set_section_number(rdata_section->get_index() + 1);
							type_desc_sym->set_value(type_desc_offset);
							
							std::cerr << "  Created type descriptor '" << type_desc_symbol << "' for exception type '" 
							          << handler.type_name << "' at offset " << type_desc_offset << std::endl;
							
							type_descriptor_offsets[handler.type_name] = type_desc_offset;
						}
					}
				}
			}
			
			// Now generate HandlerType entries with proper pType references
			size_t handler_index = 0;
			for (const auto& try_block : try_blocks) {
				for (const auto& handler : try_block.catch_handlers) {
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
					if (handler.is_catch_all || handler.type_name.empty()) {
						// catch(...) - no type descriptor
						xdata.push_back(0x00);
						xdata.push_back(0x00);
						xdata.push_back(0x00);
						xdata.push_back(0x00);
					} else {
						// Type-specific catch - add placeholder and relocation
						xdata.push_back(0x00);
						xdata.push_back(0x00);
						xdata.push_back(0x00);
						xdata.push_back(0x00);
						
						// Add relocation for pType to point to the type descriptor
						std::string mangled_type_name = mangleTypeName(handler.type_name);
						std::string type_desc_symbol = "??_R0" + mangled_type_name;
						add_xdata_relocation(xdata_offset + ptype_offset, type_desc_symbol);
						
						std::cerr << "  Added pType relocation for handler " << handler_index 
						          << " to type descriptor '" << type_desc_symbol << "'" << std::endl;
					}
					
					// catchObjOffset (0 = not used for now)
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					xdata.push_back(0x00);
					
					// addressOfHandler - RVA of catch handler (relative to function start)
					uint32_t handler_rva = function_start + handler.handler_offset;
					xdata.push_back(static_cast<char>(handler_rva & 0xFF));
					xdata.push_back(static_cast<char>((handler_rva >> 8) & 0xFF));
					xdata.push_back(static_cast<char>((handler_rva >> 16) & 0xFF));
					xdata.push_back(static_cast<char>((handler_rva >> 24) & 0xFF));
					
					handler_index++;
				}
			}
		}
		
		// Add the XDATA to the section
		add_data(xdata, SectionType::XDATA);
		
		// Add relocation for the exception handler RVA
		// Point to __CxxFrameHandler3 (MSVC's C++ exception handler)
		add_xdata_relocation(xdata_offset + handler_rva_offset, "__CxxFrameHandler3");

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
	}

	void finalize_debug_info() {
		std::cerr << "finalize_debug_info: Generating debug information..." << std::endl;
		// Exception info is now handled directly in IRConverter finalization logic

		// Finalize the current function before generating debug sections
		debug_builder_.finalizeCurrentFunction();

		// Set the correct text section number for symbol references
		uint16_t text_section_number = static_cast<uint16_t>(sectiontype_to_index[SectionType::TEXT] + 1);
		debug_builder_.setTextSectionNumber(text_section_number);
		std::cerr << "DEBUG: Set text section number to " << text_section_number << "\n";

		// Generate debug sections
		auto debug_s_data = debug_builder_.generateDebugS();
		auto debug_t_data = debug_builder_.generateDebugT();

		// Add debug relocations
		const auto& debug_relocations = debug_builder_.getDebugRelocations();
		for (const auto& reloc : debug_relocations) {
			add_debug_relocation(reloc.offset, reloc.symbol_name, reloc.relocation_type);
		}
		std::cerr << "DEBUG: Added " << debug_relocations.size() << " debug relocations\n";

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

	// Add a string literal to the .rdata section and return its symbol name
	std::string add_string_literal(const std::string& str_content) {
		// Generate a unique symbol name for this string literal
		std::string symbol_name = ".str." + std::to_string(string_literal_counter_++);

		// Get current offset in .rdata section
		auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];
		uint32_t offset = rdata_section->get_data_size();

		// Process the string: remove quotes and handle escape sequences
		std::string processed_str;
		if (str_content.size() >= 2 && str_content.front() == '"' && str_content.back() == '"') {
			// Remove quotes
			std::string_view content(str_content.data() + 1, str_content.size() - 2);

			// Process escape sequences
			for (size_t i = 0; i < content.size(); ++i) {
				if (content[i] == '\\' && i + 1 < content.size()) {
					switch (content[i + 1]) {
						case 'n': processed_str += '\n'; ++i; break;
						case 't': processed_str += '\t'; ++i; break;
						case 'r': processed_str += '\r'; ++i; break;
						case '\\': processed_str += '\\'; ++i; break;
						case '"': processed_str += '"'; ++i; break;
						case '0': processed_str += '\0'; ++i; break;
						default: processed_str += content[i]; break;
					}
				} else {
					processed_str += content[i];
				}
			}
		} else {
			processed_str = str_content;
		}

		// Add null terminator
		processed_str += '\0';

		// Add the string data to .rdata section
		std::vector<char> str_data(processed_str.begin(), processed_str.end());
		add_data(str_data, SectionType::RDATA);

		// Add a symbol for this string literal
		auto symbol = coffi_.add_symbol(symbol_name);
		symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol->set_section_number(rdata_section->get_index() + 1);
		symbol->set_value(offset);

		std::cerr << "Added string literal '" << processed_str.substr(0, processed_str.size() - 1)
		          << "' at offset " << offset << " with symbol " << symbol_name << std::endl;

		return symbol_name;
	}

	// Add a global variable to .data or .bss section
	void add_global_variable(std::string_view var_name, size_t size_in_bytes, bool is_initialized, unsigned long long init_value = 0) {
		SectionType section_type = is_initialized ? SectionType::DATA : SectionType::BSS;
		auto section = coffi_.get_sections()[sectiontype_to_index[section_type]];
		uint32_t offset = static_cast<uint32_t>(section->get_data_size());

		std::cerr << "DEBUG: add_global_variable - var_name=" << var_name << " is_initialized=" << is_initialized << " init_value=" << init_value << "\n";
		std::cerr << "DEBUG: add_global_variable - section_type=" << (section_type == SectionType::DATA ? "DATA" : "BSS") << "\n";
		std::cerr << "DEBUG: add_global_variable - section index=" << sectiontype_to_index[section_type] << " offset=" << offset << "\n";

		if (is_initialized) {
			// Add initialized data to .data section
			std::vector<char> data(size_in_bytes);
			// Store init_value in little-endian format
			for (size_t i = 0; i < size_in_bytes && i < 8; ++i) {
				data[i] = static_cast<char>((init_value >> (i * 8)) & 0xFF);
			}
			add_data(data, SectionType::DATA);
		} else {
			// For .bss, we don't add actual data, just increase the section size
			// BSS is uninitialized data that gets zero-initialized at load time
			std::vector<char> zero_data(size_in_bytes, 0);
			add_data(zero_data, SectionType::BSS);
		}

		// Add a symbol for this global variable
		auto symbol = coffi_.add_symbol(var_name);
		symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);  // Global variables are external
		symbol->set_section_number(section->get_index() + 1);
		symbol->set_value(offset);

		std::cerr << "Added global variable '" << var_name << "' at offset " << offset
		          << " in " << (is_initialized ? ".data" : ".bss") << " section (size: " << size_in_bytes << " bytes)" << std::endl;
	}

	// Base class descriptor info for RTTI emission
	struct BaseClassDescriptorInfo {
		std::string name;            // Base class name
		uint32_t num_contained_bases; // Number of bases this base has
		uint32_t offset;             // Offset of base in derived class (mdisp)
		bool is_virtual;             // Whether this is a virtual base
	};

	// Add a vtable to .rdata section with RTTI support
	// vtable_symbol: mangled vtable symbol name (e.g., "??_7Base@@6B@")
	// function_symbols: vector of mangled function names in vtable order
	// class_name: name of the class for RTTI
	// base_class_names: vector of base class names for RTTI (legacy)
	// base_class_info: detailed base class information for proper RTTI
	void add_vtable(const std::string& vtable_symbol, const std::vector<std::string>& function_symbols,
	                const std::string& class_name, const std::vector<std::string>& base_class_names,
	                const std::vector<BaseClassDescriptorInfo>& base_class_info) {
		auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];
		
		std::cerr << "DEBUG: add_vtable - vtable_symbol=" << vtable_symbol 
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
		std::string mangled_class_name = ".?AV" + class_name + "@@";
		
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
		
		std::cerr << "  Added ??_R0 Type Descriptor '" << type_desc_symbol << "' at offset " 
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
		
		std::cerr << "  Added ??_R1 self BCD '" << self_bcd_symbol << "' at offset " 
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
			uint32_t num_contained = bci.num_contained_bases;
			for (int j = 0; j < 4; ++j) base_bcd_data.push_back((num_contained >> (j * 8)) & 0xFF);
			
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
			
			std::cerr << "  Added ??_R1 base BCD for " << bci.name << std::endl;
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
		
		std::cerr << "  Added ??_R2 Base Class Array '" << bca_symbol << "' at offset " 
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
		
		std::cerr << "  Added ??_R3 Class Hierarchy Descriptor '" << chd_symbol << "' at offset " 
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
		
		std::cerr << "  Added ??_R4 Complete Object Locator '" << col_symbol << "' at offset " 
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
			
			std::cerr << "  DEBUG: Creating COL relocation at offset " << col_reloc_offset 
			          << " pointing to symbol '" << col_symbol << "' (file index " << col_symbol_index << ")" << std::endl;
			
			COFFI::rel_entry_generic relocation;
			relocation.virtual_address = col_reloc_offset;
			relocation.symbol_table_index = col_symbol_index;
			relocation.type = IMAGE_REL_AMD64_ADDR64;
			
			rdata_section->add_relocation_entry(&relocation);
			
			std::cerr << "  Added COL pointer relocation at vtable[-1]" << std::endl;
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
			
			std::cerr << "  Added relocation for vtable[" << i << "] -> " << function_symbols[i] 
			          << " at offset " << reloc_offset << " (file index " << func_symbol_index << ")" << std::endl;
		}

		std::cerr << "Added vtable '" << vtable_symbol << "' at offset " << vtable_symbol_offset
		          << " in .rdata section (total size with RTTI: " << vtable_size << " bytes)" << std::endl;
	}

	// Helper: get or create symbol index for a function name
	uint32_t get_or_create_symbol_index(const std::string& symbol_name) {
		// First, check if symbol already exists
		auto symbols = coffi_.get_symbols();
		for (size_t i = 0; i < symbols->size(); ++i) {
			if ((*symbols)[i].get_name() == symbol_name) {
				std::cerr << "    DEBUG get_or_create_symbol_index: Found existing symbol '" << symbol_name 
				          << "' at array index " << i << ", file index " << (*symbols)[i].get_index() << std::endl;
				return (*symbols)[i].get_index();
			}
		}
		
		// Symbol doesn't exist, create it as an external reference
		std::cerr << "    DEBUG get_or_create_symbol_index: Creating new symbol '" << symbol_name << "'" << std::endl;
		auto symbol = coffi_.add_symbol(symbol_name);
		symbol->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol->set_section_number(0);  // External reference
		symbol->set_value(0);
		
		// Return the index from COFFI (which includes aux entries)
		uint32_t file_index = symbol->get_index();
		std::cerr << "    DEBUG get_or_create_symbol_index: Created new symbol at file index " << file_index 
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

	// Counter for generating unique string literal symbols
	uint32_t string_literal_counter_ = 0;
};
