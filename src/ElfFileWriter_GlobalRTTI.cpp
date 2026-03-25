#include "ObjFileWriter.h"
#include "ElfFileWriter.h"

// ElfFileWriter_GlobalRTTI.cpp - Out-of-line method definitions for ElfFileWriter
// Part of ElfFileWriter class (unity build)

namespace {
	/// Collect all unique virtual-base TypeIndex values reachable from \p si
	/// (depth-first, left-to-right).  Adds to \p out; \p visited prevents cycles.
	void collectReachableVBases(const StructTypeInfo* si,
	                            std::vector<TypeIndex>& out,
	                            std::set<TypeIndex>& seen_vb,
	                            std::set<const StructTypeInfo*>& visited) {
		if (!si || !visited.insert(si).second) return;
		for (const auto& b : si->base_classes) {
			if (b.is_virtual && seen_vb.insert(b.type_index).second) {
				out.push_back(b.type_index);
			}
			if (!b.is_virtual && b.type_index.index() < getTypeInfoCount()) {
				const auto* bsi = getTypeInfo(b.type_index).getStructInfo();
				collectReachableVBases(bsi, out, seen_vb, visited);
			}
		}
	}

	/// Recursively find the offset of a virtual base \p target_tidx from the
	/// most-derived object starting at \p si + \p base_off.
	/// Returns true and sets \p result if found.
	bool findVBaseOffset(const StructTypeInfo* si, TypeIndex target_tidx,
	                     size_t base_off, size_t& result,
	                     std::set<const StructTypeInfo*>& visited) {
		if (!si || !visited.insert(si).second) return false;
		for (const auto& b : si->base_classes) {
			if (b.type_index == target_tidx && b.is_virtual) {
				result = base_off + b.offset;
				return true;
			}
		}
		// Recurse into all bases (both virtual and non-virtual) to locate deep vbases.
		// The visited set prevents infinite loops.
		for (const auto& b : si->base_classes) {
			if (b.type_index.index() < getTypeInfoCount()) {
				const auto* bsi = getTypeInfo(b.type_index).getStructInfo();
				if (findVBaseOffset(bsi, target_tidx, base_off + b.offset, result, visited))
					return true;
			}
		}
		return false;
	}
}

void ElfFileWriter::add_global_variable_data(std::string_view var_name, size_t size_in_bytes,
                              bool is_initialized, std::span<const char> init_data, bool is_rodata) {
	if (g_enable_debug_output) {
		std::cerr << "Adding global variable: " << var_name 
		          << " size=" << size_in_bytes 
		          << " initialized=" << is_initialized
		          << " rodata=" << is_rodata << std::endl;
	}

	ELFIO::section* section;
	if (is_rodata) {
		section = getSectionByName(".rodata");
		if (!section) {
			throw std::runtime_error(".rodata section not found");
		}

		uint32_t offset = section->get_size();

		if (!init_data.empty()) {
			section->append_data(init_data.data(), init_data.size());
		} else {
			std::vector<char> zero_data(size_in_bytes, 0);
			section->append_data(zero_data.data(), zero_data.size());
		}

		getOrCreateSymbol(var_name, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL,
		                 section->get_index(), offset, init_data.empty() ? size_in_bytes : init_data.size());
	} else if (is_initialized) {
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
void ElfFileWriter::add_typeinfo(std::string_view typeinfo_symbol, const void* typeinfo_data, size_t typeinfo_size) {
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
std::string ElfFileWriter::get_or_create_builtin_typeinfo(Type type) {
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
std::string ElfFileWriter::get_or_create_class_typeinfo(std::string_view class_name) {
	// Itanium C++ ABI class type_info: _ZTI + length + name
	// Example: class "Foo" -> "_ZTI3Foo"
	StringBuilder builder;
	builder.append("_ZTI").append(class_name.length()).append(class_name);
	std::string typeinfo_symbol(builder.commit());

	// Check if we've already created this symbol
	if (created_class_typeinfos_.find(typeinfo_symbol) != created_class_typeinfos_.end()) {
		return typeinfo_symbol;
	}

	// Build the type-name symbol: _ZTS + length + name
	std::string typename_symbol = "_ZTS" + std::to_string(class_name.length()) + std::string(class_name);

	// ------------------------------------------------------------------
	// 1. Create _ZTS<classname> in .rodata: the null-terminated type name
	//    string, e.g. "11MyException\0" for class MyException.
	// ------------------------------------------------------------------
	std::string type_name_str = std::to_string(class_name.length()) + std::string(class_name);
	auto* rodata = getSectionByName(".rodata");
	if (!rodata) {
		throw std::runtime_error(".rodata section not found");
	}
	uint32_t name_offset = rodata->get_size();
	rodata->append_data(type_name_str.c_str(), type_name_str.size() + 1);  // +1 for '\0'

	// Expose _ZTS as a weak symbol (may be defined in multiple TUs)
	getOrCreateSymbol(typename_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK,
	                  rodata->get_index(), name_offset, type_name_str.size() + 1);

	// ------------------------------------------------------------------
	// 2. Create _ZTI<classname> in .data.rel.ro: 16-byte structure with
	//    two 8-byte absolute pointers filled in by linker relocations:
	//      [0] vtable: _ZTVN10__cxxabiv117__class_type_infoE + 16
	//      [8] name:   _ZTS<classname>
	// ------------------------------------------------------------------
	// Get or create the .data.rel.ro section
	auto* data_rel_ro = getSectionByName(".data.rel.ro");
	if (!data_rel_ro) {
		data_rel_ro = elf_writer_.sections.add(".data.rel.ro");
		data_rel_ro->set_type(ELFIO::SHT_PROGBITS);
		data_rel_ro->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
		data_rel_ro->set_addr_align(8);
		// Keep section_name_cache_ in sync
		section_name_cache_[".data.rel.ro"] = data_rel_ro;
	}

	const char zeros[16] = {};
	uint32_t ti_offset = data_rel_ro->get_size();
	data_rel_ro->append_data(zeros, 16);

	// Expose _ZTI as a weak symbol so multiple TUs can define it
	getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK,
	                  data_rel_ro->get_index(), ti_offset, 16);

	// ------------------------------------------------------------------
	// 3. Add R_X86_64_64 relocations for both pointer slots
	// ------------------------------------------------------------------
	// Get or create .rela.data.rel.ro
	ELFIO::relocation_section_accessor* rela_acc = getRelocationAccessor(".rela.data.rel.ro");
	if (!rela_acc) {
		auto* rela_sec = elf_writer_.sections.add(".rela.data.rel.ro");
		rela_sec->set_type(ELFIO::SHT_RELA);
		rela_sec->set_flags(ELFIO::SHF_INFO_LINK);
		rela_sec->set_info(data_rel_ro->get_index());
		rela_sec->set_link(symtab_section_->get_index());
		rela_sec->set_addr_align(8);
		rela_sec->set_entry_size(elf_writer_.get_default_entry_size(ELFIO::SHT_RELA));
		rela_accessors_[".rela.data.rel.ro"] =
			std::make_unique<ELFIO::relocation_section_accessor>(elf_writer_, rela_sec);
		rela_acc = rela_accessors_[".rela.data.rel.ro"].get();
	}

	// Relocation 1: vtable pointer
	// __class_type_info vtable is _ZTVN10__cxxabiv117__class_type_infoE.
	// The typeinfo vtable pointer points 16 bytes into the vtable (past the
	// offset-to-top and RTTI pointer fields), i.e., addend = 16.
	auto vtable_sym_idx = getOrCreateSymbol(
		"_ZTVN10__cxxabiv117__class_type_infoE", ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);
	rela_acc->add_entry(ti_offset,     vtable_sym_idx, ELFIO::R_X86_64_64, 16);

	// Relocation 2: type name pointer
	auto name_sym_idx = getOrCreateSymbol(typename_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK);
	rela_acc->add_entry(ti_offset + 8, name_sym_idx,   ELFIO::R_X86_64_64, 0);

	created_class_typeinfos_.insert(typeinfo_symbol);

	FLASH_LOG_FORMAT(Codegen, Debug, "Created class typeinfo '{}' for class '{}' with ZTS '{}'",
	                 typeinfo_symbol, class_name, typename_symbol);

	return typeinfo_symbol;
}

/**
 * @brief Get or create type_info symbol for a class type with inheritance hierarchy support.
 *
 * Emits the correct Itanium C++ ABI type_info based on the class's bases:
 *   - No bases          → __class_type_info    (16 bytes)
 *   - Single non-virt.  → __si_class_type_info (24 bytes)
 *   - Multiple / virtual → __vmi_class_type_info (variable)
 *
 * Falls back to the flat __class_type_info overload when struct_info is null.
 */
std::string ElfFileWriter::get_or_create_class_typeinfo(const StructTypeInfo* struct_info) {
	if (!struct_info) {
		return {};
	}

	std::string_view class_name = StringTable::getStringView(struct_info->getName());

	// Build _ZTI symbol name
	StringBuilder builder;
	builder.append("_ZTI").append(class_name.length()).append(class_name);
	std::string typeinfo_symbol(builder.commit());

	// Reuse the same cache as the flat overload so delegation for classes without bases
	// cannot re-emit _ZTS/_ZTI symbols on a subsequent hierarchical lookup.
	if (created_class_typeinfos_.find(typeinfo_symbol) != created_class_typeinfos_.end()) {
		return typeinfo_symbol;
	}

	const auto& base_classes = struct_info->base_classes;

	// Choose hierarchy variant
	if (base_classes.empty()) {
		// Delegate to the flat overload (no inheritance)
		return get_or_create_class_typeinfo(class_name);
	}

	// Recursively ensure base class type_infos exist first
	for (const auto& base : base_classes) {
		if (base.type_index.index() < getTypeInfoCount()) {
			const TypeInfo& base_ti = getTypeInfo(base.type_index);
			const StructTypeInfo* base_si = base_ti.getStructInfo();
			if (base_si) {
				get_or_create_class_typeinfo(base_si);
			}
		}
	}

	// Build _ZTS (type name string) in .rodata
	std::string typename_symbol = "_ZTS" + std::to_string(class_name.length()) + std::string(class_name);
	std::string type_name_str   = std::to_string(class_name.length()) + std::string(class_name);

	auto* rodata = getSectionByName(".rodata");
	if (!rodata) throw std::runtime_error(".rodata section not found");

	uint32_t name_offset = rodata->get_size();
	rodata->append_data(type_name_str.c_str(), type_name_str.size() + 1);

	getOrCreateSymbol(typename_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK,
	                  rodata->get_index(), name_offset, type_name_str.size() + 1);

	// Ensure .data.rel.ro and its relocation section exist
	auto* data_rel_ro = getSectionByName(".data.rel.ro");
	if (!data_rel_ro) {
		data_rel_ro = elf_writer_.sections.add(".data.rel.ro");
		data_rel_ro->set_type(ELFIO::SHT_PROGBITS);
		data_rel_ro->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
		data_rel_ro->set_addr_align(8);
		section_name_cache_[".data.rel.ro"] = data_rel_ro;
	}

	ELFIO::relocation_section_accessor* rela_acc = getRelocationAccessor(".rela.data.rel.ro");
	if (!rela_acc) {
		auto* rela_sec = elf_writer_.sections.add(".rela.data.rel.ro");
		rela_sec->set_type(ELFIO::SHT_RELA);
		rela_sec->set_flags(ELFIO::SHF_INFO_LINK);
		rela_sec->set_info(data_rel_ro->get_index());
		rela_sec->set_link(symtab_section_->get_index());
		rela_sec->set_addr_align(8);
		rela_sec->set_entry_size(elf_writer_.get_default_entry_size(ELFIO::SHT_RELA));
		rela_accessors_[".rela.data.rel.ro"] =
			std::make_unique<ELFIO::relocation_section_accessor>(elf_writer_, rela_sec);
		rela_acc = rela_accessors_[".rela.data.rel.ro"].get();
	}

	bool single_non_virtual = (base_classes.size() == 1 && !base_classes[0].is_virtual);

	if (single_non_virtual) {
		// ------------------------------------------------------------------
		// __si_class_type_info: 24 bytes
		//   [0]  vtable ptr  → _ZTVN10__cxxabiv120__si_class_type_infoE + 16
		//   [8]  name ptr    → _ZTS<classname>
		//   [16] base ptr    → _ZTI<base_classname>
		// ------------------------------------------------------------------
		const char zeros[24] = {};
		uint32_t ti_offset = data_rel_ro->get_size();
		data_rel_ro->append_data(zeros, 24);

		getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK,
		                  data_rel_ro->get_index(), ti_offset, 24);

		// Reloc 1: vtable
		auto vtbl_sym = getOrCreateSymbol(
			"_ZTVN10__cxxabiv120__si_class_type_infoE", ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);
		rela_acc->add_entry(ti_offset,      vtbl_sym, ELFIO::R_X86_64_64, 16);

		// Reloc 2: name
		auto name_sym = getOrCreateSymbol(typename_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK);
		rela_acc->add_entry(ti_offset + 8,  name_sym, ELFIO::R_X86_64_64, 0);

		// Reloc 3: base type_info
		const auto& base = base_classes[0];
		std::string base_zti;
		if (base.type_index.index() < getTypeInfoCount()) {
			const TypeInfo& base_ti = getTypeInfo(base.type_index);
			const StructTypeInfo* base_si = base_ti.getStructInfo();
			if (base_si) {
				std::string_view base_name = StringTable::getStringView(base_si->getName());
				StringBuilder b2;
				b2.append("_ZTI").append(base_name.length()).append(base_name);
				base_zti = std::string(b2.commit());
			}
		}
		if (!base_zti.empty()) {
			auto base_sym = getOrCreateSymbol(base_zti, ELFIO::STT_OBJECT, ELFIO::STB_WEAK);
			rela_acc->add_entry(ti_offset + 16, base_sym, ELFIO::R_X86_64_64, 0);
		}

		FLASH_LOG_FORMAT(Codegen, Debug,
			"Created SI class typeinfo '{}' for '{}' (single base)",
			typeinfo_symbol, class_name);
	} else {
		// ------------------------------------------------------------------
		// __vmi_class_type_info: 24 + N*16 bytes
		//   [0]   vtable ptr → _ZTVN10__cxxabiv121__vmi_class_type_infoE + 16
		//   [8]   name ptr   → _ZTS<classname>
		//   [16]  flags      (uint32) — filled inline
		//   [20]  base_count (uint32) — filled inline
		//   [24 + i*16] base_type ptr  → _ZTI<base_i>
		//   [32 + i*16] offset_flags   (int64) — filled inline
		// ------------------------------------------------------------------
		uint32_t n_bases  = static_cast<uint32_t>(base_classes.size());
		uint32_t ti_size  = 24 + n_bases * 16;
		std::vector<char> zeros(ti_size, 0);

		// Fill in inline (non-pointer) fields
		// Itanium ABI flags for __vmi_class_type_info:
		//   bit 0 (__non_diamond_repeat_mask = 0x1): class has a base appearing more than once
		//                                            (but not in a diamond pattern, i.e. virtual bases)
		//   bit 1 (__diamond_shaped_mask = 0x2):     class has a diamond-shaped inheritance graph
		//                                            (multiple paths to the same virtual base)
		//
		// IMPORTANT: setting __diamond_shaped_mask (0x2) incorrectly for non-diamond classes
		// causes the personality routine to attempt virtual-base-table traversal, which crashes
		// because non-virtual multiple-inheritance classes have no VTT.
		// Only set flags when the corresponding pattern genuinely exists.
		uint32_t flags = 0;
		bool has_virtual = false;
		for (const auto& base : base_classes) {
			if (base.is_virtual) {
				has_virtual = true;
			}
		}
		// __non_diamond_repeat_mask: set only when there are virtual bases
		// (which may repeat in the hierarchy)
		if (has_virtual) flags |= 0x1;

		// __diamond_shaped_mask (0x2): set when two or more bases share a common
		// virtual base ancestor (diamond inheritance).  Detect by collecting all
		// transitive virtual-base TypeIndex values reachable from each direct base
		// and checking for overlap.
		{
			std::set<TypeIndex> global_vbases;
			bool diamond = false;
			for (const auto& base : base_classes) {
				if (base.type_index.index() >= getTypeInfoCount()) continue;
				const auto* bsi = getTypeInfo(base.type_index).getStructInfo();
				std::vector<TypeIndex> branch_vbases;
				std::set<TypeIndex> branch_seen;
				std::set<const StructTypeInfo*> branch_visited;
				collectReachableVBases(bsi, branch_vbases, branch_seen, branch_visited);
				for (auto vb : branch_vbases) {
					if (!global_vbases.insert(vb).second) {
						diamond = true;
					}
				}
			}
			if (diamond) flags |= 0x2;
		}

		std::memcpy(zeros.data() + 16, &flags,   sizeof(uint32_t));
		std::memcpy(zeros.data() + 20, &n_bases, sizeof(uint32_t));

		// Collect all unique virtual bases reachable from this class (depth-first,
		// left-to-right, same order as the vtable prefix). This is needed to compute
		// the correct vtable-relative offset for virtual base entries.
		std::vector<TypeIndex> vbase_order;
		{
			std::set<TypeIndex> seen_vb;
			std::set<const StructTypeInfo*> visited;
			collectReachableVBases(struct_info, vbase_order, seen_vb, visited);
		}

		// offset_flags for each base (inline: no relocation needed)
		for (uint32_t i = 0; i < n_bases; ++i) {
			const auto& base = base_classes[i];
			// offset_flags = (offset_bytes << 8) | public_mask | virtual_mask
			// public_mask = 0x2, virtual_mask = 0x1
			uint64_t public_bit  = (base.access == AccessSpecifier::Public) ? 0x2ULL : 0ULL;
			uint64_t virtual_bit = base.is_virtual ? 0x1ULL : 0ULL;

			int64_t offset_value;
			if (base.is_virtual) {
				// For virtual bases, the Itanium ABI stores the byte offset from the
				// vptr to the vtable slot that holds the actual vbase offset at runtime.
				// Vtable layout: [...vbase_offsets..., offset_to_top, RTTI, func_ptrs]
				// The vptr points to func_ptrs[0].  vtable[-1] = RTTI, [-2] = offset_to_top,
				// [-3] = first vbase_offset, [-4] = second, etc.
				// So the offset for vbase at index k is -(3 + k) * 8.
				auto it = std::find(vbase_order.begin(), vbase_order.end(), base.type_index);
				size_t vbase_idx = (it != vbase_order.end())
					? static_cast<size_t>(std::distance(vbase_order.begin(), it))
					: 0;
				offset_value = -static_cast<int64_t>((3 + vbase_idx) * 8);
			} else {
				offset_value = static_cast<int64_t>(base.offset);
			}

			uint64_t offset_flags = (static_cast<uint64_t>(offset_value) << 8) | public_bit | virtual_bit;
			std::memcpy(zeros.data() + 32 + i * 16, &offset_flags, sizeof(uint64_t));
		}

		uint32_t ti_offset = data_rel_ro->get_size();
		data_rel_ro->append_data(zeros.data(), ti_size);

		getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK,
		                  data_rel_ro->get_index(), ti_offset, ti_size);

		// Reloc 1: vtable
		auto vtbl_sym = getOrCreateSymbol(
			"_ZTVN10__cxxabiv121__vmi_class_type_infoE", ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);
		rela_acc->add_entry(ti_offset, vtbl_sym, ELFIO::R_X86_64_64, 16);

		// Reloc 2: name
		auto name_sym = getOrCreateSymbol(typename_symbol, ELFIO::STT_OBJECT, ELFIO::STB_WEAK);
		rela_acc->add_entry(ti_offset + 8, name_sym, ELFIO::R_X86_64_64, 0);

		// Reloc per base: base_type pointer
		for (uint32_t i = 0; i < n_bases; ++i) {
			const auto& base = base_classes[i];
			std::string base_zti;
			if (base.type_index.index() < getTypeInfoCount()) {
				const TypeInfo& base_ti = getTypeInfo(base.type_index);
				const StructTypeInfo* base_si = base_ti.getStructInfo();
				if (base_si) {
					std::string_view base_name = StringTable::getStringView(base_si->getName());
					StringBuilder b2;
					b2.append("_ZTI").append(base_name.length()).append(base_name);
					base_zti = std::string(b2.commit());
				}
			}
			if (!base_zti.empty()) {
				auto base_sym = getOrCreateSymbol(base_zti, ELFIO::STT_OBJECT, ELFIO::STB_WEAK);
				rela_acc->add_entry(ti_offset + 24 + i * 16, base_sym, ELFIO::R_X86_64_64, 0);
			}
		}

		FLASH_LOG_FORMAT(Codegen, Debug,
			"Created VMI class typeinfo '{}' for '{}' ({} bases)",
			typeinfo_symbol, class_name, n_bases);
	}

	created_class_typeinfos_.insert(typeinfo_symbol);
	return typeinfo_symbol;
}

/**
 * @brief Add vtable for C++ class
 * Itanium C++ ABI vtable format: array of function pointers
 */
void ElfFileWriter::add_vtable(std::string_view vtable_symbol, 
               std::span<const std::string_view> function_symbols,
               std::string_view class_name,
               [[maybe_unused]] std::span<const std::string_view> base_class_names,
               [[maybe_unused]] std::span<const BaseClassDescriptorInfo> base_class_info,
               const RTTITypeInfo* rtti_info) {
	
	FLASH_LOG_FORMAT(Codegen, Debug, "Adding vtable '{}' for class {} with {} virtual functions",
	                 vtable_symbol, class_name, function_symbols.size());
	
	// Get .rodata section (vtables go in read-only data)
	auto* rodata = getSectionByName(".rodata");
	if (!rodata) {
		throw std::runtime_error(".rodata section not found");
	}
	
	// Itanium C++ ABI vtable structure:
	// - Offset to top (8 bytes) - always 0 for simple cases
	// - RTTI pointer (8 bytes) - pointer to typeinfo structure
	// - Function pointers (8 bytes each)
	
	// First, emit typeinfo if available (goes into .rodata BEFORE the vtable)
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
	
	// Capture vtable_offset AFTER typeinfo emission, since add_typeinfo also
	// appends to .rodata and would shift the vtable position.
	uint32_t vtable_offset = rodata->get_size();
	
	char vtable_data_buf[8192]; // Stack-based buffer for vtable (reasonable max size)
	size_t vtable_data_size = 0;
	
	auto append_bytes = [&](const void* data, size_t size) {
		if (vtable_data_size + size > sizeof(vtable_data_buf)) {
			throw std::runtime_error("Vtable too large for stack buffer");
		}
		std::memcpy(vtable_data_buf + vtable_data_size, data, size);
		vtable_data_size += size;
	};

	// Collect unique virtual bases for this class (depth-first, left-to-right)
	// so we can emit vbase offset entries in the vtable prefix.
	struct VBaseEntry { size_t offset_from_derived; };
	std::vector<VBaseEntry> vbase_entries;
	{
		// Find the StructTypeInfo for this class by scanning gTypeInfo
		const StructTypeInfo* this_struct = nullptr;
		StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_name);
		for (size_t ti = 0; ti < getTypeInfoCount(); ++ti) {
			const StructTypeInfo* si = getTypeInfo(TypeIndex{ti}).getStructInfo();
			if (si && si->getName() == class_name_handle) {
				this_struct = si;
				break;
			}
		}
		if (this_struct) {
			// Collect all unique virtual base TypeIndexes reachable from this class.
			std::vector<TypeIndex> vbase_type_indices;
			{
				std::set<TypeIndex> seen;
				std::set<const StructTypeInfo*> visited;
				collectReachableVBases(this_struct, vbase_type_indices, seen, visited);
			}

			// Now find each vbase's offset in THIS class (the most-derived).
			// Virtual bases are shared: their actual offset is stored in the
			// most-derived class's base_classes list or transitively through
			// non-virtual parents.
			for (TypeIndex vb_tidx : vbase_type_indices) {
				size_t offset = 0;
				std::set<const StructTypeInfo*> visited;
				findVBaseOffset(this_struct, vb_tidx, 0, offset, visited);
				vbase_entries.push_back({offset});
			}
		}
	}
	size_t n_vbase_entries = vbase_entries.size();

	FLASH_LOG_FORMAT(Codegen, Debug, "  vtable '{}': {} vbase entries in prefix",
	                 vtable_symbol, n_vbase_entries);
	for (size_t i = 0; i < n_vbase_entries; ++i) {
		FLASH_LOG_FORMAT(Codegen, Debug, "    vbase[{}] offset_from_derived={}",
		                 i, vbase_entries[i].offset_from_derived);
	}

	// Emit vbase offset entries (before offset_to_top).
	// Itanium ABI vtable layout for classes with virtual bases:
	//   [vbase_offset[n-1]] [vbase_offset[n-2]] ... [vbase_offset[0]] [offset_to_top] [RTTI] [func_ptrs...]
	// The vptr points to func_ptrs[0].
	// vbase_offset[i] is at vtable[-(3+i)] i.e. -(3+i)*8 bytes from the vptr.
	for (size_t i = n_vbase_entries; i > 0; --i) {
		int64_t vbase_off = static_cast<int64_t>(vbase_entries[i - 1].offset_from_derived);
		append_bytes(&vbase_off, 8);
	}
	
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
	
	// Header size = vbase prefix + offset_to_top + RTTI
	uint32_t header_size = static_cast<uint32_t>(n_vbase_entries * 8 + 16);
	
	// Add vtable symbol pointing to the function pointer array (skip prefix, offset-to-top, and RTTI)
	uint32_t symbol_offset = vtable_offset + header_size;
	getOrCreateSymbol(vtable_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL, 
	                  rodata->get_index(), symbol_offset, vtable_data_size - header_size);
	
	// Add relocations for each function pointer
	[[maybe_unused]] auto* rela_rodata = getOrCreateRelocationSection(".rodata");
	auto* rela_accessor = getRelocationAccessor(".rela.rodata");
	
	// Add relocation for RTTI pointer if typeinfo was emitted
	if (!typeinfo_symbol.empty()) {
		auto typeinfo_symbol_idx = getOrCreateSymbol(typeinfo_symbol, ELFIO::STT_OBJECT, ELFIO::STB_GLOBAL);
		// RTTI pointer is at vtable_offset + vbase_prefix_size + offset_to_top_size
		uint32_t rtti_reloc_offset = vtable_offset + static_cast<uint32_t>(n_vbase_entries * 8) + 8;
		rela_accessor->add_entry(rtti_reloc_offset, typeinfo_symbol_idx, ELFIO::R_X86_64_64, 0);
		
		FLASH_LOG_FORMAT(Codegen, Debug, "  Added relocation for typeinfo {} at offset {}", 
		                 typeinfo_symbol, rtti_reloc_offset);
	}
	
	for (size_t i = 0; i < function_symbols.size(); ++i) {
		// Get or create symbol for the function
		auto func_symbol_idx = getOrCreateSymbol(function_symbols[i], ELFIO::STT_NOTYPE, ELFIO::STB_GLOBAL);
		
		// Add relocation for this function pointer
		uint32_t reloc_offset = vtable_offset + header_size + static_cast<uint32_t>(i * 8);
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
std::string_view ElfFileWriter::generateMangledName(std::string_view name, const FunctionSignature& sig) {
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
		namespace_path,
		sig.linkage,
		sig.is_const
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
std::string_view ElfFileWriter::addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
                                const std::vector<TypeSpecifierNode>& parameter_types,
                                Linkage linkage, bool is_variadic) {
	FunctionSignature sig(return_type, parameter_types);
	sig.linkage = linkage;
	sig.is_variadic = is_variadic;
	return generateMangledName(name, sig);  // generateMangledName now returns string_view to stable storage
}

