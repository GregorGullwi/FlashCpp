#pragma once

#if defined(__clang__)
// Clang compiler
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif

#include "coffi/coffi.hpp"
#include "CodeViewDebug.h"
#include "AstNodeTypes.h"
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
	std::string getMangledName(std::string_view name) const {
		// Check if the name is already a mangled name (starts with '?')
		if (!name.empty() && name[0] == '?') {
			// Already mangled, return as-is
			return std::string(name);
		}

		// Check if this is a member function call (ClassName::FunctionName format)
		size_t scope_pos = name.find("::");
		if (scope_pos != std::string_view::npos) {
			// Split into class name and function name using string_view (no allocation)
			std::string_view class_name = name.substr(0, scope_pos);
			std::string_view func_name = name.substr(scope_pos + 2);

			// Search for a mangled name that matches this member function
			// The function name in the mangled name might include the class prefix (e.g., "Container::Container")
			// So we need to search for both patterns:
			// 1. ?FunctionName@ClassName@@... (func_name without class prefix)
			// 2. ?ClassName::FunctionName@ClassName@@... (func_name with class prefix, for constructors)
			for (const auto& [mangled, sig] : function_signatures_) {
				if (mangled[0] != '?') continue;
				
				// Pattern 1: ?FunctionName@ClassName@@...
				if (mangled.size() > 1 + func_name.size() + 1 + class_name.size() &&
				    mangled.substr(1, func_name.size()) == func_name &&
				    mangled[1 + func_name.size()] == '@' &&
				    mangled.substr(2 + func_name.size(), class_name.size()) == class_name) {
					std::cerr << "DEBUG: getMangledName found match (pattern 1) for " << name << " -> " << mangled << "\n";
					return mangled;
				}
				
				// Pattern 2: ?ClassName::FunctionName@ClassName@@... (for constructors/destructors)
				std::string full_func_name = std::string(class_name) + "::" + std::string(func_name);
				if (mangled.size() > 1 + full_func_name.size() + 1 + class_name.size() &&
				    mangled.substr(1, full_func_name.size()) == full_func_name &&
				    mangled[1 + full_func_name.size()] == '@' &&
				    mangled.substr(2 + full_func_name.size(), class_name.size()) == class_name) {
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
			// Format: ?FunctionName@ClassName@@YAH@Z (assuming int return, no params for now)
			// For constructors (FunctionName == ClassName), use YAX (void return)
			// This is a simplified mangling - the actual signature will be added later
			// Reserve space to avoid reallocations
			std::string mangled;
			mangled.reserve(1 + func_name.size() + 1 + class_name.size() + 7);
			mangled += '?';
			mangled += func_name;
			mangled += '@';
			mangled += class_name;
			// Check if this is a constructor (function name == class name)
			if (func_name == class_name) {
				mangled += "@@YAX@Z";  // void return, no params
			} else {
				mangled += "@@YAH@Z";  // int return, no params (default for regular functions)
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
	enum EFunctionCallingConv : unsigned char
	{
		cdecl,
		stdcall,
		fastcall,
	};
	// Function signature information for mangling
	struct FunctionSignature {
		TypeSpecifierNode return_type;
		std::vector<TypeSpecifierNode> parameter_types;
		bool is_const = false;
		bool is_static = false;
		bool is_variadic = false;  // True if function has ... ellipsis parameter
		EFunctionCallingConv calling_convention = EFunctionCallingConv::cdecl;
		std::string namespace_name;
		std::string class_name;
		Linkage linkage = Linkage::None;  // C vs C++ linkage

		FunctionSignature() = default;
		FunctionSignature(const TypeSpecifierNode& ret_type, std::vector<TypeSpecifierNode> params)
			: return_type(ret_type), parameter_types(std::move(params)) {}
	};

	// Map from mangled name to function signature
	mutable std::unordered_map<std::string, FunctionSignature> function_signatures_;

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
		if (!sig.class_name.empty()) {
			mangled += '@';
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

		// Add end marker - different for variadic vs non-variadic
		if (sig.is_variadic) {
			mangled += "ZZ";  // Variadic functions end with 'ZZ' in MSVC mangling
		} else {
			mangled += "@Z";  // Non-variadic functions end with '@Z'
		}

		return mangled;
	}

	// Get Microsoft Visual C++ type code for mangling (with pointer support)
	std::string getTypeCode(const TypeSpecifierNode& type_node) const {
		std::string code;

		// Handle CV-qualifiers for references
		// In MSVC mangling, CV-qualifiers come before the reference/pointer markers
		auto cv_val = static_cast<uint8_t>(type_node.cv_qualifier());
		bool has_const = (cv_val & static_cast<uint8_t>(CVQualifier::Const)) != 0;
		bool has_volatile = (cv_val & static_cast<uint8_t>(CVQualifier::Volatile)) != 0;
		
		// Handle references - they need special encoding
		if (type_node.is_reference()) {
			if (type_node.is_rvalue_reference()) {
				// Rvalue reference: $$Q + CV qualifiers + base type
				code += "$$Q";
				if (has_const && has_volatile) code += "EA";
				else if (has_const) code += "EB";
				else if (has_volatile) code += "EC";
				else code += "EA";  // Non-const, non-volatile rvalue ref
			} else {
				// Lvalue reference: A/B + CV qualifiers
				if (has_const && has_volatile) code += "AED";
				else if (has_const) code += "AEB";  // const lvalue reference
				else if (has_volatile) code += "AEC";
				else code += "AEA";  // Non-const lvalue reference
			}
		}

		// Add pointer prefix for each level of indirection
		for (size_t i = 0; i < type_node.pointer_depth(); ++i) {
			code += "PE";  // Pointer prefix in MSVC mangling
		}

		// Add base type code
		switch (type_node.type()) {
			case Type::Void: code += "X"; break;
			case Type::Bool: code += "_N"; break;
			case Type::Char: code += "D"; break;
			case Type::UnsignedChar: code += "E"; break;
			case Type::Short: code += "F"; break;
			case Type::UnsignedShort: code += "G"; break;
			case Type::Int: code += "H"; break;
			case Type::UnsignedInt: code += "I"; break;
			case Type::Long: code += "J"; break;
			case Type::UnsignedLong: code += "K"; break;
			case Type::LongLong: code += "_J"; break;
			case Type::UnsignedLongLong: code += "_K"; break;
			case Type::Float: code += "M"; break;
			case Type::Double: code += "N"; break;
			case Type::LongDouble: code += "O"; break;
			case Type::Struct: code += "V"; break;  // Struct/class type
			default: code += "H"; break;  // Default to int for unknown types
		}

		return code;
	}

public:
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

	void add_function_symbol(const std::string& mangled_name, uint32_t section_offset, uint32_t stack_space) {
		std::cerr << "Adding function symbol: " << mangled_name << " at offset " << section_offset << std::endl;
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		auto symbol_func = coffi_.add_symbol(mangled_name);
		symbol_func->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol_func->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol_func->set_section_number(section_text->get_index() + 1);
		symbol_func->set_value(section_offset);

		// Extract unmangled name for debug info
		// Mangled names start with '?' followed by the function name up to '@@'
		std::string unmangled_name = mangled_name;
		if (!mangled_name.empty() && mangled_name[0] == '?') {
			size_t end_pos = mangled_name.find("@@");
			if (end_pos != std::string::npos) {
				unmangled_name = mangled_name.substr(1, end_pos - 1);
			}
		}

		// Add function to debug info with length 0 - length will be calculated later
		std::cerr << "DEBUG: Adding function to debug builder: " << unmangled_name << " (mangled: " << mangled_name << ") at offset " << section_offset << "\n";
		debug_builder_.addFunction(unmangled_name, mangled_name, section_offset, 0, stack_space);
		std::cerr << "DEBUG: Function added to debug builder \n";

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
		std::cerr << "DEBUG add_relocation: input symbol_name = " << symbol_name << "\n";
		std::string mangled_name = getMangledName(std::string(symbol_name));
		std::cerr << "DEBUG add_relocation: after getMangledName = " << mangled_name << "\n";
		auto* symbol = coffi_.get_symbol(std::string(mangled_name));
		if (!symbol) {
			// Symbol not found - add it as an external symbol (for C library functions like puts, printf, etc.)
			std::cerr << "Adding external symbol: " << mangled_name << " (original: " << symbol_name << ")" << std::endl;

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
		relocation.type = IMAGE_REL_AMD64_REL32;
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

	void add_pdata_relocations(uint32_t pdata_offset, const std::string& mangled_name, uint32_t xdata_offset) {
		std::cerr << "Adding PDATA relocations for function: " << mangled_name << " at pdata offset " << pdata_offset << std::endl;

		// Get the function symbol using mangled name
		auto* function_symbol = coffi_.get_symbol(mangled_name);
		if (!function_symbol) {
			throw std::runtime_error("Function symbol not found: " + mangled_name);
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

	void add_function_exception_info(const std::string& mangled_name, uint32_t function_start, uint32_t function_size) {
		// Check if exception info has already been added for this function
		for (const auto& existing : added_exception_functions_) {
			if (existing == mangled_name) {
				std::cerr << "Exception info already added for function: " << mangled_name << " - skipping" << std::endl;
				return;
			}
		}

		std::cerr << "Adding exception info for function: " << mangled_name << " at offset " << function_start << " size " << function_size << std::endl;
		added_exception_functions_.push_back(mangled_name);

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
	void add_global_variable(const std::string& var_name, size_t size_in_bytes, bool is_initialized, unsigned long long init_value = 0) {
		SectionType section_type = is_initialized ? SectionType::DATA : SectionType::BSS;
		auto section = coffi_.get_sections()[sectiontype_to_index[section_type]];
		uint32_t offset = static_cast<uint32_t>(section->get_data_size());

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
