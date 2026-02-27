#pragma once
#include "AstNodeTypes_TypeSystem.h"

// Struct type information
struct StructTypeInfo {
	StringHandle name;
	std::vector<StructMember> members;
	std::vector<StructStaticMember> static_members;  // Static members
	std::vector<StructMemberFunction> member_functions;
	std::vector<BaseClassSpecifier> base_classes;  // Base classes for inheritance
	size_t total_size = 0;      // Total size of struct in bytes
	size_t alignment = 1;       // Alignment requirement of struct
	size_t custom_alignment = 0; // Custom alignment from alignas(n), 0 = use natural alignment
	size_t pack_alignment = 0;   // Pack alignment from #pragma pack(n), 0 = no packing
	size_t active_bitfield_unit_offset = 0;
	size_t active_bitfield_unit_size = 0;
	size_t active_bitfield_unit_alignment = 0;
	size_t active_bitfield_bits_used = 0;
	Type active_bitfield_type = Type::Invalid;
	AccessSpecifier default_access; // Default access for struct (public) vs class (private)
	bool is_union = false;      // True if this is a union (all members at offset 0)
	bool is_final = false;      // True if this class/struct is declared with 'final' keyword
	bool needs_default_constructor = false;  // True if struct needs an implicit default constructor

	// Deleted special member functions tracking
	bool has_deleted_default_constructor = false;  // True if default constructor is = delete
	bool has_deleted_copy_constructor = false;     // True if copy constructor is = delete
	bool has_deleted_move_constructor = false;     // True if move constructor is = delete
	bool has_deleted_copy_assignment = false;      // True if copy assignment operator is = delete
	bool has_deleted_move_assignment = false;      // True if move assignment operator is = delete
	bool has_deleted_destructor = false;           // True if destructor is = delete

	// Virtual function support (Phase 2)
	bool has_vtable = false;    // True if this struct has virtual functions
	bool is_abstract = false;   // True if this struct has pure virtual functions
	std::vector<const StructMemberFunction*> vtable;  // Virtual function table (pointers to member functions)
	std::string_view vtable_symbol;  // MSVC mangled vtable symbol name (e.g., "??_7Base@@6B@"), empty if no vtable

	// Virtual base class support (Phase 3)
	std::vector<const BaseClassSpecifier*> virtual_bases;  // Virtual base classes (shared across inheritance paths)

	// RTTI support (Phase 5)
	RTTITypeInfo* rtti_info = nullptr;  // Runtime type information (for polymorphic classes)

	// Friend declarations support (Phase 2)
	std::vector<StringHandle> friend_functions_;      // Friend function names
	std::vector<StringHandle> friend_classes_;        // Friend class names
	std::vector<std::pair<StringHandle, StringHandle>> friend_member_functions_;  // (class, function)

	// Nested class support (Phase 2)
	std::vector<StructTypeInfo*> nested_classes_;    // Nested classes
	StructTypeInfo* enclosing_class_ = nullptr;      // Enclosing class (if this is nested)

	// Nested enum support - tracks enum TypeIndex values for enums declared inside this struct
	std::vector<TypeIndex> nested_enum_indices_;

	// Error tracking for semantic errors detected during finalization
	std::string finalization_error_;  // Non-empty if semantic error occurred during finalization

	StructTypeInfo(StringHandle n, AccessSpecifier default_acc = AccessSpecifier::Public, bool union_type = false)
		: name(n), default_access(default_acc), is_union(union_type) {}
	
	StringHandle getName() const {
		return name;
	}

	void addMember(StringHandle member_name, Type member_type, TypeIndex type_index,
	               size_t member_size, size_t member_alignment, AccessSpecifier access,
	               std::optional<ASTNode> default_initializer,
	               bool is_reference,
	               bool is_rvalue_reference,
	               size_t referenced_size_bits,
	               bool is_array = false,
	               std::vector<size_t> array_dimensions = {},
	               int pointer_depth = 0,
	               std::optional<size_t> bitfield_width = std::nullopt) {
		// Apply pack alignment if specified
		// Pack alignment limits the maximum alignment of members.
		// Some dependent/template paths can transiently report 0 alignment; treat that as byte alignment.
		size_t effective_alignment = member_alignment ? member_alignment : 1;
		if (pack_alignment > 0 && pack_alignment < member_alignment) {
			effective_alignment = pack_alignment;
		}

		// Calculate offset with effective alignment
		// For unions, all members are at offset 0
		size_t offset = is_union ? 0 : ((total_size + effective_alignment - 1) & ~(effective_alignment - 1));

		bool placed_in_active_bitfield_unit = false;
		size_t bitfield_bit_offset = 0;
		if (!is_union && bitfield_width.has_value()) {
			size_t width = *bitfield_width;
			size_t storage_bits = member_size * 8;
			if (width > storage_bits) {
				width = storage_bits;
			}

			if (width == 0) {
				// Zero-width bitfield forces alignment to next allocation unit boundary
				total_size = ((total_size + effective_alignment - 1) & ~(effective_alignment - 1));
				active_bitfield_unit_size = 0;
				active_bitfield_bits_used = 0;
				active_bitfield_unit_alignment = 0;
				active_bitfield_type = Type::Invalid;
				offset = total_size;
			} else {
				bool can_pack_into_active_unit =
					active_bitfield_unit_size == member_size &&
					active_bitfield_unit_alignment == effective_alignment &&
					active_bitfield_type == member_type &&
					(active_bitfield_bits_used + width) <= storage_bits;

				if (!can_pack_into_active_unit) {
					total_size = ((total_size + effective_alignment - 1) & ~(effective_alignment - 1));
					active_bitfield_unit_offset = total_size;
					active_bitfield_unit_size = member_size;
					active_bitfield_unit_alignment = effective_alignment;
					active_bitfield_bits_used = 0;
					active_bitfield_type = member_type;
					total_size += member_size;
				}

				offset = active_bitfield_unit_offset;
				bitfield_bit_offset = active_bitfield_bits_used;
				active_bitfield_bits_used += width;
			}
		} else if (!is_union) {
			if (active_bitfield_unit_size > 0) {
				size_t unit_end = active_bitfield_unit_offset + active_bitfield_unit_size;
				size_t raw_candidate_offset = active_bitfield_unit_offset + ((active_bitfield_bits_used + 7) / 8);
				size_t candidate_offset = ((raw_candidate_offset + effective_alignment - 1) / effective_alignment) * effective_alignment;
				if ((candidate_offset + member_size) <= unit_end) {
					offset = candidate_offset;
					placed_in_active_bitfield_unit = true;
				}
			}

			active_bitfield_unit_size = 0;
			active_bitfield_bits_used = 0;
			active_bitfield_unit_alignment = 0;
			active_bitfield_type = Type::Invalid;
			if (!placed_in_active_bitfield_unit) {
				offset = ((total_size + effective_alignment - 1) & ~(effective_alignment - 1));
			}
		}

		if (!referenced_size_bits) {
			referenced_size_bits = member_size * 8;
		}
		members.emplace_back(member_name, member_type, type_index, offset, member_size, effective_alignment,
			              access, std::move(default_initializer), is_reference, is_rvalue_reference,
			              referenced_size_bits, is_array, std::move(array_dimensions), pointer_depth, bitfield_width);
		members.back().bitfield_bit_offset = bitfield_bit_offset;

		// Update struct size and alignment
		if (is_union) {
			total_size = std::max(total_size, member_size);
		} else if (!bitfield_width.has_value()) {
			if (placed_in_active_bitfield_unit) {
				total_size = std::max(total_size, offset + member_size);
			} else {
				total_size = offset + member_size;
			}
		}
		alignment = std::max(alignment, effective_alignment);
	}

	// StringHandle overload for addMemberFunction - Phase 7A
	void addMemberFunction(StringHandle function_name, ASTNode function_decl, AccessSpecifier access = AccessSpecifier::Public,
	                       bool is_virtual = false, bool is_pure_virtual = false, bool is_override = false, bool is_final_func = false) {
		auto& func = member_functions.emplace_back(function_name, function_decl, access, false, false);
		func.is_virtual = is_virtual;
		func.is_pure_virtual = is_pure_virtual;
		func.is_override = is_override;
		func.is_final = is_final_func;
	}

	void addConstructor(ASTNode constructor_decl, AccessSpecifier access = AccessSpecifier::Public) {
		member_functions.emplace_back(getName(), constructor_decl, access, true, false);
	}

	void addDestructor(ASTNode destructor_decl, AccessSpecifier access = AccessSpecifier::Public, bool is_virtual = false) {
		StringBuilder sb;
		sb.append('~').append(StringTable::getStringView(getName()));
		StringHandle dtor_name_handle = StringTable::getOrInternStringHandle(sb.commit());
		auto& dtor = member_functions.emplace_back(dtor_name_handle, destructor_decl, access, false, true, false, "");
		dtor.is_virtual = is_virtual;
	}

	void addOperatorOverload(std::string_view operator_symbol, ASTNode function_decl, AccessSpecifier access = AccessSpecifier::Public,
	                         bool is_virtual = false, bool is_pure_virtual = false, bool is_override = false, bool is_final_func = false) {
		StringBuilder sb;
		sb.append("operator").append(operator_symbol);
		StringHandle op_name_handle = StringTable::getOrInternStringHandle(sb.commit());
		auto& func = member_functions.emplace_back(op_name_handle, function_decl, access, false, false, true, operator_symbol);
		func.is_virtual = is_virtual;
		func.is_pure_virtual = is_pure_virtual;
		func.is_override = is_override;
		func.is_final = is_final_func;
	}

	// Mark a constructor as deleted
	void markConstructorDeleted(bool is_copy, bool is_move) {
		if (is_copy) {
			has_deleted_copy_constructor = true;
		} else if (is_move) {
			has_deleted_move_constructor = true;
		} else {
			has_deleted_default_constructor = true;
		}
	}

	// Mark an assignment operator as deleted
	void markAssignmentDeleted(bool is_move) {
		if (is_move) {
			has_deleted_move_assignment = true;
		} else {
			has_deleted_copy_assignment = true;
		}
	}

	// Mark destructor as deleted
	void markDestructorDeleted() {
		has_deleted_destructor = true;
	}

	// Check if default constructor is deleted
	bool isDefaultConstructorDeleted() const { return has_deleted_default_constructor; }

	// Check if copy constructor is deleted
	bool isCopyConstructorDeleted() const { return has_deleted_copy_constructor; }

	// Check if move constructor is deleted
	bool isMoveConstructorDeleted() const { return has_deleted_move_constructor; }

	// Check if copy assignment is deleted
	bool isCopyAssignmentDeleted() const { return has_deleted_copy_assignment; }

	// Check if move assignment is deleted
	bool isMoveAssignmentDeleted() const { return has_deleted_move_assignment; }

	// Check if destructor is deleted
	bool isDestructorDeleted() const { return has_deleted_destructor; }

	// Check if finalization had errors
	bool hasFinalizationError() const { return !finalization_error_.empty(); }
	const std::string& getFinalizationError() const { return finalization_error_; }

	bool finalize() {
		// Build vtable first (if struct has virtual functions)
		if (!buildVTable()) {
			return false;  // Semantic error during vtable building
		}

		// Build RTTI information (after vtable, before layout)
		buildRTTI();

		// If custom alignment is specified, use it instead of natural alignment
		if (custom_alignment > 0) {
			alignment = custom_alignment;
		}

		// Add vptr if this struct has virtual functions
		if (has_vtable) {
			// vptr is at offset 0, size 8 (pointer size on x64)
			// Shift all existing members by 8 bytes
			for (auto& member : members) {
				member.offset += 8;
			}
			total_size += 8;
			alignment = std::max(alignment, size_t(8));  // At least pointer alignment
		}

		// Pad struct to its alignment
		total_size = (total_size + alignment - 1) & ~(alignment - 1);
		return true;
	}

	// Finalize with base classes - computes layout including base class subobjects
	// Returns false if semantic errors were detected
	bool finalizeWithBases();

	// Build vtable for virtual functions (called during finalization)
	// Returns false if semantic errors were detected (e.g., overriding final function)
	bool buildVTable();

	// Update abstract flag based on pure virtual functions in vtable
	void updateAbstractFlag();

	// Build RTTI information for polymorphic classes (called during finalization)
	void buildRTTI();

	// Add a base class
	void addBaseClass(std::string_view base_name, TypeIndex base_type_index, AccessSpecifier access, bool is_virtual = false, bool is_deferred = false) {
		base_classes.emplace_back(base_name, base_type_index, access, is_virtual, 0, is_deferred);
	}

	// Find static member by name
	const StructStaticMember* findStaticMember(StringHandle member_name) const {
		for (const auto& static_member : static_members) {
			if (static_member.getName() == member_name) {
				return &static_member;
			}
		}
		return nullptr;
	}
	StructStaticMember* findStaticMember(StringHandle member_name) {
		for (auto& static_member : static_members) {
			if (static_member.getName() == member_name) {
				return &static_member;
			}
		}
		return nullptr;
	}

	// Add static member
	void addStaticMember(StringHandle member_name, Type type, TypeIndex type_index, size_t size, size_t member_alignment,
	                     AccessSpecifier access = AccessSpecifier::Public, std::optional<ASTNode> initializer = std::nullopt, bool is_const = false) {
		static_members.push_back(StructStaticMember(member_name, type, type_index, size, member_alignment, access, initializer, is_const));
	}

	// Update an existing static member's initializer (used for lazy instantiation)
	// Returns true if the member was found and updated, false otherwise
	// Note: Uses linear search through static_members vector. This is acceptable because:
	// 1. Most structs have very few static members (typically 1-5)
	// 2. This function is only called once per lazy-instantiated member
	bool updateStaticMemberInitializer(StringHandle member_name, std::optional<ASTNode> initializer) {
		for (auto& static_member : static_members) {
			if (static_member.name == member_name) {
				static_member.initializer = initializer;
				return true;
			}
		}
		return false;
	}

	// Find member recursively through base classes
	const StructMember* findMemberRecursive(StringHandle member_name) const;
	
	// Find static member recursively through base classes
	// Returns a pair of the static member and the StructTypeInfo that defines it
	std::pair<const StructStaticMember*, const StructTypeInfo*> findStaticMemberRecursive(StringHandle member_name) const;

	void set_custom_alignment(size_t align) {
		custom_alignment = align;
	}

	void set_pack_alignment(size_t align) {
		pack_alignment = align;
	}

	const StructMember* findMember(std::string_view member_name) const {
		StringHandle name_handle = StringTable::getOrInternStringHandle(member_name);
		for (const auto& member : members) {
			if (member.getName() == name_handle) {
				return &member;
			}
		}
		return nullptr;
	}

	// StringHandle overload for findMember - Phase 7A
	const StructMember* findMember(StringHandle member_name) const {
		for (const auto& member : members) {
			// Compare by handle directly for O(1) comparison
			if (member.getName() == member_name) {
				return &member;
			}
		}
		return nullptr;
	}

	// StringHandle overload for findMemberFunction - Phase 7A
	const StructMemberFunction* findMemberFunction(StringHandle func_name) const {
		for (const auto& func : member_functions) {
			// Compare by handle directly for O(1) comparison
			if (func.name == func_name) {
				return &func;
			}
		}
		return nullptr;
	}

	// Convenience overload that interns string_view
	const StructMemberFunction* findMemberFunction(std::string_view func_name) const {
		return findMemberFunction(StringTable::getOrInternStringHandle(func_name));
	}

	// Friend declaration support methods - Phase 7A (StringHandle only)
	void addFriendFunction(StringHandle func_name) {
		friend_functions_.push_back(func_name);
	}

	void addFriendClass(StringHandle class_name) {
		friend_classes_.push_back(class_name);
	}

	void addFriendMemberFunction(StringHandle class_name, StringHandle func_name) {
		friend_member_functions_.emplace_back(class_name, func_name);
	}

	bool isFriendFunction(std::string_view func_name) const {
		StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
		return std::find(friend_functions_.begin(), friend_functions_.end(), func_name_handle) != friend_functions_.end();
	}

	bool isFriendClass(std::string_view class_name) const {
		StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_name);
		return std::find(friend_classes_.begin(), friend_classes_.end(), class_name_handle) != friend_classes_.end();
	}

	// StringHandle overload for isFriendClass - Phase 7A
	bool isFriendClass(StringHandle class_name) const {
		return std::find(friend_classes_.begin(), friend_classes_.end(), class_name) != friend_classes_.end();
	}

	bool isFriendMemberFunction(std::string_view class_name, std::string_view func_name) const {
		StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_name);
		StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
		auto it = std::find_if(friend_member_functions_.begin(), friend_member_functions_.end(),
		                       [class_name_handle, func_name_handle](const auto& pair) {
		                           return pair.first == class_name_handle && pair.second == func_name_handle;
		                       });
		return it != friend_member_functions_.end();
	}

	// StringHandle overload for isFriendMemberFunction - Phase 7A
	bool isFriendMemberFunction(StringHandle class_name, StringHandle func_name) const {
		auto it = std::find_if(friend_member_functions_.begin(), friend_member_functions_.end(),
		                       [class_name, func_name](const auto& pair) {
		                           return pair.first == class_name && pair.second == func_name;
		                       });
		return it != friend_member_functions_.end();
	}

	// Nested class support methods
	void addNestedClass(StructTypeInfo* nested) {
		if (nested) {
			nested_classes_.push_back(nested);
			nested->enclosing_class_ = this;
		}
	}

	void addNestedEnumIndex(TypeIndex enum_type_index) {
		nested_enum_indices_.push_back(enum_type_index);
	}

	const std::vector<TypeIndex>& getNestedEnumIndices() const {
		return nested_enum_indices_;
	}

	bool isNested() const {
		return enclosing_class_ != nullptr;
	}

	StructTypeInfo* getEnclosingClass() const {
		return enclosing_class_;
	}

	const std::vector<StructTypeInfo*>& getNestedClasses() const {
		return nested_classes_;
	}

	// Get fully qualified name (e.g., "Outer::Inner")
	StringHandle getQualifiedName() const {
		StringBuilder sb;
		if (enclosing_class_) {
			sb.append(StringTable::getStringView(enclosing_class_->getQualifiedName()));
			sb.append("::");
		}
		sb.append(StringTable::getStringView(getName()));
		return StringTable::getOrInternStringHandle(sb.commit());
	}

	// Find default constructor (no parameters)
	const StructMemberFunction* findDefaultConstructor() const;

	// Find copy constructor (takes const Type& or Type& parameter)
	const StructMemberFunction* findCopyConstructor() const;

	// Find move constructor (takes Type&& parameter)
	const StructMemberFunction* findMoveConstructor() const;

	// Find destructor
	const StructMemberFunction* findDestructor() const {
		for (const auto& func : member_functions) {
			if (func.is_destructor) {
				return &func;
			}
		}
		return nullptr;
	}

	// Check if any constructor exists (user-defined)
	bool hasAnyConstructor() const {
		for (const auto& func : member_functions) {
			if (func.is_constructor) {
				return true;
			}
		}
		return false;
	}

	bool hasConstructor() const {
		// Check for explicit constructors OR if we need to generate a trivial default constructor
		return findDefaultConstructor() != nullptr || needs_default_constructor;
	}

	bool hasCopyConstructor() const {
		return findCopyConstructor() != nullptr;
	}

	bool hasMoveConstructor() const {
		return findMoveConstructor() != nullptr;
	}

	// Find copy assignment operator (operator= taking const Type& or Type& parameter)
	const StructMemberFunction* findCopyAssignmentOperator() const;

	// Find move assignment operator (operator= taking Type&& parameter)
	const StructMemberFunction* findMoveAssignmentOperator() const;

	bool hasCopyAssignmentOperator() const {
		return findCopyAssignmentOperator() != nullptr;
	}

	bool hasMoveAssignmentOperator() const {
		return findMoveAssignmentOperator() != nullptr;
	}

	bool hasDestructor() const {
		return findDestructor() != nullptr;
	}

	// Check if the class has any user-defined constructor
	bool hasUserDefinedConstructor() const {
		for (const auto& func : member_functions) {
			if (func.is_constructor) {
				return true;
			}
		}
		return false;
	}

	// Check if any member has a default initializer (e.g., "int x = 5;")
	// This is important because implicit default constructors must be called
	// to initialize these members.
	bool hasDefaultMemberInitializers() const {
		for (const auto& member : members) {
			if (member.default_initializer.has_value()) {
				return true;
			}
		}
		return false;
	}

	// Check if the class has a user-defined destructor
	// Note: In FlashCpp's type system, destructors are only stored in member_functions
	// if explicitly declared by the user, so hasDestructor() == hasUserDefinedDestructor()
	bool hasUserDefinedDestructor() const {
		return hasDestructor();
	}

	// Check if this is a standard-layout type
	bool isStandardLayout() const {
		// Standard layout requires:
		// 1. No virtual functions or virtual base classes
		// 2. All non-static data members have the same access control
		// 3. No non-static data members of reference type
		if (has_vtable) return false;
		if (members.empty()) return true;
		
		AccessSpecifier first_access = members[0].access;
		for (const auto& member : members) {
			if (member.access != first_access) {
				return false;
			}
		}
		return true;
	}
};

// Enumerator information
struct Enumerator {
	StringHandle name;
	long long value;  // Enumerator value (always an integer)

	Enumerator(StringHandle n, long long v)
		: name(n), value(v) {}
	
	StringHandle getName() const {
		return name;
	}
};

// Enum type information
struct EnumTypeInfo {
	StringHandle name;
	bool is_scoped;                  // true for enum class, false for enum
	Type underlying_type;            // Underlying type (default: int)
	unsigned char underlying_size;   // Size in bits of underlying type
	std::vector<Enumerator> enumerators;

	EnumTypeInfo(StringHandle n, bool scoped = false, Type underlying = Type::Int, unsigned char size = 32)
		: name(n), is_scoped(scoped), underlying_type(underlying), underlying_size(size) {}
	
	StringHandle getName() const {
		return name;
	}

	void addEnumerator(StringHandle enumerator_name, long long value) {
		enumerators.emplace_back(enumerator_name, value);
	}

	const Enumerator* findEnumerator(StringHandle name_str) const {
		for (const auto& enumerator : enumerators) {
			if (enumerator.getName() == name_str) {
				return &enumerator;
			}
		}
		return nullptr;
	}

	long long getEnumeratorValue(StringHandle name_str) const {
		const Enumerator* e = findEnumerator(name_str);
		return e ? e->value : 0;
	}
};

// Bundles a namespace and identifier so they always travel together.
// Used by TypeInfo to track the source namespace of template instantiations.
struct QualifiedIdentifier {
	NamespaceHandle namespace_handle;  // hierarchical namespace, GLOBAL_NAMESPACE for global
	StringHandle identifier_handle;    // unqualified name, e.g. "vector"

	bool valid() const { return identifier_handle.handle != 0; }
	bool hasNamespace() const { return namespace_handle.isValid() && !namespace_handle.isGlobal(); }

	// Construct from a StringHandle — resolves to string_view and delegates.
	static QualifiedIdentifier fromQualifiedName(
			StringHandle name,
			NamespaceHandle current_ns) {
		return fromQualifiedName(StringTable::getStringView(name), current_ns);
	}

	// Construct from a possibly-qualified name like "std::vector".
	// current_ns is the namespace the code is being parsed in — used to resolve
	// unqualified names so the namespace context is never lost.
	static QualifiedIdentifier fromQualifiedName(
			std::string_view name,
			NamespaceHandle current_ns) {
		QualifiedIdentifier result;
		size_t pos = name.rfind("::");
		if (pos != std::string_view::npos) {
			std::string_view ns_part = name.substr(0, pos);
			// Walk namespace path components (supports nested like "std::chrono")
			NamespaceHandle ns = NamespaceRegistry::GLOBAL_NAMESPACE;
			size_t start = 0;
			while (start < ns_part.size()) {
				size_t sep = ns_part.find("::", start);
				std::string_view component = (sep == std::string_view::npos)
					? ns_part.substr(start) : ns_part.substr(start, sep - start);
				ns = gNamespaceRegistry.getOrCreateNamespace(ns,
					StringTable::getOrInternStringHandle(component));
				start = (sep == std::string_view::npos) ? ns_part.size() : sep + 2;
			}
			result.namespace_handle = ns;
			result.identifier_handle = StringTable::getOrInternStringHandle(name.substr(pos + 2));
		} else {
			result.namespace_handle = current_ns;
			result.identifier_handle = StringTable::getOrInternStringHandle(name);
		}
		return result;
	}
};

struct TypeInfo
{
	TypeInfo() : type_(Type::Void), type_index_(0) {}
	TypeInfo(StringHandle name, Type type, TypeIndex idx, int type_size) : name_(name), type_(type), type_index_(idx), type_size_(type_size) {
	}

	StringHandle name_;  // Pure StringHandle
	Type type_;
	TypeIndex type_index_;

	// True if this type was created with unresolved template args (set directly at placeholder creation sites)
	bool is_incomplete_instantiation_ = false;

	// For struct types, store additional information
	std::unique_ptr<StructTypeInfo> struct_info_;

	// For enum types, store additional information
	std::unique_ptr<EnumTypeInfo> enum_info_;

	// For typedef, store the size in bits (for primitive types)
	int type_size_ = 0;  // Changed from unsigned char to int for large types

	// For typedef of pointer types, store the pointer depth
	size_t pointer_depth_ = 0;
	
	// For typedef of reference types, store the reference qualifier
	bool is_reference_ = false;
	bool is_rvalue_reference_ = false;
	
	// For function pointer/reference type aliases, store the function signature
	std::optional<FunctionSignature> function_signature_;
	
	// For template instantiations: store metadata to avoid name parsing
	// If base_template_ is valid, this type is a template instantiation
	QualifiedIdentifier base_template_;  // e.g., {std, "vector"} for std::vector<int>
	
	// Lightweight storage for template argument type indices (avoids TemplateTypeArg dependency)
	// For type arguments: stores TypeIndex (index into gTypeInfo)
	// For non-type arguments: stores the value directly (supports int64_t, double, StringHandle)
	struct TemplateArgInfo {
		Type base_type = Type::Invalid;  // For primitive types
		TypeIndex type_index = 0;        // For user-defined types
		InlineVector<CVQualifier, 4> pointer_cv_qualifiers;
		size_t pointer_depth = 0;        // Pointer indirection level
		CVQualifier cv_qualifier = CVQualifier::None;  // cv-qualifiers on the argument
		ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
		std::variant<int64_t, double, StringHandle> value = int64_t{0};  // For non-type arguments
		bool is_value = false;           // true if this is a non-type argument
		bool is_array = false;
		std::optional<size_t> array_size = std::nullopt;
		StringHandle dependent_name;     // Name of the dependent template parameter (for inner deduction)
		
		// Helper methods for value access
		int64_t intValue() const { return std::holds_alternative<int64_t>(value) ? std::get<int64_t>(value) : 0; }
		double doubleValue() const { return std::holds_alternative<double>(value) ? std::get<double>(value) : 0.0; }
		StringHandle stringValue() const { return std::holds_alternative<StringHandle>(value) ? std::get<StringHandle>(value) : StringHandle{}; }
	};
	InlineVector<TemplateArgInfo, 4> template_args_;

	StringHandle name() const { 
		return name_;
	};
	
	
	// Helper methods for template instantiations
	bool isTemplateInstantiation() const { return base_template_.valid(); }
	StringHandle baseTemplateName() const { return base_template_.identifier_handle; }
	NamespaceHandle sourceNamespace() const { return base_template_.namespace_handle; }
	const InlineVector<TemplateArgInfo, 4>& templateArgs() const { return template_args_; }
	
	void setTemplateInstantiationInfo(QualifiedIdentifier base_template, InlineVector<TemplateArgInfo, 4> args) {
		base_template_ = base_template;
		template_args_ = std::move(args);
	}

	// Helper methods for struct types
	bool isStruct() const { return type_ == Type::Struct; }
	const StructTypeInfo* getStructInfo() const { return struct_info_.get(); }
	StructTypeInfo* getStructInfo() { return struct_info_.get(); }

	void setStructInfo(std::unique_ptr<StructTypeInfo> info) {
		struct_info_ = std::move(info);
	}

	// Helper methods for enum types
	bool isEnum() const { return type_ == Type::Enum; }
	const EnumTypeInfo* getEnumInfo() const { return enum_info_.get(); }
	EnumTypeInfo* getEnumInfo() { return enum_info_.get(); }

	void setEnumInfo(std::unique_ptr<EnumTypeInfo> info) {
		enum_info_ = std::move(info);
	}
};

extern std::deque<TypeInfo> gTypeInfo;

// Custom hash and equality for heterogeneous lookup with string_view
struct StringHash {
	// No transparent lookup - all keys must be StringHandle
	size_t operator()(StringHandle sh) const { 
		// Use identity hash - the handle value is already well-distributed
		return std::hash<uint32_t>{}(sh.handle); 
	}
};

struct StringEqual {
	// No transparent lookup - all keys must be StringHandle
	bool operator()(StringHandle lhs, StringHandle rhs) const { 
		return lhs.handle == rhs.handle; 
	}
};

extern std::unordered_map<StringHandle, TypeInfo*, StringHash, StringEqual> gTypesByName;

extern std::unordered_map<Type, const TypeInfo*> gNativeTypes;

TypeInfo& add_user_type(StringHandle name, int size_in_bits);

TypeInfo& add_function_type(StringHandle name, Type /*return_type*/);

TypeInfo& add_struct_type(StringHandle name);

TypeInfo& add_enum_type(StringHandle name);

void initialize_native_types();

// Get the natural alignment for a type (in bytes)
// This follows the x64 Windows ABI alignment rules
inline size_t get_type_alignment(Type type, size_t type_size_bytes) {
	switch (type) {
		case Type::Void:
			return 1;
		case Type::Bool:
		case Type::Char:
		case Type::UnsignedChar:
			return 1;
		case Type::Short:
		case Type::UnsignedShort:
			return 2;
		case Type::Int:
		case Type::UnsignedInt:
		case Type::Long:
		case Type::UnsignedLong:
		case Type::Float:
			return 4;
		case Type::LongLong:
		case Type::UnsignedLongLong:
		case Type::Double:
			return 8;
		case Type::LongDouble:
			// On x64 Windows, long double is 8 bytes (same as double)
			return 8;
		case Type::Struct:
			// For structs, alignment is determined by the struct's alignment field
			// This should be passed separately
			return type_size_bytes;
		default:
			// For other types, use the size as alignment (up to 8 bytes max on x64)
			return std::min(type_size_bytes, size_t(8));
	}
}

// Type utilities
bool is_integer_type(Type type);
bool is_signed_integer_type(Type type);
bool is_unsigned_integer_type(Type type);
bool is_bool_type(Type type);
bool is_floating_point_type(Type type);
bool is_struct_type(Type type);  // Check if type is Struct or UserDefined
int get_integer_rank(Type type);
int get_floating_point_rank(Type type);

// Get the size of 'long' in bits based on the target data model
inline int get_long_size_bits() {
	return (g_target_data_model == TargetDataModel::LLP64) ? 32 : 64;
}

// wchar_t is 16-bit unsigned on Windows (LLP64), 32-bit signed on Linux (LP64)
inline int get_wchar_size_bits() {
	return (g_target_data_model == TargetDataModel::LLP64) ? 16 : 32;
}

int get_type_size_bits(Type type);
Type promote_integer_type(Type type);
Type promote_floating_point_type(Type type);
Type get_common_type(Type left, Type right);
bool requires_conversion(Type from, Type to);

// Helper to calculate alignment from size in bytes
// Standard alignment rules: min(size, 8) for most platforms, with special case for long double
inline size_t calculate_alignment_from_size(size_t size_in_bytes, Type type) {
	// Special case for long double on x86-64: often has 16-byte alignment
	if (type == Type::LongDouble) {
		return 16;
	}
	// Standard alignment: same as size, up to 8 bytes
	return (size_in_bytes < 8) ? size_in_bytes : 8;
}

// Pointer level information - stores CV-qualifiers for each pointer level
// Example: const int* const* volatile
//   - Level 0 (base): const int
//   - Level 1: const pointer to (const int)
//   - Level 2: volatile pointer to (const pointer to const int)
struct PointerLevel {
	CVQualifier cv_qualifier = CVQualifier::None;

	PointerLevel() = default;
	explicit PointerLevel(CVQualifier cv) : cv_qualifier(cv) {}
};

class TypeSpecifierNode {
public:
	TypeSpecifierNode() = default;
	TypeSpecifierNode(Type type, TypeQualifier qualifier, int sizeInBits,
		const Token& token = {}, CVQualifier cv_qualifier = CVQualifier::None)
		: type_(type), size_(sizeInBits), qualifier_(qualifier), cv_qualifier_(cv_qualifier), token_(token), type_index_(0) {}

	// Constructor for struct types
	TypeSpecifierNode(Type type, TypeIndex type_index, int sizeInBits,
		const Token& token = {}, CVQualifier cv_qualifier = CVQualifier::None, ReferenceQualifier reference_qualifier = ReferenceQualifier::None)
		: type_(type), size_(sizeInBits), qualifier_(TypeQualifier::None), cv_qualifier_(cv_qualifier), token_(token), type_index_(type_index), reference_qualifier_(reference_qualifier) {}

	auto type() const { return type_; }
	auto size_in_bits() const { return size_; }
	auto qualifier() const { return qualifier_; }
	auto cv_qualifier() const { return cv_qualifier_; }
	void set_cv_qualifier(CVQualifier cv) { cv_qualifier_ = cv; }
	// Adds a cv-qualifier using bitwise OR - safe to call multiple times with same qualifier
	// (e.g., parsing "T const volatile" or accidentally "T const const" will just set the bits)
	void add_cv_qualifier(CVQualifier cv) {
		cv_qualifier_ = static_cast<CVQualifier>(static_cast<uint8_t>(cv_qualifier_) | static_cast<uint8_t>(cv));
	}
	auto type_index() const { return type_index_; }
	bool is_const() const { return (static_cast<uint8_t>(cv_qualifier_) & static_cast<uint8_t>(CVQualifier::Const)) != 0; }
	bool is_volatile() const { return (static_cast<uint8_t>(cv_qualifier_) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0; }

	// Pointer support
	bool is_pointer() const { return !pointer_levels_.empty(); }
	size_t pointer_depth() const { return pointer_levels_.empty() ? 0 : pointer_levels_.size(); }
	const std::vector<PointerLevel>& pointer_levels() const { return pointer_levels_; }
	void limit_pointer_depth(size_t max_depth) { pointer_levels_.resize(std::min(max_depth, pointer_levels_.size())); }
	void add_pointer_level(CVQualifier cv = CVQualifier::None) { pointer_levels_.push_back(PointerLevel(cv)); }
	void add_pointer_levels(int pointer_depth) { while (pointer_depth) { pointer_levels_.push_back(PointerLevel(CVQualifier::None)); --pointer_depth; } }
	void remove_pointer_level() { if (!pointer_levels_.empty()) pointer_levels_.pop_back(); }
	void copy_pointer_levels_from(const TypeSpecifierNode& other) { pointer_levels_ = other.pointer_levels_; }

	// Reference support
	bool is_reference() const { return reference_qualifier_ != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier_ == ReferenceQualifier::RValueReference; }
	bool is_lvalue_reference() const { return reference_qualifier_ == ReferenceQualifier::LValueReference; }
	ReferenceQualifier reference_qualifier() const { return reference_qualifier_; }
	void set_reference_qualifier(ReferenceQualifier qual) {
		reference_qualifier_ = qual;
	}

	// Function pointer support
	bool is_function_pointer() const { return type_ == Type::FunctionPointer; }
	bool is_member_function_pointer() const { return type_ == Type::MemberFunctionPointer; }
	bool is_member_object_pointer() const { return type_ == Type::MemberObjectPointer; }
	void set_function_signature(const FunctionSignature& sig) { function_signature_ = sig; }
	const FunctionSignature& function_signature() const { return *function_signature_; }
	bool has_function_signature() const { return function_signature_.has_value(); }

	// Array support (for type trait checking)
	bool is_array() const { return is_array_; }
	void set_array(bool is_array, std::optional<size_t> array_size = std::nullopt) {
		is_array_ = is_array;
		array_dimensions_.clear();
		if (array_size.has_value()) {
			array_dimensions_.push_back(*array_size);
		}
	}
	// Multidimensional array support
	void add_array_dimension(size_t size) {
		is_array_ = true;
		array_dimensions_.push_back(size);
	}
	void set_array_dimensions(const std::vector<size_t>& dims) {
		is_array_ = !dims.empty();
		array_dimensions_ = dims;
	}
	// Returns the first (outermost) dimension size for backwards compatibility
	std::optional<size_t> array_size() const { 
		if (array_dimensions_.empty()) return std::nullopt;
		return array_dimensions_[0];
	}
	// Returns all array dimensions (e.g., [2][3][4] returns {2, 3, 4})
	const std::vector<size_t>& array_dimensions() const { return array_dimensions_; }
	size_t array_dimension_count() const { return array_dimensions_.size(); }

	// Pack expansion support (for variadic templates like Args...)
	bool is_pack_expansion() const { return is_pack_expansion_; }
	void set_pack_expansion(bool is_pack) { is_pack_expansion_ = is_pack; }

	// Pointer-to-member support (for types like int Class::*)
	bool has_member_class() const { return member_class_name_.has_value(); }
	StringHandle member_class_name() const { return *member_class_name_; }
	void set_member_class_name(StringHandle class_name) { 
		member_class_name_ = class_name; 
	}

	void set_type_index(TypeIndex index) { type_index_ = index; }
	void set_type(Type t) { type_ = t; }
	const Token& token() const { return token_; }
	void copy_indirection_from(const TypeSpecifierNode& other) {
		pointer_levels_ = other.pointer_levels_;
		reference_qualifier_ = other.reference_qualifier_;
		is_array_ = other.is_array_;
		array_dimensions_ = other.array_dimensions_;
		// Note: is_pack_expansion_ is NOT copied - it's context-specific during parsing
		// and shouldn't be propagated during type substitution in template instantiation
	}

	// Get readable string representation
	std::string getReadableString() const;

	// Compare two type specifiers for function overload resolution
	// Returns true if they represent the same type signature
	bool matches_signature(const TypeSpecifierNode& other) const {
		// Check basic type
		if (type_ != other.type_) {
			// Be lenient for typedef/alias cases where the underlying size and indirection match
			bool same_size = size_ != 0 && other.size_ != 0 && size_ == other.size_;
			bool same_indirection = pointer_levels_.size() == other.pointer_levels_.size()
				&& reference_qualifier_ == other.reference_qualifier_;
			if (!(same_size && same_indirection)) {
				return false;
			}
		}
		
		// Check type index for user-defined types
		if (type_ == Type::UserDefined || type_ == Type::Struct) {
			if (type_index_ != other.type_index_) {
				// Be lenient for dependent/alias types: treat as match when the identifier tokens are the same
				if (token_.value() != other.token_.value()) {
					return false;
				}
			}
		}
		
		// For function signature matching, top-level CV qualifiers on value types are ignored
		// Example: void f(const int) and void f(int) have the same signature
		// However, CV qualifiers matter for pointers/references
		// Example: void f(const int*) and void f(int*) have different signatures
		bool has_indirection = !pointer_levels_.empty() || reference_qualifier_ != ReferenceQualifier::None;
		if (has_indirection) {
			// For pointers/references, CV qualifiers DO matter
			if (cv_qualifier_ != other.cv_qualifier_) return false;
		}
		// else: For value types, ignore top-level CV qualifiers
		
		// Check reference qualifiers
		if (reference_qualifier_ != other.reference_qualifier_) return false;
		
		// Check pointer depth and qualifiers at each level
		if (pointer_levels_.size() != other.pointer_levels_.size()) return false;
		for (size_t i = 0; i < pointer_levels_.size(); ++i) {
			if (pointer_levels_[i].cv_qualifier != other.pointer_levels_[i].cv_qualifier) return false;
		}
		
		return true;
	}

private:
	Type type_;
	int size_;  // Size in bits - changed from unsigned char to int to support large structs
	TypeQualifier qualifier_;
	CVQualifier cv_qualifier_;  // CV-qualifier for the base type
	Token token_;
	TypeIndex type_index_;      // Index into gTypeInfo for user-defined types (structs, etc.)
	std::vector<PointerLevel> pointer_levels_;  // Empty if not a pointer, one entry per * level
	ReferenceQualifier reference_qualifier_ = ReferenceQualifier::None;  // Reference qualifier (None, LValue, or RValue)
	bool is_array_ = false;      // True if this is an array type (T[N] or T[])
	std::vector<size_t> array_dimensions_;  // Array dimensions (e.g., int[2][3][4] -> {2, 3, 4})
	std::optional<FunctionSignature> function_signature_;  // For function pointers
	bool is_pack_expansion_ = false;  // True if this type is followed by ... (pack expansion)
	std::optional<StringHandle> member_class_name_;  // For pointer-to-member types (int Class::*)
	std::string_view concept_constraint_;  // Non-empty if this was a constrained auto parameter (e.g., IsInt auto x)

public:
	// For concept constraints on auto parameters (C++20)
	bool has_concept_constraint() const { return !concept_constraint_.empty(); }
	std::string_view concept_constraint() const { return concept_constraint_; }
	void set_concept_constraint(std::string_view constraint) { concept_constraint_ = constraint; }
};

class DeclarationNode {
public:
	DeclarationNode() = default;
	DeclarationNode(ASTNode type_node, Token identifier)
		: type_node_(type_node), identifier_(std::move(identifier)), custom_alignment_(0), is_parameter_pack_(false), is_unsized_array_(false) {}
	DeclarationNode(ASTNode type_node, Token identifier, std::optional<ASTNode> array_size)
		: type_node_(type_node), identifier_(std::move(identifier)), custom_alignment_(0), is_parameter_pack_(false), is_unsized_array_(false) {
		if (array_size.has_value()) {
			array_dimensions_.push_back(*array_size);
		}
	}
	// Multidimensional array constructor
	DeclarationNode(ASTNode type_node, Token identifier, std::vector<ASTNode> array_dimensions)
		: type_node_(type_node), identifier_(std::move(identifier)), array_dimensions_(std::move(array_dimensions)), custom_alignment_(0), is_parameter_pack_(false), is_unsized_array_(false) {}

	ASTNode type_node() const { return type_node_; }
	void set_type_node(const ASTNode& type_node) { type_node_ = type_node; }
	const Token& identifier_token() const { return identifier_; }
	void set_identifier_token(Token token) { identifier_ = std::move(token); }
	uint32_t line_number() const { return identifier_.line(); }
	bool is_array() const { return !array_dimensions_.empty() || is_unsized_array_; }
	// Returns the first (outermost) dimension for backwards compatibility
	const std::optional<ASTNode> array_size() const { 
		if (array_dimensions_.empty()) return std::nullopt;
		return array_dimensions_[0];
	}
	// Multidimensional array support
	const std::vector<ASTNode>& array_dimensions() const { return array_dimensions_; }
	size_t array_dimension_count() const { return array_dimensions_.size(); }
	void add_array_dimension(ASTNode dim) { array_dimensions_.push_back(dim); }
	void set_array_dimensions(std::vector<ASTNode> dims) { array_dimensions_ = std::move(dims); }

	// Unsized array support (e.g., int arr[] = {1, 2, 3})
	bool is_unsized_array() const { return is_unsized_array_; }
	void set_unsized_array(bool unsized) { is_unsized_array_ = unsized; }

	// Alignment support
	size_t custom_alignment() const { return custom_alignment_; }
	void set_custom_alignment(size_t alignment) { custom_alignment_ = alignment; }

	// Parameter pack support (for variadic function templates)
	bool is_parameter_pack() const { return is_parameter_pack_; }
	void set_parameter_pack(bool is_pack) { is_parameter_pack_ = is_pack; }

	// Default value support (for function parameters with default arguments)
	bool has_default_value() const { return default_value_.has_value(); }
	const ASTNode& default_value() const { return default_value_.value(); }
	void set_default_value(ASTNode value) { default_value_ = value; }

private:
	ASTNode type_node_;
	Token identifier_;
	std::vector<ASTNode> array_dimensions_;  // For array declarations like int arr[2][3][4]
	size_t custom_alignment_;            // Custom alignment from alignas(n), 0 = use natural alignment
	bool is_parameter_pack_;             // True for parameter packs like Args... args
	bool is_unsized_array_;              // True for unsized arrays like int arr[] = {1, 2, 3}
	std::optional<ASTNode> default_value_;  // Default argument value for function parameters
};

class IdentifierNode {
public:
	explicit IdentifierNode(Token identifier) : identifier_(identifier) {}

	std::optional<Token> try_get_parent_token() { return parent_token_; }
	std::string_view name() const { return identifier_.value(); }
	StringHandle nameHandle() const { return identifier_.handle(); }

private:
	Token identifier_;
	std::optional<Token> parent_token_;
};

// Qualified identifier node for namespace::identifier chains
class QualifiedIdentifierNode {
public:
	explicit QualifiedIdentifierNode(NamespaceHandle namespace_handle, Token identifier)
		: namespace_handle_(namespace_handle), identifier_(identifier) {}

	NamespaceHandle namespace_handle() const { return namespace_handle_; }
	std::string_view name() const { return identifier_.value(); }
	StringHandle nameHandle() const { return identifier_.handle(); }
	const Token& identifier_token() const { return identifier_; }

	// Convert to QualifiedIdentifier (Phase 3 bridge)
	QualifiedIdentifier qualifiedIdentifier() const {
		return QualifiedIdentifier{namespace_handle_, identifier_.handle()};
	}

	// Get the full qualified name as a string (e.g., "std::print")
	// Note: This allocates a string, so use sparingly (mainly for debugging)
	std::string full_name() const {
		std::string result;
		std::string_view ns_name = gNamespaceRegistry.getQualifiedName(namespace_handle_);
		if (!ns_name.empty()) {
			result = std::string(ns_name) + "::";
		}
		result += std::string(identifier_.value());
		return result;
	}

private:
	NamespaceHandle namespace_handle_;  // Handle to namespace, e.g., handle for "std" in std::print
	Token identifier_;                  // The final identifier (e.g., "print", "func")
};

using NumericLiteralValue = std::variant<unsigned long long, double>;

class NumericLiteralNode {
public:
	explicit NumericLiteralNode(Token identifier, NumericLiteralValue value, Type type, TypeQualifier qualifier, unsigned char size) : value_(value), type_(type), size_(size), qualifier_(qualifier), identifier_(identifier) {}

	std::string_view token() const { return identifier_.value(); }
	NumericLiteralValue value() const { return value_; }
	Type type() const { return type_; }
	unsigned char sizeInBits() const { return size_; }
	TypeQualifier qualifier() const { return qualifier_; }

private:
	NumericLiteralValue value_;
	Type type_;
	unsigned char size_;	// Size in bits
	TypeQualifier qualifier_;
	Token identifier_;
};

class StringLiteralNode {
public:
	explicit StringLiteralNode(Token identifier) : identifier_(identifier) {}

	std::string_view value() const { return identifier_.value(); }

private:
	Token identifier_;
};

class BoolLiteralNode {
public:
	explicit BoolLiteralNode(Token identifier, bool value) : identifier_(identifier), value_(value) {}

	bool value() const { return value_; }
	std::string_view token() const { return identifier_.value(); }

private:
	Token identifier_;
	bool value_;
};

class BinaryOperatorNode {
public:
	explicit BinaryOperatorNode(Token identifier, ASTNode lhs_node,
		ASTNode rhs_node)
		: identifier_(identifier), lhs_node_(lhs_node), rhs_node_(rhs_node) {}

	std::string_view op() const { return identifier_.value(); }
	const Token& get_token() const { return identifier_; }
	auto get_lhs() const { return lhs_node_; }
	auto get_rhs() const { return rhs_node_; }

private:
	class Token identifier_;
	ASTNode lhs_node_;
	ASTNode rhs_node_;
};

class UnaryOperatorNode {
public:
	explicit UnaryOperatorNode(Token identifier, ASTNode operand_node, bool is_prefix = true, bool is_builtin_addressof = false)
		: identifier_(identifier), operand_node_(operand_node), is_prefix_(is_prefix), is_builtin_addressof_(is_builtin_addressof) {}

	std::string_view op() const { return identifier_.value(); }
	const Token& get_token() const { return identifier_; }
	auto get_operand() const { return operand_node_; }
	bool is_prefix() const { return is_prefix_; }
	bool is_builtin_addressof() const { return is_builtin_addressof_; }

private:
	Token identifier_;
	ASTNode operand_node_;
	bool is_prefix_;
	bool is_builtin_addressof_; // True if created from __builtin_addressof intrinsic
};

class TernaryOperatorNode {
public:
	explicit TernaryOperatorNode(ASTNode condition, ASTNode true_expr, ASTNode false_expr, Token question_token)
		: condition_(condition), true_expr_(true_expr), false_expr_(false_expr), question_token_(question_token) {}

	const ASTNode& condition() const { return condition_; }
	const ASTNode& true_expr() const { return true_expr_; }
	const ASTNode& false_expr() const { return false_expr_; }
	const Token& get_token() const { return question_token_; }

private:
	ASTNode condition_;
	ASTNode true_expr_;
	ASTNode false_expr_;
	Token question_token_;
};

// C++17 Fold Expressions
// Supports: (...op pack), (pack op...), (init op...op pack), (pack op...op init)
class FoldExpressionNode {
public:
	enum class Direction { Left, Right };
	enum class Type { Unary, Binary };

	// Unary fold: (... op pack) or (pack op ...)
	explicit FoldExpressionNode(std::string_view pack_name, std::string_view op, Direction dir, Token token)
		: pack_name_(pack_name), op_(op), direction_(dir), type_(Type::Unary), 
		  init_expr_(std::nullopt), pack_expr_(std::nullopt), token_(token) {}

	// Binary fold: (init op ... op pack) or (pack op ... op init)
	explicit FoldExpressionNode(std::string_view pack_name, std::string_view op, 
		                         Direction dir, ASTNode init, Token token)
		: pack_name_(pack_name), op_(op), direction_(dir), type_(Type::Binary), 
		  init_expr_(init), pack_expr_(std::nullopt), token_(token) {}

	// Unary fold with complex pack expression: (expr op ...) or (... op expr)
	// Used when the pack is a complex expression like a function call
	explicit FoldExpressionNode(ASTNode pack_expr, std::string_view op, Direction dir, Token token)
		: pack_name_(""), op_(op), direction_(dir), type_(Type::Unary),
		  init_expr_(std::nullopt), pack_expr_(pack_expr), token_(token) {}

	std::string_view pack_name() const { return pack_name_; }
	std::string_view op() const { return op_; }
	Direction direction() const { return direction_; }
	Type type() const { return type_; }
	const std::optional<ASTNode>& init_expr() const { return init_expr_; }
	const std::optional<ASTNode>& pack_expr() const { return pack_expr_; }
	bool has_complex_pack_expr() const { return pack_expr_.has_value(); }
	const Token& get_token() const { return token_; }

private:
	std::string_view pack_name_;
	std::string_view op_;
	Direction direction_;
	Type type_;
	std::optional<ASTNode> init_expr_;
	std::optional<ASTNode> pack_expr_;  // Complex pack expression (if any)
	Token token_;
};

// Pack expansion expression: expr...
// Used in template argument contexts like (declval<Args>()...)
class PackExpansionExprNode {
public:
	explicit PackExpansionExprNode(ASTNode pattern, Token ellipsis_token)
		: pattern_(pattern), ellipsis_token_(ellipsis_token) {}

	ASTNode pattern() const { return pattern_; }
	const Token& get_token() const { return ellipsis_token_; }

private:
	ASTNode pattern_;        // The expression being expanded
	Token ellipsis_token_;   // The ... token
};

class BlockNode {
public:
	explicit BlockNode() {}

	const auto& get_statements() const { return statements_; }
	void add_statement_node(ASTNode node) { statements_.push_back(node); }

private:
	ChunkedVector<ASTNode, 128, 256> statements_;
};

class FunctionDeclarationNode {
public:
	FunctionDeclarationNode() = delete;
	FunctionDeclarationNode(DeclarationNode& decl_node)
		: decl_node_(decl_node), parent_struct_name_(""), is_member_function_(false), is_implicit_(false), linkage_(Linkage::None), is_constexpr_(false), is_constinit_(false), is_consteval_(false) {}
	FunctionDeclarationNode(DeclarationNode& decl_node, std::string_view parent_struct_name)
		: decl_node_(decl_node), parent_struct_name_(parent_struct_name), is_member_function_(true), is_implicit_(false), linkage_(Linkage::None), is_constexpr_(false), is_constinit_(false), is_consteval_(false) {}
	FunctionDeclarationNode(DeclarationNode& decl_node, StringHandle parent_struct_name_handle)
		: decl_node_(decl_node), parent_struct_name_(StringTable::getStringView(parent_struct_name_handle)), is_member_function_(true), is_implicit_(false), linkage_(Linkage::None), is_constexpr_(false), is_constinit_(false), is_consteval_(false) {}
	FunctionDeclarationNode(DeclarationNode& decl_node, Linkage linkage)
		: decl_node_(decl_node), parent_struct_name_(""), is_member_function_(false), is_implicit_(false), linkage_(linkage), is_constexpr_(false), is_constinit_(false), is_consteval_(false) {}

	const DeclarationNode& decl_node() const {
		return decl_node_;
	}
	DeclarationNode& decl_node() {
		return decl_node_;
	}
	const std::vector<ASTNode>& parameter_nodes() const {
		return parameter_nodes_;
	}
	void add_parameter_node(ASTNode parameter_node) {
		parameter_nodes_.push_back(parameter_node);
	}
	// Update parameter nodes from the definition (to use definition's parameter names)
	// C++ allows declaration and definition to have different parameter names
	void update_parameter_nodes_from_definition(const std::vector<ASTNode>& definition_params) {
		if (definition_params.size() != parameter_nodes_.size()) {
			return; // Signature mismatch - shouldn't happen after validation
		}
		parameter_nodes_ = definition_params;
	}
	const std::optional<ASTNode>& get_definition() const {
		return definition_block_;
	}
	bool set_definition(ASTNode block_node) {
		if (definition_block_.has_value())
			return false;

		definition_block_.emplace(block_node);
		return true;
	}

	// Member function support
	bool is_member_function() const { return is_member_function_; }
	std::string_view parent_struct_name() const { return parent_struct_name_; }

	// Implicit function support (for compiler-generated functions like operator=)
	void set_is_implicit(bool implicit) { is_implicit_ = implicit; }
	bool is_implicit() const { return is_implicit_; }

	// Linkage support (C vs C++)
	void set_linkage(Linkage linkage) { linkage_ = linkage; }
	Linkage linkage() const { return linkage_; }

	// Calling convention support (for Windows ABI and variadic validation)
	void set_calling_convention(CallingConvention cc) { calling_convention_ = cc; }
	CallingConvention calling_convention() const { return calling_convention_; }

	// Template body position support (for delayed parsing of template bodies)
	// Uses SaveHandle as handle (opaque ID from Parser's save_token_position())
	void set_template_body_position(SaveHandle handle) {
		has_template_body_ = true;
		template_body_position_handle_ = handle;
	}
	bool has_template_body_position() const { return has_template_body_; }
	SaveHandle template_body_position() const { return template_body_position_handle_; }

	// Template declaration position support (for re-parsing function declarations during instantiation)
	// Needed for SFINAE: re-parse return type with substituted template parameters
	void set_template_declaration_position(SaveHandle handle) {
		has_template_declaration_ = true;
		template_declaration_position_handle_ = handle;
	}
	bool has_template_declaration_position() const { return has_template_declaration_; }
	SaveHandle template_declaration_position() const { return template_declaration_position_handle_; }

	// Needed for SFINAE: save position of trailing return type (the '->' token)
	// so we can re-parse the trailing return type with concrete template parameters
	void set_trailing_return_type_position(SaveHandle handle) {
		trailing_return_type_position_handle_ = handle;
	}
	bool has_trailing_return_type_position() const { return trailing_return_type_position_handle_.has_value(); }
	SaveHandle trailing_return_type_position() const { return *trailing_return_type_position_handle_; }

	// Variadic function support (functions with ... ellipsis parameter)
	void set_is_variadic(bool variadic) { is_variadic_ = variadic; }
	bool is_variadic() const { return is_variadic_; }

	// Constexpr/constinit/consteval support
	void set_is_constexpr(bool is_constexpr) { is_constexpr_ = is_constexpr; }
	bool is_constexpr() const { return is_constexpr_; }

	void set_is_constinit(bool is_constinit) { is_constinit_ = is_constinit; }
	bool is_constinit() const { return is_constinit_; }

	void set_is_consteval(bool is_consteval) { is_consteval_ = is_consteval; }
	bool is_consteval() const { return is_consteval_; }

	// noexcept support
	void set_noexcept(bool is_noexcept) { is_noexcept_ = is_noexcept; }
	bool is_noexcept() const { return is_noexcept_; }
	void set_noexcept_expression(ASTNode expr) { noexcept_expression_ = expr; }
	const std::optional<ASTNode>& noexcept_expression() const { return noexcept_expression_; }
	bool has_noexcept_expression() const { return noexcept_expression_.has_value(); }

	// Static member function support
	void set_is_static(bool is_static) { is_static_ = is_static; }
	bool is_static() const { return is_static_; }

	// Deleted function support (= delete)
	void set_is_deleted(bool deleted) { is_deleted_ = deleted; }
	bool is_deleted() const { return is_deleted_; }

	// Inline always support (for template instantiations that are pure expressions)
	// When true, this function should always be inlined and never generate a call
	void set_inline_always(bool inline_always) { inline_always_ = inline_always; }
	bool is_inline_always() const { return inline_always_; }

	// Pre-computed mangled name for consistent access across all compiler stages
	// Generated once during parsing, reused by CodeGen and ObjFileWriter
	void set_mangled_name(std::string_view name) { mangled_name_ = name; }
	std::string_view mangled_name() const { return mangled_name_; }
	bool has_mangled_name() const { return !mangled_name_.empty(); }

	// Non-type template argument support for template specializations
	// Used to generate correct mangled names for get<0>, get<1>, etc.
	void set_non_type_template_args(std::vector<int64_t> args) { non_type_template_args_ = std::move(args); }
	const std::vector<int64_t>& non_type_template_args() const { return non_type_template_args_; }
	bool has_non_type_template_args() const { return !non_type_template_args_.empty(); }

private:
	DeclarationNode& decl_node_;
	std::vector<ASTNode> parameter_nodes_;
	std::optional<ASTNode> definition_block_;  // Store ASTNode to keep BlockNode alive
	std::string_view parent_struct_name_;  // Points directly into source text from lexer token or ChunkedStringAllocator
	bool is_member_function_;
	bool is_implicit_;  // True if this is an implicitly generated function (e.g., operator=)
	bool has_template_body_ = false;
	bool has_template_declaration_ = false;  // True if template declaration position is saved (for SFINAE re-parsing)

	bool is_variadic_ = false;  // True if this function has ... ellipsis parameter
	Linkage linkage_;  // Linkage specification (C, C++, or None)
	CallingConvention calling_convention_ = CallingConvention::Default;  // Calling convention (__cdecl, __stdcall, etc.)
	SaveHandle template_body_position_handle_;  // Handle to saved position for template body (from Parser::save_token_position())
	SaveHandle template_declaration_position_handle_;  // Handle to saved position for template declaration (for SFINAE)
	std::optional<SaveHandle> trailing_return_type_position_handle_;  // Handle to saved position for trailing return type '->'
	bool is_constexpr_;
	bool is_constinit_;
	bool is_consteval_;
	bool is_noexcept_ = false;  // True if function is declared noexcept
	bool is_deleted_ = false;  // True if function is declared = delete
	bool is_static_ = false;  // True if function is a static member function (no 'this' pointer)
	bool inline_always_ = false;  // True if function should always be inlined (e.g., template pure expressions)
	std::optional<ASTNode> noexcept_expression_;  // Optional noexcept(expr) expression
	std::string_view mangled_name_;  // Pre-computed mangled name (points to ChunkedStringAllocator storage)
	std::vector<int64_t> non_type_template_args_;  // Non-type template arguments (e.g., 0 for get<0>)
};

class FunctionCallNode {
public:
	explicit FunctionCallNode(const DeclarationNode& func_decl, ChunkedVector<ASTNode>&& arguments, Token called_from_token)
		: func_decl_(func_decl), arguments_(std::move(arguments)), called_from_(called_from_token) {}

	const auto& arguments() const { return arguments_; }
	const auto& function_declaration() const { return func_decl_; }

	void add_argument(ASTNode argument) { arguments_.push_back(argument); }

	Token called_from() const { return called_from_; }
	
	// Pre-computed mangled name support (for namespace-scoped functions)
	void set_mangled_name(std::string_view name) { mangled_name_ = StringTable::getOrInternStringHandle(name); }
	std::string_view mangled_name() const { return mangled_name_.view(); }
	StringHandle mangled_name_handle() const { return mangled_name_; }
	bool has_mangled_name() const { return mangled_name_.isValid(); }
	
	// Qualified source name support (for template lookup in constexpr evaluation)
	// This stores the source-level qualified name (e.g., "std::__is_complete_or_unbounded")
	// which is needed for template function lookup in the template registry
	void set_qualified_name(std::string_view name) { qualified_name_ = StringTable::getOrInternStringHandle(name); }
	std::string_view qualified_name() const { return qualified_name_.view(); }
	StringHandle qualified_name_handle() const { return qualified_name_; }
	bool has_qualified_name() const { return qualified_name_.isValid(); }
	
	// Explicit template arguments support (for calls like foo<int>())
	// These are stored as expression nodes which may contain TemplateParameterReferenceNode for dependent args
	void set_template_arguments(std::vector<ASTNode>&& template_args) { 
		template_arguments_ = std::move(template_args);
	}
	const std::vector<ASTNode>& template_arguments() const { return template_arguments_; }
	bool has_template_arguments() const { return !template_arguments_.empty(); }
	
	// Indirect call support (for function pointers and function references)
	// When true, the call is through a variable holding a function address, not a direct function name
	void set_indirect_call(bool indirect) { is_indirect_call_ = indirect; }
	bool is_indirect_call() const { return is_indirect_call_; }

private:
	const DeclarationNode& func_decl_;
	ChunkedVector<ASTNode> arguments_;
	Token called_from_;
	StringHandle mangled_name_;  // Pre-computed mangled name
	StringHandle qualified_name_;  // Source-level qualified name (e.g., "std::func")
	std::vector<ASTNode> template_arguments_;  // Explicit template arguments (e.g., <T> in foo<T>())
	bool is_indirect_call_ = false;  // True for function pointer/reference calls
};

// Constructor call node - represents constructor calls like T(args)
class ConstructorCallNode {
public:
	explicit ConstructorCallNode(ASTNode type_node, ChunkedVector<ASTNode>&& arguments, Token called_from_token)
		: type_node_(type_node), arguments_(std::move(arguments)), called_from_(called_from_token) {}

	const ASTNode& type_node() const { return type_node_; }
	const auto& arguments() const { return arguments_; }

	void add_argument(ASTNode argument) { arguments_.push_back(argument); }

	Token called_from() const { return called_from_; }

private:
	ASTNode type_node_;  // TypeSpecifierNode representing the type being constructed
	ChunkedVector<ASTNode> arguments_;
	Token called_from_;
};

