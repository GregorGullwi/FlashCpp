	void add_global_variable_data(std::string_view var_name, size_t size_in_bytes,
	                              bool is_initialized, std::span<const char> init_data) {
		if (g_enable_debug_output) {
			std::cerr << "Adding global variable: " << var_name 
			          << " size=" << size_in_bytes 
			          << " initialized=" << is_initialized << std::endl;
		}

		ELFIO::section* section;
		if (is_initialized) {
			section = getSectionByName(".data");
			if (!section) {
				throw std::runtime_error(".data section not found");
			}

			uint32_t offset = section->get_size();

			// Add initialized data
			if (!init_data.empty()) {
				section->append_data(init_data.data(), init_data.size());
			} else {
				// Zero-initialized
				std::vector<char> zero_data(size_in_bytes, 0);
				section->append_data(zero_data.data(), zero_data.size());
			}

			// Add symbol
			getOrCreateSymbol(var_name, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
			                 section->get_index(), offset, size_in_bytes);
		} else {
			// Uninitialized - goes in .bss
			section = getSectionByName(".bss");
			if (!section) {
				throw std::runtime_error(".bss section not found");
			}

			uint32_t offset = section->get_size();

			// For .bss, we just increase the size (no actual data)
			section->set_size(section->get_size() + size_in_bytes);

			// Add symbol
			getOrCreateSymbol(var_name, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
			                 section->get_index(), offset, size_in_bytes);
		}
	}

	/**
	 * @brief Add typeinfo symbol for RTTI (Itanium C++ ABI)
	 * @param typeinfo_symbol Symbol name (e.g., "_ZTI4Base" for typeinfo for Base)
	 * @param typeinfo_data Pointer to the typeinfo structure
	 * @param typeinfo_size Size of the typeinfo structure in bytes
	 */
	void add_typeinfo(std::string_view typeinfo_symbol, const void* typeinfo_data, size_t typeinfo_size) {
		FLASH_LOG_FORMAT(Codegen, Debug, "Adding typeinfo '{}' of size {}", typeinfo_symbol, typeinfo_size);
		
		// Get .rodata section (typeinfo goes in read-only data)
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		
		uint32_t typeinfo_offset = rodata->get_size();
		
		// Add typeinfo data to .rodata
		rodata->append_data(reinterpret_cast<const char*>(typeinfo_data), typeinfo_size);
		
		// Add typeinfo symbol
		getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
		                  rodata->get_index(), typeinfo_offset, typeinfo_size);
	}

	/**
	 * @brief Get type_info symbol name for a built-in type
	 * @param type The type to get type_info for
	 * @return Symbol name for the type_info (e.g., "_ZTIi" for int)
	 * 
	 * For built-in types, the type_info is provided by the C++ runtime library (libstdc++/libc++).
	 * We only need to generate references to these external symbols - the actual type_info
	 * structures are defined in the runtime.
	 */
	std::string get_or_create_builtin_typeinfo(Type type) {
		// Map types to Itanium C++ ABI mangled type codes
		std::string_view type_code;
		switch (type) {
			case Type::Void: type_code = "v"; break;
			case Type::Bool: type_code = "b"; break;
			case Type::Char: type_code = "c"; break;
			case Type::UnsignedChar: type_code = "h"; break;
			case Type::Short: type_code = "s"; break;
			case Type::UnsignedShort: type_code = "t"; break;
			case Type::Int: type_code = "i"; break;
			case Type::UnsignedInt: type_code = "j"; break;
			case Type::Long: type_code = "l"; break;
			case Type::UnsignedLong: type_code = "m"; break;
			case Type::LongLong: type_code = "x"; break;
			case Type::UnsignedLongLong: type_code = "y"; break;
			case Type::Float: type_code = "f"; break;
			case Type::Double: type_code = "d"; break;
			case Type::LongDouble: type_code = "e"; break;
			default:
				// For non-builtin types, return empty string
				return "";
		}
		
		// Type info symbol: _ZTI + type_code (e.g., _ZTIi for int)
		char typeinfo_symbol_buf[8]; // "_ZTI" + single char + null = max 6 bytes
		std::snprintf(typeinfo_symbol_buf, sizeof(typeinfo_symbol_buf), "_ZTI%.*s", 
		              static_cast<int>(type_code.length()), type_code.data());
		std::string typeinfo_symbol(typeinfo_symbol_buf);
		
		// Built-in type_info symbols are external (provided by C++ runtime)
		// Just return the symbol name - the linker will resolve it
		FLASH_LOG_FORMAT(Codegen, Debug, "Using external typeinfo '{}' for type code '{}'", 
		                 typeinfo_symbol, type_code);
		
		return typeinfo_symbol;
	}

	/**
	 * @brief Get or create type_info symbol for a class type
	 * @param class_name The name of the class
	 * @return Symbol name for the type_info (e.g., "_ZTI7MyClass")
	 */
	std::string get_or_create_class_typeinfo(std::string_view class_name) {
		// Itanium C++ ABI class type_info: _ZTI + length + name
		// Example: class "Foo" -> "_ZTI3Foo"
		StringBuilder builder;
		builder.append("_ZTI").append(class_name.length()).append(class_name);
		std::string typeinfo_symbol(builder.commit());
		
		// Check if we've already created this symbol
		static std::set<std::string> created_class_typeinfos;
		if (created_class_typeinfos.find(typeinfo_symbol) != created_class_typeinfos.end()) {
			return typeinfo_symbol;
		}
		
		// For class types, create a minimal __class_type_info structure
		// This is just vtable pointer + name pointer (16 bytes total)
		char typeinfo_data_buf[16] = {0};
		
		// Add typeinfo data to .rodata
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		
		uint32_t typeinfo_offset = rodata->get_size();
		rodata->append_data(typeinfo_data_buf, sizeof(typeinfo_data_buf));
		
		// Add typeinfo symbol as weak (may be provided by other translation units)
		getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK,
		                  rodata->get_index(), typeinfo_offset, sizeof(typeinfo_data_buf));
		
		created_class_typeinfos.insert(typeinfo_symbol);
		
		FLASH_LOG_FORMAT(Codegen, Debug, "Created class typeinfo '{}' for class '{}'", 
		                 typeinfo_symbol, class_name);
		
		return typeinfo_symbol;
	}

	/**
	 * @brief Add vtable for C++ class
	 * Itanium C++ ABI vtable format: array of function pointers
	 */
	void add_vtable(std::string_view vtable_symbol, 
	               std::span<const std::string_view> function_symbols,
	               std::string_view class_name,
	               [[maybe_unused]] std::span<const std::string_view> base_class_names,
	               [[maybe_unused]] std::span<const BaseClassDescriptorInfo> base_class_info,
	               const RTTITypeInfo* rtti_info = nullptr) {
		
		FLASH_LOG_FORMAT(Codegen, Debug, "Adding vtable '{}' for class {} with {} virtual functions",
		                 vtable_symbol, class_name, function_symbols.size());
		
		// Get .rodata section (vtables go in read-only data)
		auto* rodata = getSectionByName(".rodata");
		if (!rodata) {
			throw std::runtime_error(".rodata section not found");
		}
		
		uint32_t vtable_offset = rodata->get_size();
		
		// Itanium C++ ABI vtable structure:
		// - Offset to top (8 bytes) - always 0 for simple cases
		// - RTTI pointer (8 bytes) - pointer to typeinfo structure
		// - Function pointers (8 bytes each)
		
		// First, emit typeinfo if available
		std::string typeinfo_symbol;
		if (rtti_info && rtti_info->itanium_type_info) {
			// Generate typeinfo symbol name: _ZTI + mangled class name
			// For now, use the class name length-prefixed
			StringBuilder typeinfo_builder;
			typeinfo_builder.append("_ZTI").append(class_name.length()).append(class_name);
			typeinfo_symbol = std::string(typeinfo_builder.commit());
			
			// Determine which typeinfo structure to emit based on kind
			if (rtti_info->itanium_kind == RTTITypeInfo::ItaniumTypeInfoKind::ClassTypeInfo) {
				add_typeinfo(typeinfo_symbol, rtti_info->itanium_type_info, sizeof(ItaniumClassTypeInfo));
			} else if (rtti_info->itanium_kind == RTTITypeInfo::ItaniumTypeInfoKind::SIClassTypeInfo) {
				add_typeinfo(typeinfo_symbol, rtti_info->itanium_type_info, sizeof(ItaniumSIClassTypeInfo));
			} else if (rtti_info->itanium_kind == RTTITypeInfo::ItaniumTypeInfoKind::VMIClassTypeInfo) {
				// Variable size - need to calculate
				const ItaniumVMIClassTypeInfo* vmi = static_cast<const ItaniumVMIClassTypeInfo*>(rtti_info->itanium_type_info);
				// Safe calculation - VMI always has at least 1 base class
				size_t vmi_size = sizeof(ItaniumVMIClassTypeInfo);
				if (vmi->base_count > 1) {
					vmi_size += (vmi->base_count - 1) * sizeof(ItaniumBaseClassTypeInfo);
				}
				add_typeinfo(typeinfo_symbol, rtti_info->itanium_type_info, vmi_size);
			}
		}
		
		char vtable_data_buf[8192]; // Stack-based buffer for vtable (reasonable max size)
		size_t vtable_data_size = 0;
		
		auto append_bytes = [&](const void* data, size_t size) {
			if (vtable_data_size + size > sizeof(vtable_data_buf)) {
				throw std::runtime_error("Vtable too large for stack buffer");
			}
			std::memcpy(vtable_data_buf + vtable_data_size, data, size);
			vtable_data_size += size;
		};
		
		// Offset to top (8 bytes, value = 0)
		uint64_t offset_to_top = 0;
		append_bytes(&offset_to_top, 8);
		
		// RTTI pointer (8 bytes, null for now)
		uint64_t rtti_ptr = 0;
		append_bytes(&rtti_ptr, 8);
		
		// Function pointers (8 bytes each, will be filled by relocations)
		uint64_t func_ptr = 0;
		for (size_t i = 0; i < function_symbols.size(); ++i) {
			append_bytes(&func_ptr, 8);
		}
		
		// Add vtable data to .rodata
		rodata->append_data(vtable_data_buf, vtable_data_size);
		
		// Add vtable symbol pointing to the function pointer array (skip offset-to-top and RTTI)
		uint32_t symbol_offset = vtable_offset + 16;  // Skip offset-to-top (8) and RTTI (8)
		getOrCreateSymbol(vtable_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
		                  rodata->get_index(), symbol_offset, vtable_data_size - 16);
		
		// Add relocations for each function pointer
		[[maybe_unused]] auto* rela_rodata = getOrCreateRelocationSection(".rodata");
		auto* rela_accessor = getRelocationAccessor(".rela.rodata");
		
		// Add relocation for RTTI pointer if typeinfo was emitted
		if (!typeinfo_symbol.empty()) {
			auto typeinfo_symbol_idx = getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL);
			uint32_t rtti_reloc_offset = vtable_offset + 8;  // Offset to top is first 8 bytes
			rela_accessor->add_entry(rtti_reloc_offset, typeinfo_symbol_idx, ELFIO::R_X86_64_64, 0);
			
			FLASH_LOG_FORMAT(Codegen, Debug, "  Added relocation for typeinfo {} at offset {}", 
			                 typeinfo_symbol, rtti_reloc_offset);
		}
		
		for (size_t i = 0; i < function_symbols.size(); ++i) {
			// Get or create symbol for the function
			auto func_symbol_idx = getOrCreateSymbol(function_symbols[i], ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);
			
			// Add relocation for this function pointer
			uint32_t reloc_offset = vtable_offset + 16 + (i * 8);  // Skip header + i*8 for function pointer
			rela_accessor->add_entry(reloc_offset, func_symbol_idx, ELFIO::R_X86_64_64, 0);
			
			FLASH_LOG_FORMAT(Codegen, Debug, "  Added relocation for function {} at offset {}", 
			                 function_symbols[i], reloc_offset);
		}
		
		FLASH_LOG_FORMAT(Codegen, Debug, "Vtable '{}' added at offset {} with {} bytes",
		                 vtable_symbol, symbol_offset, vtable_data_size);
	}

	// Note: Mangled names are pre-computed by the Parser.
	// All names are passed through as-is.

	/**
	 * @brief Generate mangled name using platform-appropriate mangling
	 * Returns string_view to stable storage in function_signatures_ map
	 */
	std::string_view generateMangledName(std::string_view name, const FunctionSignature& sig) {
		// Check linkage first - extern "C" always uses C linkage (unmangled)
		if (sig.linkage == Linkage::C) {
			// For C linkage, store the name in function_signatures_ to ensure stability
			std::string key(name);
			auto it = function_signatures_.find(key);
			if (it != function_signatures_.end()) {
				return std::string_view(it->first);  // Return view to key in map
			}
			// Store it
			function_signatures_[key] = sig;
			return std::string_view(function_signatures_.find(key)->first);
		}
		
		// Split namespace_name into components for mangling functions
		std::vector<std::string_view> namespace_path;
		if (!sig.namespace_name.empty()) {
			// Split by "::" delimiter
			size_t start = 0;
			size_t end = sig.namespace_name.find("::");
			while (end != std::string::npos) {
				namespace_path.push_back(std::string_view(sig.namespace_name.c_str() + start, end - start));
				start = end + 2;
				end = sig.namespace_name.find("::", start);
			}
			if (start < sig.namespace_name.size()) {
				namespace_path.push_back(std::string_view(sig.namespace_name.c_str() + start));
			}
		}
		
		// Use NameMangling::generateMangledName which handles both MSVC and Itanium
		NameMangling::MangledName mangled = NameMangling::generateMangledName(
			name,
			sig.return_type,
			sig.parameter_types,
			sig.is_variadic,
			sig.class_name,
			namespace_path
		);
		
		// Store in function_signatures_ map to ensure stable storage
		std::string key(mangled.view());
		function_signatures_[key] = sig;
		
		// Return string_view to the key in the map (stable storage)
		return std::string_view(function_signatures_.find(key)->first);
	}

	/**
	 * @brief Add function signature (for future name mangling)
	 * Returns std::string_view to stable storage in function_signatures_ map
	 */
	std::string_view addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
	                                const std::vector<TypeSpecifierNode>& parameter_types,
	                                Linkage linkage = Linkage::None, bool is_variadic = false) {
		FunctionSignature sig(return_type, parameter_types);
		sig.linkage = linkage;
		sig.is_variadic = is_variadic;
		return generateMangledName(name, sig);  // generateMangledName now returns string_view to stable storage
	}

