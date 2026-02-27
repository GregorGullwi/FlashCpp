
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

		if (is_seh && is_cpp) {
			FLASH_LOG(Codegen, Warning, "Function has both SEH and C++ exception handling - using SEH");
			is_cpp = false;  // Prevent C++ EH metadata from corrupting SEH scope table
		}

		// Determine flags based on exception type
		uint8_t unwind_flags = 0x00;
		if (is_seh || is_cpp) {
			// SEH needs both UNW_FLAG_EHANDLER (0x01) and UNW_FLAG_UHANDLER (0x02)
			// For C++ EH with __CxxFrameHandler3, use both dispatch and unwind handler flags.
			unwind_flags = 0x03;  // UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER
		}

		// Build unwind codes
		UnwindCodeResult unwind_info = build_unwind_codes(is_cpp, stack_frame_size);
		uint32_t effective_frame_size = unwind_info.effective_frame_size;

		// Build UNWIND_INFO header + codes
		std::vector<char> xdata = {
			static_cast<char>(0x01 | (unwind_flags << 3)),  // Version 1, Flags
			static_cast<char>(unwind_info.prolog_size),     // Size of prolog
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
		if (is_cpp && !try_blocks.empty()) {
			build_cpp_exception_metadata(xdata, xdata_offset, function_start, function_size,
			                             mangled_name, try_blocks, unwind_map,
			                             effective_frame_size, stack_frame_size,
			                             cpp_funcinfo_rva_field_offset, has_cpp_funcinfo_rva_field,
			                             cpp_funcinfo_local_offset,
			                             cpp_xdata_rva_field_offsets, cpp_text_rva_field_offsets);
		}

		// Add the XDATA to the section
		add_data(xdata, SectionType::XDATA);

		// Emit relocations for exception handler and metadata
		emit_exception_relocations(xdata_offset, handler_rva_offset, is_seh, is_cpp,
		                           scope_relocs, cpp_xdata_rva_field_offsets, cpp_text_rva_field_offsets);

		// Build and emit PDATA entries
		build_pdata_entries(function_start, function_size, mangled_name, try_blocks,
		                    is_cpp, xdata_offset, unwind_info, cpp_funcinfo_local_offset);
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
		type_desc_data.reserve(16 + mangled_class_name.size() + 1);  // 8+8 header + name + null
		// vtable pointer (8 bytes) - null
		ObjectFileCommon::appendZeros(type_desc_data, 8);
		// spare pointer (8 bytes) - null
		ObjectFileCommon::appendZeros(type_desc_data, 8);
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
		self_bcd_data.reserve(28);  // 8 + 5*4 = 28 bytes total
		
		// type_descriptor pointer (8 bytes) - will add relocation
		ObjectFileCommon::appendZeros(self_bcd_data, 8);
		// num_contained_bases (4 bytes)
		uint32_t num_contained = static_cast<uint32_t>(base_class_names.size());
		ObjectFileCommon::appendLE(self_bcd_data, num_contained);
		// mdisp (4 bytes) - 0 for self
		ObjectFileCommon::appendLE(self_bcd_data, uint32_t(0));
		// pdisp (4 bytes) - -1 for non-virtual
		ObjectFileCommon::appendLE(self_bcd_data, uint32_t(0xFFFFFFFF));
		// vdisp (4 bytes) - 0
		ObjectFileCommon::appendLE(self_bcd_data, uint32_t(0));
		// attributes (4 bytes) - 0 for self
		ObjectFileCommon::appendLE(self_bcd_data, uint32_t(0));
		
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
			ObjectFileCommon::appendZeros(base_bcd_data, 8);
			
			// num_contained_bases (4 bytes) - actual value from base class info
			uint32_t base_num_contained = bci.num_contained_bases;
			ObjectFileCommon::appendLE(base_bcd_data, base_num_contained);
			
			// mdisp (4 bytes) - offset of base in derived class
			uint32_t mdisp = bci.offset;
			ObjectFileCommon::appendLE(base_bcd_data, mdisp);
			
			// pdisp (4 bytes) - vbtable displacement
			// -1 for non-virtual bases (not applicable)
			// 0+ for virtual bases (offset into vbtable)
			int32_t pdisp = bci.is_virtual ? 0 : -1;
			ObjectFileCommon::appendLE(base_bcd_data, pdisp);
			
			// vdisp (4 bytes) - displacement inside vbtable (0 for simplicity)
			ObjectFileCommon::appendLE(base_bcd_data, uint32_t(0));
			
			// attributes (4 bytes) - flags
			// Bit 0: virtual base (1 if virtual, 0 if non-virtual)
			uint32_t attributes = bci.is_virtual ? 1 : 0;
			ObjectFileCommon::appendLE(base_bcd_data, attributes);
			
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
		std::vector<char> bca_data(bcd_offsets.size() * 8, 0);
		
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
		ObjectFileCommon::appendLE(chd_data, uint32_t(0));
		// attributes (4 bytes) - 0 (can be extended for multiple/virtual inheritance)
		ObjectFileCommon::appendLE(chd_data, uint32_t(0));
		// num_base_classes (4 bytes) - total including self
		uint32_t total_bases = static_cast<uint32_t>(bcd_offsets.size());
		ObjectFileCommon::appendLE(chd_data, total_bases);
		// base_class_array pointer (8 bytes) - will add relocation
		ObjectFileCommon::appendZeros(chd_data, 8);
		
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
		ObjectFileCommon::appendLE(col_data, uint32_t(1));
		// offset (4 bytes) - 0 for primary vtable
		ObjectFileCommon::appendLE(col_data, uint32_t(0));
		// cd_offset (4 bytes) - 0
		ObjectFileCommon::appendLE(col_data, uint32_t(0));
		// type_descriptor pointer (8 bytes) - relocation added at offset+12
		ObjectFileCommon::appendZeros(col_data, 8);
		// hierarchy pointer (8 bytes) - relocation added at offset+20
		ObjectFileCommon::appendZeros(col_data, 8);
		
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

	// Helper: get or create symbol index for a function name (cached for O(1) repeated lookups)
	uint32_t get_or_create_symbol_index(const std::string& symbol_name) {
		// Check cache first
		auto cache_it = symbol_index_cache_.find(symbol_name);
		if (cache_it != symbol_index_cache_.end()) {
			if (g_enable_debug_output) std::cerr << "    DEBUG get_or_create_symbol_index: Cache hit for '" << symbol_name 
			          << "' at file index " << cache_it->second << std::endl;
			return cache_it->second;
		}

		// Check if symbol already exists in COFFI
		auto symbols = coffi_.get_symbols();
		for (size_t i = 0; i < symbols->size(); ++i) {
			if ((*symbols)[i].get_name() == symbol_name) {
				uint32_t file_index = (*symbols)[i].get_index();
				if (g_enable_debug_output) std::cerr << "    DEBUG get_or_create_symbol_index: Found existing symbol '" << symbol_name 
				          << "' at array index " << i << ", file index " << file_index << std::endl;
				symbol_index_cache_[symbol_name] = file_index;
				return file_index;
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
		symbol_index_cache_[symbol_name] = file_index;
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
	std::unordered_map<std::string, uint32_t, ObjectFileCommon::StringViewHash, std::equal_to<>> type_descriptor_offsets_;

	// Track generated throw-info symbols by type name
	std::unordered_map<std::string, std::string, ObjectFileCommon::StringViewHash, std::equal_to<>> throw_info_symbols_;

	// Cache for symbol name → file index lookups (avoids O(n) linear scan)
	std::unordered_map<std::string, uint32_t, ObjectFileCommon::StringViewHash, std::equal_to<>> symbol_index_cache_;

	// Counter for generating unique string literal symbols
	uint64_t string_literal_counter_ = 0;
	
	// Thread-local reusable buffer for string literal processing to avoid repeated allocations
	inline static thread_local std::string string_literal_buffer_;
};
