// ObjFileWriter_Symbols.h - Function symbol, relocation, and signature methods
// Part of ObjFileWriter class body (unity build shard) - included inside class ObjectFileWriter

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

	std::string get_or_create_exception_throw_info(const std::string& type_name, size_t type_size = 0, bool is_simple_type = false) {
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

			append_u32(is_simple_type ? 1 : 0);  // properties (1 = CT_IsSimpleType for scalars)
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
