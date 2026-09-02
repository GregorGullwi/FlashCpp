#include "ObjFileWriter.h"
#include "Log.h"
#include "StringLiteralTokenUtils.h"
#include <optional>

// ObjFileWriter_RTTI.cpp - Out-of-line method definitions for ObjectFileWriter
// Part of ObjectFileWriter class (unity build)

namespace {
char getMsvcUserTypeTag(std::string_view class_name, TypeIndex type_index) {
	const TypeInfo* type_info = nullptr;
	if (type_index.is_valid()) {
		type_info = tryGetTypeInfo(type_index);
	}
	if (!type_info) {
		type_info = findTypeByName(StringTable::getOrInternStringHandle(class_name));
	}
	if (type_info) {
		if (const StructTypeInfo* struct_info = type_info->getStructInfo()) {
			return struct_info->default_access == AccessSpecifier::Private ? 'V' : 'U';
		}
	}
	return 'V';
}

std::string buildMsvcTypeDescriptorName(std::string_view class_name, TypeIndex type_index) {
	std::string_view mangled_name_sv = StringBuilder()
										  .append(".?A"sv)
										  .append(getMsvcUserTypeTag(class_name, type_index))
										  .append(class_name)
										  .append("@@"sv)
										  .commit();
	return std::string(mangled_name_sv);
}

std::string buildMsvcTypeDescriptorSymbol(std::string_view mangled_type_name) {
	std::string_view type_desc_symbol_sv = StringBuilder()
											   .append("??_R0"sv)
											   .append(mangled_type_name.substr(1))
											   .append("@8"sv)
											   .commit();
	return std::string(type_desc_symbol_sv);
}

// Map a built-in/arithmetic TypeCategory to its MSVC RTTI Type Descriptor
// code (the bit that appears between ".?A"-less prefix and the "@" terminator
// and is also embedded in the ??_R0 symbol name). Returns an empty view for
// categories that don't correspond to a standard MSVC built-in mangling.
std::string_view getMsvcBuiltinTypeCode(TypeCategory cat) {
	using namespace std::literals;
	switch (cat) {
	case TypeCategory::Void:              return "X"sv;
	case TypeCategory::Bool:              return "_N"sv;
	case TypeCategory::Char:              return "D"sv;
	case TypeCategory::UnsignedChar:      return "E"sv;
	case TypeCategory::WChar:             return "_W"sv;
	case TypeCategory::Char8:             return "_Q"sv; // C++20 char8_t
	case TypeCategory::Char16:            return "_S"sv;
	case TypeCategory::Char32:            return "_U"sv;
	case TypeCategory::Short:             return "F"sv;
	case TypeCategory::UnsignedShort:     return "G"sv;
	case TypeCategory::Int:               return "H"sv;
	case TypeCategory::UnsignedInt:       return "I"sv;
	case TypeCategory::Long:              return "J"sv;
	case TypeCategory::UnsignedLong:      return "K"sv;
	case TypeCategory::LongLong:          return "_J"sv;
	case TypeCategory::UnsignedLongLong:  return "_K"sv;
	case TypeCategory::Float:             return "M"sv;
	case TypeCategory::Double:            return "N"sv;
	case TypeCategory::LongDouble:        return "O"sv;
	default:                              return {};
	}
}

uint32_t getMsvcClassHierarchyAttributes(std::string_view class_name) {
	const TypeInfo* type_info = findTypeByName(StringTable::getOrInternStringHandle(class_name));
	const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
	if (!struct_info) {
		return 0;
	}

	uint32_t attributes = 0;
	const size_t direct_base_count = struct_info->base_classes.size();
	if (direct_base_count > 1) {
		attributes |= 0x1; // CHD_MULTINH
	}

	auto hasVirtualBase = [&](const auto& self, const StructTypeInfo* current_struct) -> bool {
		if (!current_struct) {
			return false;
		}
		if (!current_struct->virtual_bases.empty()) {
			return true;
		}
		for (const auto& base : current_struct->base_classes) {
			if (base.is_virtual) {
				return true;
			}
			const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index);
			if (self(self, base_type_info ? base_type_info->getStructInfo() : nullptr)) {
				return true;
			}
		}
		return false;
	};
	if (hasVirtualBase(hasVirtualBase, struct_info)) {
		attributes |= 0x2; // CHD_VIRTINH
	}

	return attributes;
}

struct VagueLinkageRdataGroup {
	std::vector<char> bytes;
	std::vector<ObjectFileWriter::ComdatReloc> relocs;
	struct NamedOffset {
		std::string name;
		uint32_t offset = 0;
	};
	std::vector<NamedOffset> external_symbols;

	uint32_t size() const {
		return static_cast<uint32_t>(bytes.size());
	}

	void append(std::span<const char> data) {
		bytes.insert(bytes.end(), data.begin(), data.end());
	}

	void append(const std::vector<char>& data) {
		append(std::span<const char>(data));
	}

	void addImageRelative(uint32_t offset, uint32_t symbol_table_index) {
		relocs.push_back({offset, symbol_table_index, IMAGE_REL_AMD64_ADDR32NB});
	}

	void addAddr64(uint32_t offset, uint32_t symbol_table_index) {
		relocs.push_back({offset, symbol_table_index, IMAGE_REL_AMD64_ADDR64});
	}

	void addExternalSymbol(std::string name, uint32_t offset) {
		external_symbols.push_back({std::move(name), offset});
	}
};
} // namespace

void ObjectFileWriter::add_function_exception_info(std::string_view mangled_name, uint32_t function_start, uint32_t function_size, std::span<const TryBlockInfo> try_blocks, std::span<const UnwindMapEntryInfo> unwind_map, std::span<const SehTryBlockInfo> seh_try_blocks, uint32_t stack_frame_size) {
	// Check if exception info has already been added for this function
	for (const auto& existing : added_exception_functions_) {
		if (existing == mangled_name) {
			if (g_enable_debug_output)
				std::cerr << "Exception info already added for function: " << mangled_name << " - skipping" << std::endl;
			return;
		}
	}

	if (g_enable_debug_output)
		std::cerr << "Adding exception info for function: " << mangled_name << " at offset " << function_start << " size " << function_size << std::endl;
	added_exception_functions_.push_back(std::string(mangled_name));

	// Get current XDATA section size to calculate the offset for this function's unwind info
	auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
	uint32_t xdata_offset = static_cast<uint32_t>(xdata_section->get_data_size());

	// Determine if this is SEH or C++ exception handling
	bool is_seh = !seh_try_blocks.empty();
	bool is_cpp = !try_blocks.empty() || !unwind_map.empty();
	bool has_cpp_dispatch = !try_blocks.empty();
	uint32_t cpp_funcinfo_local_offset = 0;

	if (is_seh && is_cpp) {
		FLASH_LOG(Codegen, Warning, "Function has both SEH and C++ exception handling - using SEH");
		is_cpp = false;	// Prevent C++ EH metadata from corrupting SEH scope table
	}

	// Determine flags based on exception type
	uint8_t unwind_flags = 0x00;
	if (is_seh) {
			// SEH uses the handler during both dispatch and unwind.
		unwind_flags = 0x03;	 // UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER
	} else if (is_cpp) {
			// MSVC uses UHANDLER-only for C++ functions that only need cleanup/unwind
			// actions, and EHANDLER|UHANDLER when the function has actual try/catch
			// dispatch metadata.
		unwind_flags = has_cpp_dispatch ? 0x03 : 0x02;
	}

	// Build unwind codes
	UnwindCodeResult unwind_info = build_unwind_codes(is_cpp, stack_frame_size);
	uint32_t effective_frame_size = unwind_info.effective_frame_size;

	// Build UNWIND_INFO header + codes
	std::vector<char> xdata = {
		static_cast<char>(0x01 | (unwind_flags << 3)),  // Version 1, Flags
		static_cast<char>(unwind_info.prolog_size),		// Size of prolog
		static_cast<char>(unwind_info.count_of_codes),  // Count of unwind codes
		static_cast<char>(unwind_info.frame_reg_and_offset) // Frame register and offset
	};
	for (auto b : unwind_info.codes) {
		xdata.push_back(static_cast<char>(b));
	}

	// Add exception handler RVA placeholder when EHANDLER/UHANDLER flags are present
	uint32_t handler_rva_offset = 0;
	if (is_seh || is_cpp) {
		handler_rva_offset = static_cast<uint32_t>(xdata.size());
		appendLE_xdata(xdata, uint32_t(0));
	}

	// For C++ EH, reserve space for FuncInfo RVA pointer
	uint32_t cpp_funcinfo_rva_field_offset = 0;
	bool has_cpp_funcinfo_rva_field = false;
	if (is_cpp) {
		cpp_funcinfo_rva_field_offset = static_cast<uint32_t>(xdata.size());
		appendLE_xdata(xdata, uint32_t(0));
		has_cpp_funcinfo_rva_field = true;
	}

	// Relocation tracking
	std::vector<ScopeTableReloc> scope_relocs;
	std::vector<uint32_t> cpp_xdata_rva_field_offsets;
	std::vector<uint32_t> cpp_text_rva_field_offsets;

	// Build SEH scope table
	if (is_seh) {
		build_seh_scope_table(xdata, function_start, seh_try_blocks, scope_relocs);
	}

	// Build C++ FuncInfo and associated metadata
	if (is_cpp) {
		build_cpp_exception_metadata(xdata, xdata_offset, function_start, function_size,
									 mangled_name, try_blocks, unwind_map,
									 effective_frame_size, stack_frame_size,
									 cpp_funcinfo_rva_field_offset, has_cpp_funcinfo_rva_field,
									 cpp_funcinfo_local_offset,
									 cpp_xdata_rva_field_offsets, cpp_text_rva_field_offsets);
	}

	// Add the XDATA to the section
	add_data(xdata, SectionType::XDATA);
	recordFunctionXdataRange(mangled_name, xdata_offset, static_cast<uint32_t>(xdata.size()));

	// Emit relocations for exception handler and metadata
	emit_exception_relocations(xdata_offset, handler_rva_offset, is_seh, is_cpp,
							   scope_relocs, cpp_xdata_rva_field_offsets, cpp_text_rva_field_offsets);

	// Build and emit PDATA entries
	build_pdata_entries(function_start, function_size, mangled_name, try_blocks, unwind_map,
						is_cpp, xdata_offset, unwind_info, cpp_funcinfo_local_offset);
}

void ObjectFileWriter::finalize_debug_info() {
	if (g_enable_debug_output)
		std::cerr << "finalize_debug_info: Generating debug information..." << std::endl;
	// Exception info is now handled directly in IRConverter finalization logic

	// Finalize the current function before generating debug sections
	debug_builder_.finalizeCurrentFunction();

	// Set the correct text section number for symbol references
	uint16_t text_section_number = static_cast<uint16_t>(sectiontype_to_index[SectionType::TEXT] + 1);
	debug_builder_.setTextSectionNumber(text_section_number);
	if (g_enable_debug_output)
		std::cerr << "DEBUG: Set text section number to " << text_section_number << "\n";

	// Generate debug sections
	auto debug_s_data = debug_builder_.generateDebugS();
	auto debug_t_data = debug_builder_.generateDebugT();

	// Add debug relocations
	const auto& debug_relocations = debug_builder_.getDebugRelocations();
	for (const auto& reloc : debug_relocations) {
		add_debug_relocation(reloc.offset, reloc.symbol_name, reloc.relocation_type);
	}
	if (g_enable_debug_output)
		std::cerr << "DEBUG: Added " << debug_relocations.size() << " debug relocations\n";

	// Add debug data to sections
	if (!debug_s_data.empty()) {
		add_data(std::vector<char>(debug_s_data.begin(), debug_s_data.end()), SectionType::DEBUG_S);
		if (g_enable_debug_output)
			std::cerr << "Added " << debug_s_data.size() << " bytes of .debug$S data" << std::endl;
	}
	if (!debug_t_data.empty()) {
		add_data(std::vector<char>(debug_t_data.begin(), debug_t_data.end()), SectionType::DEBUG_T);
		if (g_enable_debug_output)
			std::cerr << "Added " << debug_t_data.size() << " bytes of .debug$T data" << std::endl;
	}
}

// Add a string literal to the .rdata section and return its symbol name
std::string_view ObjectFileWriter::add_string_literal(std::string_view str_content) {
	// Generate a unique symbol name for this string literal
	std::string_view symbol_name = StringBuilder().append(".str."sv).append(string_literal_counter_++).commit();

	// Get current offset in .rdata section
	auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];
	uint32_t offset = rdata_section->get_data_size();

	// Process the string: remove quotes and handle escape sequences
	// Reuse buffer and clear it (capacity is retained)
	string_literal_buffer_.clear();
	string_literal_buffer_.reserve(str_content.size() + 1);

	const auto parsed_literal = FlashCpp::parseStringLiteralToken(str_content);
	if (parsed_literal.is_raw) {
		if (parsed_literal.has_delimited_content) {
			string_literal_buffer_.append(parsed_literal.content);
		} else {
			string_literal_buffer_.append(parsed_literal.normalized_token);
		}
	} else if (parsed_literal.has_delimited_content) {
		// Remove quotes
		std::string_view content = parsed_literal.content;

		// Process escape sequences
		for (size_t i = 0; i < content.size(); ++i) {
			if (content[i] == '\\' && i + 1 < content.size()) {
				switch (content[i + 1]) {
				case 'n':
					string_literal_buffer_ += '\n';
					++i;
					break;
				case 't':
					string_literal_buffer_ += '\t';
					++i;
					break;
				case 'r':
					string_literal_buffer_ += '\r';
					++i;
					break;
				case '\\':
					string_literal_buffer_ += '\\';
					++i;
					break;
				case '"':
					string_literal_buffer_ += '"';
					++i;
					break;
				case '0':
					string_literal_buffer_ += '\0';
					++i;
					break;
				default:
					string_literal_buffer_ += content[i];
					break;
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

	if (g_enable_debug_output)
		std::cerr << "Added string literal '" << string_literal_buffer_.substr(0, string_literal_buffer_.size() - 1)
				  << "' at offset " << offset << " with symbol " << symbol_name << std::endl;

	return symbol_name;
}

// Add a global variable with raw initialization data
void ObjectFileWriter::add_global_variable_data(std::string_view var_name, size_t size_in_bytes,
												bool is_initialized, std::span<const char> init_data, bool is_rodata,
												bool is_selectany) {
	if (is_selectany) {
		// MSVC __declspec(selectany): each definition lives in its own COMDAT section
		// (e.g. .data$_Avx2WmemEnabledWeakValue) with IMAGE_COMDAT_SELECT_ANY so the
		// linker may pick any duplicate from this TU or from the CRT.
		const char* section_prefix = is_rodata ? ".rdata$" : (is_initialized ? ".data$" : ".bss$");
		std::string section_name = std::string(section_prefix) + std::string(var_name);
		int32_t flags = IMAGE_SCN_LNK_COMDAT | IMAGE_SCN_ALIGN_4BYTES;
		if (is_rodata) {
			flags |= IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA;
		} else if (is_initialized) {
			flags |= IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA;
		} else {
			flags |= IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_UNINITIALIZED_DATA;
		}

		COFFI::section* section = coffi_.add_section(section_name);
		section->set_flags(flags);

		if ((is_initialized || is_rodata) && !init_data.empty()) {
			section->append_data(init_data.data(), init_data.size());
		} else {
			std::vector<char> zero_data(size_in_bytes, 0);
			section->append_data(zero_data.data(), zero_data.size());
		}

		auto section_sym = coffi_.add_symbol(section_name);
		section_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		section_sym->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		section_sym->set_section_number(section->get_index() + 1);
		section_sym->set_value(0);

		COFFI::auxiliary_symbol_record_5 aux = {};
		aux.length = static_cast<uint32_t>(size_in_bytes);
		aux.number_of_relocations = 0;
		aux.number_of_linenumbers = 0;
		aux.check_sum = 0;
		aux.number = 0;
		aux.selection = IMAGE_COMDAT_SELECT_ANY;
		COFFI::auxiliary_symbol_record aux_record;
		std::memcpy(aux_record.value, &aux, sizeof(aux_record.value));
		section_sym->get_auxiliary_symbols().push_back(aux_record);
		section_sym->set_aux_symbols_number(1);

		auto symbol = coffi_.add_symbol(var_name);
		symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol->set_section_number(section->get_index() + 1);
		symbol->set_value(0);

		if (g_enable_debug_output)
			std::cerr << "Added selectany COMDAT global '" << var_name << "' in section '"
					  << section_name << "' (size: " << size_in_bytes << " bytes)" << std::endl;
		return;
	}

	SectionType section_type = is_rodata ? SectionType::RDATA : (is_initialized ? SectionType::DATA : SectionType::BSS);
	auto section = coffi_.get_sections()[sectiontype_to_index[section_type]];
	uint32_t offset = static_cast<uint32_t>(section->get_data_size());

	if (g_enable_debug_output)
		std::cerr << "DEBUG: add_global_variable_data - var_name=" << var_name
				  << " size=" << size_in_bytes << " is_initialized=" << is_initialized << " is_rodata=" << is_rodata << "\n";

	if ((is_initialized || is_rodata) && !init_data.empty()) {
		// Add initialized data to .data or .rdata section
		add_data(init_data, section_type);
	} else {
		// For .bss or uninitialized, use zero-filled data
		std::vector<char> zero_data(size_in_bytes, 0);
		add_data(zero_data, (is_initialized || is_rodata) ? section_type : SectionType::BSS);
	}

	// Add a symbol for this global variable
	auto symbol = coffi_.add_symbol(var_name);
	symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
	symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);	 // Global variables are external
	symbol->set_section_number(section->get_index() + 1);
	symbol->set_value(offset);

	if (g_enable_debug_output)
		std::cerr << "Added global variable '" << var_name << "' at offset " << offset
				  << " in " << (is_rodata ? ".rdata" : (is_initialized ? ".data" : ".bss")) << " section (size: " << size_in_bytes << " bytes)" << std::endl;
}

// Add a vtable to .rdata section with RTTI support
// vtable_symbol: mangled vtable symbol name (e.g., "??_7Base@@6B@")
// function_symbols: span of mangled function names in vtable order
// class_name: name of the class for RTTI
// base_class_names: span of base class names for RTTI (legacy)
// base_class_info: detailed base class information for proper RTTI
void ObjectFileWriter::add_vtable(std::string_view vtable_symbol, std::span<const std::string_view> function_symbols,
								  std::string_view class_name, std::span<const std::string_view> base_class_names,
								  std::span<const BaseClassDescriptorInfo> base_class_info,
								  std::span<const int64_t> virtual_base_offsets,
								  [[maybe_unused]] const RTTITypeInfo* rtti_info,
								  [[maybe_unused]] TypeIndex subobject_type_index,
								  [[maybe_unused]] int64_t offset_to_top) {
	VagueLinkageRdataGroup vtable_group;
	struct NamedReloc {
		uint32_t offset = 0;
		std::string symbol_name;
		uint32_t type = 0;
	};
	std::vector<NamedReloc> named_relocs;
	const bool is_secondary_vtable = offset_to_top != 0;

	if (g_enable_debug_output)
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

	// MSVC RTTI uses .?AV for class and .?AU for struct names.
	std::string mangled_class_name = buildMsvcTypeDescriptorName(class_name, subobject_type_index);

	// ??_R0 - Type Descriptor (16 bytes header + mangled name)
	std::string type_desc_symbol = get_or_create_type_descriptor(class_name, subobject_type_index);
	auto addDeferredRelocationByName = [&](uint32_t virtual_address, std::string symbol_name, uint32_t type) {
		named_relocs.push_back({virtual_address, std::move(symbol_name), type});
	};

	std::string chd_symbol = "??_R3" + mangled_class_name + "8";

	if (!is_secondary_vtable) {
		std::vector<uint32_t> bcd_offsets;
		std::vector<std::string> bcd_symbol_names;

		// ??_R1 - Base Class Descriptors (one for self + one per base)
		// Self descriptor
		uint32_t self_bcd_offset = vtable_group.size();
		std::string self_bcd_symbol = "??_R1" + mangled_class_name + "8";  // "8" suffix for self
		std::vector<char> self_bcd_data;
		self_bcd_data.reserve(28);  // 7 image-relative / scalar DWORDs
		// type_descriptor image-relative pointer (4 bytes) - relocation added below
		ObjectFileCommon::appendZeros(self_bcd_data, 4);
		// num_contained_bases (4 bytes)
		ObjectFileCommon::appendLE(self_bcd_data, static_cast<uint32_t>(base_class_info.size()));
		// mdisp (4 bytes) - 0 for self
		ObjectFileCommon::appendLE(self_bcd_data, uint32_t(0));
		// pdisp (4 bytes) - -1 for non-virtual
		ObjectFileCommon::appendLE(self_bcd_data, uint32_t(0xFFFFFFFF));
		// vdisp (4 bytes) - 0
		ObjectFileCommon::appendLE(self_bcd_data, uint32_t(0));
		// attributes (4 bytes) - include the pClassDescriptor field
		ObjectFileCommon::appendLE(self_bcd_data, uint32_t(0x40));
		// class hierarchy descriptor image-relative pointer (4 bytes) - relocation added later
		ObjectFileCommon::appendZeros(self_bcd_data, 4);
		vtable_group.append(self_bcd_data);
		vtable_group.addExternalSymbol(self_bcd_symbol, self_bcd_offset);
		addDeferredRelocationByName(self_bcd_offset, type_desc_symbol, IMAGE_REL_AMD64_ADDR32NB);
		bcd_offsets.push_back(self_bcd_offset);
			bcd_symbol_names.push_back(self_bcd_symbol);

		// Base class descriptors
		for (size_t i = 0; i < base_class_info.size(); ++i) {
			const auto& bci = base_class_info[i];
			std::string base_mangled = buildMsvcTypeDescriptorName(bci.name, {});
			std::string base_type_desc_symbol = get_or_create_type_descriptor(bci.name);

			uint32_t base_bcd_offset = vtable_group.size();
			std::string_view base_bcd_symbol_sv = StringBuilder()
													  .append("??_R1"sv)
													  .append(mangled_class_name)
													  .append("0"sv)
													  .append(base_mangled)
													  .append("_"sv)
													  .append(static_cast<uint64_t>(bcd_symbol_names.size()))
													  .commit();
			std::string base_bcd_symbol(base_bcd_symbol_sv);
			std::vector<char> base_bcd_data;
			// type_descriptor image-relative pointer (4 bytes) - will add relocation
			ObjectFileCommon::appendZeros(base_bcd_data, 4);
			// num_contained_bases (4 bytes) - actual value from base class info
			ObjectFileCommon::appendLE(base_bcd_data, bci.num_contained_bases);
			// mdisp (4 bytes) - offset of base in derived class
			ObjectFileCommon::appendLE(base_bcd_data, bci.offset);
			// pdisp (4 bytes) - vbtable displacement
			// -1 for non-virtual bases (not applicable)
			// 0+ for virtual bases (offset into vbtable)
			ObjectFileCommon::appendLE(base_bcd_data, bci.is_virtual ? 0 : -1);
			// vdisp (4 bytes) - displacement inside vbtable (0 for simplicity)
			ObjectFileCommon::appendLE(base_bcd_data, uint32_t(0));
			// attributes (4 bytes) - flags
			// Bit 0: virtual base (1 if virtual, 0 if non-virtual)
			// Bit 6: pClassDescriptor field is present
			ObjectFileCommon::appendLE(base_bcd_data, (bci.is_virtual ? 1u : 0u) | 0x40u);
			// class hierarchy descriptor image-relative pointer (4 bytes) - relocation added later
			ObjectFileCommon::appendZeros(base_bcd_data, 4);
			vtable_group.append(base_bcd_data);
			vtable_group.addExternalSymbol(base_bcd_symbol, base_bcd_offset);
			addDeferredRelocationByName(base_bcd_offset, base_type_desc_symbol, IMAGE_REL_AMD64_ADDR32NB);
			bcd_offsets.push_back(base_bcd_offset);
			bcd_symbol_names.push_back(base_bcd_symbol);
		}

		// ??_R2 - Base Class Array (pointers to all BCDs)
		uint32_t bca_offset = vtable_group.size();
		std::string bca_symbol = "??_R2" + mangled_class_name + "8";
		std::vector<char> bca_data(bcd_offsets.size() * 4, 0);
		vtable_group.append(bca_data);
		vtable_group.addExternalSymbol(bca_symbol, bca_offset);
		for (size_t i = 0; i < bcd_symbol_names.size(); ++i) {
			addDeferredRelocationByName(bca_offset + static_cast<uint32_t>(i * 4), bcd_symbol_names[i], IMAGE_REL_AMD64_ADDR32NB);
		}

		// ??_R3 - Class Hierarchy Descriptor
		uint32_t chd_offset = vtable_group.size();
		std::vector<char> chd_data;
		// signature (4 bytes) - 0
		ObjectFileCommon::appendLE(chd_data, uint32_t(0));
		// attributes (4 bytes) - inheritance model flags
		ObjectFileCommon::appendLE(chd_data, getMsvcClassHierarchyAttributes(class_name));
		// num_base_classes (4 bytes) - total including self
		ObjectFileCommon::appendLE(chd_data, static_cast<uint32_t>(bcd_offsets.size()));
		// base_class_array image-relative pointer (4 bytes) - will add relocation
		ObjectFileCommon::appendZeros(chd_data, 4);
		vtable_group.append(chd_data);
		vtable_group.addExternalSymbol(chd_symbol, chd_offset);
		addDeferredRelocationByName(chd_offset + 12, bca_symbol, IMAGE_REL_AMD64_ADDR32NB);
		// Each BCD also stores an image-relative pointer back to the CHD.
		for (uint32_t bcd_offset : bcd_offsets) {
			addDeferredRelocationByName(bcd_offset + 24, chd_symbol, IMAGE_REL_AMD64_ADDR32NB);
		}
	}

	// ??_R4 - Complete Object Locator
	uint32_t col_offset = vtable_group.size();
	std::string col_symbol = is_secondary_vtable
		? (std::string(vtable_symbol) + "$col")
		: ("??_R4" + mangled_class_name + "6B@");
	std::vector<char> col_data;
	// signature (4 bytes) - 1 for 64-bit
	ObjectFileCommon::appendLE(col_data, uint32_t(1));
	// offset (4 bytes) - offset of this vtable within the complete object
	ObjectFileCommon::appendLE(col_data, static_cast<uint32_t>(-offset_to_top));
	// cd_offset (4 bytes) - 0
	ObjectFileCommon::appendLE(col_data, uint32_t(0));
	// type_descriptor image-relative pointer (4 bytes) - relocation added at offset+12
	ObjectFileCommon::appendZeros(col_data, 4);
	// hierarchy image-relative pointer (4 bytes) - relocation added at offset+16
	ObjectFileCommon::appendZeros(col_data, 4);
	// self image-relative pointer (4 bytes) - relocation added at offset+20
	ObjectFileCommon::appendZeros(col_data, 4);
	vtable_group.append(col_data);
	vtable_group.addExternalSymbol(col_symbol, col_offset);
	addDeferredRelocationByName(col_offset + 12, type_desc_symbol, IMAGE_REL_AMD64_ADDR32NB);
	addDeferredRelocationByName(col_offset + 16, chd_symbol, IMAGE_REL_AMD64_ADDR32NB);
	addDeferredRelocationByName(col_offset + 20, col_symbol, IMAGE_REL_AMD64_ADDR32NB);

	// Layout: [vbase offsets in reverse order][COL pointer][function pointers...]
	// The vptr points at the first function. Keeping the entries in reverse
	// order makes virtual-base index zero the closest entry, matching the
	// Itanium prefix convention and the backend's vtable[-(2+i)] lookup.
	uint32_t vtable_offset = vtable_group.size();
	size_t vtable_size = (virtual_base_offsets.size() + 1 + function_symbols.size()) * 8;
	std::vector<char> vtable_data(vtable_size, 0);
	for (size_t i = 0; i < virtual_base_offsets.size(); ++i) {
		const int64_t offset = virtual_base_offsets[virtual_base_offsets.size() - 1 - i];
		std::memcpy(vtable_data.data() + i * sizeof(int64_t), &offset, sizeof(offset));
	}
	vtable_group.append(vtable_data);

	// COL (Complete Object Locator) pointer at vtable[0] (before actual vtable)
	uint32_t col_reloc_offset = vtable_offset + static_cast<uint32_t>(virtual_base_offsets.size() * 8);
	named_relocs.push_back({col_reloc_offset, col_symbol, IMAGE_REL_AMD64_ADDR64});

	// Vtable symbol points to the first virtual function, AFTER COL
	uint32_t vtable_symbol_offset = vtable_offset + static_cast<uint32_t>((virtual_base_offsets.size() + 1) * 8);
	vtable_group.addExternalSymbol(std::string(vtable_symbol), vtable_symbol_offset);
	for (size_t i = 0; i < function_symbols.size(); ++i) {
		if (function_symbols[i].empty()) {
			// Skip empty entries (pure virtual functions might be empty initially)
			continue;
		}
		uint32_t reloc_offset = vtable_symbol_offset + static_cast<uint32_t>(i * 8);
		addDeferredRelocationByName(reloc_offset, std::string(function_symbols[i]), IMAGE_REL_AMD64_ADDR64);
	}

	COFFI::section* vtable_section = emitComdatSection(
		".rdata$", inline_comdat_rdata_section_counter_,
		IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_8BYTES,
		vtable_group.bytes, {}, 0, IMAGE_COMDAT_SELECT_ANY, vtable_symbol, false, vtable_symbol_offset);
	for (const VagueLinkageRdataGroup::NamedOffset& external_symbol : vtable_group.external_symbols) {
		if (external_symbol.name != vtable_symbol) {
			add_vague_linkage_comdat_rdata_symbol(vtable_section, external_symbol.name, external_symbol.offset);
		}
	}

	std::vector<ComdatReloc> final_relocs;
	final_relocs.reserve(named_relocs.size());
	for (const NamedReloc& named_reloc : named_relocs) {
		final_relocs.push_back({
			named_reloc.offset,
			get_or_create_symbol_index(named_reloc.symbol_name),
			named_reloc.type});
	}
	addComdatSectionRelocations(vtable_section, final_relocs);
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
std::string ObjectFileWriter::get_or_create_builtin_throwinfo(TypeCategory type) {
	if (type != TypeCategory::Int) {
		return std::string();
	}

	const std::string throw_info_symbol = "_TI1H";
	auto* existing_throw_info = coffi_.get_symbol(throw_info_symbol);
	if (existing_throw_info) {
		return throw_info_symbol;
	}

	auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];

	// Ensure RTTI type descriptor for int exists: ??_R0H@8
	// Delegate to the shared emitMsvcTypeDescriptor path so that
	// symbol_index_cache_ is populated — avoids duplicate symbols when
	// both throw(int) and typeid(int) appear in the same TU.
	const std::string type_desc_symbol_name = get_or_create_builtin_type_descriptor(TypeCategory::Int);
	auto* type_desc_symbol = coffi_.get_symbol(type_desc_symbol_name);

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

	if (g_enable_debug_output)
		std::cerr << "Created builtin throw metadata symbol: " << throw_info_symbol << std::endl;
	return throw_info_symbol;
}

// Get or create MSVC ??_R0 Type Descriptor symbol for a class.
// Returns the symbol name (e.g., "??_R0.?AVMyClass@@").
// If the descriptor doesn't exist yet, it will be created in .rdata section.
std::string ObjectFileWriter::get_or_create_type_descriptor(std::string_view class_name) {
	return get_or_create_type_descriptor(class_name, {});
}

// Shared emission path for MSVC Type Descriptors.
// `mangled_type_name` is the value stored at the tail of the descriptor (and
// whose tail — minus the leading category prefix character — appears inside
// the descriptor's symbol name between "??_R0" and "@8"). Examples:
//   class MyClass   -> mangled ".?AVMyClass@@",  symbol "??_R0?AVMyClass@@@8"
//   int             -> mangled ".H",             symbol "??_R0H@8"
//   bool            -> mangled "._N",            symbol "??_R0_N@8"
static std::string emitMsvcTypeDescriptor(ObjectFileWriter& writer,
										  COFFI::coffi& coffi,
										  std::unordered_map<std::string, uint32_t, ObjectFileCommon::StringViewHash, std::equal_to<>>& symbol_cache,
										  std::string_view mangled_type_name) {
	std::string type_desc_symbol = buildMsvcTypeDescriptorSymbol(mangled_type_name);

	// Check if the type descriptor was already emitted
	auto td_cache_it = symbol_cache.find(type_desc_symbol);
	if (td_cache_it != symbol_cache.end()) {
		auto* existing_sym = coffi.get_symbol(td_cache_it->second);
		// Only reuse if the symbol has a definition (section_number > 0)
		if (existing_sym && existing_sym->get_section_number() > 0) {
			if (g_enable_debug_output)
				std::cerr << "Reusing existing ??_R0 Type Descriptor '" << type_desc_symbol << "'" << std::endl;
			return type_desc_symbol;
		}
	}

	std::vector<char> type_desc_data;
	type_desc_data.reserve(16 + mangled_type_name.size() + 1); // 8+8 header + name + null
	// vtable pointer (8 bytes) - null, patched by relocation below
	ObjectFileCommon::appendZeros(type_desc_data, 8);
	// spare pointer (8 bytes) - null
	ObjectFileCommon::appendZeros(type_desc_data, 8);
	// mangled name (null-terminated)
	for (char c : mangled_type_name)
		type_desc_data.push_back(c);
	type_desc_data.push_back(0);

	auto* type_info_vftable = coffi.get_symbol("??_7type_info@@6B@");
	if (!type_info_vftable) {
		type_info_vftable = coffi.add_symbol("??_7type_info@@6B@");
		type_info_vftable->set_value(0);
		type_info_vftable->set_section_number(0);
		type_info_vftable->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		type_info_vftable->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
	}

	std::vector<ObjectFileWriter::ComdatReloc> comdat_relocs;
	comdat_relocs.push_back({0, type_info_vftable->get_index(), IMAGE_REL_AMD64_ADDR64});
	writer.emit_vague_linkage_comdat_rdata(type_desc_symbol, std::span<const char>(type_desc_data),
		std::span<const ObjectFileWriter::ComdatReloc>(comdat_relocs), 0);

	auto* type_desc_sym = coffi.get_symbol(type_desc_symbol);
	if (!type_desc_sym) {
		throw InternalError("COMDAT type descriptor symbol was not created");
	}

	// Update cache
	symbol_cache[type_desc_symbol] = type_desc_sym->get_index();

	if (g_enable_debug_output)
		std::cerr << "Created ??_R0 Type Descriptor '" << type_desc_symbol << "' as COMDAT" << std::endl;

	return type_desc_symbol;
}

std::string ObjectFileWriter::get_or_create_type_descriptor(std::string_view class_name, TypeIndex type_index) {
	std::string mangled_class_name = buildMsvcTypeDescriptorName(class_name, type_index);
	return emitMsvcTypeDescriptor(*this, coffi_, symbol_index_cache_, mangled_class_name);
}

std::string ObjectFileWriter::get_or_create_builtin_type_descriptor(TypeCategory cat) {
	std::string_view code = getMsvcBuiltinTypeCode(cat);
	if (code.empty()) {
		return {};
	}
	// The descriptor body is "." + code (e.g. ".H" for int, "._N" for bool).
	// buildMsvcTypeDescriptorSymbol strips the leading '.', so the resulting
	// symbol name is e.g. "??_R0H@8" / "??_R0_N@8".
	std::string_view mangled_type_name = StringBuilder()
											 .append("."sv)
											 .append(code)
											 .commit();
	return emitMsvcTypeDescriptor(*this, coffi_, symbol_index_cache_, mangled_type_name);
}

// Helper: get or create symbol index for a function name (cached for O(1) repeated lookups)
uint32_t ObjectFileWriter::get_or_create_symbol_index(const std::string& symbol_name) {
	auto findDefinedSymbolIndex = [&](std::string_view name) -> std::optional<uint32_t> {
		auto* defined_symbol = coffi_.get_symbol(name);
		if (defined_symbol != nullptr && defined_symbol->get_section_number() > 0) {
			return defined_symbol->get_index();
		}
		return std::nullopt;
	};

	if (auto defined_index = findDefinedSymbolIndex(symbol_name); defined_index.has_value()) {
		symbol_index_cache_[symbol_name] = *defined_index;
		return *defined_index;
	}

	auto cache_it = symbol_index_cache_.find(symbol_name);
	if (cache_it != symbol_index_cache_.end()) {
		if (g_enable_debug_output)
			std::cerr << "    DEBUG get_or_create_symbol_index: Cache hit for '" << symbol_name
					  << "' at file index " << cache_it->second << std::endl;
		return cache_it->second;
	}

	// Check if symbol already exists in COFFI
	auto symbols = coffi_.get_symbols();
	for (size_t i = 0; i < symbols->size(); ++i) {
		if ((*symbols)[i].get_name() == symbol_name) {
			uint32_t file_index = (*symbols)[i].get_index();
			if ((*symbols)[i].get_section_number() > 0) {
				if (g_enable_debug_output)
					std::cerr << "    DEBUG get_or_create_symbol_index: Found existing defined symbol '" << symbol_name
							  << "' at array index " << i << ", file index " << file_index << std::endl;
				symbol_index_cache_[symbol_name] = file_index;
				return file_index;
			}
		}
	}

	// Symbol doesn't exist, create it as an external reference
	if (g_enable_debug_output)
		std::cerr << "    DEBUG get_or_create_symbol_index: Creating new symbol '" << symbol_name << "'" << std::endl;
	auto symbol = coffi_.add_symbol(symbol_name);
	symbol->set_type(IMAGE_SYM_TYPE_FUNCTION);
	symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
	symbol->set_section_number(0);  // External reference
	symbol->set_value(0);

	uint32_t file_index = symbol->get_index();
	symbol_index_cache_[symbol_name] = file_index;
	if (g_enable_debug_output)
		std::cerr << "    DEBUG get_or_create_symbol_index: Created new symbol at file index " << file_index
				  << " for '" << symbol_name << "'" << std::endl;
	return file_index;
}
