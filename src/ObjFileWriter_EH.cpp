// ObjFileWriter_EH.cpp - Out-of-line method definitions for ObjectFileWriter
// Part of ObjectFileWriter class (unity build)



void ObjectFileWriter::build_seh_scope_table(std::vector<char>& xdata, uint32_t function_start,
                           const std::vector<SehTryBlockInfo>& seh_try_blocks,
                           std::vector<ObjectFileWriter::ScopeTableReloc>& scope_relocs) {
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
	appendLE_xdata(xdata, scope_count);

	// Generate scope table entries
	for (const auto& seh_block : seh_try_blocks) {
		ScopeTableReloc reloc_info;

		// BeginAddress - absolute .text offset (relocation against .text section symbol with value=0)
		uint32_t begin_address = function_start + seh_block.try_start_offset;
		reloc_info.begin_offset = static_cast<uint32_t>(xdata.size());
		appendLE_xdata(xdata, begin_address);

		// EndAddress - absolute .text offset
		uint32_t end_address = function_start + seh_block.try_end_offset;
		reloc_info.end_offset = static_cast<uint32_t>(xdata.size());
		appendLE_xdata(xdata, end_address);

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
		appendLE_xdata(xdata, handler_address);

		reloc_info.jump_offset = static_cast<uint32_t>(xdata.size());
		appendLE_xdata(xdata, jump_target);

		scope_relocs.push_back(reloc_info);

		FLASH_LOG_FORMAT(Codegen, Debug, "SEH scope: begin={} end={} handler={} type={}",
		                 seh_block.try_start_offset, seh_block.try_end_offset,
		                 (seh_block.has_except_handler ? seh_block.except_handler.handler_offset : seh_block.finally_handler.handler_offset),
		                 (seh_block.has_except_handler ? "__except" : "__finally"));
	}
}

void ObjectFileWriter::ensure_type_descriptor(const std::string& type_name) {
	// Mangle the type name to get the symbol name
	auto [type_desc_symbol, type_desc_runtime_name] = getMsvcTypeDescriptorInfo(type_name);
	
	// Check if we've already created a descriptor for this type
	// by checking both the class member map and if the symbol already exists
	if (type_descriptor_offsets_.find(type_name) == type_descriptor_offsets_.end()) {
		// Check if the symbol already exists (could have been created elsewhere)
		auto* existing_symbol = coffi_.get_symbol(type_desc_symbol);
		if (existing_symbol) {
			// Symbol already exists, just record its offset for later use
			type_descriptor_offsets_[type_name] = existing_symbol->get_value();
			if (g_enable_debug_output) std::cerr << "  Type descriptor '" << type_desc_symbol 
			          << "' already exists for exception type '" << type_name << "'" << std::endl;
		} else {
			// Symbol doesn't exist, create it
			// Validate that RDATA section exists
			auto rdata_section_it = sectiontype_to_index.find(SectionType::RDATA);
			if (rdata_section_it == sectiontype_to_index.end()) {
				if (g_enable_debug_output) std::cerr << "ERROR: RDATA section not found for type descriptor generation of '" << type_name << "'" << std::endl;
				return;
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
			          << type_name << "' at offset " << type_desc_offset << std::endl;
			
			type_descriptor_offsets_[type_name] = type_desc_offset;
		}
	}
}

void ObjectFileWriter::build_cpp_exception_metadata(std::vector<char>& xdata, uint32_t xdata_offset,
                                  uint32_t function_start, uint32_t function_size,
                                  std::string_view mangled_name,
                                  const std::vector<TryBlockInfo>& try_blocks,
                                  const std::vector<UnwindMapEntryInfo>& unwind_map,
                                  uint32_t effective_frame_size, [[maybe_unused]] uint32_t stack_frame_size,
                                  uint32_t cpp_funcinfo_rva_field_offset,
                                  bool has_cpp_funcinfo_rva_field,
                                  uint32_t& cpp_funcinfo_local_offset_out,
                                  std::vector<uint32_t>& cpp_xdata_rva_field_offsets,
                                  std::vector<uint32_t>& cpp_text_rva_field_offsets) {
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

	[[maybe_unused]] uint32_t funcinfo_offset = static_cast<uint32_t>(xdata.size());
	cpp_funcinfo_local_offset_out = funcinfo_offset;
	if (has_cpp_funcinfo_rva_field) {
		uint32_t funcinfo_rva = xdata_offset + funcinfo_offset;
		patch_xdata_u32(xdata, cpp_funcinfo_rva_field_offset, funcinfo_rva);
		cpp_xdata_rva_field_offsets.push_back(cpp_funcinfo_rva_field_offset);
	}

	struct CatchStateBinding {
		const CatchHandlerInfo* handler;
		int32_t old_catch_state;
		int32_t catch_state;
	};

	struct TryStateLayout {
		int32_t old_try_state;
		int32_t landing_state;
		int32_t active_try_state;
		int32_t try_low;
		int32_t try_high;
		int32_t catch_high;
		bool has_cleanup_landing;
		std::vector<CatchStateBinding> catches;
	};

	std::vector<TryStateLayout> try_state_layout;
	try_state_layout.reserve(try_blocks.size());

	// Sort try blocks innermost-first (smaller range first) — MSVC convention.
	// This must happen BEFORE state assignment so states follow nesting order.
        std::vector<TryBlockInfo> sorted_try_blocks(try_blocks.begin(), try_blocks.end());
	        std::sort(sorted_try_blocks.begin(), sorted_try_blocks.end(), [](const auto& a, const auto& b) {
	            uint32_t range_a = a.try_end_offset - a.try_start_offset;
	            uint32_t range_b = b.try_end_offset - b.try_start_offset;
	            if (range_a != range_b) {
	                return range_a < range_b;
	            }
	            return a.try_start_offset < b.try_start_offset;
	        });

	// Determine nesting relationships: block j contains block i if
	// j.start <= i.start && i.end <= j.end (sorted innermost-first, so j > i means j is outer).
	// parent_index[i] = index of the immediately enclosing try block, or -1 if top-level.
	std::vector<int> parent_index(sorted_try_blocks.size(), -1);
	for (size_t i = 0; i < sorted_try_blocks.size(); i++) {
		for (size_t j = i + 1; j < sorted_try_blocks.size(); j++) {
			if (sorted_try_blocks[j].try_start_offset <= sorted_try_blocks[i].try_start_offset &&
				sorted_try_blocks[i].try_end_offset <= sorted_try_blocks[j].try_end_offset) {
				parent_index[i] = static_cast<int>(j);
				break;  // First match is the immediate parent (next-smallest enclosing)
			}
		}
	}

	// Reconstruct the IR state numbering first. The IR currently uses:
	//   - one try state per try block (outermost-first numbering)
	//   - catch states after all try states
	//   - extra cleanup chain states appended after that when needed
	std::vector<int32_t> old_try_state_by_sorted_index(sorted_try_blocks.size(), -1);
	int32_t next_old_state = 0;
	for (int i = static_cast<int>(sorted_try_blocks.size()) - 1; i >= 0; --i) {
		old_try_state_by_sorted_index[i] = next_old_state++;
	}

	std::vector<std::vector<int32_t>> old_catch_states_by_sorted_index(sorted_try_blocks.size());
	for (size_t i = 0; i < sorted_try_blocks.size(); ++i) {
		old_catch_states_by_sorted_index[i].reserve(sorted_try_blocks[i].catch_handlers.size());
		for ([[maybe_unused]] const auto& handler : sorted_try_blocks[i].catch_handlers) {
			old_catch_states_by_sorted_index[i].push_back(next_old_state++);
		}
	}

	auto unwind_state_has_action = [&](int32_t state) -> bool {
		return state >= 0 && static_cast<size_t>(state) < unwind_map.size() && !unwind_map[static_cast<size_t>(state)].action.empty();
	};

	// FH3 same-function try/catch needs a distinct post-cleanup landing state before
	// control transfers into catch funclets. Synthesize that state only when the try
	// both owns cleanup work and has catches.
	std::vector<bool> has_cleanup_landing_by_sorted_index(sorted_try_blocks.size(), false);
	for (size_t i = 0; i < sorted_try_blocks.size(); ++i) {
		has_cleanup_landing_by_sorted_index[i] = !sorted_try_blocks[i].catch_handlers.empty() &&
			unwind_state_has_action(old_try_state_by_sorted_index[i]);
	}

		std::vector<std::vector<size_t>> child_indices_by_sorted_index(sorted_try_blocks.size());
		std::vector<size_t> top_level_sorted_indices;
		for (size_t i = 0; i < sorted_try_blocks.size(); ++i) {
			if (parent_index[i] >= 0) {
				child_indices_by_sorted_index[static_cast<size_t>(parent_index[i])].push_back(i);
			} else {
				top_level_sorted_indices.push_back(i);
			}
		}

		auto sort_try_indices_by_source_order = [&](std::vector<size_t>& indices) {
			std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
				if (sorted_try_blocks[a].try_start_offset != sorted_try_blocks[b].try_start_offset) {
					return sorted_try_blocks[a].try_start_offset < sorted_try_blocks[b].try_start_offset;
				}
				return sorted_try_blocks[a].try_end_offset < sorted_try_blocks[b].try_end_offset;
			});
		};

		sort_try_indices_by_source_order(top_level_sorted_indices);
		for (auto& child_indices : child_indices_by_sorted_index) {
			sort_try_indices_by_source_order(child_indices);
		}

	std::vector<int32_t> landing_state_by_sorted_index(sorted_try_blocks.size(), -1);
	std::vector<int32_t> try_low_by_sorted_index(sorted_try_blocks.size(), -1);
		std::vector<std::vector<int32_t>> catch_states_by_sorted_index(sorted_try_blocks.size());
	int32_t next_state = 0;

		auto assign_emitted_states = [&](auto&& self, size_t try_index) -> void {
			if (has_cleanup_landing_by_sorted_index[try_index]) {
				landing_state_by_sorted_index[try_index] = next_state++;
			}
			try_low_by_sorted_index[try_index] = next_state++;

			for (size_t child_index : child_indices_by_sorted_index[try_index]) {
				self(self, child_index);
			}

			auto& catch_states = catch_states_by_sorted_index[try_index];
			catch_states.reserve(sorted_try_blocks[try_index].catch_handlers.size());
			for ([[maybe_unused]] const auto& handler : sorted_try_blocks[try_index].catch_handlers) {
				catch_states.push_back(next_state++);
			}
		};

		for (size_t top_level_index : top_level_sorted_indices) {
			assign_emitted_states(assign_emitted_states, top_level_index);
		}

	std::vector<int32_t> state_remap(unwind_map.size(), -1);
	for (size_t i = 0; i < sorted_try_blocks.size(); ++i) {
		int32_t old_try_state = old_try_state_by_sorted_index[i];
		if (old_try_state >= 0 && static_cast<size_t>(old_try_state) < state_remap.size()) {
			state_remap[static_cast<size_t>(old_try_state)] = try_low_by_sorted_index[i];
		}
		for (size_t catch_index = 0; catch_index < old_catch_states_by_sorted_index[i].size(); ++catch_index) {
			int32_t old_catch_state = old_catch_states_by_sorted_index[i][catch_index];
			if (old_catch_state >= 0 && static_cast<size_t>(old_catch_state) < state_remap.size()) {
				state_remap[static_cast<size_t>(old_catch_state)] = catch_states_by_sorted_index[i][catch_index];
			}
		}
	}
	for (size_t old_state = 0; old_state < state_remap.size(); ++old_state) {
		if (state_remap[old_state] < 0) {
			state_remap[old_state] = next_state++;
		}
	}

	// Build layouts innermost-first for TryBlockMap emission.
	for (size_t i = 0; i < sorted_try_blocks.size(); ++i) {
		TryStateLayout layout;
		layout.old_try_state = old_try_state_by_sorted_index[i];
		layout.landing_state = landing_state_by_sorted_index[i];
		layout.active_try_state = try_low_by_sorted_index[i];
		layout.has_cleanup_landing = has_cleanup_landing_by_sorted_index[i];
		layout.try_low = layout.has_cleanup_landing ? layout.landing_state : layout.active_try_state;
		layout.try_high = layout.active_try_state;
		layout.catch_high = layout.try_high;
		layout.catches.reserve(sorted_try_blocks[i].catch_handlers.size());

		for (size_t catch_index = 0; catch_index < sorted_try_blocks[i].catch_handlers.size(); ++catch_index) {
			const auto& handler = sorted_try_blocks[i].catch_handlers[catch_index];
			int32_t old_catch_state = old_catch_states_by_sorted_index[i][catch_index];
			int32_t catch_state = catch_states_by_sorted_index[i][catch_index];
			layout.catches.push_back(CatchStateBinding{&handler, old_catch_state, catch_state});
			layout.catch_high = catch_state;
		}

		try_state_layout.push_back(std::move(layout));
	}

	struct CatchStateRange {
		uint32_t start;
		uint32_t end;
		int32_t catch_state;
	};
	std::vector<CatchStateRange> catch_state_ranges;
	for (size_t i = 0; i < try_state_layout.size(); ++i) {
		for (const auto& catch_binding : try_state_layout[i].catches) {
			const auto& handler = *catch_binding.handler;
			uint32_t catch_end_offset = handler.funclet_end_offset != 0 ? handler.funclet_end_offset : handler.handler_end_offset;
			if (handler.handler_offset != 0 && catch_end_offset > handler.handler_offset) {
				catch_state_ranges.push_back({handler.handler_offset, catch_end_offset, catch_binding.catch_state});
			}
		}
	}

	std::vector<int32_t> enclosing_catch_state_by_sorted_index(sorted_try_blocks.size(), -1);
	for (size_t i = 0; i < sorted_try_blocks.size(); ++i) {
		uint32_t try_start = sorted_try_blocks[i].try_start_offset;
		uint32_t try_end = sorted_try_blocks[i].try_end_offset;
		uint32_t best_range_size = UINT32_MAX;
		for (const auto& catch_range : catch_state_ranges) {
			if (catch_range.start <= try_start && try_end <= catch_range.end) {
				uint32_t range_size = catch_range.end - catch_range.start;
				if (range_size < best_range_size) {
					best_range_size = range_size;
					enclosing_catch_state_by_sorted_index[i] = catch_range.catch_state;
				}
			}
		}
	}

	// Third pass: adjust outer try's tryHigh to encompass nested states
	for (size_t i = 0; i < try_state_layout.size(); i++) {
		for (size_t j = i + 1; j < try_state_layout.size(); j++) {
			if (sorted_try_blocks[j].try_start_offset <= sorted_try_blocks[i].try_start_offset &&
				sorted_try_blocks[i].try_end_offset <= sorted_try_blocks[j].try_end_offset) {
				if (try_state_layout[i].catch_high > try_state_layout[j].try_high) {
					try_state_layout[j].try_high = try_state_layout[i].catch_high;
				}
			}
		}
	}

	if (IS_FLASH_LOG_ENABLED(Codegen, Debug))
	{
		// Debug: log state layout for each try block
		for (size_t i = 0; i < try_state_layout.size(); i++) {
			FLASH_LOG_FORMAT(Codegen, Debug, "  TryBlock[{}]: oldTryState={}, landingState={}, activeTryState={}, tryLow={}, tryHigh={}, catchHigh={}, offsets=[{},{}], catches={}",
				i, try_state_layout[i].old_try_state, try_state_layout[i].landing_state, try_state_layout[i].active_try_state,
				try_state_layout[i].try_low, try_state_layout[i].try_high, try_state_layout[i].catch_high,
				sorted_try_blocks[i].try_start_offset, sorted_try_blocks[i].try_end_offset,
				try_state_layout[i].catches.size());
		}
	}

	// Magic number for x64 FH3 FuncInfo layout.
	// 0x19930522 = FuncInfo with 10 fields (40 bytes) including dispUnwindHelp,
	// pESTypeList, and EHFlags. Requires a stack-based state variable at
	// [establisher_frame + dispUnwindHelp], initialized to -2 by the prologue.
	// 0x19930520 = FuncInfo with 7 fields (28 bytes), basic FH3.
	uint32_t magic = 0x19930522;
	bool use_disp_unwind_help = (magic >= 0x19930521);
	appendLE_xdata(xdata, magic);
	
	// maxState - state count used by FH3 state machine.
	uint32_t max_state = static_cast<uint32_t>(next_state);
	if (!unwind_map.empty() && unwind_map.size() > max_state) {
		max_state = static_cast<uint32_t>(std::max<size_t>(next_state, unwind_map.size()));
	}

	// FH3 expects a valid unwind map for the active state range.
	// If IR-level unwind actions are missing, synthesize no-op entries.
	uint32_t unwind_entry_count = !unwind_map.empty()
		? static_cast<uint32_t>(std::max<size_t>(max_state, unwind_map.size()))
		: max_state;
	if (unwind_entry_count > max_state) {
		max_state = unwind_entry_count;
	}
	appendLE_xdata(xdata, max_state);

	// pUnwindMap - patch after map emission
	uint32_t p_unwind_map_field_offset = static_cast<uint32_t>(xdata.size());
	appendLE_xdata(xdata, uint32_t(0));

	// nTryBlocks - number of try blocks
	uint32_t num_try_blocks = static_cast<uint32_t>(try_blocks.size());
	appendLE_xdata(xdata, num_try_blocks);

	// pTryBlockMap - patch after map emission
	uint32_t p_try_block_map_field_offset = static_cast<uint32_t>(xdata.size());
	appendLE_xdata(xdata, uint32_t(0));

	// nIPMapEntries - patch after map emission
	uint32_t n_ip_map_entries_field_offset = static_cast<uint32_t>(xdata.size());
	appendLE_xdata(xdata, uint32_t(0));

	// pIPToStateMap - patch after map emission
	uint32_t p_ip_to_state_map_field_offset = static_cast<uint32_t>(xdata.size());
	appendLE_xdata(xdata, uint32_t(0));

	// dispUnwindHelp - displacement from establisher frame to the state variable.
	// EstablisherFrame = RBP - FrameOffset*16 (= RBP - effective_frame_size).
	// State variable at [rbp-8].
	// So dispUnwindHelp = (rbp-8) - (RBP - effective_frame_size) = effective_frame_size - 8.
	// Only present when magic >= 0x19930521.
	if (use_disp_unwind_help) {
		int32_t disp_unwind_help = static_cast<int32_t>(effective_frame_size) - 8;
		appendLE_xdata(xdata, static_cast<uint32_t>(disp_unwind_help));
	}

	// pESTypeList - dynamic exception specification type list (unused)
	// Only present when magic >= 0x19930522.
	if (magic >= 0x19930522) {
		appendLE_xdata(xdata, uint32_t(0));
	}

	// EHFlags (bit 0 set for /EHs semantics)
	// Only present when magic >= 0x19930522.
	if (magic >= 0x19930522) {
		uint32_t eh_flags = 0x1;
		appendLE_xdata(xdata, eh_flags);
	}

	uint32_t unwind_map_offset = 0;
	if (unwind_entry_count > 0) {
		unwind_map_offset = xdata_offset + static_cast<uint32_t>(xdata.size());
		patch_xdata_u32(xdata, p_unwind_map_field_offset, unwind_map_offset);
		cpp_xdata_rva_field_offsets.push_back(p_unwind_map_field_offset);
	}
	
	// Build proper unwind map with state chaining for nested try/catch.
	// For each state, determine toState: the state to unwind to (parent state).
	// - synthesized landing state → parent try's active state, or -1 if top-level
	// - try_low state → landing state when present, otherwise parent try's active state
	// - catch states → landing state when present, otherwise parent try's active state
	std::vector<int32_t> computed_to_state(max_state, -1);
	for (size_t i = 0; i < try_state_layout.size(); i++) {
		const auto& layout = try_state_layout[i];
		int32_t parent_active_try_state = enclosing_catch_state_by_sorted_index[i];
		if (parent_index[i] >= 0) {
			parent_active_try_state = try_state_layout[parent_index[i]].active_try_state;
		}
		if (layout.landing_state >= 0 && layout.landing_state < static_cast<int32_t>(max_state)) {
			computed_to_state[layout.landing_state] = parent_active_try_state;
		}
		int32_t cleanup_target_state = layout.has_cleanup_landing ? layout.landing_state : parent_active_try_state;
		if (layout.active_try_state >= 0 && layout.active_try_state < static_cast<int32_t>(max_state)) {
			computed_to_state[layout.active_try_state] = cleanup_target_state;
		}
		for (const auto& cb : layout.catches) {
			if (cb.catch_state >= 0 && cb.catch_state < static_cast<int32_t>(max_state)) {
				computed_to_state[cb.catch_state] = cleanup_target_state;
			}
		}
	}

	std::vector<UnwindMapEntryInfo> emitted_unwind_map;
	emitted_unwind_map.resize(unwind_entry_count);
	for (uint32_t i = 0; i < unwind_entry_count; ++i) {
		emitted_unwind_map[i].to_state = i < computed_to_state.size() ? computed_to_state[i] : -1;
	}

	auto remap_old_state = [&](int32_t old_state) -> int32_t {
		if (old_state < 0) {
			return -1;
		}
		if (static_cast<size_t>(old_state) >= state_remap.size()) {
			return -1;
		}
		return state_remap[static_cast<size_t>(old_state)];
	};

	if (!unwind_map.empty()) {
		for (size_t old_state = 0; old_state < unwind_map.size(); ++old_state) {
			int32_t new_state = remap_old_state(static_cast<int32_t>(old_state));
			if (new_state < 0 || static_cast<uint32_t>(new_state) >= unwind_entry_count) {
				continue;
			}
			emitted_unwind_map[static_cast<size_t>(new_state)].to_state = remap_old_state(unwind_map[old_state].to_state);
			emitted_unwind_map[static_cast<size_t>(new_state)].action = unwind_map[old_state].action;
		}

		auto find_cleanup_tail_state = [&](int32_t start_state, int32_t parent_state) -> int32_t {
			int32_t state = start_state;
			std::vector<int32_t> visited;
			while (state >= 0 && static_cast<size_t>(state) < unwind_map.size()) {
				if (std::find(visited.begin(), visited.end(), state) != visited.end()) {
					break;
				}
				visited.push_back(state);

				int32_t next = unwind_map[static_cast<size_t>(state)].to_state;
				if (next < 0 || next == parent_state) {
					return state;
				}
				if (static_cast<size_t>(next) >= unwind_map.size() || unwind_map[static_cast<size_t>(next)].action.empty()) {
					return state;
				}
				state = next;
			}
			return start_state;
		};

		for (size_t i = 0; i < try_state_layout.size(); ++i) {
			const auto& layout = try_state_layout[i];
			int32_t parent_old_try_state = -1;
			if (parent_index[i] >= 0) {
				parent_old_try_state = try_state_layout[parent_index[i]].old_try_state;
			}

			if (layout.has_cleanup_landing && layout.landing_state >= 0) {
				int32_t cleanup_tail_old_state = find_cleanup_tail_state(layout.old_try_state, parent_old_try_state);
				int32_t cleanup_tail_new_state = remap_old_state(cleanup_tail_old_state);
				if (cleanup_tail_new_state >= 0 && static_cast<uint32_t>(cleanup_tail_new_state) < unwind_entry_count) {
					emitted_unwind_map[static_cast<size_t>(cleanup_tail_new_state)].to_state = layout.landing_state;
				}
			}

			int32_t catch_unwind_target = layout.has_cleanup_landing ? layout.landing_state : computed_to_state[layout.active_try_state];
			for (const auto& catch_binding : layout.catches) {
				if (catch_binding.catch_state >= 0 && static_cast<uint32_t>(catch_binding.catch_state) < unwind_entry_count) {
					emitted_unwind_map[static_cast<size_t>(catch_binding.catch_state)].to_state = catch_unwind_target;
				}
			}
		}
	}

	// Now add UnwindMap entries
	// Each UnwindMapEntry:
	//   int toState (state to transition to, -1 = end of unwind chain)
	//   DWORD action (RVA to cleanup/destructor function, or 0 for no action)
	if (unwind_entry_count > 0) {
		for (uint32_t i = 0; i < unwind_entry_count; ++i) {
			appendLE_xdata(xdata, static_cast<uint32_t>(emitted_unwind_map[i].to_state));

			// action - RVA to destructor/cleanup function
			uint32_t action_field_offset = static_cast<uint32_t>(xdata.size());
			uint32_t action_rva = 0;
			if (!emitted_unwind_map[i].action.empty()) {
				add_xdata_relocation(xdata_offset + action_field_offset, emitted_unwind_map[i].action);
			}
			appendLE_xdata(xdata, action_rva);
		}
	}

	uint32_t tryblock_map_offset = xdata_offset + static_cast<uint32_t>(xdata.size());
	patch_xdata_u32(xdata, p_try_block_map_field_offset, tryblock_map_offset);
	cpp_xdata_rva_field_offsets.push_back(p_try_block_map_field_offset);
	
	// Now add TryBlockMap entries
	// Each TryBlockMapEntry:
	//   int tryLow (state number)
	//   int tryHigh (state number)
	//   int catchHigh (state number after try)
	//   int nCatches
	//   DWORD pHandlerArray (RVA)
	
	uint32_t handler_array_base = tryblock_map_offset + (num_try_blocks * 20); // 20 bytes per TryBlockMapEntry
	
	for (size_t i = 0; i < sorted_try_blocks.size(); ++i) {
		const auto& try_block = sorted_try_blocks[i];
		const auto& state_layout = try_state_layout[i];
		
		// tryLow (state when entering try block)
		uint32_t try_low = static_cast<uint32_t>(state_layout.try_low);
		appendLE_xdata(xdata, try_low);
		
		// tryHigh (inclusive state range for the try body)
		uint32_t try_high = static_cast<uint32_t>(state_layout.try_high);
		appendLE_xdata(xdata, try_high);
		
		// catchHigh (highest state owned by this try + catch funclets)
		uint32_t catch_high = static_cast<uint32_t>(state_layout.catch_high);
		appendLE_xdata(xdata, catch_high);
		
		// nCatches
		uint32_t num_catches = static_cast<uint32_t>(try_block.catch_handlers.size());
		appendLE_xdata(xdata, num_catches);
		
		// pHandlerArray - RVA to handler array for this try block
		uint32_t handler_array_offset = handler_array_base;
		uint32_t p_handler_array_field_offset = static_cast<uint32_t>(xdata.size());
		appendLE_xdata(xdata, handler_array_offset);
		cpp_xdata_rva_field_offsets.push_back(p_handler_array_field_offset);
		
		handler_array_base += num_catches * 20; // 20 bytes per HandlerType entry (x64 FH3 includes dispFrame)
	}
	
	// Now add HandlerType arrays for each try block
	// Each HandlerType:
	//   DWORD adjectives (0x01 = const, 0x08 = reference, 0 = by-value)
	//   DWORD pType (RVA to type descriptor, 0 for catch-all)
	//   int catchObjOffset (frame offset of catch parameter, negative)
	//   DWORD addressOfHandler (RVA of catch handler code)
	
	// First, generate type descriptors for all unique exception types
	// Use class member to track across multiple function calls
	
	for (const auto& try_block : sorted_try_blocks) {
		for (const auto& handler : try_block.catch_handlers) {
			if (!handler.is_catch_all && !handler.type_name.empty()) {
				ensure_type_descriptor(handler.type_name);
			}
		}
	}
	
	// Now generate HandlerType entries with proper pType references
	auto ensure_catch_symbol = [this, function_start](std::string_view parent_mangled_name, uint32_t funclet_entry_offset, size_t handler_idx) -> std::string {
		StringBuilder sb;
		sb.append("$catch$").append(parent_mangled_name).append("$").append(handler_idx);
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
	for (size_t try_index = 0; try_index < sorted_try_blocks.size(); ++try_index) {
		const auto& state_layout = try_state_layout[try_index];
		for (const auto& catch_binding : state_layout.catches) {
			const auto& handler = *catch_binding.handler;
			// adjectives - MSVC exception handler flags
			// 0x01 = const
			// 0x08 = reference (lvalue reference &)
			// 0x10 = rvalue reference (&&)
			uint32_t adjectives = 0;
			if (handler.is_catch_all) {
				adjectives |= 0x40;
			}
			if (handler.is_const) {
				adjectives |= 0x01;
			}
			if (handler.is_reference) {
				adjectives |= 0x08;
			}
			if (handler.is_rvalue_reference) {
				adjectives |= 0x10;
			}
			
			appendLE_xdata(xdata, adjectives);
			
			// pType - RVA to type descriptor (0 for catch-all)
			uint32_t ptype_offset = static_cast<uint32_t>(xdata.size());
			// Add placeholder for pType (4 bytes)
			appendLE_xdata(xdata, uint32_t(0));
			
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
				// The CRT copies the exception object to [EstablisherFrame + dispCatchObj].
				// EstablisherFrame = RBP - effective_frame_size.
				// The catch variable is at [rbp + catch_obj_offset] (catch_obj_offset is negative).
				// So: (RBP - effective_frame_size) + dispCatchObj = RBP + catch_obj_offset
				//     dispCatchObj = catch_obj_offset + effective_frame_size
				int32_t catch_offset = handler.catch_obj_offset;
				if (catch_offset != 0) {
					catch_offset += static_cast<int32_t>(effective_frame_size);
				}
				appendLE_xdata(xdata, static_cast<uint32_t>(catch_offset));

				// addressOfHandler - RVA of catch handler entry.
				// Use a dedicated catch symbol to mirror MSVC's handler map relocation style.
				uint32_t funclet_entry_offset = handler.funclet_entry_offset != 0 ? handler.funclet_entry_offset : handler.handler_offset;
				std::string catch_symbol_name = ensure_catch_symbol(mangled_name, funclet_entry_offset, handler_index);
			uint32_t address_of_handler_field_offset = static_cast<uint32_t>(xdata.size());
			appendLE_xdata(xdata, uint32_t(0));
			add_xdata_relocation(xdata_offset + address_of_handler_field_offset, catch_symbol_name);

			// dispFrame: offset from catch funclet's EstablisherFrame to where the
			// funclet saved the parent's establisher frame (RDX arg).
			// __CxxFrameHandler3 uses: parent_estab = *(funclet_estab + dispFrame)
			// to find the parent's frame when dispatching nested exceptions.
			//
			// Funclet prologue on x64:
			//   movq %rdx, 0x10(%rsp)  ; save parent estab at [entry_RSP + 0x10]
			//   pushq %rbp             ; RSP -= 8
			//   subq , %rsp       ; RSP -= 0x20  (total prolog delta = 0x28)
			// RtlVirtualUnwind returns funclet_estab = entry_RSP (unwinds 0x28).
			// funclet_estab + dispFrame = entry_RSP + 0x10 => dispFrame = 0x28 + 0x10 = 0x38.
			// This is a CONSTANT 0x38, regardless of parent function's frame size.
			// (Verified empirically: clang-cl emits 0x38 for all frame sizes.)
			int32_t disp_frame = 0x38;
			appendLE_xdata(xdata, static_cast<uint32_t>(disp_frame));
			
			handler_index++;
		}
	}

	// Build IP-to-state map for FH3 state lookup.
	// Include both try body state ranges and catch funclet state ranges.
	struct IpStateEntry {
		uint32_t ip_rva;
		int32_t state;
	};

	std::vector<IpStateEntry> ip_to_state_entries;
	ip_to_state_entries.reserve(sorted_try_blocks.size() * 4 + 2);
	ip_to_state_entries.push_back({function_start, -1});
		uint32_t first_cleanup_funclet_rva = function_start + function_size;
		for (const auto& entry : unwind_map) {
			if (entry.action.empty()) {
				continue;
			}
			auto* action_symbol = coffi_.get_symbol(entry.action);
			if (!action_symbol) {
				continue;
			}
			first_cleanup_funclet_rva = std::min(first_cleanup_funclet_rva, action_symbol->get_value());
		}
		if (sorted_try_blocks.empty() && !unwind_map.empty()) {
			ip_to_state_entries.push_back({function_start, 0});
			if (first_cleanup_funclet_rva > function_start && first_cleanup_funclet_rva < function_start + function_size) {
				ip_to_state_entries.push_back({first_cleanup_funclet_rva, -1});
			}
		}

	for (size_t i = 0; i < sorted_try_blocks.size(); ++i) {
		const auto& tb = sorted_try_blocks[i];
		const auto& layout = try_state_layout[i];
		ip_to_state_entries.push_back({function_start + tb.try_start_offset, layout.active_try_state});
		uint32_t try_end_ip = tb.try_end_offset;
		if (try_end_ip < function_size) {
			try_end_ip += 1;
		}
		// After this try ends, determine if we're still inside a parent try block.
		// If so, transition to the parent's try_low state; otherwise transition to -1.
			int32_t post_try_state = enclosing_catch_state_by_sorted_index[i];
		if (parent_index[i] >= 0) {
			post_try_state = try_state_layout[parent_index[i]].active_try_state;
		}
		ip_to_state_entries.push_back({function_start + try_end_ip, post_try_state});

			// MSVC FH3 also maps same-function catches in the parent FuncInfo's ip2state map.
			// On Windows those catches are emitted as real funclets, so use the funclet end when
			// available; the logical catch-body end can be too early or too late once fixup/return
			// stubs are involved. After the catch completes, control resumes after the entire
			// try/catch statement in the parent state, not in the handled try's own state.
		for (const auto& catch_binding : layout.catches) {
			const auto& handler = *catch_binding.handler;
			if (handler.handler_offset != 0 && handler.handler_offset < function_size) {
				ip_to_state_entries.push_back({function_start + handler.handler_offset, catch_binding.catch_state});
			}
				uint32_t catch_end_offset = handler.funclet_end_offset != 0 ? handler.funclet_end_offset : handler.handler_end_offset;
				for (const auto& catch_range : catch_state_ranges) {
					if (catch_range.start > handler.handler_offset && catch_range.end < catch_end_offset) {
						ip_to_state_entries.push_back({function_start + catch_range.end, catch_binding.catch_state});
					}
				}
				if (catch_end_offset != 0 && catch_end_offset <= function_size) {
					ip_to_state_entries.push_back({function_start + catch_end_offset, post_try_state});
			}
		}
	}

	ip_to_state_entries.push_back({function_start + function_size, -1});

	std::sort(ip_to_state_entries.begin(), ip_to_state_entries.end(), [](const IpStateEntry& a, const IpStateEntry& b) {
		if (a.ip_rva != b.ip_rva) {
			return a.ip_rva < b.ip_rva;
		}
		return a.state < b.state;
	});

	std::vector<IpStateEntry> compact_entries;
	compact_entries.reserve(ip_to_state_entries.size());
	for (const auto& entry : ip_to_state_entries) {
		if (!compact_entries.empty() && compact_entries.back().ip_rva == entry.ip_rva) {
			compact_entries.back().state = entry.state;
		} else {
			compact_entries.push_back(entry);
		}
	}

	uint32_t ip_to_state_map_offset = xdata_offset + static_cast<uint32_t>(xdata.size());
	patch_xdata_u32(xdata, n_ip_map_entries_field_offset, static_cast<uint32_t>(compact_entries.size()));
	patch_xdata_u32(xdata, p_ip_to_state_map_field_offset, ip_to_state_map_offset);
	cpp_xdata_rva_field_offsets.push_back(p_ip_to_state_map_field_offset);

	for (const auto& entry : compact_entries) {
		FLASH_LOG_FORMAT(Codegen, Debug, "  IP-to-state: ip_rva=0x{:X} (func+{}), state={}", entry.ip_rva, entry.ip_rva - function_start, entry.state);
		uint32_t ip_field_offset = static_cast<uint32_t>(xdata.size());
		appendLE_xdata(xdata, entry.ip_rva);
		cpp_text_rva_field_offsets.push_back(ip_field_offset);
		appendLE_xdata(xdata, static_cast<uint32_t>(entry.state));
	}
}

void ObjectFileWriter::emit_exception_relocations(uint32_t xdata_offset, uint32_t handler_rva_offset,
                                bool is_seh, bool is_cpp,
                                const std::vector<ObjectFileWriter::ScopeTableReloc>& scope_relocs,
                                const std::vector<uint32_t>& cpp_xdata_rva_field_offsets,
                                const std::vector<uint32_t>& cpp_text_rva_field_offsets) {
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
}

void ObjectFileWriter::build_pdata_entries(uint32_t function_start, uint32_t function_size,
                         std::string_view mangled_name,
                         const std::vector<TryBlockInfo>& try_blocks,
	                         const std::vector<UnwindMapEntryInfo>& unwind_map,
                         bool is_cpp, uint32_t xdata_offset,
                         const ObjectFileWriter::UnwindCodeResult& unwind_info,
                         uint32_t cpp_funcinfo_local_offset) {
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

	auto add_cpp_funcinfo_relocation = [this](uint32_t record_xdata_offset, uint32_t funcinfo_rva_local_offset) {
		auto xdata_sec = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
		auto* xdata_sym = coffi_.get_symbol(".xdata");
		if (!xdata_sym) {
			return;
		}

		COFFI::rel_entry_generic reloc;
		reloc.virtual_address = record_xdata_offset + funcinfo_rva_local_offset;
		reloc.symbol_table_index = xdata_sym->get_index();
		reloc.type = IMAGE_REL_AMD64_ADDR32NB;
		xdata_sec->add_relocation_entry(&reloc);
	};

	// Build catch funclet ranges for C++ EH so parent ranges can avoid overlap.
	struct RelativeRange {
		uint32_t start;
		uint32_t end;
	};
	std::vector<RelativeRange> all_catch_funclet_ranges;
	std::vector<RelativeRange> catch_funclet_ranges;
		std::vector<RelativeRange> cleanup_funclet_ranges;
	std::vector<ObjectFileWriter::PendingPdataEntry> pending_pdata_entries;
	if (is_cpp) {
		for (const auto& tb : try_blocks) {
			for (size_t i = 0; i < tb.catch_handlers.size(); ++i) {
				const auto& handler = tb.catch_handlers[i];
				const CatchHandlerInfo* next_handler = (i + 1 < tb.catch_handlers.size()) ? &tb.catch_handlers[i + 1] : nullptr;
				uint32_t handler_start_rel = resolve_funclet_start(handler);
				uint32_t handler_end_rel = resolve_funclet_end(handler, next_handler);
				if (handler_end_rel > handler_start_rel && handler_end_rel <= function_size) {
					all_catch_funclet_ranges.push_back({handler_start_rel, handler_end_rel});
					catch_funclet_ranges.push_back({handler_start_rel, handler_end_rel});
				}
			}
		}

		std::sort(catch_funclet_ranges.begin(), catch_funclet_ranges.end(), [](const RelativeRange& a, const RelativeRange& b) {
			if (a.start != b.start) {
				return a.start < b.start;
			}
			return a.end < b.end;
		});

		std::vector<RelativeRange> merged_ranges;
		for (const auto& range : catch_funclet_ranges) {
			if (merged_ranges.empty() || range.start > merged_ranges.back().end) {
				merged_ranges.push_back(range);
			} else if (range.end > merged_ranges.back().end) {
				merged_ranges.back().end = range.end;
			}
		}
		catch_funclet_ranges = std::move(merged_ranges);

			std::vector<uint32_t> cleanup_starts;
			for (const auto& entry : unwind_map) {
				if (entry.action.empty()) {
					continue;
				}
				auto* symbol = coffi_.get_symbol(entry.action);
				if (!symbol) {
					continue;
				}
				uint32_t start = symbol->get_value();
				if (start < function_start || start >= function_start + function_size) {
					continue;
				}
				cleanup_starts.push_back(start - function_start);
			}

			std::sort(cleanup_starts.begin(), cleanup_starts.end());
			cleanup_starts.erase(std::unique(cleanup_starts.begin(), cleanup_starts.end()), cleanup_starts.end());
			for (size_t i = 0; i < cleanup_starts.size(); ++i) {
				uint32_t start = cleanup_starts[i];
				uint32_t end = (i + 1 < cleanup_starts.size()) ? cleanup_starts[i + 1] : function_size;
				if (end > start) {
					cleanup_funclet_ranges.push_back({start, end});
				}
			}
	}

	// Emit parent PDATA ranges. For C++ EH, carve out catch funclet ranges to avoid overlap.
	std::vector<RelativeRange> parent_ranges;
		std::vector<RelativeRange> excluded_ranges = catch_funclet_ranges;
		excluded_ranges.insert(excluded_ranges.end(), cleanup_funclet_ranges.begin(), cleanup_funclet_ranges.end());
		if (!excluded_ranges.empty()) {
			std::sort(excluded_ranges.begin(), excluded_ranges.end(), [](const RelativeRange& a, const RelativeRange& b) {
				if (a.start != b.start) {
					return a.start < b.start;
				}
				return a.end < b.end;
			});
			std::vector<RelativeRange> merged_excluded;
			for (const auto& range : excluded_ranges) {
				if (merged_excluded.empty() || range.start > merged_excluded.back().end) {
					merged_excluded.push_back(range);
				} else if (range.end > merged_excluded.back().end) {
					merged_excluded.back().end = range.end;
				}
			}
			excluded_ranges = std::move(merged_excluded);
		}

		if (!is_cpp || excluded_ranges.empty()) {
		parent_ranges.push_back({0, function_size});
	} else {
		uint32_t cursor = 0;
			for (const auto& excluded_range : excluded_ranges) {
				if (cursor < excluded_range.start) {
					parent_ranges.push_back({cursor, excluded_range.start});
			}
				if (excluded_range.end > cursor) {
					cursor = excluded_range.end;
			}
		}
		if (cursor < function_size) {
			parent_ranges.push_back({cursor, function_size});
		}
		if (parent_ranges.empty()) {
			parent_ranges.push_back({0, function_size});
		}
	}

	// For C++ EH, the first parent range starts at function entry so it uses the
	// main UNWIND_INFO (with correct SizeOfProlog matching the actual prologue).
	// Subsequent parent ranges (post-catch code) need their own UNWIND_INFO with
	// SizeOfProlog=0, because the unwinder calculates (IP - range_start) and compares
	// to SizeOfProlog. If we reuse the parent's UNWIND_INFO (SizeOfProlog=11), the
	// unwinder thinks the post-catch code is mid-prologue and applies zero unwind codes,
	// which prevents proper stack unwinding and causes crashes.
	const uint8_t post_catch_unwind_flags = try_blocks.empty() ? 0x02 : 0x03;
	uint32_t post_catch_xdata_offset = 0;
	bool has_post_catch_xdata = false;
	for (size_t ri = 0; ri < parent_ranges.size(); ++ri) {
		const auto& parent_range = parent_ranges[ri];
		if (parent_range.end <= parent_range.start) {
			continue;
		}
		if (ri == 0 || !is_cpp) {
			// First parent range (or non-C++ EH): use the main UNWIND_INFO
			pending_pdata_entries.push_back({
				function_start + parent_range.start,
				function_start + parent_range.end,
				xdata_offset
			});
			} else {
				// Post-catch parent ranges: need UNWIND_INFO with SizeOfProlog=0
				if (!has_post_catch_xdata) {
					// Create a new UNWIND_INFO: same unwind codes and frame register,
					// but keep the same C++ handler/FuncInfo linkage with SizeOfProlog=0.
					std::vector<char> post_catch_xdata = {
						static_cast<char>(0x01 | (post_catch_unwind_flags << 3)),
						static_cast<char>(0x00),              // SizeOfProlog = 0
						static_cast<char>(unwind_info.count_of_codes),    // Same count of unwind codes
						static_cast<char>(unwind_info.frame_reg_and_offset) // Same frame register and offset
					};
					for (auto b : unwind_info.codes) {
						post_catch_xdata.push_back(static_cast<char>(b));
					}
					uint32_t post_catch_handler_rva_local = static_cast<uint32_t>(post_catch_xdata.size());
					appendLE_xdata(post_catch_xdata, uint32_t(0));
					uint32_t post_catch_funcinfo_rva_local = static_cast<uint32_t>(post_catch_xdata.size());
					appendLE_xdata(post_catch_xdata, xdata_offset + cpp_funcinfo_local_offset);
					auto xdata_section_curr = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
					post_catch_xdata_offset = static_cast<uint32_t>(xdata_section_curr->get_data_size());
					add_data(post_catch_xdata, SectionType::XDATA);
					add_xdata_relocation(post_catch_xdata_offset + post_catch_handler_rva_local, "__CxxFrameHandler3");
					add_cpp_funcinfo_relocation(post_catch_xdata_offset, post_catch_funcinfo_rva_local);
					has_post_catch_xdata = true;
				}
				pending_pdata_entries.push_back({
					function_start + parent_range.start,
					function_start + parent_range.end,
					post_catch_xdata_offset
				});
			}
	}

	// Emit PDATA/XDATA for C++ catch funclets.
	// Catch handlers are emitted as real funclets with prologue:
	//   push rbp; sub rsp, 32; mov rbp, rdx
	if (is_cpp) {
			struct CatchFuncletMetadataRef {
				uint32_t start_rel;
				uint32_t end_rel;
				uint32_t funcinfo_rva;
			};
			struct CatchFuncletXdataResult {
				uint32_t xdata_offset;
				uint32_t funcinfo_rva;
			};
			std::vector<CatchFuncletMetadataRef> catch_funclet_metadata_refs;

			auto emit_cpp_metadata_relocations = [this](uint32_t record_xdata_offset,
			                                           const std::vector<uint32_t>& cpp_xdata_rva_field_offsets,
			                                           const std::vector<uint32_t>& cpp_text_rva_field_offsets) {
				auto* xdata_symbol = coffi_.get_symbol(".xdata");
				auto* text_symbol = coffi_.get_symbol(".text");
				auto xdata_sec = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];

				if (xdata_symbol) {
					for (uint32_t field_off : cpp_xdata_rva_field_offsets) {
						COFFI::rel_entry_generic reloc;
						reloc.virtual_address = record_xdata_offset + field_off;
						reloc.symbol_table_index = xdata_symbol->get_index();
						reloc.type = IMAGE_REL_AMD64_ADDR32NB;
						xdata_sec->add_relocation_entry(&reloc);
					}
				}

				if (text_symbol) {
					for (uint32_t field_off : cpp_text_rva_field_offsets) {
						COFFI::rel_entry_generic reloc;
						reloc.virtual_address = record_xdata_offset + field_off;
						reloc.symbol_table_index = text_symbol->get_index();
						reloc.type = IMAGE_REL_AMD64_ADDR32NB;
						xdata_sec->add_relocation_entry(&reloc);
					}
				}
			};

			auto collect_funclet_try_blocks = [&](uint32_t funclet_start_rel, uint32_t funclet_end_rel) {
				std::vector<TryBlockInfo> funclet_try_blocks;
				for (const auto& candidate_try_block : try_blocks) {
					if (candidate_try_block.try_start_offset < funclet_start_rel || candidate_try_block.try_end_offset > funclet_end_rel) {
						continue;
					}

					TryBlockInfo local_try_block = candidate_try_block;
					local_try_block.try_start_offset -= funclet_start_rel;
					local_try_block.try_end_offset -= funclet_start_rel;
					for (auto& local_handler : local_try_block.catch_handlers) {
						local_handler.handler_offset -= funclet_start_rel;
						local_handler.handler_end_offset -= funclet_start_rel;
						if (local_handler.funclet_entry_offset != 0) {
							local_handler.funclet_entry_offset -= funclet_start_rel;
						}
						if (local_handler.funclet_end_offset != 0) {
							local_handler.funclet_end_offset -= funclet_start_rel;
						}
					}
					funclet_try_blocks.push_back(std::move(local_try_block));
				}
				return funclet_try_blocks;
			};

			auto emit_catch_funclet_xdata = [this, function_start, effective_frame_size = unwind_info.effective_frame_size, &add_cpp_funcinfo_relocation, &emit_cpp_metadata_relocations](
				bool zero_prologue,
				std::string_view funclet_metadata_name,
				uint32_t funclet_start_rel,
				uint32_t funclet_end_rel,
				const std::vector<TryBlockInfo>& funclet_try_blocks,
				uint32_t inherited_funcinfo_rva) -> CatchFuncletXdataResult {
			std::vector<char> catch_xdata = {
				static_cast<char>(0x19),
				static_cast<char>(zero_prologue ? 0x00 : 0x0A),
				static_cast<char>(0x02),
				static_cast<char>(0x00),
				static_cast<char>(0x0A),
				static_cast<char>(0x32),
				static_cast<char>(0x06),
				static_cast<char>(0x50)
			};

			uint32_t catch_handler_rva_local = static_cast<uint32_t>(catch_xdata.size());
			appendLE_xdata(catch_xdata, uint32_t(0));
			uint32_t catch_funcinfo_rva_local = static_cast<uint32_t>(catch_xdata.size());
				appendLE_xdata(catch_xdata, uint32_t(0));

			auto xdata_section_curr = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
			uint32_t catch_xdata_offset = static_cast<uint32_t>(xdata_section_curr->get_data_size());
				bool has_local_cpp_metadata = !funclet_try_blocks.empty();
				if (has_local_cpp_metadata) {
					uint32_t local_cpp_funcinfo_offset = 0;
					std::vector<uint32_t> local_cpp_xdata_rva_field_offsets;
					std::vector<uint32_t> local_cpp_text_rva_field_offsets;
					build_cpp_exception_metadata(catch_xdata,
						catch_xdata_offset,
						function_start + funclet_start_rel,
						funclet_end_rel - funclet_start_rel,
						funclet_metadata_name,
						funclet_try_blocks,
						{},
						effective_frame_size,
						0,
						catch_funcinfo_rva_local,
						true,
						local_cpp_funcinfo_offset,
						local_cpp_xdata_rva_field_offsets,
						local_cpp_text_rva_field_offsets);
					add_data(catch_xdata, SectionType::XDATA);
					add_xdata_relocation(catch_xdata_offset + catch_handler_rva_local, "__CxxFrameHandler3");
					emit_cpp_metadata_relocations(catch_xdata_offset, local_cpp_xdata_rva_field_offsets, local_cpp_text_rva_field_offsets);
					return {catch_xdata_offset, catch_xdata_offset + local_cpp_funcinfo_offset};
				}

				patch_xdata_u32(catch_xdata, catch_funcinfo_rva_local, inherited_funcinfo_rva);
			add_data(catch_xdata, SectionType::XDATA);
			add_xdata_relocation(catch_xdata_offset + catch_handler_rva_local, "__CxxFrameHandler3");
			add_cpp_funcinfo_relocation(catch_xdata_offset, catch_funcinfo_rva_local);
				return {catch_xdata_offset, inherited_funcinfo_rva};
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

				std::vector<RelativeRange> nested_excluded_ranges;
				for (const auto& range : all_catch_funclet_ranges) {
					if (range.start > handler_start_rel && range.end < handler_end_rel) {
						nested_excluded_ranges.push_back(range);
					}
				}
				for (const auto& range : cleanup_funclet_ranges) {
					if (range.start > handler_start_rel && range.end < handler_end_rel) {
						nested_excluded_ranges.push_back(range);
					}
				}

				std::sort(nested_excluded_ranges.begin(), nested_excluded_ranges.end(), [](const RelativeRange& a, const RelativeRange& b) {
					if (a.start != b.start) {
						return a.start < b.start;
					}
					return a.end < b.end;
				});

				std::vector<RelativeRange> handler_segments;
				uint32_t segment_cursor = handler_start_rel;
				for (const auto& excluded_range : nested_excluded_ranges) {
					if (segment_cursor < excluded_range.start) {
						handler_segments.push_back({segment_cursor, excluded_range.start});
					}
					if (excluded_range.end > segment_cursor) {
						segment_cursor = excluded_range.end;
					}
				}
				if (segment_cursor < handler_end_rel) {
					handler_segments.push_back({segment_cursor, handler_end_rel});
				}
				if (handler_segments.empty()) {
					handler_segments.push_back({handler_start_rel, handler_end_rel});
				}

					std::vector<TryBlockInfo> funclet_try_blocks = collect_funclet_try_blocks(handler_start_rel, handler_end_rel);
					std::string funclet_metadata_name = std::string(mangled_name) + "$catchfunclet$" + std::to_string(handler_start_rel);
					uint32_t inherited_funcinfo_rva = xdata_offset + cpp_funcinfo_local_offset;
					uint32_t best_enclosing_size = UINT32_MAX;
					for (const auto& metadata_ref : catch_funclet_metadata_refs) {
						if (handler_start_rel >= metadata_ref.start_rel && handler_end_rel <= metadata_ref.end_rel) {
							uint32_t enclosing_size = metadata_ref.end_rel - metadata_ref.start_rel;
							if (enclosing_size < best_enclosing_size) {
								best_enclosing_size = enclosing_size;
								inherited_funcinfo_rva = metadata_ref.funcinfo_rva;
							}
						}
					}

					auto catch_xdata_result = emit_catch_funclet_xdata(false, funclet_metadata_name, handler_start_rel, handler_end_rel, funclet_try_blocks, inherited_funcinfo_rva);
					uint32_t catch_xdata_offset = catch_xdata_result.xdata_offset;
					uint32_t funclet_funcinfo_rva = catch_xdata_result.funcinfo_rva;
					if (!funclet_try_blocks.empty()) {
						catch_funclet_metadata_refs.push_back({handler_start_rel, handler_end_rel, funclet_funcinfo_rva});
					}
				uint32_t catch_continuation_xdata_offset = 0;
				bool has_catch_continuation_xdata = false;
				for (size_t segment_index = 0; segment_index < handler_segments.size(); ++segment_index) {
					const auto& segment = handler_segments[segment_index];
					if (segment.end <= segment.start) {
						continue;
					}

					uint32_t segment_xdata_offset = catch_xdata_offset;
					if (segment_index > 0) {
						if (!has_catch_continuation_xdata) {
								catch_continuation_xdata_offset = emit_catch_funclet_xdata(true, funclet_metadata_name, handler_start_rel, handler_end_rel, {}, funclet_funcinfo_rva).xdata_offset;
							has_catch_continuation_xdata = true;
						}
						segment_xdata_offset = catch_continuation_xdata_offset;
					}

					pending_pdata_entries.push_back({
						function_start + segment.start,
						function_start + segment.end,
						segment_xdata_offset
					});
				}
			}
		}

			for (const auto& cleanup_range : cleanup_funclet_ranges) {
				std::vector<char> cleanup_xdata = {
					static_cast<char>(0x01),
					static_cast<char>(0x06),
					static_cast<char>(0x02),
					static_cast<char>(0x00),
					static_cast<char>(0x06),
					static_cast<char>(0x32),
					static_cast<char>(0x02),
					static_cast<char>(0x50)
				};

				auto xdata_section_curr = coffi_.get_sections()[sectiontype_to_index[SectionType::XDATA]];
				uint32_t cleanup_xdata_offset = static_cast<uint32_t>(xdata_section_curr->get_data_size());
				add_data(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(cleanup_xdata.data()), cleanup_xdata.size()), SectionType::XDATA);
				pending_pdata_entries.push_back({
					function_start + cleanup_range.start,
					function_start + cleanup_range.end,
					cleanup_xdata_offset
				});
			}
	}

	std::sort(pending_pdata_entries.begin(), pending_pdata_entries.end(), [](const ObjectFileWriter::PendingPdataEntry& a, const ObjectFileWriter::PendingPdataEntry& b) {
		if (a.begin_rva != b.begin_rva) {
			return a.begin_rva < b.begin_rva;
		}
		return a.end_rva < b.end_rva;
	});

	for (const auto& entry : pending_pdata_entries) {
		auto pdata_section = coffi_.get_sections()[sectiontype_to_index[SectionType::PDATA]];
		uint32_t pdata_offset = static_cast<uint32_t>(pdata_section->get_data_size());
		std::vector<char> pdata(12);
		*reinterpret_cast<uint32_t*>(&pdata[0]) = entry.begin_rva;
		*reinterpret_cast<uint32_t*>(&pdata[4]) = entry.end_rva;
		*reinterpret_cast<uint32_t*>(&pdata[8]) = entry.unwind_rva;
		add_data(pdata, SectionType::PDATA);
		add_pdata_relocations(pdata_offset, mangled_name, entry.unwind_rva);
	}
}

// --- Main exception info function ---
