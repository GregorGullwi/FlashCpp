#include "ObjFileWriter.h"
#include "Log.h"
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
	pending_functions_.push_back({std::string(mangled_name), section_offset, 0, is_inline});
	if (!is_inline) {
		auto section_text = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
		auto symbol_func = coffi_.add_symbol(mangled_name);
		symbol_func->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol_func->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol_func->set_section_number(section_text->get_index() + 1);
		symbol_func->set_value(section_offset);
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

	auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];

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
	// Simple mapping for built-in types
	// MSVC type codes: H=int, I=unsigned int, D=char, E=unsigned char, etc.
	if (type_name == "int")
		return "H@";
	if (type_name == "unsigned int")
		return "I@";
	if (type_name == "char")
		return "D@";
	if (type_name == "unsigned char")
		return "E@";
	if (type_name == "short")
		return "F@";
	if (type_name == "unsigned short")
		return "G@";
	if (type_name == "long")
		return "J@";
	if (type_name == "unsigned long")
		return "K@";
	if (type_name == "long long")
		return "_J@";
	if (type_name == "unsigned long long")
		return "_K@";
	if (type_name == "float")
		return "M@";
	if (type_name == "double")
		return "N@";
	if (type_name == "long double")
		return "O@";
	if (type_name == "bool")
		return "_N@";
	if (type_name == "void")
		return "X@";

	// For class/struct types, use the name directly with @ suffix
	// This is a simplified approach - full MSVC would encode nested namespaces, templates, etc.
	// Format: V<name>@@ for struct/class
	return "V" + type_name + "@@";
}

// Returns (type descriptor symbol name, type descriptor runtime name string)
// for use in MSVC exception metadata.
std::pair<std::string, std::string> ObjectFileWriter::getMsvcTypeDescriptorInfo(const std::string& type_name) const {
	// Built-ins use canonical MSVC RTTI descriptor naming with @8 suffix
	// and runtime type name strings with leading dot (e.g., ".H" for int).
	if (type_name == "int") {
		return {"??_R0H@8", ".H"};
	}

	// Fallback to existing simplified naming for non-builtins.
	std::string mangled_type_name = mangleTypeName(type_name);
	return {"??_R0" + mangled_type_name, mangled_type_name};
}

std::string ObjectFileWriter::get_or_create_exception_throw_info(const std::string& type_name, size_t type_size, bool is_simple_type, std::string_view destructor_symbol, const StructTypeInfo* thrown_struct_info) {
	if (type_name.empty() || type_name == "void") {
		return std::string();
	}

	// Keep canonical, known-good path for int.
	if (type_name == "int") {
		return get_or_create_builtin_throwinfo(TypeCategory::Int);
	}

	auto cached_it = throw_info_symbols_.find(type_name);
	if (cached_it != throw_info_symbols_.end()) {
		return cached_it->second;
	}

	auto rdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::RDATA]];
	if (!rdata_section) {
		return std::string();
	}

	auto ensure_type_descriptor_symbol = [&](const std::string& catch_type_name) -> std::string {
		auto [type_desc_symbol, type_runtime_name] = getMsvcTypeDescriptorInfo(catch_type_name);

		auto* type_desc_sym = coffi_.get_symbol(type_desc_symbol);
		if (!type_desc_sym) {
			uint32_t type_desc_offset = static_cast<uint32_t>(rdata_section->get_data_size());

			std::vector<char> type_desc_data;
			type_desc_data.resize(POINTER_SIZE * 2, 0);
			for (char c : type_runtime_name) {
				type_desc_data.push_back(c);
			}
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

		return type_desc_symbol;
	};

	struct CatchableTypeEntry {
		std::string catch_type_name;
		uint32_t properties;
		uint32_t mdisp;
		int32_t pdisp;
		int32_t vdisp;
		uint32_t size_or_offset;
	};

	std::vector<CatchableTypeEntry> catchable_types;
	const uint32_t throw_size = static_cast<uint32_t>(type_size == 0 ? 8 : type_size);
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
		if (!catchable_type_sym) {
			const std::string type_desc_symbol = ensure_type_descriptor_symbol(catchable_type.catch_type_name);
			uint32_t catchable_type_offset = static_cast<uint32_t>(rdata_section->get_data_size());

			std::vector<char> catchable_type_data;
			catchable_type_data.reserve(28);
			ObjectFileCommon::appendLE(catchable_type_data, catchable_type.properties);
			ObjectFileCommon::appendLE(catchable_type_data, uint32_t(0));
			ObjectFileCommon::appendLE(catchable_type_data, catchable_type.mdisp);
			ObjectFileCommon::appendLE(catchable_type_data, static_cast<uint32_t>(catchable_type.pdisp));
			ObjectFileCommon::appendLE(catchable_type_data, static_cast<uint32_t>(catchable_type.vdisp));
			ObjectFileCommon::appendLE(catchable_type_data, catchable_type.size_or_offset);
			ObjectFileCommon::appendLE(catchable_type_data, uint32_t(0));

			add_data(catchable_type_data, SectionType::RDATA);

			catchable_type_sym = coffi_.add_symbol(catchable_type_symbol);
			catchable_type_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
			catchable_type_sym->set_storage_class(IMAGE_SYM_CLASS_STATIC);
			catchable_type_sym->set_section_number(rdata_section->get_index() + 1);
			catchable_type_sym->set_value(catchable_type_offset);

			add_rdata_relocation(catchable_type_offset + 4, type_desc_symbol, IMAGE_REL_AMD64_ADDR32NB);
		}
	}

	auto* catchable_array_sym = coffi_.get_symbol(catchable_array_symbol);
	if (!catchable_array_sym) {
		uint32_t catchable_array_offset = static_cast<uint32_t>(rdata_section->get_data_size());
		std::vector<char> catchable_array_data;
		catchable_array_data.reserve(4 + catchable_type_symbols.size() * 4);
		ObjectFileCommon::appendLE(catchable_array_data, static_cast<uint32_t>(catchable_type_symbols.size()));
		for (size_t i = 0; i < catchable_type_symbols.size(); ++i) {
			ObjectFileCommon::appendLE(catchable_array_data, uint32_t(0));
		}
		add_data(catchable_array_data, SectionType::RDATA);

		catchable_array_sym = coffi_.add_symbol(catchable_array_symbol);
		catchable_array_sym->set_type(IMAGE_SYM_TYPE_NOT_FUNCTION);
		catchable_array_sym->set_storage_class(IMAGE_SYM_CLASS_STATIC);
		catchable_array_sym->set_section_number(rdata_section->get_index() + 1);
		catchable_array_sym->set_value(catchable_array_offset);

		for (size_t i = 0; i < catchable_type_symbols.size(); ++i) {
			add_rdata_relocation(catchable_array_offset + 4 + static_cast<uint32_t>(i * 4), catchable_type_symbols[i], IMAGE_REL_AMD64_ADDR32NB);
		}
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

		if (!destructor_symbol.empty()) {
			// pmfnUnwind should reference a real destructor only when one is required and emitted.
			// For trivial implicit destructors (e.g. throw bad_any_cast{}), forcing a destructor
			// symbol here creates an unnecessary unresolved external during link.
			add_rdata_relocation(throw_info_offset + 4, destructor_symbol, IMAGE_REL_AMD64_ADDR32NB);
		}

		add_rdata_relocation(throw_info_offset + 12, catchable_array_symbol, IMAGE_REL_AMD64_ADDR32NB);
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

void ObjectFileWriter::addComdatSectionExternalSymbol(COFFI::section* section, std::string_view symbol_name, uint32_t value) {
	defineComdatExternalSymbol(section, symbol_name, value, false, false);
}

void ObjectFileWriter::emitVagueLinkageComdatRdata(std::string_view external_symbol_name, std::span<const char> data,
	std::span<const ComdatReloc> relocations, uint32_t external_symbol_value) {
	emitComdatSection(".rdata$", inline_comdat_rdata_section_counter_,
		IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_8BYTES,
		data, relocations, 0, IMAGE_COMDAT_SELECT_ANY, external_symbol_name, false, external_symbol_value);
}

void ObjectFileWriter::emit_vague_linkage_comdat_rdata(std::string_view external_symbol_name, std::span<const char> data,
	std::span<const ComdatReloc> relocations, uint32_t external_symbol_value) {
	emitVagueLinkageComdatRdata(external_symbol_name, data, relocations, external_symbol_value);
}

void ObjectFileWriter::add_vague_linkage_comdat_rdata_symbol(COFFI::section* section, std::string_view symbol_name, uint32_t value) {
	addComdatSectionExternalSymbol(section, symbol_name, value);
}

void ObjectFileWriter::emitInlineFunctionComdats(std::span<const uint8_t> text_data) {
	// The COFF section-definition symbol must precede the external COMDAT
	// leader. COFFI cannot reorder symbols after emission, so vague-linkage
	// function symbols are deferred until this point.
	auto text_section = coffi_.get_sections()[sectiontype_to_index[SectionType::TEXT]];
	auto xdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
	auto pdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::PDATA]];
	const auto& text_relocations = text_section->get_relocations();
	const auto& xdata_relocations = xdata_section->get_relocations();
	const char* xdata_bytes = xdata_section->get_data();
	const uint32_t xdata_byte_count = static_cast<uint32_t>(xdata_section->get_data_size());
	const char* pdata_bytes = pdata_section->get_data();
	const uint32_t pdata_byte_count = static_cast<uint32_t>(pdata_section->get_data_size());
	auto* unified_text_symbol = coffi_.get_symbol(".text");
	auto* unified_xdata_symbol = coffi_.get_symbol(".xdata");
	if (!unified_text_symbol || !unified_xdata_symbol) {
		throw InternalError("Unified unwind section symbols are missing");
	}
	// Capture indices immediately. add_symbol() can reallocate the symbol
	// vector, so later uses of these pointers would be dangling. .text is
	// index 0, so a zeroed dangling pointer still "worked" for text remaps
	// and hid the same bug for .xdata (index != 0).
	const uint32_t unified_text_symbol_index = unified_text_symbol->get_index();
	const uint32_t unified_xdata_symbol_index = unified_xdata_symbol->get_index();

	auto addRegularFunctionSymbol = [&](const PendingFunctionInfo& function) {
		auto symbol = coffi_.add_symbol(function.name);
		symbol->set_type(IMAGE_SYM_TYPE_FUNCTION);
		symbol->set_storage_class(IMAGE_SYM_CLASS_EXTERNAL);
		symbol->set_section_number(text_section->get_index() + 1);
		symbol->set_value(function.offset);
	};

	auto patchU32 = [](std::vector<char>& buffer, uint32_t offset, uint32_t value) {
		if (offset + 4 > buffer.size()) {
			throw InternalError("COMDAT patch offset is outside section data");
		}
		std::memcpy(buffer.data() + offset, &value, sizeof(value));
	};

	auto readU32 = [](std::span<const char> buffer, uint32_t offset) -> uint32_t {
		if (offset + 4 > buffer.size()) {
			throw InternalError("COMDAT read offset is outside section data");
		}
		uint32_t value = 0;
		std::memcpy(&value, buffer.data() + offset, sizeof(value));
		return value;
	};

	for (const PendingFunctionInfo& function : pending_functions_) {
		if (!function.is_inline) {
			continue;
		}
		if (function.length == 0) {
			addRegularFunctionSymbol(function);
			continue;
		}
		if (function.offset > text_data.size() || function.length > text_data.size() - function.offset) {
			throw InternalError("Inline function COMDAT range is outside the text section");
		}

		auto unwind_it = function_unwind_bundles_.find(function.name);
		uint32_t comdat_begin = function.offset;
		uint32_t comdat_end = function.offset + function.length;
		if (unwind_it != function_unwind_bundles_.end()) {
			for (const PendingPdataRecord& pdata_record : unwind_it->second.pdata_records) {
				if (pdata_record.begin_rva < comdat_begin) {
					comdat_begin = pdata_record.begin_rva;
				}
				if (pdata_record.end_rva > comdat_end) {
					comdat_end = pdata_record.end_rva;
				}
			}
		}
		if (comdat_end > text_data.size() || comdat_begin > comdat_end) {
			throw InternalError("Inline function COMDAT range is outside the text section");
		}
		const uint32_t leader_value = function.offset - comdat_begin;

		std::vector<ComdatReloc> text_relocations_out;
		for (const COFFI::relocation& relocation : text_relocations) {
			if (relocation.get_virtual_address() < comdat_begin ||
				relocation.get_virtual_address() >= comdat_end) {
				continue;
			}
			text_relocations_out.push_back({
				relocation.get_virtual_address() - comdat_begin,
				relocation.get_symbol_table_index(),
				relocation.get_type()});
		}

		std::vector<char> text_comdat_data(reinterpret_cast<const char*>(text_data.data() + comdat_begin),
			reinterpret_cast<const char*>(text_data.data() + comdat_end));
		COFFI::section* text_comdat_section = emitComdatSection(
			".text$", inline_comdat_section_counter_,
			IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_CODE | IMAGE_SCN_ALIGN_16BYTES,
			text_comdat_data, text_relocations_out, 0, IMAGE_COMDAT_SELECT_ANY, function.name, true, leader_value);
		auto* text_comdat_symbol = findComdatSectionSymbol(text_comdat_section);
		if (!text_comdat_symbol) {
			throw InternalError("COMDAT text section symbol was not created");
		}
		const uint32_t text_comdat_symbol_index = text_comdat_symbol->get_index();
		const uint16_t leader_section_number = static_cast<uint16_t>(text_comdat_section->get_index() + 1);
		const int32_t unified_text_section_number = static_cast<int32_t>(text_section->get_index() + 1);
		struct RehomedTextSymbol {
			uint32_t old_index = 0;
			std::string name;
			uint16_t type = 0;
			uint32_t new_value = 0;
		};
		std::vector<RehomedTextSymbol> rehomed_text_symbols;
		auto* symbols = coffi_.get_symbols();
		for (size_t i = 0; i < symbols->size(); ++i) {
			COFFI::symbol& symbol = (*symbols)[i];
			if (symbol.get_section_number() != unified_text_section_number ||
				symbol.get_storage_class() != IMAGE_SYM_CLASS_STATIC ||
				symbol.get_aux_symbols_number() != 0) {
				continue;
			}
			const uint32_t value = symbol.get_value();
			if (value < comdat_begin || value >= comdat_end) {
				continue;
			}
			rehomed_text_symbols.push_back({symbol.get_index(), symbol.get_name(), symbol.get_type(),
				value - comdat_begin});
		}
		for (const RehomedTextSymbol& rehomed : rehomed_text_symbols) {
			COFFI::symbol* new_symbol = coffi_.add_symbol(rehomed.name);
			new_symbol->set_type(rehomed.type);
			new_symbol->set_storage_class(IMAGE_SYM_CLASS_STATIC);
			new_symbol->set_section_number(leader_section_number);
			new_symbol->set_value(rehomed.new_value);
			retargetRelocations(rehomed.old_index, new_symbol->get_index());
			COFFI::symbol* old_symbol = coffi_.get_symbol(rehomed.old_index);
			if (old_symbol == nullptr) {
				throw InternalError("Replaced unified text COMDAT symbol is missing");
			}
			neutralizeReplacedUndefSymbol(old_symbol);
		}

		if (unwind_it == function_unwind_bundles_.end() || unwind_it->second.xdata_ranges.empty()) {
			continue;
		}

		struct XdataComdat {
			uint32_t unified_offset = 0;
			uint32_t length = 0;
			COFFI::section* section = nullptr;
			uint32_t symbol_index = 0;
		};
		std::vector<XdataComdat> xdata_comdats;
		xdata_comdats.reserve(unwind_it->second.xdata_ranges.size());
		for (const auto& xdata_range : unwind_it->second.xdata_ranges) {
			if (xdata_range.first + xdata_range.second > xdata_byte_count) {
				throw InternalError("Inline function XDATA range is outside the xdata section");
			}
			std::vector<char> xdata_comdat_data(xdata_bytes + xdata_range.first,
				xdata_bytes + xdata_range.first + xdata_range.second);
			std::vector<ComdatReloc> xdata_relocations_out;
			for (const COFFI::relocation& relocation : xdata_relocations) {
				if (relocation.get_virtual_address() < xdata_range.first ||
					relocation.get_virtual_address() >= xdata_range.first + xdata_range.second) {
					continue;
				}
				const uint32_t local_offset = relocation.get_virtual_address() - xdata_range.first;
				uint32_t symbol_index = relocation.get_symbol_table_index();
				if (symbol_index == unified_text_symbol_index) {
					uint32_t addend = readU32(std::span<const char>(xdata_comdat_data), local_offset);
					// IPtoStateMap end markers are exclusive and may equal
					// comdat_end (one-past-last byte of the copied range).
					if (addend >= comdat_begin && addend <= comdat_end) {
						symbol_index = text_comdat_symbol_index;
						patchU32(xdata_comdat_data, local_offset, addend - comdat_begin);
					}
				}
				xdata_relocations_out.push_back({local_offset, symbol_index, relocation.get_type()});
			}

			COFFI::section* xdata_comdat_section = emitComdatSection(
				".xdata$", inline_comdat_xdata_section_counter_,
				IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES,
				xdata_comdat_data, xdata_relocations_out, leader_section_number, IMAGE_COMDAT_SELECT_ASSOCIATIVE, {}, false, 0);
			auto* xdata_comdat_symbol = findComdatSectionSymbol(xdata_comdat_section);
			if (!xdata_comdat_symbol) {
				throw InternalError("COMDAT xdata section symbol was not created");
			}
			xdata_comdats.push_back({xdata_range.first, xdata_range.second, xdata_comdat_section,
				xdata_comdat_symbol->get_index()});
		}

		auto findXdataComdat = [&](uint32_t unified_offset) -> const XdataComdat* {
			for (const XdataComdat& xdata_comdat : xdata_comdats) {
				if (unified_offset >= xdata_comdat.unified_offset &&
					unified_offset < xdata_comdat.unified_offset + xdata_comdat.length) {
					return &xdata_comdat;
				}
			}
			return nullptr;
		};

		for (const XdataComdat& xdata_comdat : xdata_comdats) {
			char* section_bytes = const_cast<char*>(xdata_comdat.section->get_data());
			const uint32_t section_size = static_cast<uint32_t>(xdata_comdat.section->get_data_size());
			for (auto& relocation : xdata_comdat.section->get_relocations()) {
				if (relocation.get_symbol_table_index() != unified_xdata_symbol_index) {
					continue;
				}
				const uint32_t local_offset = relocation.get_virtual_address();
				uint32_t addend = readU32(std::span<const char>(section_bytes, section_size), local_offset);
				const XdataComdat* target = findXdataComdat(addend);
				if (target == nullptr) {
					continue;
				}
				if (local_offset + 4 > section_size) {
					throw InternalError("COMDAT xdata reloc is outside section data");
				}
				uint32_t local_addend = addend - target->unified_offset;
				std::memcpy(section_bytes + local_offset, &local_addend, sizeof(local_addend));
				relocation.set_symbol(target->symbol_index);
			}
		}

		for (const PendingPdataRecord& pdata_record : unwind_it->second.pdata_records) {
			if (pdata_record.offset + 12 > pdata_byte_count) {
				throw InternalError("Inline function PDATA range is outside the pdata section");
			}
			std::vector<char> pdata_comdat_data(pdata_bytes + pdata_record.offset,
				pdata_bytes + pdata_record.offset + 12);
			patchU32(pdata_comdat_data, 0, pdata_record.begin_rva - comdat_begin);
			patchU32(pdata_comdat_data, 4, pdata_record.end_rva - comdat_begin);

			const XdataComdat* xdata_target = findXdataComdat(pdata_record.unwind_rva);
			if (xdata_target == nullptr) {
				throw InternalError("Inline function PDATA references unknown XDATA range");
			}
			patchU32(pdata_comdat_data, 8, pdata_record.unwind_rva - xdata_target->unified_offset);

			std::vector<ComdatReloc> pdata_relocations_out = {
				{0, text_comdat_symbol_index, IMAGE_REL_AMD64_ADDR32NB},
				{4, text_comdat_symbol_index, IMAGE_REL_AMD64_ADDR32NB},
				{8, xdata_target->symbol_index, IMAGE_REL_AMD64_ADDR32NB},
			};
			emitComdatSection(".pdata$", inline_comdat_pdata_section_counter_,
				IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_4BYTES,
				pdata_comdat_data, pdata_relocations_out, leader_section_number, IMAGE_COMDAT_SELECT_ASSOCIATIVE, {}, false, 0);
		}
	}
}

void ObjectFileWriter::set_function_debug_range(const std::string_view manged_name, uint32_t prologue_size, uint32_t epilogue_size) {
	debug_builder_.setFunctionDebugRange(manged_name, prologue_size, epilogue_size);
}

void ObjectFileWriter::finalize_current_function() {
	debug_builder_.finalizeCurrentFunction();
}
