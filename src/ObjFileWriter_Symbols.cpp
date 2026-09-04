#include "ObjFileWriter.h"
#include "Log.h"
#include <cstring>
#include <set>

// ObjFileWriter_Symbols.cpp - Out-of-line method definitions for ObjectFileWriter
// Part of ObjectFileWriter class (unity build)

// Add function signature information for member functions with class name
// Returns the mangled name for the function
std::string ObjectFileWriter::addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type, std::span<const TypeSpecifierNode> parameter_types, std::string_view class_name, Linkage linkage, bool is_variadic) {
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
void ObjectFileWriter::addFunctionSignature([[maybe_unused]] std::string_view name, const TypeSpecifierNode& return_type, std::span<const TypeSpecifierNode> parameter_types, std::string_view class_name, Linkage linkage, bool is_variadic, std::string_view mangled_name, bool is_inline) {
	FunctionSignature sig(return_type, parameter_types);
	sig.class_name = class_name;
	sig.linkage = linkage;
	sig.is_variadic = is_variadic;
	sig.is_inline = is_inline;
	function_signatures_[std::string(mangled_name)] = sig;
}

void ObjectFileWriter::add_function_symbol(std::string_view mangled_name, uint32_t section_offset, uint32_t stack_space, Linkage linkage, bool is_inline) {
	if (g_enable_debug_output)
		std::cerr << "Adding function symbol: " << mangled_name << " at offset " << section_offset << " with linkage " << static_cast<int>(linkage) << std::endl;
	if (native_comdat_function_active_) {
		throw InternalError("Native COMDAT function was not finished before the next symbol");
	}
	pending_functions_.push_back({std::string(mangled_name), section_offset, 0, is_inline, false});
	if (!is_inline) {
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		auto symbol_func = coffi_.add_symbol(mangled_name);
		symbol_func->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol_func->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol_func->set_section_number(section_text->get_index() + 1);
		symbol_func->set_value(section_offset);
	} else {
		beginNativeComdatFunction(mangled_name, section_offset);
	}

	// Handle dllexport - add export directive
	if (linkage == Linkage::DllExport) {
		auto section_drectve = coffi_.get_sections()[sectiontype_to_index[SectionType::DRECTVE]];
		// Validate mangled_name contains no spaces or directive-terminating characters
		// to prevent linker directive injection via crafted symbol names.
		// Valid MSVC-mangled names only contain [A-Za-z0-9?@_$] - no spaces.
		bool name_is_safe = mangled_name.find(' ') == std::string_view::npos &&
							mangled_name.find('\t') == std::string_view::npos &&
							mangled_name.find('\n') == std::string_view::npos &&
							!mangled_name.empty();
		if (name_is_safe) {
			std::string export_directive = std::string(" /EXPORT:") + std::string(mangled_name);
			if (g_enable_debug_output)
				std::cerr << "Adding export directive: " << export_directive << std::endl;
			section_drectve->append_data(export_directive.c_str(), export_directive.size());
		} else {
			if (g_enable_debug_output)
				std::cerr << "Skipping export directive for invalid mangled name: " << mangled_name << std::endl;
		}
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
	if (g_enable_debug_output)
		std::cerr << "DEBUG: Adding function to debug builder: " << unmangled_name << " (mangled: " << mangled_name << ") at offset " << section_offset << "\n";
	debug_builder_.addFunction(unmangled_name, std::string(mangled_name), section_offset, 0, stack_space);
	if (g_enable_debug_output)
		std::cerr << "DEBUG: Function added to debug builder \n";

	// Exception info is now handled directly in IRConverter finalization logic

	if (g_enable_debug_output)
		std::cerr << "Function symbol added successfully" << std::endl;
}

void ObjectFileWriter::add_static_text_symbol(std::string_view symbol_name, uint32_t section_offset) {
	if (native_comdat_function_active_ && section_offset >= native_comdat_function_start_) {
		native_comdat_static_symbols_.push_back({std::string(symbol_name), section_offset - native_comdat_function_start_});
		return;
	}
	auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
	auto* symbol = coffi_.get_symbol(symbol_name);
	if (!symbol) {
		symbol = coffi_.add_symbol(symbol_name);
	}
	symbol->set_type(IMAGE_SYM_TYPE_FUNCTION);
	symbol->set_storage_class(IMAGE_SYM_CLASS_STATIC);
	symbol->set_section_number(section_text->get_index() + 1);
	symbol->set_value(section_offset);
}

void ObjectFileWriter::add_data(std::span<const uint8_t> data, SectionType section_type) {
	add_data(std::span<const char>(reinterpret_cast<const char*>(data.data()), data.size()), section_type);
}

void ObjectFileWriter::add_data(std::span<const char> data, SectionType section_type) {
	if (section_type == SectionType::XDATA && native_comdat_xdata_section_ != nullptr) {
		if (!data.empty()) {
			native_comdat_xdata_section_->append_data(data.data(), static_cast<uint32_t>(data.size()));
		}
		return;
	}
	if (section_type == SectionType::PDATA && native_comdat_pdata_section_ != nullptr) {
		if (!data.empty()) {
			native_comdat_pdata_section_->append_data(data.data(), static_cast<uint32_t>(data.size()));
		}
		return;
	}
	int section_index = sectiontype_to_index[section_type];
	if (g_enable_debug_output)
		std::cerr << "Adding " << data.size() << " bytes to section " << static_cast<int>(section_type) << " (index=" << section_index << ")";
	auto section = coffi_.get_sections()[section_index];
	uint32_t size_before = section->get_data_size();
	if (g_enable_debug_output)
		std::cerr << " (current size: " << size_before << ")" << std::endl;
	if (section_type == SectionType::TEXT) {
		if (g_enable_debug_output)
			std::cerr << "Machine code bytes (" << data.size() << " total): ";
		for (size_t i = 0; i < data.size(); ++i) {
			if (g_enable_debug_output)
				std::cerr << std::hex << std::setfill('0') << std::setw(2) << (static_cast<unsigned char>(data[i]) & 0xFF) << " ";
		}
		if (g_enable_debug_output)
			std::cerr << std::dec << std::endl;
	}
	section->append_data(data.data(), data.size());
	uint32_t size_after = section->get_data_size();
	uint32_t size_increase = size_after - size_before;
	if (g_enable_debug_output)
		std::cerr << "DEBUG: Section " << section_index << " size after append: " << size_after
				  << " (increased by " << size_increase << ", expected " << data.size() << ")" << std::endl;
	if (size_increase != data.size()) {
		if (g_enable_debug_output)
			std::cerr << "WARNING: Size increase mismatch! Expected " << data.size() << " but got " << size_increase << std::endl;
	}
}

void ObjectFileWriter::add_relocation(uint64_t offset, std::string_view symbol_name) {
	add_relocation(offset, symbol_name, IMAGE_REL_AMD64_REL32);
}

void ObjectFileWriter::add_relocation(uint64_t offset, std::string_view symbol_name, uint32_t relocation_type) {
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
		symbol->set_type(0x20);	// 0x20 = function type
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
	}

	if (shouldBufferNativeTextReloc(offset)) {
		native_comdat_text_relocs_.push_back({
			static_cast<uint32_t>(offset - native_comdat_function_start_),
			symbol_str,
			relocation_type});
		return;
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
void ObjectFileWriter::add_text_relocation(uint64_t offset, const std::string& symbol_name, uint32_t relocation_type, [[maybe_unused]] int64_t addend) {
	// For COFF format, addend is not used (it's a REL format, not RELA)
	// The addend is encoded in the instruction itself
	// Look up the symbol (could be a global variable, function, etc.)
	auto* symbol = coffi_.get_symbol(symbol_name);
	if (!symbol) {
		// Symbol not found - add it as an external symbol (for globals referenced
		// from text section but defined in another object file, e.g. extern variables).
		symbol = coffi_.add_symbol(symbol_name);
		symbol->set_value(0);
		symbol->set_section_number(0);  // 0 = undefined/external symbol
		symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);  // unknown type — use null (data safe)
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		if (g_enable_debug_output)
			std::cerr << "Created external symbol for text relocation: " << symbol_name << std::endl;
	}

	if (shouldBufferNativeTextReloc(offset)) {
		native_comdat_text_relocs_.push_back({
			static_cast<uint32_t>(offset - native_comdat_function_start_),
			symbol_name,
			relocation_type});
		return;
	}

	auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
	COFFI::rel_entry_generic relocation;
	relocation.virtual_address = offset;
	relocation.symbol_table_index = symbol->get_index();
	relocation.type = relocation_type;
	section_text->add_relocation_entry(&relocation);

	if (g_enable_debug_output)
		std::cerr << "Added text relocation at offset " << offset << " for symbol " << symbol_name
				  << " type: 0x" << std::hex << relocation_type << std::dec << std::endl;
}

void ObjectFileWriter::add_data_relocation(std::string_view var_name, std::string_view target_name) {
	// Find the variable's symbol to get its offset in the containing data section.
	auto* var_symbol = coffi_.get_symbol(var_name);
	if (!var_symbol)
		return;
	if (var_symbol->get_section_number() <= 0) {
		if (g_enable_debug_output)
			std::cerr << "Skipping data relocation for " << var_name
					  << ": invalid section number " << var_symbol->get_section_number() << std::endl;
		return;
	}

	uint32_t var_offset = var_symbol->get_value();

	// Get or create the target symbol
	auto* target_symbol = coffi_.get_symbol(target_name);
	if (!target_symbol) {
		target_symbol = coffi_.add_symbol(target_name);
		target_symbol->set_value(0);
		target_symbol->set_section_number(0);
		target_symbol->set_type(0);
		target_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
	}

	auto section_index = static_cast<size_t>(var_symbol->get_section_number() - 1);
	if (section_index >= coffi_.get_sections().get_count()) {
		if (g_enable_debug_output)
			std::cerr << "Skipping data relocation for " << var_name
					  << ": section index " << section_index
					  << " is out of bounds" << std::endl;
		return;
	}
	auto data_section = coffi_.get_sections()[section_index];

	COFFI::rel_entry_generic reloc;
	reloc.virtual_address = var_offset;
	reloc.symbol_table_index = target_symbol->get_index();
	reloc.type = IMAGE_REL_AMD64_ADDR64;
	data_section->add_relocation_entry(&reloc);

	if (g_enable_debug_output)
		std::cerr << "Added data relocation: " << var_name << " -> " << target_name
				  << " at section #" << var_symbol->get_section_number() << " offset " << var_offset << std::endl;
}

void ObjectFileWriter::add_pdata_relocations(uint32_t pdata_offset, std::string_view mangled_name, [[maybe_unused]] uint32_t xdata_offset) {
	if (g_enable_debug_output)
		std::cerr << "Adding PDATA relocations for function: " << mangled_name << " at pdata offset " << pdata_offset << std::endl;

	// Use the unwind text section symbol (value=0) for BeginAddress/EndAddress.
	// The pdata data already contains section-relative offsets as addends, so:
	//   result = text_RVA + 0 + addend = text_RVA + addend = correct
	// Using the function symbol would double-count: text_RVA + func_start + func_start
	COFFI::symbol* text_symbol = unwindTextSymbol();
	if (!text_symbol) {
		throw std::runtime_error("Text section symbol not found");
	}

	COFFI::symbol* xdata_symbol = unwindXdataSymbol();
	if (!xdata_symbol) {
		throw std::runtime_error("XDATA section symbol not found");
	}

	COFFI::section* pdata_section = unwindPdataSection();

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

	if (g_enable_debug_output)
		std::cerr << "Added 3 PDATA relocations for function " << mangled_name << std::endl;
}

void ObjectFileWriter::add_xdata_relocation(uint32_t xdata_offset, std::string_view handler_name) {
	if (g_enable_debug_output)
		std::cerr << "Adding XDATA relocation at offset " << xdata_offset << " for handler: " << handler_name << std::endl;

	// Get or create the exception handler symbol
	auto* handler_symbol = coffi_.get_symbol(handler_name);
	if (!handler_symbol) {
		// Add external symbol for the C++ exception handler
		handler_symbol = coffi_.add_symbol(handler_name);
		handler_symbol->set_value(0);
		handler_symbol->set_section_number(0);  // 0 = undefined/external symbol
		handler_symbol->set_type(0x20);	// 0x20 = function type
		handler_symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		if (g_enable_debug_output)
			std::cerr << "Created external symbol for exception handler: " << handler_name << std::endl;
	}

	COFFI::section* xdata_section = unwindXdataSection();

	// Add relocation for the exception handler RVA in XDATA
	COFFI::rel_entry_generic reloc;
	reloc.virtual_address = xdata_offset;
	reloc.symbol_table_index = handler_symbol->get_index();
	reloc.type = IMAGE_REL_AMD64_ADDR32NB;  // 32-bit address without base
	xdata_section->add_relocation_entry(&reloc);

	if (g_enable_debug_output)
		std::cerr << "Added XDATA relocation for handler " << handler_name << " at offset " << xdata_offset << std::endl;
}

void ObjectFileWriter::add_rdata_relocation(uint32_t rdata_offset, std::string_view symbol_name, uint32_t relocation_type) {
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
std::string ObjectFileWriter::mangleTypeName(const std::string& type_name) const {
	if (auto cat = typeCategoryFromName(type_name)) {
		std::string_view code = msvcBuiltinTypeCode(*cat);
		if (!code.empty()) {
			return std::string(StringBuilder().append(code).append("@"sv).commit());
		}
	}

	// For class/struct types, use the name directly with @ suffix
	// This is a simplified approach - full MSVC would encode nested namespaces, templates, etc.
	// Format: V<name>@@ for struct/class
	return "V" + type_name + "@@";
}

// Returns (type descriptor symbol name, type descriptor runtime name string)
// for use in MSVC exception metadata.
std::pair<std::string, std::string> ObjectFileWriter::getMsvcTypeDescriptorInfo(const std::string& type_name) const {
	if (auto cat = typeCategoryFromName(type_name)) {
		std::string_view code = msvcBuiltinTypeCode(*cat);
		if (!code.empty()) {
			std::string symbol(StringBuilder().append("??_R0"sv).append(code).append("@8"sv).commit());
			std::string runtime(StringBuilder().append("."sv).append(code).commit());
			return {std::move(symbol), std::move(runtime)};
		}
	}

	std::string mangled_type_name = mangleTypeName(type_name);
	return {"??_R0" + mangled_type_name, mangled_type_name};
}

std::string ObjectFileWriter::get_or_create_exception_throw_info(const std::string& type_name, size_t type_size, bool is_simple_type, std::string_view destructor_symbol, const StructTypeInfo* thrown_struct_info) {
	if (type_name.empty() || type_name == "void") {
		return std::string();
	}

	if (auto builtin_category = typeCategoryFromName(type_name)) {
		if (*builtin_category == TypeCategory::Void) {
			return std::string();
		}
		return get_or_create_builtin_throwinfo(*builtin_category);
	}

	auto cached_it = throw_info_symbols_.find(type_name);
	if (cached_it != throw_info_symbols_.end()) {
		return cached_it->second;
	}

	struct CatchableTypeEntry {
		std::string catch_type_name;
		uint32_t properties;
		uint32_t mdisp;
		int32_t pdisp;
		int32_t vdisp;
		uint32_t size_or_offset;
	};

	std::vector<CatchableTypeEntry> catchable_types;
	uint32_t throw_size = static_cast<uint32_t>(type_size);
	if (throw_size == 0 && thrown_struct_info && thrown_struct_info->sizeInBytes().is_set()) {
		throw_size = static_cast<uint32_t>(toSizeT(thrown_struct_info->sizeInBytes()));
	}
	if (throw_size == 0) {
		throw_size = 1;
	}
	const uint32_t CT_IsSimpleType = 0x00000001u;
	const uint32_t CT_HasVirtualBase = 0x00000004u;

	auto add_catchable_type = [&](std::string_view catch_type_name, uint32_t properties, uint32_t mdisp, int32_t pdisp, int32_t vdisp, uint32_t size_or_offset) {
		for (const auto& existing : catchable_types) {
			if (existing.catch_type_name == catch_type_name && existing.mdisp == mdisp && existing.pdisp == pdisp && existing.vdisp == vdisp) {
				return;
			}
		}

		catchable_types.push_back({
			std::string(catch_type_name),
			properties,
			mdisp,
			pdisp,
			vdisp,
			size_or_offset,
		});
	};

	uint32_t thrown_properties = is_simple_type ? CT_IsSimpleType : 0u;
	if (thrown_struct_info && !thrown_struct_info->virtual_bases.empty()) {
		thrown_properties |= CT_HasVirtualBase;
	}
	add_catchable_type(type_name, thrown_properties, 0, -1, 0, throw_size);

	if (thrown_struct_info && !is_simple_type) {
		auto find_complete_object_virtual_base_offset = [&](TypeIndex base_type_index) -> std::optional<uint32_t> {
			for (const auto& virtual_base : thrown_struct_info->virtual_bases) {
				if (virtual_base.type_index == base_type_index) {
					return static_cast<uint32_t>(virtual_base.offset);
				}
			}
			return std::nullopt;
		};

		std::set<std::pair<const StructTypeInfo*, uint32_t>> visited_public_base_paths;
		std::function<void(const StructTypeInfo*, uint32_t)> collect_public_bases = [&](const StructTypeInfo* current_struct_info, uint32_t current_offset) {
			if (!current_struct_info) {
				return;
			}

			if (!visited_public_base_paths.insert({current_struct_info, current_offset}).second) {
				return;
			}

			for (const auto& base : current_struct_info->base_classes) {
				if (base.is_deferred || base.access != AccessSpecifier::Public) {
					continue;
				}

				const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index);
				const StructTypeInfo* base_struct_info = base_type_info ? base_type_info->getStructInfo() : nullptr;
				if (!base_struct_info) {
					continue;
				}

				uint32_t base_offset = current_offset + static_cast<uint32_t>(base.offset);
				if (base.is_virtual) {
					if (auto complete_object_offset = find_complete_object_virtual_base_offset(base.type_index)) {
						base_offset = *complete_object_offset;
					}
				}
				uint32_t base_properties = base.is_virtual || !base_struct_info->virtual_bases.empty() ? CT_HasVirtualBase : 0u;
				uint32_t base_size = static_cast<uint32_t>(base_struct_info->sizeInBytes().is_set() ? toSizeT(base_struct_info->sizeInBytes()) : throw_size);

				add_catchable_type(StringTable::getStringView(base_type_info->name()), base_properties, base_offset, -1, 0, base_size);

				// Transitive catches through deeply nested virtual bases need their own
				// CatchableType entries as well. Recurse through both virtual and
				// non-virtual public bases, while guarding against repeated paths.
				collect_public_bases(base_struct_info, base_offset);
			}
		};

		collect_public_bases(thrown_struct_info, 0);
	}

	const std::string mangled_type_name = mangleTypeName(type_name);
	const std::string catchable_array_symbol = "$flash$cta$" + mangled_type_name;
	const std::string throw_info_symbol = "$flash$ti$" + mangled_type_name;
	std::vector<std::string> catchable_type_symbols;
	catchable_type_symbols.reserve(catchable_types.size());

	for (size_t i = 0; i < catchable_types.size(); ++i) {
		const auto& catchable_type = catchable_types[i];
		const std::string catchable_type_symbol = "$flash$ct$" + mangled_type_name + "$" + std::to_string(i);
		catchable_type_symbols.push_back(catchable_type_symbol);

		auto* catchable_type_sym = coffi_.get_symbol(catchable_type_symbol);
		if (catchable_type_sym == nullptr || catchable_type_sym->get_section_number() <= 0) {
			const std::string type_desc_symbol = get_or_create_type_descriptor_for_spelling(catchable_type.catch_type_name);
			// MSVC x64 CatchableType COMDATs are 0x24 bytes (28-byte relative
			// field prefix plus 8 bytes of trailing padding). The CRT reads
			// copyFunction as a 4-byte RVA at +0x18; over-reading a 0x1C
			// section can treat the next COMDAT as a copy constructor.
			std::vector<char> catchable_type_data;
			catchable_type_data.reserve(0x24);
			ObjectFileCommon::appendLE(catchable_type_data, catchable_type.properties);
			ObjectFileCommon::appendLE(catchable_type_data, uint32_t(0));
			ObjectFileCommon::appendLE(catchable_type_data, catchable_type.mdisp);
			ObjectFileCommon::appendLE(catchable_type_data, static_cast<uint32_t>(catchable_type.pdisp));
			ObjectFileCommon::appendLE(catchable_type_data, static_cast<uint32_t>(catchable_type.vdisp));
			ObjectFileCommon::appendLE(catchable_type_data, catchable_type.size_or_offset);
			ObjectFileCommon::appendLE(catchable_type_data, uint32_t(0));
			ObjectFileCommon::appendZeros(catchable_type_data, 8);
			std::vector<NamedComdatReloc> catchable_type_relocs{
				{4, type_desc_symbol, IMAGE_REL_AMD64_ADDR32NB}};
			emitVagueLinkageComdatRdataNamed(catchable_type_symbol, catchable_type_data, catchable_type_relocs, 0);
		}
	}

	auto* catchable_array_sym = coffi_.get_symbol(catchable_array_symbol);
	if (catchable_array_sym == nullptr || catchable_array_sym->get_section_number() <= 0) {
		std::vector<char> catchable_array_data;
		const size_t catchable_array_bytes = 4 + catchable_type_symbols.size() * 4;
		const size_t catchable_array_padded = catchable_array_bytes < 0x0C ? 0x0C : catchable_array_bytes;
		catchable_array_data.reserve(catchable_array_padded);
		ObjectFileCommon::appendLE(catchable_array_data, static_cast<uint32_t>(catchable_type_symbols.size()));
		for (size_t i = 0; i < catchable_type_symbols.size(); ++i) {
			ObjectFileCommon::appendLE(catchable_array_data, uint32_t(0));
		}
		if (catchable_array_data.size() < catchable_array_padded) {
			ObjectFileCommon::appendZeros(catchable_array_data, catchable_array_padded - catchable_array_data.size());
		}
		std::vector<NamedComdatReloc> catchable_array_relocs;
		catchable_array_relocs.reserve(catchable_type_symbols.size());
		for (size_t i = 0; i < catchable_type_symbols.size(); ++i) {
			catchable_array_relocs.push_back({
				4 + static_cast<uint32_t>(i * 4),
				catchable_type_symbols[i],
				IMAGE_REL_AMD64_ADDR32NB});
		}
		emitVagueLinkageComdatRdataNamed(catchable_array_symbol, catchable_array_data, catchable_array_relocs, 0);
	}

	auto* throw_info_sym = coffi_.get_symbol(throw_info_symbol);
	if (throw_info_sym == nullptr || throw_info_sym->get_section_number() <= 0) {
		std::vector<char> throw_info_data(0x1C, 0);
		std::vector<NamedComdatReloc> throw_info_relocs;
		if (!destructor_symbol.empty()) {
			// pmfnUnwind should reference a real destructor only when one is required and emitted.
			// For trivial implicit destructors (e.g. throw bad_any_cast{}), forcing a destructor
			// symbol here creates an unnecessary unresolved external during link.
			throw_info_relocs.push_back({4, std::string(destructor_symbol), IMAGE_REL_AMD64_ADDR32NB});
		}
		throw_info_relocs.push_back({12, catchable_array_symbol, IMAGE_REL_AMD64_ADDR32NB});
		emitVagueLinkageComdatRdataNamed(throw_info_symbol, throw_info_data, throw_info_relocs, 0);
	}

	throw_info_symbols_[type_name] = throw_info_symbol;
	return throw_info_symbol;
}

void ObjectFileWriter::add_debug_relocation(uint32_t offset, const std::string& symbol_name, uint32_t relocation_type) {
	if (g_enable_debug_output)
		std::cerr << "Adding debug relocation at offset " << offset << " for symbol: " << symbol_name
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

	if (g_enable_debug_output)
		std::cerr << "Added debug relocation for symbol " << symbol_name << " at offset " << offset
				  << " type: 0x" << std::hex << relocation_type << std::dec << std::endl;
}

// Debug information methods
void ObjectFileWriter::add_source_file(const std::string& filename) {
	debug_builder_.addSourceFile(filename);
}

void ObjectFileWriter::set_current_function_for_debug(const std::string& name, uint32_t file_id) {
	debug_builder_.setCurrentFunction(name, file_id);
}

void ObjectFileWriter::add_line_mapping(uint32_t code_offset, uint32_t line_number) {
	debug_builder_.addLineMapping(code_offset, line_number);
}

void ObjectFileWriter::add_local_variable(const std::string& name, uint32_t type_index, uint16_t flags,
										  std::span<const CodeView::VariableLocation> locations) {
	debug_builder_.addLocalVariable(name, type_index, flags, locations);
}

void ObjectFileWriter::add_function_parameter(const std::string& name, uint32_t type_index, int32_t stack_offset) {
	debug_builder_.addFunctionParameter(name, type_index, stack_offset);
}

void ObjectFileWriter::update_function_length(const std::string_view manged_name, uint32_t code_length) {
	debug_builder_.updateFunctionLength(manged_name, code_length);
	for (auto it = pending_functions_.rbegin(); it != pending_functions_.rend(); ++it) {
		if (it->name == manged_name) {
			it->length = code_length;
			return;
		}
	}
	throw InternalError("Function length was reported for an unknown symbol");
}

void ObjectFileWriter::recordFunctionXdataRange(std::string_view mangled_name, uint32_t offset, uint32_t length) {
	if (length == 0) {
		return;
	}
	FunctionUnwindBundle& bundle = function_unwind_bundles_[std::string(mangled_name)];
	bundle.xdata_ranges.push_back({offset, length});
}

void ObjectFileWriter::recordFunctionPdataEntry(std::string_view mangled_name, const PendingPdataRecord& entry) {
	function_unwind_bundles_[std::string(mangled_name)].pdata_records.push_back(entry);
}

void ObjectFileWriter::retargetRelocations(uint32_t old_symbol_index, uint32_t new_symbol_index) {
	if (old_symbol_index == new_symbol_index) {
		return;
	}
	auto& sections = coffi_.get_sections();
	for (size_t i = 0; i < sections.get_count(); ++i) {
		auto& relocations = sections[i]->get_relocations();
		for (auto& relocation : relocations) {
			if (relocation.get_symbol_table_index() == old_symbol_index) {
				relocation.set_symbol(new_symbol_index);
			}
		}
	}
}

void ObjectFileWriter::neutralizeReplacedUndefSymbol(COFFI::symbol* symbol) {
	std::string_view discarded_name = StringBuilder()
										  .append(".comdat$replaced$"sv)
										  .append(static_cast<uint64_t>(replaced_undef_counter_++))
										  .commit();
	symbol->set_name(discarded_name);
	symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
	symbol->set_storage_class(IMAGE_SYM_CLASS_STATIC);
	symbol->set_section_number(0xFFFF);
	symbol->set_value(0);
}

COFFI::symbol* ObjectFileWriter::findComdatSectionSymbol(COFFI::section* section) {
	const int32_t section_number = static_cast<int32_t>(section->get_index() + 1);
	auto* symbols = coffi_.get_symbols();
	for (size_t i = 0; i < symbols->size(); ++i) {
		COFFI::symbol& section_symbol = (*symbols)[i];
		if (section_symbol.get_section_number() != section_number ||
			section_symbol.get_storage_class() != IMAGE_SYM_CLASS_STATIC ||
			section_symbol.get_aux_symbols_number() != 1) {
			continue;
		}
		return &section_symbol;
	}
	return nullptr;
}

COFFI::section* ObjectFileWriter::emitComdatSection(std::string_view section_name_prefix, uint32_t& section_counter,
	int32_t section_flags, std::span<const char> data, std::span<const ComdatReloc> relocations,
	uint16_t associated_section_number, uint8_t comdat_selection, std::string_view external_symbol_name,
	bool external_is_function, uint32_t external_symbol_value) {
	if (comdat_selection == IMAGE_COMDAT_SELECT_ASSOCIATIVE && associated_section_number == 0) {
		throw InternalError("Associative COMDAT requires a 1-based leader section index");
	}

	// COFFI cannot reliably serialize long section names in object files, so
	// keep this private section name within COFF's eight-byte inline limit.
	std::string section_name = std::string(section_name_prefix) + std::to_string(section_counter++);
	auto* section = coffi_.add_section(section_name);
	section->set_flags(section_flags | IMAGE_SCN_LNK_COMDAT);
	if (!data.empty()) {
		section->append_data(data.data(), data.size());
	}

	auto* section_symbol = coffi_.add_symbol(section_name);
	section_symbol->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
	section_symbol->set_storage_class(IMAGE_SYM_CLASS_STATIC);
	section_symbol->set_section_number(section->get_index() + 1);
	section_symbol->set_value(0);
	COFFI::auxiliary_symbol_record_5 aux = {};
	aux.length = static_cast<uint32_t>(data.size());
	aux.number_of_relocations = static_cast<uint16_t>(relocations.size());
	aux.number_of_linenumbers = 0;
	aux.check_sum = 0;
	// PE/COFF aux format 5: SELECT_ANY leaders use number=0. ASSOCIATIVE
	// sections store the 1-based section index of the COMDAT leader.
	aux.number = (comdat_selection == IMAGE_COMDAT_SELECT_ASSOCIATIVE) ? associated_section_number : 0;
	aux.selection = comdat_selection;
	COFFI::auxiliary_symbol_record aux_record;
	std::memcpy(aux_record.value, &aux, sizeof(aux_record.value));
	section_symbol->get_auxiliary_symbols().push_back(aux_record);
	// COFFI calculates the file-header symbol count before serializing the
	// auxiliary records, so the final symbol must carry this count already.
	section_symbol->set_aux_symbols_number(1);

	for (const ComdatReloc& relocation : relocations) {
		COFFI::rel_entry_generic copied_relocation;
		copied_relocation.virtual_address = relocation.virtual_address;
		copied_relocation.symbol_table_index = relocation.symbol_table_index;
		copied_relocation.type = relocation.type;
		section->add_relocation_entry(&copied_relocation);
	}

	if (comdat_selection != IMAGE_COMDAT_SELECT_ASSOCIATIVE && !external_symbol_name.empty()) {
		defineComdatExternalSymbol(section, external_symbol_name, external_symbol_value, external_is_function, true);
	}
	return section;
}

COFFI::symbol* ObjectFileWriter::defineComdatExternalSymbol(COFFI::section* section, std::string_view symbol_name,
	uint32_t value, bool is_function, bool is_external) {
	const std::string symbol_key(symbol_name);
	auto* existing = coffi_.get_symbol(symbol_key);
	uint32_t replaced_undef_index = 0;
	bool replace_undef = false;
	if (existing != nullptr) {
		if (existing->get_section_number() > 0 && existing->get_storage_class() == IMAGE_SYM_CLASS_EXTERNAL) {
			throw InternalError("COMDAT external symbol already defined");
		}
		if (existing->get_section_number() == 0 && existing->get_storage_class() == IMAGE_SYM_CLASS_EXTERNAL) {
			replaced_undef_index = existing->get_index();
			replace_undef = true;
		}
	}

	// Always append the defined symbol immediately after the current tail so
	// the COMDAT leader is the first non-section symbol for this section.
	// Promoting an earlier UNDEF would leave that symbol before STATIC+aux5.
	// Non-leader symbols stay IMAGE_SYM_CLASS_STATIC: MSVC LINK treats extra
	// EXTERNAL symbols in a SELECT_ANY section as ordinary strong definitions
	// (LNK2005) even though they live in the same COMDAT as the leader.
	COFFI::symbol* symbol = coffi_.add_symbol(symbol_key);
	symbol->set_type(is_function ? IMAGE_SYM_TYPE_FUNCTION : IMAGE_SYM_TYPE_NOT_FUNCTION);
	symbol->set_storage_class(is_external ? IMAGE_SYM_CLASS_EXTERNAL : IMAGE_SYM_CLASS_STATIC);
	symbol->set_section_number(section->get_index() + 1);
	symbol->set_value(value);
	symbol_index_cache_[symbol_key] = symbol->get_index();

	if (replace_undef) {
		retargetRelocations(replaced_undef_index, symbol->get_index());
		COFFI::symbol* replaced = coffi_.get_symbol(replaced_undef_index);
		if (replaced == nullptr) {
			throw InternalError("Replaced UNDEF COMDAT symbol is missing");
		}
		neutralizeReplacedUndefSymbol(replaced);
	}
	return symbol;
}

void ObjectFileWriter::addComdatSectionRelocations(COFFI::section* section, std::span<const ComdatReloc> relocations) {
	for (const ComdatReloc& relocation : relocations) {
		COFFI::rel_entry_generic copied_relocation;
		copied_relocation.virtual_address = relocation.virtual_address;
		copied_relocation.symbol_table_index = relocation.symbol_table_index;
		copied_relocation.type = relocation.type;
		section->add_relocation_entry(&copied_relocation);
	}
	finalizeComdatSectionRelocationCount(section);
}

void ObjectFileWriter::finalizeComdatSectionRelocationCount(COFFI::section* section) {
	const uint16_t relocation_count = static_cast<uint16_t>(section->get_relocations().size());
	COFFI::symbol* section_symbol = findComdatSectionSymbol(section);
	if (section_symbol == nullptr) {
		throw InternalError("COMDAT section symbol was not created");
	}
	auto& aux_symbols = section_symbol->get_auxiliary_symbols();
	if (aux_symbols.empty()) {
		throw InternalError("COMDAT section symbol is missing aux format 5");
	}
	COFFI::auxiliary_symbol_record_5 aux = {};
	std::memcpy(&aux, aux_symbols[0].value, sizeof(aux_symbols[0].value));
	aux.number_of_relocations = relocation_count;
	std::memcpy(aux_symbols[0].value, &aux, sizeof(aux_symbols[0].value));
}

void ObjectFileWriter::emitVagueLinkageComdatRdata(std::string_view external_symbol_name, std::span<const char> data,
	std::span<const ComdatReloc> relocations, uint32_t external_symbol_value) {
	emitComdatSection(".rdata$", inline_comdat_rdata_section_counter_,
		IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_8BYTES,
		data, relocations, 0, IMAGE_COMDAT_SELECT_ANY, external_symbol_name, false, external_symbol_value);
}

void ObjectFileWriter::emitVagueLinkageComdatRdataNamed(std::string_view external_symbol_name, std::span<const char> data,
	std::span<const NamedComdatReloc> named_relocations, uint32_t external_symbol_value) {
	std::vector<ComdatReloc> relocations;
	relocations.reserve(named_relocations.size());
	for (const NamedComdatReloc& named_relocation : named_relocations) {
		relocations.push_back({
			named_relocation.virtual_address,
			get_or_create_symbol_index(named_relocation.symbol_name),
			named_relocation.type});
	}
	emitVagueLinkageComdatRdata(external_symbol_name, data, relocations, external_symbol_value);
}

void ObjectFileWriter::emit_vague_linkage_comdat_rdata(std::string_view external_symbol_name, std::span<const char> data,
	std::span<const ComdatReloc> relocations, uint32_t external_symbol_value) {
	emitVagueLinkageComdatRdata(external_symbol_name, data, relocations, external_symbol_value);
}

void ObjectFileWriter::beginNativeComdatFunction(std::string_view mangled_name, uint32_t unified_offset) {
	native_comdat_function_active_ = true;
	native_comdat_function_start_ = unified_offset;
	native_comdat_function_name_ = std::string(mangled_name);
	native_comdat_text_relocs_.clear();
	native_comdat_static_symbols_.clear();
}

bool ObjectFileWriter::shouldBufferNativeTextReloc(uint64_t offset) {
	return native_comdat_function_active_ && offset >= native_comdat_function_start_;
}

COFFI::section* ObjectFileWriter::unwindXdataSection() {
	if (native_comdat_xdata_section_ != nullptr) {
		return native_comdat_xdata_section_;
	}
	return coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
}

COFFI::section* ObjectFileWriter::unwindPdataSection() {
	if (native_comdat_pdata_section_ != nullptr) {
		return native_comdat_pdata_section_;
	}
	return coffi_.get_sections()[sectiontype_to_index[SectionType::PDATA]];
}

COFFI::symbol* ObjectFileWriter::unwindTextSymbol() {
	if (native_comdat_text_section_ != nullptr) {
		COFFI::symbol* symbol = coffi_.get_symbol(native_comdat_text_symbol_index_);
		if (symbol == nullptr) {
			throw InternalError("Native COMDAT text section symbol is missing");
		}
		return symbol;
	}
	return coffi_.get_symbol(".text");
}

COFFI::symbol* ObjectFileWriter::unwindXdataSymbol() {
	if (native_comdat_xdata_section_ != nullptr) {
		COFFI::symbol* symbol = coffi_.get_symbol(native_comdat_xdata_symbol_index_);
		if (symbol == nullptr) {
			throw InternalError("Native COMDAT xdata section symbol is missing");
		}
		return symbol;
	}
	return coffi_.get_symbol(".xdata");
}

void ObjectFileWriter::finalizeComdatSectionAux(COFFI::section* section) {
	COFFI::symbol* section_symbol = findComdatSectionSymbol(section);
	if (section_symbol == nullptr) {
		throw InternalError("COMDAT section symbol was not created");
	}
	auto& aux_symbols = section_symbol->get_auxiliary_symbols();
	if (aux_symbols.empty()) {
		throw InternalError("COMDAT section symbol is missing aux format 5");
	}
	COFFI::auxiliary_symbol_record_5 aux = {};
	std::memcpy(&aux, aux_symbols[0].value, sizeof(aux_symbols[0].value));
	aux.length = static_cast<uint32_t>(section->get_data_size());
	aux.number_of_relocations = static_cast<uint16_t>(section->get_relocations().size());
	std::memcpy(aux_symbols[0].value, &aux, sizeof(aux_symbols[0].value));
}

void ObjectFileWriter::emitNativeVagueLinkageFunction(std::string_view mangled_name, std::span<const uint8_t> text_bytes) {
	PendingFunctionInfo* pending = nullptr;
	for (auto it = pending_functions_.rbegin(); it != pending_functions_.rend(); ++it) {
		if (it->name == mangled_name) {
			pending = &(*it);
			break;
		}
	}
	if (pending == nullptr) {
		throw InternalError("Native COMDAT emission was requested for an unknown function");
	}
	if (!pending->is_inline) {
		throw InternalError("Native COMDAT emission was requested for a unique function");
	}
	if (pending->natively_emitted) {
		throw InternalError("Native COMDAT function was emitted twice");
	}

	native_comdat_function_active_ = false;

	if (text_bytes.empty()) {
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		auto symbol = coffi_.add_symbol(pending->name);
		symbol->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol->set_section_number(section_text->get_index() + 1);
		symbol->set_value(pending->offset);
		pending->natively_emitted = true;
		native_comdat_text_relocs_.clear();
		native_comdat_static_symbols_.clear();
		native_comdat_function_name_.clear();
		return;
	}

	std::vector<ComdatReloc> text_relocations_out;
	text_relocations_out.reserve(native_comdat_text_relocs_.size());
	for (const PendingNativeTextReloc& reloc : native_comdat_text_relocs_) {
		text_relocations_out.push_back({
			reloc.local_offset,
			get_or_create_symbol_index(reloc.symbol_name),
			reloc.type});
	}

	std::vector<char> text_comdat_data(reinterpret_cast<const char*>(text_bytes.data()),
		reinterpret_cast<const char*>(text_bytes.data() + text_bytes.size()));
	COFFI::section* text_comdat_section = emitComdatSection(
		".text$", inline_comdat_section_counter_,
		IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_CODE | IMAGE_SCN_ALIGN_16BYTES,
		text_comdat_data, text_relocations_out, 0, IMAGE_COMDAT_SELECT_ANY, pending->name, true, 0);
	auto* text_comdat_symbol = findComdatSectionSymbol(text_comdat_section);
	if (!text_comdat_symbol) {
		throw InternalError("COMDAT text section symbol was not created");
	}
	native_comdat_text_section_ = text_comdat_section;
	native_comdat_text_symbol_index_ = text_comdat_symbol->get_index();
	native_comdat_leader_section_number_ = static_cast<uint16_t>(text_comdat_section->get_index() + 1);

	for (const PendingNativeStaticSymbol& static_symbol : native_comdat_static_symbols_) {
		COFFI::symbol* symbol = coffi_.add_symbol(static_symbol.name);
		symbol->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		symbol->set_section_number(native_comdat_leader_section_number_);
		symbol->set_value(static_symbol.local_offset);
	}

	COFFI::section* xdata_comdat_section = emitComdatSection(
		".xdata$", inline_comdat_xdata_section_counter_,
		IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES,
		{}, {}, native_comdat_leader_section_number_, IMAGE_COMDAT_SELECT_ASSOCIATIVE, {}, false, 0);
	auto* xdata_comdat_symbol = findComdatSectionSymbol(xdata_comdat_section);
	if (!xdata_comdat_symbol) {
		throw InternalError("COMDAT xdata section symbol was not created");
	}
	native_comdat_xdata_section_ = xdata_comdat_section;
	native_comdat_xdata_symbol_index_ = xdata_comdat_symbol->get_index();

	COFFI::section* pdata_comdat_section = emitComdatSection(
		".pdata$", inline_comdat_pdata_section_counter_,
		IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES,
		{}, {}, native_comdat_leader_section_number_, IMAGE_COMDAT_SELECT_ASSOCIATIVE, {}, false, 0);
	if (pdata_comdat_section == nullptr) {
		throw InternalError("COMDAT pdata section was not created");
	}
	native_comdat_pdata_section_ = pdata_comdat_section;

	native_comdat_text_relocs_.clear();
	native_comdat_static_symbols_.clear();
	pending->natively_emitted = true;
}

void ObjectFileWriter::finishNativeComdatFunction() {
	if (native_comdat_xdata_section_ != nullptr) {
		finalizeComdatSectionAux(native_comdat_xdata_section_);
	}
	if (native_comdat_pdata_section_ != nullptr) {
		finalizeComdatSectionAux(native_comdat_pdata_section_);
	}
	native_comdat_function_active_ = false;
	native_comdat_function_start_ = 0;
	native_comdat_function_name_.clear();
	native_comdat_text_relocs_.clear();
	native_comdat_static_symbols_.clear();
	native_comdat_text_section_ = nullptr;
	native_comdat_text_symbol_index_ = 0;
	native_comdat_leader_section_number_ = 0;
	native_comdat_xdata_section_ = nullptr;
	native_comdat_xdata_symbol_index_ = 0;
	native_comdat_pdata_section_ = nullptr;
}

void ObjectFileWriter::emitInlineFunctionComdats(std::span<const uint8_t> text_data) {
	(void)text_data;
	for (const PendingFunctionInfo& function : pending_functions_) {
		if (!function.is_inline || function.natively_emitted) {
			continue;
		}
		if (function.length == 0) {
			auto symbol = coffi_.add_symbol(function.name);
			symbol->set_type(IMAGE_SYM_TYPE_FUNCTION);
			symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
			symbol->set_section_number(coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]]->get_index() + 1);
			symbol->set_value(function.offset);
			continue;
		}
		throw InternalError("Vague-linkage function was not natively emitted as COMDAT");
	}
}

void ObjectFileWriter::set_function_debug_range(const std::string_view manged_name, uint32_t prologue_size, uint32_t epilogue_size) {
	debug_builder_.setFunctionDebugRange(manged_name, prologue_size, epilogue_size);
}

void ObjectFileWriter::finalize_current_function() {
	debug_builder_.finalizeCurrentFunction();
}
