#pragma once

#include <climits>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <deque>
#include <unordered_map>
#include <any>
#include <optional>
#include <memory>

#include "Token.h"
#include "ChunkedAnyVector.h"
#include "StackString.h"
#include "Lexer.h"
#include "StringTable.h"

// SaveHandle type for parser save/restore operations
// Matches Parser::SaveHandle typedef in Parser.h
using SaveHandle = size_t;

// Deferred template member function body information
// Used to store template member function bodies for parsing during instantiation
struct DeferredTemplateMemberBody {
	StringHandle function_name;           // Name of the function (for matching during instantiation)
	StringHandle struct_name;             // Name of the struct (from token, persistent)
	SaveHandle body_start;                // Handle to saved position at '{'
	SaveHandle initializer_list_start;    // Handle to saved position at ':' for constructor initializer list
	size_t struct_type_index;             // Type index (will be 0 for templates during definition)
	bool has_initializer_list;            // True if constructor has an initializer list
	bool is_constructor;                  // Special handling for constructors
	bool is_destructor;                   // Special handling for destructors
	bool is_const_method;                 // True if this is a const member function
	std::vector<StringHandle> template_param_names; // Template parameter names (copied, not views)
};

// Forward declarations
struct TemplateTypeArg;

class ASTNode {
public:
	ASTNode() = default;

	template <typename T> ASTNode(T* node) : node_(node) {}

	template <typename T, typename... Args>
	static ASTNode emplace_node(Args&&... args) {
		T& t = gChunkedAnyStorage.emplace_back<T>(std::forward<Args>(args)...);
		return ASTNode(&t);
	}

	template <typename T> bool is() const {
		return node_.type() == typeid(T*);
	}

	template <typename T> T& as() {
		return *std::any_cast<T*>(node_);
	}

	template <typename T> const T& as() const {
		return *std::any_cast<T*>(node_);
	}

	auto type_name() const {
		return node_.type().name();
	}

	bool has_value() const {
		return node_.has_value();
	}
	
	// Direct access to underlying std::any (for debugging/workarounds)
	const std::any& get_any() const {
		return node_;
	}

private:
	std::any node_;
};

enum class TypeQualifier {
	None,
	Signed,
	Unsigned,
};

// CV-qualifiers (const/volatile) - separate from sign qualifiers
// These can be combined with type qualifiers using bitwise operations
enum class CVQualifier : uint8_t {
	None = 0,
	Const = 1 << 0,
	Volatile = 1 << 1,
	ConstVolatile = Const | Volatile
};

// Reference qualifiers - mutually exclusive enum (not a bitmask)
enum class ReferenceQualifier : uint8_t {
	None = 0,
	LValueReference = 1 << 0,  // &
	RValueReference = 1 << 1,  // &&
};

enum class Type : int_fast16_t {
	Void,
	Bool,
	Char,
	UnsignedChar,
	Short,
	UnsignedShort,
	Int,
	UnsignedInt,
	Long,
	UnsignedLong,
	LongLong,
	UnsignedLongLong,
	Float,
	Double,
	LongDouble,
	FunctionPointer,
	MemberFunctionPointer,
	MemberObjectPointer,  // Pointer to data member: int MyClass::*
	UserDefined,
	Auto,
	Function,
	Struct,
	Enum,
	Nullptr,              // nullptr_t type
	Template,             // Nested template param
	Invalid,
};

using TypeIndex = size_t;

/// Helper function to get the C++ name string for a Type
/// Returns the string used in C++ source code (e.g., "int", "unsigned long")
/// Returns empty string_view for non-primitive types
inline std::string_view getTypeName(Type t) {
	switch (t) {
		case Type::Int: return "int";
		case Type::UnsignedInt: return "unsigned int";
		case Type::Long: return "long";
		case Type::UnsignedLong: return "unsigned long";
		case Type::LongLong: return "long long";
		case Type::UnsignedLongLong: return "unsigned long long";
		case Type::Short: return "short";
		case Type::UnsignedShort: return "unsigned short";
		case Type::Char: return "char";
		case Type::UnsignedChar: return "unsigned char";
		case Type::Bool: return "bool";
		case Type::Float: return "float";
		case Type::Double: return "double";
		case Type::LongDouble: return "long double";
		case Type::Void: return "void";
		default: return "";
	}
}

/// Helper function to determine if a Type is signed (for MOVSX vs MOVZX)
/// MSVC treats char as signed by default.
inline bool isSignedType(Type t) {
	switch (t) {
		case Type::Char:      // char is signed by default in MSVC
		case Type::Short:
		case Type::Int:
		case Type::Long:
		case Type::LongLong:
			return true;
		// Explicitly unsigned types
		case Type::Bool:
		case Type::UnsignedChar:
		case Type::UnsignedShort:
		case Type::UnsignedInt:
		case Type::UnsignedLong:
		case Type::UnsignedLongLong:
		// Non-integer types
		case Type::Float:
		case Type::Double:
		case Type::LongDouble:
		case Type::Void:
		case Type::UserDefined:
		case Type::Auto:
		case Type::Function:
		case Type::Struct:
		case Type::Enum:
		case Type::FunctionPointer:
		case Type::MemberFunctionPointer:
		case Type::MemberObjectPointer:
		case Type::Nullptr:
		case Type::Invalid:
		default:
			return false;
	}
}

// Linkage specification for functions (C vs C++)
enum class Linkage : uint8_t {
	None,           // Default C++ linkage (with name mangling)
	C,              // C linkage (no name mangling)
	CPlusPlus,      // Explicit C++ linkage
	DllImport,      // __declspec(dllimport) - symbol imported from DLL
	DllExport,      // __declspec(dllexport) - symbol exported from DLL
};

// Calling conventions (primarily for x86, but tracked for compatibility and diagnostics)
enum class CallingConvention : uint8_t {
	Default,        // Platform default (x64: Microsoft x64, x86: __cdecl)
	Cdecl,          // __cdecl - caller cleans stack, supports variadic
	Stdcall,        // __stdcall - callee cleans stack, no variadic
	Fastcall,       // __fastcall - first args in registers
	Vectorcall,     // __vectorcall - optimized for SIMD
	Thiscall,       // __thiscall - C++ member functions (this in register)
	Clrcall,        // __clrcall - .NET/CLI interop
};

// Access specifier for struct/class members
enum class AccessSpecifier {
	Public,
	Protected,
	Private
};

// Friend declaration types
enum class FriendKind {
	Function,      // friend void func();
	Class,         // friend class ClassName;
	MemberFunction // friend void Class::func();
};

// Base class specifier for inheritance
struct BaseClassSpecifier {
	std::string_view name;           // Base class name
	TypeIndex type_index;       // Index into gTypeInfo for base class type
	AccessSpecifier access;     // Inheritance access (public/protected/private)
	bool is_virtual;            // True for virtual inheritance (Phase 3)
	size_t offset;              // Offset of base subobject in derived class
	bool is_deferred;           // True for template parameters (resolved at instantiation)

	BaseClassSpecifier(std::string_view n, TypeIndex tidx, AccessSpecifier acc, bool virt = false, size_t off = 0, bool deferred = false)
		: name(n), type_index(tidx), access(acc), is_virtual(virt), offset(off), is_deferred(deferred) {}
};

// Deferred base class specifier for decltype bases in templates
// These are resolved during template instantiation
struct DeferredBaseClassSpecifier {
	ASTNode decltype_expression;  // The parsed decltype expression
	AccessSpecifier access;        // Inheritance access (public/protected/private)
	bool is_virtual;               // True for virtual inheritance

	DeferredBaseClassSpecifier(ASTNode expr, AccessSpecifier acc, bool virt = false)
		: decltype_expression(expr), access(acc), is_virtual(virt) {}
};

struct TemplateArgumentNodeInfo {
	ASTNode node;
	bool is_pack = false;
	bool is_dependent = false;
};

struct DeferredTemplateBaseClassSpecifier {
	StringHandle base_template_name;
	std::vector<TemplateArgumentNodeInfo> template_arguments;
	std::optional<StringHandle> member_type; // e.g., ::type
	AccessSpecifier access;
	bool is_virtual;

	DeferredTemplateBaseClassSpecifier(StringHandle name,
	                                   std::vector<TemplateArgumentNodeInfo> args,
	                                   std::optional<StringHandle> member,
	                                   AccessSpecifier acc,
	                                   bool virt)
		: base_template_name(name),
		  template_arguments(std::move(args)),
		  member_type(member),
		  access(acc),
		  is_virtual(virt) {}
};

// Function signature for function pointers
struct FunctionSignature {
	Type return_type;
	std::vector<Type> parameter_types;
	Linkage linkage = Linkage::None;           // C vs C++ linkage
	std::optional<std::string> class_name;     // For member function pointers
	bool is_const = false;                     // For const member functions
	bool is_volatile = false;                  // For volatile member functions
};

// Struct member information
struct StructMember {
	StringHandle name;
	Type type;
	TypeIndex type_index;   // Index into gTypeInfo for complex types (structs, etc.)
	size_t offset;          // Offset in bytes from start of struct
	size_t size;            // Size in bytes
	size_t referenced_size_bits; // Size of the referenced value in bits (for references)
	size_t alignment;       // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)
	bool is_reference;      // True if member is an lvalue reference
	bool is_rvalue_reference; // True if member is an rvalue reference
	std::optional<ASTNode> default_initializer;  // C++11 default member initializer

	StructMember(StringHandle n, Type t, TypeIndex tidx, size_t off, size_t sz, size_t align,
	            AccessSpecifier acc = AccessSpecifier::Public,
	            std::optional<ASTNode> init = std::nullopt,
	            bool is_ref = false,
	            bool is_rvalue_ref = false,
	            size_t ref_size_bits = 0)
		: name(n), type(t), type_index(tidx), offset(off), size(sz),
		  referenced_size_bits(ref_size_bits ? ref_size_bits : sz * 8), alignment(align),
		  access(acc), is_reference(is_ref), is_rvalue_reference(is_rvalue_ref),
		  default_initializer(std::move(init)) {}
	
	StringHandle getName() const {
		return name;
	}
};

// Forward declaration for member function support
class FunctionDeclarationNode;

// Struct member function information
struct StructMemberFunction {
	StringHandle name;
	ASTNode function_decl;  // FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
	AccessSpecifier access; // Access level (public/protected/private)
	bool is_constructor;    // True if this is a constructor
	bool is_destructor;     // True if this is a destructor
	bool is_operator_overload; // True if this is an operator overload (operator=, operator+, etc.)
	std::string_view operator_symbol; // The operator symbol (e.g., "=", "+", "==") if is_operator_overload is true

	// Virtual function support (Phase 2)
	bool is_virtual = false;        // True if declared with 'virtual' keyword
	bool is_pure_virtual = false;   // True if pure virtual (= 0)
	bool is_override = false;       // True if declared with 'override' keyword
	bool is_final = false;          // True if declared with 'final' keyword
	int vtable_index = -1;          // Index in vtable, -1 if not virtual

	// CV qualifiers for member functions (Phase 4)
	bool is_const = false;          // True if const member function (e.g., void foo() const)
	bool is_volatile = false;       // True if volatile member function (e.g., void foo() volatile)

	StructMemberFunction(StringHandle n, ASTNode func_decl, AccessSpecifier acc = AccessSpecifier::Public,
	                     bool is_ctor = false, bool is_dtor = false, bool is_op_overload = false, std::string_view op_symbol = "")
		: name(n), function_decl(func_decl), access(acc), is_constructor(is_ctor), is_destructor(is_dtor),
		  is_operator_overload(is_op_overload), operator_symbol(op_symbol) {}
	
	StringHandle getName() const {
		return name;
	}
};

// MSVC RTTI structures - multi-component format for runtime compatibility
// These structures match the MSVC ABI for RTTI to work with __dynamic_cast

// ??_R0 - Type Descriptor (simplified type_info equivalent)
struct MSVCTypeDescriptor {
	const void* vtable;              // Pointer to type_info vtable (usually null in our case)
	const void* spare;               // Reserved/spare pointer (unused)
	char name[1];                    // Variable-length mangled name (null-terminated)
};

// ??_R1 - Base Class Descriptor
struct MSVCBaseClassDescriptor {
	const MSVCTypeDescriptor* type_descriptor;  // Pointer to base class type descriptor (??_R0)
	uint32_t num_contained_bases;    // Number of nested base classes
	int32_t mdisp;                   // Member displacement (offset in class)
	int32_t pdisp;                   // Vbtable displacement (-1 if not virtual base)
	int32_t vdisp;                   // Displacement inside vbtable (0 if not virtual base)
	uint32_t attributes;             // Flags (virtual, ambiguous, etc.)
};

// ??_R2 - Base Class Array (array of pointers to ??_R1)
struct MSVCBaseClassArray {
	const MSVCBaseClassDescriptor* base_class_descriptors[1]; // Variable-length array
};

// ??_R3 - Class Hierarchy Descriptor
struct MSVCClassHierarchyDescriptor {
	uint32_t signature;              // Always 0
	uint32_t attributes;             // Bit flags (multiple inheritance, virtual inheritance, etc.)
	uint32_t num_base_classes;       // Number of base classes (including self)
	const MSVCBaseClassArray* base_class_array;  // Pointer to base class array (??_R2)
};

// ??_R4 - Complete Object Locator (referenced by vtable)
struct MSVCCompleteObjectLocator {
	uint32_t signature;              // 0 for 32-bit, 1 for 64-bit
	uint32_t offset;                 // Offset of this vtable in the complete class
	uint32_t cd_offset;              // Constructor displacement offset
	const MSVCTypeDescriptor* type_descriptor;        // Pointer to type descriptor (??_R0)
	const MSVCClassHierarchyDescriptor* hierarchy;    // Pointer to class hierarchy (??_R3)
};

// Itanium C++ ABI RTTI structures - standard format for Linux/Unix systems
// These structures match the Itanium C++ ABI specification for RTTI

// Base class info structure for __vmi_class_type_info
struct ItaniumBaseClassTypeInfo {
	const void* base_type;      // Pointer to base class type_info (__class_type_info*)
	int64_t offset_flags;       // Combined offset and flags
	
	// Flags in offset_flags:
	// bit 0: __virtual_mask (0x1) - base class is virtual
	// bit 1: __public_mask (0x2) - base class is public
	// bits 8+: offset of base class in derived class (signed)
};

// __class_type_info - Type info for classes without base classes
struct ItaniumClassTypeInfo {
	const void* vtable;         // Pointer to vtable for __class_type_info
	const char* name;           // Mangled type name (e.g., "3Foo" for class Foo)
};

// __si_class_type_info - Type info for classes with single, public, non-virtual base
struct ItaniumSIClassTypeInfo {
	const void* vtable;         // Pointer to vtable for __si_class_type_info
	const char* name;           // Mangled type name
	const void* base_type;      // Pointer to base class type_info (__class_type_info*)
};

// __vmi_class_type_info - Type info for classes with multiple or virtual bases
struct ItaniumVMIClassTypeInfo {
	const void* vtable;         // Pointer to vtable for __vmi_class_type_info
	const char* name;           // Mangled type name
	uint32_t flags;             // Inheritance flags
	uint32_t base_count;        // Number of direct base classes
	ItaniumBaseClassTypeInfo base_info[1];  // Variable-length array of base class info
	
	// Flags:
	// __non_diamond_repeat_mask = 0x1 - has repeated bases (but not diamond)
	// __diamond_shaped_mask = 0x2     - has diamond-shaped inheritance
};

// Legacy RTTITypeInfo for compatibility with existing code
// This will hold references to both MSVC and Itanium structures
struct RTTITypeInfo {
	const char* type_name;           // Mangled type name
	const char* demangled_name;      // Human-readable type name
	size_t num_bases;                // Number of base classes
	const RTTITypeInfo** base_types; // Array of pointers to base class type_info

	// MSVC RTTI structures
	MSVCCompleteObjectLocator* col;         // ??_R4 - Complete Object Locator
	MSVCClassHierarchyDescriptor* chd;      // ??_R3 - Class Hierarchy Descriptor
	MSVCBaseClassArray* bca;                // ??_R2 - Base Class Array
	std::vector<MSVCBaseClassDescriptor*> base_descriptors;  // ??_R1 - Base Class Descriptors
	MSVCTypeDescriptor* type_descriptor;    // ??_R0 - Type Descriptor

	// Itanium C++ ABI RTTI structures
	void* itanium_type_info;        // Pointer to __class_type_info, __si_class_type_info, or __vmi_class_type_info
	enum class ItaniumTypeInfoKind {
		None,
		ClassTypeInfo,      // __class_type_info (no bases)
		SIClassTypeInfo,    // __si_class_type_info (single inheritance)
		VMIClassTypeInfo    // __vmi_class_type_info (multiple/virtual inheritance)
	} itanium_kind;

	RTTITypeInfo(const char* mangled, const char* demangled, size_t num_base = 0)
		: type_name(mangled), demangled_name(demangled), num_bases(num_base), base_types(nullptr),
		  col(nullptr), chd(nullptr), bca(nullptr), type_descriptor(nullptr),
		  itanium_type_info(nullptr), itanium_kind(ItaniumTypeInfoKind::None) {}

	// Check if this type is derived from another type (for dynamic_cast)
	bool isDerivedFrom(const RTTITypeInfo* base) const {
		if (this == base) return true;

		for (size_t i = 0; i < num_bases; ++i) {
			if (base_types[i] && base_types[i]->isDerivedFrom(base)) {
				return true;
			}
		}
		return false;
	}
};

// Static member information
struct StructStaticMember {
	StringHandle name;
	Type type;
	TypeIndex type_index;   // Index into gTypeInfo for complex types
	size_t size;            // Size in bytes
	size_t alignment;       // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)
	std::optional<ASTNode> initializer;  // Optional initializer expression
	bool is_const;          // True if declared with const qualifier

	StructStaticMember(StringHandle n, Type t, TypeIndex tidx, size_t sz, size_t align, AccessSpecifier acc = AccessSpecifier::Public,
	                   std::optional<ASTNode> init = std::nullopt, bool is_const_val = false)
		: name(n), type(t), type_index(tidx), size(sz), alignment(align), access(acc),
		  initializer(init), is_const(is_const_val) {}
	
	StringHandle getName() const {
		return name;
	}
};

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
	AccessSpecifier default_access; // Default access for struct (public) vs class (private)
	bool is_union = false;      // True if this is a union (all members at offset 0)
	bool is_final = false;      // True if this class/struct is declared with 'final' keyword
	bool needs_default_constructor = false;  // True if struct needs an implicit default constructor

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
	               size_t referenced_size_bits) {
		// Apply pack alignment if specified
		// Pack alignment limits the maximum alignment of members
		size_t effective_alignment = member_alignment;
		if (pack_alignment > 0 && pack_alignment < member_alignment) {
			effective_alignment = pack_alignment;
		}

		// Calculate offset with effective alignment
		// For unions, all members are at offset 0
		size_t offset = is_union ? 0 : ((total_size + effective_alignment - 1) & ~(effective_alignment - 1));

		if (!referenced_size_bits) {
			referenced_size_bits = member_size * 8;
		}
		members.emplace_back(member_name, member_type, type_index, offset, member_size, effective_alignment,
			              access, std::move(default_initializer), is_reference, is_rvalue_reference,
			              referenced_size_bits);

		// Update struct size and alignment
		total_size = offset + member_size;
		alignment = std::max(alignment, effective_alignment);
	}

	// StringHandle overload for addMemberFunction - Phase 7A
	void addMemberFunction(StringHandle function_name, ASTNode function_decl, AccessSpecifier access = AccessSpecifier::Public,
	                       bool is_virtual = false, bool is_pure_virtual = false, bool is_override = false, bool is_final = false) {
		auto& func = member_functions.emplace_back(function_name, function_decl, access, false, false);
		func.is_virtual = is_virtual;
		func.is_pure_virtual = is_pure_virtual;
		func.is_override = is_override;
		func.is_final = is_final;
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
	                         bool is_virtual = false, bool is_pure_virtual = false, bool is_override = false, bool is_final = false) {
		StringBuilder sb;
		sb.append("operator").append(operator_symbol);
		StringHandle op_name_handle = StringTable::getOrInternStringHandle(sb.commit());
		auto& func = member_functions.emplace_back(op_name_handle, function_decl, access, false, false, true, operator_symbol);
		func.is_virtual = is_virtual;
		func.is_pure_virtual = is_pure_virtual;
		func.is_override = is_override;
		func.is_final = is_final;
	}

	void finalize() {
		// Build vtable first (if struct has virtual functions)
		buildVTable();

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
	}

	// Finalize with base classes - computes layout including base class subobjects
	void finalizeWithBases();

	// Build vtable for virtual functions (called during finalization)
	void buildVTable();

	// Update abstract flag based on pure virtual functions in vtable
	void updateAbstractFlag();

	// Build RTTI information for polymorphic classes (called during finalization)
	void buildRTTI();

	// Add a base class
	void addBaseClass(std::string_view base_name, TypeIndex base_type_index, AccessSpecifier access, bool is_virtual = false, bool is_deferred = false) {
		base_classes.emplace_back(base_name, base_type_index, access, is_virtual, 0, is_deferred);
	}

	// Find static member by name
	const StructStaticMember* findStaticMember(StringHandle name) const {
		for (const auto& static_member : static_members) {
			if (static_member.getName() == name) {
				return &static_member;
			}
		}
		return nullptr;
	}

	// Add static member
	void addStaticMember(StringHandle name, Type type, TypeIndex type_index, size_t size, size_t alignment,
	                     AccessSpecifier access = AccessSpecifier::Public, std::optional<ASTNode> initializer = std::nullopt, bool is_const = false) {
		static_members.push_back(StructStaticMember(name, type, type_index, size, alignment, access, initializer, is_const));
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

	const StructMember* findMember(std::string_view name) const {
		StringHandle name_handle = StringTable::getOrInternStringHandle(name);
		for (const auto& member : members) {
			if (member.getName() == name_handle) {
				return &member;
			}
		}
		return nullptr;
	}

	// StringHandle overload for findMember - Phase 7A
	const StructMember* findMember(StringHandle name) const {
		for (const auto& member : members) {
			// Compare by handle directly for O(1) comparison
			if (member.getName() == name) {
				return &member;
			}
		}
		return nullptr;
	}

	// StringHandle overload for findMemberFunction - Phase 7A
	const StructMemberFunction* findMemberFunction(StringHandle name) const {
		for (const auto& func : member_functions) {
			// Compare by handle directly for O(1) comparison
			if (func.name == name) {
				return &func;
			}
		}
		return nullptr;
	}

	// Convenience overload that interns string_view
	const StructMemberFunction* findMemberFunction(std::string_view name) const {
		return findMemberFunction(StringTable::getOrInternStringHandle(name));
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

struct TypeInfo
{
	TypeInfo() : type_(Type::Void), type_index_(0) {}
	TypeInfo(StringHandle name, Type type, TypeIndex idx) : name_(name), type_(type), type_index_(idx) {}

	StringHandle name_;  // Pure StringHandle
	Type type_;
	TypeIndex type_index_;

	// For struct types, store additional information
	std::unique_ptr<StructTypeInfo> struct_info_;

	// For enum types, store additional information
	std::unique_ptr<EnumTypeInfo> enum_info_;

	// For typedef, store the size in bits (for primitive types)
	unsigned short type_size_ = 0;  // Changed from unsigned char to support large types

	// For typedef of pointer types, store the pointer depth
	size_t pointer_depth_ = 0;
	
	// For typedef of reference types, store the reference qualifier
	bool is_reference_ = false;
	bool is_rvalue_reference_ = false;

	StringHandle name() const { 
		return name_;
	};

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

extern std::unordered_map<StringHandle, const TypeInfo*, StringHash, StringEqual> gTypesByName;

extern std::unordered_map<Type, const TypeInfo*> gNativeTypes;

TypeInfo& add_user_type(StringHandle name);

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
	TypeSpecifierNode(Type type, TypeQualifier qualifier, unsigned short sizeInBits,
		const Token& token = {}, CVQualifier cv_qualifier = CVQualifier::None)
		: type_(type), size_(sizeInBits), qualifier_(qualifier), cv_qualifier_(cv_qualifier), token_(token), type_index_(0) {}

	// Constructor for struct types
	TypeSpecifierNode(Type type, TypeIndex type_index, unsigned short sizeInBits,
		const Token& token = {}, CVQualifier cv_qualifier = CVQualifier::None)
		: type_(type), size_(sizeInBits), qualifier_(TypeQualifier::None), cv_qualifier_(cv_qualifier), token_(token), type_index_(type_index) {}

	auto type() const { return type_; }
	auto size_in_bits() const { return size_; }
	auto qualifier() const { return qualifier_; }
	auto cv_qualifier() const { return cv_qualifier_; }
	void set_cv_qualifier(CVQualifier cv) { cv_qualifier_ = cv; }
	auto type_index() const { return type_index_; }
	bool is_const() const { return (static_cast<uint8_t>(cv_qualifier_) & static_cast<uint8_t>(CVQualifier::Const)) != 0; }
	bool is_volatile() const { return (static_cast<uint8_t>(cv_qualifier_) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0; }

	// Pointer support
	bool is_pointer() const { return !pointer_levels_.empty(); }
	size_t pointer_depth() const { return pointer_levels_.empty() ? 0 : pointer_levels_.size(); }
	const std::vector<PointerLevel>& pointer_levels() const { return pointer_levels_; }
	void add_pointer_level(CVQualifier cv = CVQualifier::None) { pointer_levels_.push_back(PointerLevel(cv)); }
	void remove_pointer_level() { if (!pointer_levels_.empty()) pointer_levels_.pop_back(); }
	void copy_pointer_levels_from(const TypeSpecifierNode& other) { pointer_levels_ = other.pointer_levels_; }

	// Reference support
	bool is_reference() const { return reference_qualifier_ != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier_ == ReferenceQualifier::RValueReference; }
	bool is_lvalue_reference() const { return reference_qualifier_ == ReferenceQualifier::LValueReference; }
	ReferenceQualifier reference_qualifier() const { return reference_qualifier_; }
	void set_reference(bool is_rvalue = false) {
		reference_qualifier_ = is_rvalue ? ReferenceQualifier::RValueReference : ReferenceQualifier::LValueReference;
	}
	void set_reference_qualifier(ReferenceQualifier qual) {
		reference_qualifier_ = qual;
	}
	void set_lvalue_reference(bool is_lvalue = true) {
		if (is_lvalue) {
			reference_qualifier_ = ReferenceQualifier::LValueReference;
		} else {
			reference_qualifier_ = ReferenceQualifier::None;
		}
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
		array_size_ = array_size;
	}
	std::optional<size_t> array_size() const { return array_size_; }

	void set_type_index(TypeIndex index) { type_index_ = index; }
	const Token& token() const { return token_; }
	void copy_indirection_from(const TypeSpecifierNode& other) {
		pointer_levels_ = other.pointer_levels_;
		reference_qualifier_ = other.reference_qualifier_;
		is_array_ = other.is_array_;
		array_size_ = other.array_size_;
	}

	// Get readable string representation
	std::string getReadableString() const;

	// Compare two type specifiers for function overload resolution
	// Returns true if they represent the same type signature
	bool matches_signature(const TypeSpecifierNode& other) const {
		// Check basic type
		if (type_ != other.type_) return false;
		
		// Check type index for user-defined types
		if (type_ == Type::UserDefined || type_ == Type::Struct) {
			if (type_index_ != other.type_index_) return false;
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
	unsigned short size_;  // Changed from unsigned char to support large structs (max 65535 bits = 8191 bytes)
	TypeQualifier qualifier_;
	CVQualifier cv_qualifier_;  // CV-qualifier for the base type
	Token token_;
	TypeIndex type_index_;      // Index into gTypeInfo for user-defined types (structs, etc.)
	std::vector<PointerLevel> pointer_levels_;  // Empty if not a pointer, one entry per * level
	ReferenceQualifier reference_qualifier_ = ReferenceQualifier::None;  // Reference qualifier (None, LValue, or RValue)
	bool is_array_ = false;      // True if this is an array type (T[N] or T[])
	std::optional<size_t> array_size_;  // Array size if known (e.g., int[10] -> 10)
	std::optional<FunctionSignature> function_signature_;  // For function pointers
};

class DeclarationNode {
public:
	DeclarationNode() = default;
	DeclarationNode(ASTNode type_node, Token identifier)
		: type_node_(type_node), identifier_(std::move(identifier)), array_size_(std::nullopt), custom_alignment_(0), is_parameter_pack_(false), is_unsized_array_(false) {}
	DeclarationNode(ASTNode type_node, Token identifier, std::optional<ASTNode> array_size)
		: type_node_(type_node), identifier_(std::move(identifier)), array_size_(array_size), custom_alignment_(0), is_parameter_pack_(false), is_unsized_array_(false) {}

	ASTNode type_node() const { return type_node_; }
	void set_type_node(const ASTNode& type_node) { type_node_ = type_node; }
	const Token& identifier_token() const { return identifier_; }
	uint32_t line_number() const { return identifier_.line(); }
	bool is_array() const { return array_size_.has_value() || is_unsized_array_; }
	const std::optional<ASTNode>& array_size() const { return array_size_; }

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
	std::optional<ASTNode> array_size_;  // For array declarations like int arr[10]
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
	StringHandle nameHandle() const { return StringTable::getOrInternStringHandle(identifier_.value()); }

private:
	Token identifier_;
	std::optional<Token> parent_token_;
};

// Qualified identifier node for namespace::identifier chains
class QualifiedIdentifierNode {
public:
	explicit QualifiedIdentifierNode(std::vector<StringType<>> namespaces, Token identifier)
		: namespaces_(std::move(namespaces)), identifier_(identifier) {}

	const std::vector<StringType<>>& namespaces() const { return namespaces_; }
	std::string_view name() const { return identifier_.value(); }
	StringHandle nameHandle() const { return StringTable::getOrInternStringHandle(identifier_.value()); }
	const Token& identifier_token() const { return identifier_; }

	// Get the full qualified name as a string (e.g., "std::print")
	// Note: This allocates a string, so use sparingly (mainly for debugging)
	std::string full_name() const {
		std::string result;
		for (const auto& ns : namespaces_) {
#if USE_OLD_STRING_APPROACH
			result += ns + "::";
#else
			result += std::string(ns.view()) + "::";
#endif
		}
		result += std::string(identifier_.value());
		return result;
	}

private:
	std::vector<StringType<>> namespaces_;  // e.g., ["std"] for std::print, ["A", "B"] for A::B::func
	Token identifier_;                          // The final identifier (e.g., "print", "func")
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
		  init_expr_(std::nullopt), token_(token) {}

	// Binary fold: (init op ... op pack) or (pack op ... op init)
	explicit FoldExpressionNode(std::string_view pack_name, std::string_view op, 
		                         Direction dir, ASTNode init, Token token)
		: pack_name_(pack_name), op_(op), direction_(dir), type_(Type::Binary), 
		  init_expr_(init), token_(token) {}

	std::string_view pack_name() const { return pack_name_; }
	std::string_view op() const { return op_; }
	Direction direction() const { return direction_; }
	Type type() const { return type_; }
	const std::optional<ASTNode>& init_expr() const { return init_expr_; }
	const Token& get_token() const { return token_; }

private:
	std::string_view pack_name_;
	std::string_view op_;
	Direction direction_;
	Type type_;
	std::optional<ASTNode> init_expr_;
	Token token_;
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

	// Inline always support (for template instantiations that are pure expressions)
	// When true, this function should always be inlined and never generate a call
	void set_inline_always(bool inline_always) { inline_always_ = inline_always; }
	bool is_inline_always() const { return inline_always_; }

	// Pre-computed mangled name for consistent access across all compiler stages
	// Generated once during parsing, reused by CodeGen and ObjFileWriter
	void set_mangled_name(std::string_view name) { mangled_name_ = name; }
	std::string_view mangled_name() const { return mangled_name_; }
	bool has_mangled_name() const { return !mangled_name_.empty(); }

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
	bool is_constexpr_;
	bool is_constinit_;
	bool is_consteval_;
	bool is_noexcept_ = false;  // True if function is declared noexcept
	bool inline_always_ = false;  // True if function should always be inlined (e.g., template pure expressions)
	std::optional<ASTNode> noexcept_expression_;  // Optional noexcept(expr) expression
	std::string_view mangled_name_;  // Pre-computed mangled name (points to ChunkedStringAllocator storage)
};

class FunctionCallNode {
public:
	explicit FunctionCallNode(DeclarationNode& func_decl, ChunkedVector<ASTNode>&& arguments, Token called_from_token)
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
	
	// Explicit template arguments support (for calls like foo<int>())
	// These are stored as expression nodes which may contain TemplateParameterReferenceNode for dependent args
	void set_template_arguments(std::vector<ASTNode>&& template_args) { 
		template_arguments_ = std::move(template_args);
	}
	const std::vector<ASTNode>& template_arguments() const { return template_arguments_; }
	bool has_template_arguments() const { return !template_arguments_.empty(); }

private:
	DeclarationNode& func_decl_;
	ChunkedVector<ASTNode> arguments_;
	Token called_from_;
	StringHandle mangled_name_;  // Pre-computed mangled name
	std::vector<ASTNode> template_arguments_;  // Explicit template arguments (e.g., <T> in foo<T>())
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

// Template parameter kinds
enum class TemplateParameterKind {
	Type,      // typename T or class T
	NonType,   // int N, bool B, etc.
	Template   // template<typename> class Container (template template parameter)
};

// Template parameter node - represents a single template parameter
class TemplateParameterNode {
public:
	// Type parameter: template<typename T> or template<class T>
	TemplateParameterNode(StringHandle name, Token token)
		: kind_(TemplateParameterKind::Type), name_(name), token_(token) {}

	// Non-type parameter: template<int N>
	TemplateParameterNode(StringHandle name, ASTNode type_node, Token token)
		: kind_(TemplateParameterKind::NonType), name_(name), type_node_(type_node), token_(token) {}

	// Template template parameter: template<template<typename> class Container>
	TemplateParameterNode(StringHandle name, std::vector<ASTNode> nested_params, Token token)
		: kind_(TemplateParameterKind::Template), name_(name), nested_params_(std::move(nested_params)), token_(token) {}

	TemplateParameterKind kind() const { return kind_; }
	std::string_view name() const { return name_.view(); }
	StringHandle nameHandle() const { return name_; }
	Token token() const { return token_; }

	// For non-type parameters
	bool has_type() const { return type_node_.has_value(); }
	ASTNode type_node() const { return type_node_.value(); }

	// For template template parameters
	const std::vector<ASTNode>& nested_parameters() const { return nested_params_; }

	// For default arguments (future enhancement)
	bool has_default() const { return default_value_.has_value(); }
	ASTNode default_value() const { return default_value_.value(); }
	void set_default_value(ASTNode value) { default_value_ = value; }

	// For variadic templates (parameter packs)
	bool is_variadic() const { return is_variadic_; }
	void set_variadic(bool variadic) { is_variadic_ = variadic; }

	// For concept constraints (C++20)
	bool has_concept_constraint() const { return concept_constraint_.has_value(); }
	std::string_view concept_constraint() const { return concept_constraint_.value(); }
	void set_concept_constraint(std::string_view constraint) { concept_constraint_ = constraint; }

private:
	TemplateParameterKind kind_;
	StringHandle name_;  // Points directly into source text from lexer token
	std::optional<ASTNode> type_node_;  // For non-type parameters (e.g., int N)
	std::vector<ASTNode> nested_params_;  // For template template parameters (nested template parameters)
	std::optional<ASTNode> default_value_;  // Default argument (e.g., typename T = int)
	Token token_;  // For error reporting
	bool is_variadic_ = false;  // True for parameter packs (typename... Args)
	std::optional<std::string_view> concept_constraint_;  // Concept name for constrained parameters (e.g., Addable T)
};

// Template function declaration node - represents a function template
class TemplateFunctionDeclarationNode {
public:
	TemplateFunctionDeclarationNode() = delete;
	TemplateFunctionDeclarationNode(std::vector<ASTNode> template_params, ASTNode function_decl, 
	                                std::optional<ASTNode> requires_clause = std::nullopt)
		: template_parameters_(std::move(template_params)), 
		  function_declaration_(function_decl),
		  requires_clause_(requires_clause) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	ASTNode function_declaration() const { return function_declaration_; }
	const std::optional<ASTNode>& requires_clause() const { return requires_clause_; }
	bool has_requires_clause() const { return requires_clause_.has_value(); }

	// Get the underlying FunctionDeclarationNode
	FunctionDeclarationNode& function_decl_node() {
		return function_declaration_.as<FunctionDeclarationNode>();
	}
	const FunctionDeclarationNode& function_decl_node() const {
		return function_declaration_.as<FunctionDeclarationNode>();
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	ASTNode function_declaration_;  // FunctionDeclarationNode
	std::optional<ASTNode> requires_clause_;  // Optional RequiresClauseNode
};

// Template alias declaration: template<typename T> using Ptr = T*;
class TemplateAliasNode {
public:
	TemplateAliasNode() = delete;
	TemplateAliasNode(std::vector<ASTNode> template_params,
	                  std::vector<StringHandle> param_names,
	                  StringHandle alias_name,
	                  ASTNode target_type)
		: template_parameters_(std::move(template_params))
		, template_param_names_(std::move(param_names))
		, alias_name_(alias_name)
		, target_type_(target_type)
		, is_deferred_(false) {}
	
	// Constructor for deferred template instantiation (Option 1)
	TemplateAliasNode(std::vector<ASTNode> template_params,
	                  std::vector<StringHandle> param_names,
	                  StringHandle alias_name,
	                  ASTNode target_type,
	                  StringHandle target_template_name,
	                  std::vector<ASTNode> target_template_args)
		: template_parameters_(std::move(template_params))
		, template_param_names_(std::move(param_names))
		, alias_name_(alias_name)
		, target_type_(target_type)
		, is_deferred_(true)
		, target_template_name_(target_template_name)
		, target_template_args_(std::move(target_template_args)) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	const std::vector<StringHandle>& template_param_names() const { return template_param_names_; }
	std::string_view alias_name() const { return alias_name_.view(); }
	ASTNode target_type() const { return target_type_; }
	
	// Deferred instantiation support
	bool is_deferred() const { return is_deferred_; }
	std::string_view target_template_name() const { return target_template_name_.view(); }
	const std::vector<ASTNode>& target_template_args() const { return target_template_args_; }

	// Get the underlying TypeSpecifierNode
	TypeSpecifierNode& target_type_node() {
		return target_type_.as<TypeSpecifierNode>();
	}
	const TypeSpecifierNode& target_type_node() const {
		return target_type_.as<TypeSpecifierNode>();
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	std::vector<StringHandle> template_param_names_;  // Parameter names for lookup
	StringHandle alias_name_;  // The name of the alias (e.g., "Ptr")
	ASTNode target_type_;  // TypeSpecifierNode - the target type (e.g., T*)
	
	// Deferred instantiation (Option 1: cleaner than string parsing)
	bool is_deferred_;  // True if target is a template with unresolved parameters
	StringHandle target_template_name_;  // Template name (e.g., "integral_constant")
	std::vector<ASTNode> target_template_args_;  // Unevaluated argument AST nodes
};

// Deduction guide declaration: template<typename T> ClassName(T) -> ClassName<T>;
// Enables class template argument deduction (CTAD) in C++17
class DeductionGuideNode {
public:
	DeductionGuideNode() = delete;
	DeductionGuideNode(std::vector<ASTNode> template_params,
	                   std::string_view class_name,
	                   std::vector<ASTNode> guide_params,
	                   std::vector<ASTNode> deduced_template_args)
		: template_parameters_(std::move(template_params))
		, class_name_(class_name)
		, guide_parameters_(std::move(guide_params))
		, deduced_template_args_(std::move(deduced_template_args)) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	std::string_view class_name() const { return class_name_; }
	const std::vector<ASTNode>& guide_parameters() const { return guide_parameters_; }
	const std::vector<ASTNode>& deduced_template_args_nodes() const { return deduced_template_args_; }

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances for the guide's template params
	std::string_view class_name_;  // Name of the class template
	std::vector<ASTNode> guide_parameters_;  // Parameters of the guide (like constructor params)
	std::vector<ASTNode> deduced_template_args_;  // RHS nodes for template arguments (TypeSpecifierNode instances)
};

// Forward declarations
class TemplateClassDeclarationNode;
class VariableDeclarationNode;

// Variable template declaration: template<typename T> constexpr T pi = T(3.14159265358979323846);
class TemplateVariableDeclarationNode {
public:
	TemplateVariableDeclarationNode() = delete;
	TemplateVariableDeclarationNode(std::vector<ASTNode> template_params, ASTNode variable_decl)
		: template_parameters_(std::move(template_params)), variable_declaration_(variable_decl) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	ASTNode variable_declaration() const { return variable_declaration_; }

	// Get the underlying VariableDeclarationNode
	VariableDeclarationNode& variable_decl_node() {
		return variable_declaration_.as<VariableDeclarationNode>();
	}
	const VariableDeclarationNode& variable_decl_node() const {
		return variable_declaration_.as<VariableDeclarationNode>();
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	ASTNode variable_declaration_;  // VariableDeclarationNode
};

// Storage class specifiers
enum class StorageClass {
	None,      // No storage class specified (automatic for local, external for global)
	Static,    // static keyword
	Extern,    // extern keyword
	Register,  // register keyword (deprecated in C++17)
	Mutable    // mutable keyword (for class members)
};

class VariableDeclarationNode {
public:
	explicit VariableDeclarationNode(ASTNode declaration_node, std::optional<ASTNode> initializer = std::nullopt, StorageClass storage_class = StorageClass::None)
		: declaration_node_(declaration_node), initializer_(initializer), storage_class_(storage_class), is_constexpr_(false), is_constinit_(false) {}

	const DeclarationNode& declaration() const { return declaration_node_.as<DeclarationNode>(); }
	const ASTNode& declaration_node() const { return declaration_node_; }
	const std::optional<ASTNode>& initializer() const { return initializer_; }
	StorageClass storage_class() const { return storage_class_; }

	void set_is_constexpr(bool is_constexpr) { is_constexpr_ = is_constexpr; }
	bool is_constexpr() const { return is_constexpr_; }

	void set_is_constinit(bool is_constinit) { is_constinit_ = is_constinit; }
	bool is_constinit() const { return is_constinit_; }

private:
	ASTNode declaration_node_;
	std::optional<ASTNode> initializer_;
	StorageClass storage_class_;
	bool is_constexpr_;
	bool is_constinit_;
};

// Structured binding declaration node (C++17 feature)
// Represents: auto [a, b, c] = expr;
class StructuredBindingNode {
public:
	StructuredBindingNode() = delete;
	StructuredBindingNode(std::vector<StringHandle> identifiers, 
	                      ASTNode initializer,
	                      CVQualifier cv_qualifiers,
	                      ReferenceQualifier ref_qualifier)
		: identifiers_(std::move(identifiers))
		, initializer_(initializer)
		, cv_qualifiers_(cv_qualifiers)
		, ref_qualifier_(ref_qualifier) {}
	
	const std::vector<StringHandle>& identifiers() const { return identifiers_; }
	const ASTNode& initializer() const { return initializer_; }
	CVQualifier cv_qualifiers() const { return cv_qualifiers_; }
	ReferenceQualifier ref_qualifier() const { return ref_qualifier_; }
	
	bool is_const() const { 
		return (static_cast<uint8_t>(cv_qualifiers_) & static_cast<uint8_t>(CVQualifier::Const)) != 0;
	}
	
	bool is_lvalue_reference() const { 
		return ref_qualifier_ == ReferenceQualifier::LValueReference;
	}
	
	bool is_rvalue_reference() const { 
		return ref_qualifier_ == ReferenceQualifier::RValueReference;
	}

private:
	std::vector<StringHandle> identifiers_;  // Binding names: [a, b, c]
	ASTNode initializer_;                     // Expression to decompose
	CVQualifier cv_qualifiers_;               // const/volatile qualifiers
	ReferenceQualifier ref_qualifier_;        // &, &&, or none
};

// Member initializer for constructor initializer lists
struct MemberInitializer {
	std::string_view member_name;
	ASTNode initializer_expr;

	MemberInitializer(std::string_view name, ASTNode expr)
		: member_name(name), initializer_expr(expr) {}
};

// Base class initializer for constructor initializer lists
struct BaseInitializer {
	StringHandle base_class_name;
	std::vector<ASTNode> arguments;

	BaseInitializer(StringHandle name, std::vector<ASTNode> args)
		: base_class_name(name), arguments(std::move(args)) {}
	
	StringHandle getBaseClassName() const {
		return base_class_name;
	}
};

// Delegating constructor initializer (C++11 feature)
struct DelegatingInitializer {
	std::vector<ASTNode> arguments;

	explicit DelegatingInitializer(std::vector<ASTNode> args)
		: arguments(std::move(args)) {}
};

// Constructor declaration node
class ConstructorDeclarationNode {
public:
	ConstructorDeclarationNode() = delete;
	ConstructorDeclarationNode(StringHandle struct_name_handle, StringHandle name_handle)
		: struct_name_(struct_name_handle), name_(name_handle), is_implicit_(false) {}

	StringHandle struct_name() const { return struct_name_; }
	StringHandle name() const { return name_; }
	Token name_token() const { return Token(Token::Type::Identifier, StringTable::getStringView(name_), 0, 0, 0); }  // Create token on demand
	const std::vector<ASTNode>& parameter_nodes() const { return parameter_nodes_; }
	const std::vector<MemberInitializer>& member_initializers() const { return member_initializers_; }
	const std::vector<BaseInitializer>& base_initializers() const { return base_initializers_; }
	const std::optional<DelegatingInitializer>& delegating_initializer() const { return delegating_initializer_; }
	bool is_implicit() const { return is_implicit_; }

	void add_parameter_node(ASTNode parameter_node) {
		parameter_nodes_.push_back(parameter_node);
	}

	void add_member_initializer(std::string_view member_name, ASTNode initializer_expr) {
		member_initializers_.emplace_back(member_name, initializer_expr);
	}

	void add_base_initializer(StringHandle base_name, std::vector<ASTNode> args) {
		base_initializers_.emplace_back(base_name, std::move(args));
	}

	void set_delegating_initializer(std::vector<ASTNode> args) {
		delegating_initializer_.emplace(std::move(args));
	}

	void set_is_implicit(bool implicit) {
		is_implicit_ = implicit;
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

	// Pre-computed mangled name for consistent access across all compiler stages
	void set_mangled_name(std::string_view name) { mangled_name_ = name; }
	std::string_view mangled_name() const { return mangled_name_; }
	bool has_mangled_name() const { return !mangled_name_.empty(); }

private:
	StringHandle struct_name_;
	StringHandle name_;
	std::vector<ASTNode> parameter_nodes_;
	std::vector<MemberInitializer> member_initializers_;
	std::vector<BaseInitializer> base_initializers_;  // Base class initializers
	std::optional<DelegatingInitializer> delegating_initializer_;  // Delegating constructor call
	std::optional<ASTNode> definition_block_;  // Store ASTNode to keep BlockNode alive
	bool is_implicit_;  // True if this is an implicitly generated default constructor
	std::string_view mangled_name_;  // Pre-computed mangled name (points to ChunkedStringAllocator storage)
};

// Destructor declaration node
class DestructorDeclarationNode {
public:
	DestructorDeclarationNode() = delete;
	DestructorDeclarationNode(StringHandle struct_name_handle, StringHandle name_handle)
		: struct_name_(struct_name_handle), name_(name_handle) {}

	StringHandle struct_name() const { return struct_name_; }
	StringHandle name() const { return name_; }
	Token name_token() const { return Token(Token::Type::Identifier, StringTable::getStringView(name_), 0, 0, 0); }  // Create token on demand

	const std::optional<ASTNode>& get_definition() const {
		return definition_block_;
	}

	bool set_definition(ASTNode block_node) {
		if (definition_block_.has_value())
			return false;
		definition_block_.emplace(block_node);
		return true;
	}

	// Pre-computed mangled name for consistent access across all compiler stages
	void set_mangled_name(StringHandle name) { mangled_name_ = name; }
	StringHandle mangled_name() const { return mangled_name_; }
	bool has_mangled_name() const { return mangled_name_.isValid(); }

private:
	StringHandle struct_name_;  // Points directly into source text from lexer token
	StringHandle name_;         // Points directly into source text from lexer token
	std::optional<ASTNode> definition_block_;  // Store ASTNode to keep BlockNode alive
	StringHandle mangled_name_;  // Pre-computed mangled name (points to ChunkedStringAllocator storage)
};

// Struct member with access specifier
struct StructMemberDecl {
	ASTNode declaration;
	AccessSpecifier access;
	std::optional<ASTNode> default_initializer;  // C++11 default member initializer

	StructMemberDecl(ASTNode decl, AccessSpecifier acc, std::optional<ASTNode> init = std::nullopt)
		: declaration(decl), access(acc), default_initializer(init) {}
};

// Struct member function with access specifier
struct StructMemberFunctionDecl {
	ASTNode function_declaration;  // FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
	AccessSpecifier access;
	bool is_constructor;
	bool is_destructor;
	bool is_operator_overload;
	std::string_view operator_symbol;  // The operator symbol (e.g., "=", "+") if is_operator_overload is true

	// Virtual function support (Phase 2)
	bool is_virtual = false;        // True if declared with 'virtual' keyword
	bool is_pure_virtual = false;   // True if pure virtual (= 0)
	bool is_override = false;       // True if declared with 'override' keyword
	bool is_final = false;          // True if declared with 'final' keyword

	// CV qualifiers for member functions (Phase 4)
	bool is_const = false;          // True if const member function (e.g., void foo() const)
	bool is_volatile = false;       // True if volatile member function (e.g., void foo() volatile)

	StructMemberFunctionDecl(ASTNode func_decl, AccessSpecifier acc, bool is_ctor = false, bool is_dtor = false,
	                         bool is_op_overload = false, std::string_view op_symbol = "")
		: function_declaration(func_decl), access(acc), is_constructor(is_ctor), is_destructor(is_dtor),
		  is_operator_overload(is_op_overload), operator_symbol(op_symbol) {}
};

// Friend declaration node
class FriendDeclarationNode {
public:
	// Friend class declaration: friend class ClassName;
	explicit FriendDeclarationNode(FriendKind kind, StringHandle name)
		: kind_(kind), name_(name) {}

	// Friend member function declaration: friend void ClassName::functionName();
	FriendDeclarationNode(FriendKind kind, StringHandle name, StringHandle class_name)
		: kind_(kind), name_(name), class_name_(class_name) {}

	FriendKind kind() const { return kind_; }
	StringHandle name() const { return name_; }
	StringHandle class_name() const { return class_name_; }

	// For friend functions, store the function declaration
	void set_function_declaration(ASTNode decl) { function_decl_ = decl; }
	std::optional<ASTNode> function_declaration() const { return function_decl_; }

private:
	FriendKind kind_;
	StringHandle name_;           // Function or class name
	StringHandle class_name_;     // For member functions: the class name
	std::optional<ASTNode> function_decl_;  // For friend functions
};

// Type alias declaration (using alias = type;)
struct TypeAliasDecl {
	StringHandle alias_name;  // The alias name
	ASTNode type_node;            // TypeSpecifierNode representing the aliased type
	AccessSpecifier access;       // Access specifier (public/private/protected)

	TypeAliasDecl(StringHandle name, ASTNode type, AccessSpecifier acc)
		: alias_name(name), type_node(type), access(acc) {}
};

class StructDeclarationNode {
public:
	explicit StructDeclarationNode(StringHandle name, bool is_class = false)
		: name_(name), is_class_(is_class) {}

	StringHandle name() const { return name_; }
	const std::vector<StructMemberDecl>& members() const { return members_; }
	const std::vector<StructMemberFunctionDecl>& member_functions() const { return member_functions_; }
	std::vector<StructMemberFunctionDecl>& member_functions() { return member_functions_; }
	const std::vector<BaseClassSpecifier>& base_classes() const { return base_classes_; }
	const std::vector<DeferredBaseClassSpecifier>& deferred_base_classes() const { return deferred_base_classes_; }
	std::vector<DeferredBaseClassSpecifier>& deferred_base_classes() { return deferred_base_classes_; }
	const std::vector<DeferredTemplateBaseClassSpecifier>& deferred_template_base_classes() const { return deferred_template_base_classes_; }
	bool is_class() const { return is_class_; }
	bool is_final() const { return is_final_; }
	void set_is_final(bool final) { is_final_ = final; }
	AccessSpecifier default_access() const {
		return is_class_ ? AccessSpecifier::Private : AccessSpecifier::Public;
	}

	void add_member(const ASTNode& member, AccessSpecifier access, std::optional<ASTNode> default_initializer = std::nullopt) {
		members_.emplace_back(member, access, std::move(default_initializer));
	}

	void add_base_class(std::string_view base_name, TypeIndex base_type_index, AccessSpecifier access, bool is_virtual = false, bool is_deferred = false) {
		base_classes_.emplace_back(base_name, base_type_index, access, is_virtual, 0, is_deferred);
	}

	void add_deferred_base_class(ASTNode decltype_expr, AccessSpecifier access, bool is_virtual = false) {
		deferred_base_classes_.emplace_back(decltype_expr, access, is_virtual);
	}

	void add_deferred_template_base_class(StringHandle base_template_name,
	                                      std::vector<TemplateArgumentNodeInfo> args,
	                                      std::optional<StringHandle> member_type,
	                                      AccessSpecifier access,
	                                      bool is_virtual = false) {
		deferred_template_base_classes_.emplace_back(base_template_name, std::move(args), member_type, access, is_virtual);
	}

	void add_member_function(ASTNode function_decl, AccessSpecifier access,
	                         bool is_virtual = false, bool is_pure_virtual = false,
	                         bool is_override = false, bool is_final = false,
	                         bool is_const = false, bool is_volatile = false) {
		auto& func_decl = member_functions_.emplace_back(function_decl, access, false, false);
		func_decl.is_virtual = is_virtual;
		func_decl.is_pure_virtual = is_pure_virtual;
		func_decl.is_override = is_override;
		func_decl.is_final = is_final;
		func_decl.is_const = is_const;
		func_decl.is_volatile = is_volatile;
	}

	void add_constructor(ASTNode constructor_decl, AccessSpecifier access) {
		member_functions_.emplace_back(constructor_decl, access, true, false);
	}

	void add_destructor(ASTNode destructor_decl, AccessSpecifier access, bool is_virtual = false) {
		auto& dtor_decl = member_functions_.emplace_back(destructor_decl, access, false, true, false, "");
		dtor_decl.is_virtual = is_virtual;
	}

	void add_operator_overload(std::string_view operator_symbol, ASTNode function_decl, AccessSpecifier access,
	                           bool is_virtual = false, bool is_pure_virtual = false,
	                           bool is_override = false, bool is_final = false,
	                           bool is_const = false, bool is_volatile = false) {
		auto& func_decl = member_functions_.emplace_back(function_decl, access, false, false, true, operator_symbol);
		func_decl.is_virtual = is_virtual;
		func_decl.is_pure_virtual = is_pure_virtual;
		func_decl.is_override = is_override;
		func_decl.is_final = is_final;
		func_decl.is_const = is_const;
		func_decl.is_volatile = is_volatile;
	}

	// Friend declaration support
	void add_friend(ASTNode friend_decl) {
		friend_declarations_.push_back(friend_decl);
	}

	const std::vector<ASTNode>& friend_declarations() const {
		return friend_declarations_;
	}

	// Nested class support
	void add_nested_class(ASTNode nested_class) {
		nested_classes_.push_back(nested_class);
	}

	const std::vector<ASTNode>& nested_classes() const {
		return nested_classes_;
	}

	// Type alias support
	void add_type_alias(StringHandle alias_name, ASTNode type_node, AccessSpecifier access) {
		type_aliases_.emplace_back(alias_name, type_node, access);
	}

	const std::vector<TypeAliasDecl>& type_aliases() const {
		return type_aliases_;
	}

	void set_enclosing_class(StructDeclarationNode* enclosing) {
		enclosing_class_ = enclosing;
	}

	StructDeclarationNode* enclosing_class() const {
		return enclosing_class_;
	}

	bool is_nested() const {
		return enclosing_class_ != nullptr;
	}

	// Get fully qualified name (e.g., "Outer::Inner")
	StringHandle qualified_name() const {
		if (enclosing_class_) {
			return StringTable::getOrInternStringHandle(StringBuilder().append(enclosing_class_->qualified_name()).append("::"sv).append(name_).commit());
		}
		return name_;
	}

private:
	StringHandle name_;  // Points directly into source text from lexer token
	std::vector<StructMemberDecl> members_;
	std::vector<StructMemberFunctionDecl> member_functions_;
	std::vector<BaseClassSpecifier> base_classes_;  // Base classes for inheritance
	std::vector<DeferredBaseClassSpecifier> deferred_base_classes_;  // Decltype base classes (for templates)
	std::vector<DeferredTemplateBaseClassSpecifier> deferred_template_base_classes_;  // Template-dependent base classes
	std::vector<ASTNode> friend_declarations_;  // Friend declarations
	std::vector<ASTNode> nested_classes_;  // Nested classes
	std::vector<TypeAliasDecl> type_aliases_;  // Type aliases (using X = Y;)
	StructDeclarationNode* enclosing_class_ = nullptr;  // Enclosing class (if nested)
	bool is_class_;  // true for class, false for struct
	bool is_final_ = false;  // true if declared with 'final' keyword
};

// Template class declaration node - represents a class template
class TemplateClassDeclarationNode {
public:
	TemplateClassDeclarationNode() = delete;
	TemplateClassDeclarationNode(std::vector<ASTNode> template_params, 
								std::vector<std::string_view> param_names,
								ASTNode class_decl)
		: template_parameters_(std::move(template_params))
		, template_param_names_(std::move(param_names))
		, class_declaration_(class_decl) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	const std::vector<std::string_view>& template_param_names() const { return template_param_names_; }
	ASTNode class_declaration() const { return class_declaration_; }

	// Get the underlying StructDeclarationNode
	StructDeclarationNode& class_decl_node() {
		return class_declaration_.as<StructDeclarationNode>();
	}
	const StructDeclarationNode& class_decl_node() const {
		return class_declaration_.as<StructDeclarationNode>();
	}

	// Deferred template body parsing support
	void set_deferred_bodies(std::vector<DeferredTemplateMemberBody> bodies) {
		deferred_bodies_ = std::move(bodies);
	}
	const std::vector<DeferredTemplateMemberBody>& deferred_bodies() const {
		return deferred_bodies_;
	}
	std::vector<DeferredTemplateMemberBody>& deferred_bodies() {
		return deferred_bodies_;
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	std::vector<std::string_view> template_param_names_;  // Parameter names for lookup
	ASTNode class_declaration_;  // StructDeclarationNode
	std::vector<DeferredTemplateMemberBody> deferred_bodies_;  // Member function bodies to parse at instantiation
};

// Namespace declaration node
class NamespaceDeclarationNode {
public:
	explicit NamespaceDeclarationNode(std::string_view name)
		: name_(name) {}

	std::string_view name() const { return name_; }
	const std::vector<ASTNode>& declarations() const { return declarations_; }
	bool is_anonymous() const { return name_.empty(); }

	void add_declaration(ASTNode decl) {
		declarations_.push_back(decl);
	}

private:
	std::string_view name_;  // Points directly into source text from lexer token (empty for anonymous namespaces)
	std::vector<ASTNode> declarations_;  // Declarations within the namespace
};

// Using directive node: using namespace std;
class UsingDirectiveNode {
public:
	explicit UsingDirectiveNode(std::vector<StringType<>> namespace_path, Token using_token)
		: namespace_path_(std::move(namespace_path)), using_token_(using_token) {}

	const std::vector<StringType<>>& namespace_path() const { return namespace_path_; }
	const Token& using_token() const { return using_token_; }

	// Get the full namespace name as a string (e.g., "std::filesystem")
	std::string full_namespace_name() const {
		std::string result;
		for (size_t i = 0; i < namespace_path_.size(); ++i) {
			if (i > 0) result += "::";
#if USE_OLD_STRING_APPROACH
			result += namespace_path_[i];
#else
			result += std::string(namespace_path_[i].view());
#endif
		}
		return result;
	}

private:
	std::vector<StringType<>> namespace_path_;  // e.g., ["std", "filesystem"] for "using namespace std::filesystem;"
	Token using_token_;  // For error reporting
};

// Using declaration node: using std::vector;
class UsingDeclarationNode {
public:
	explicit UsingDeclarationNode(std::vector<StringType<>> namespace_path, Token identifier, Token using_token)
		: namespace_path_(std::move(namespace_path)), identifier_(identifier), using_token_(using_token) {}

	const std::vector<StringType<>>& namespace_path() const { return namespace_path_; }
	std::string_view identifier_name() const { return identifier_.value(); }
	const Token& identifier_token() const { return identifier_; }
	const Token& using_token() const { return using_token_; }

private:
	std::vector<StringType<>> namespace_path_;  // e.g., ["std"] for "using std::vector;"
	Token identifier_;  // The identifier being imported (e.g., "vector")
	Token using_token_;  // For error reporting
};

// Namespace alias node: namespace fs = std::filesystem;
class NamespaceAliasNode {
public:
	explicit NamespaceAliasNode(Token alias_name, std::vector<StringType<>> target_namespace)
		: alias_name_(alias_name), target_namespace_(std::move(target_namespace)) {}

	std::string_view alias_name() const { return alias_name_.value(); }
	const std::vector<StringType<>>& target_namespace() const { return target_namespace_; }
	const Token& alias_token() const { return alias_name_; }

private:
	Token alias_name_;  // The alias (e.g., "fs")
	std::vector<StringType<>> target_namespace_;  // e.g., ["std", "filesystem"]
};

// Enumerator node - represents a single enumerator in an enum
class EnumeratorNode {
public:
	EnumeratorNode(Token name, std::optional<ASTNode> value = std::nullopt)
		: name_(name), value_(value) {}

	std::string_view name() const { return name_.value(); }
	const Token& name_token() const { return name_; }
	bool has_value() const { return value_.has_value(); }
	const std::optional<ASTNode>& value() const { return value_; }

private:
	Token name_;                    // Enumerator name
	std::optional<ASTNode> value_;  // Optional initializer expression
};

// Enum declaration node - represents enum or enum class
class EnumDeclarationNode {
public:
	explicit EnumDeclarationNode(StringHandle name_handle, bool is_scoped = false)
		: name_(StringTable::getStringView(name_handle)), is_scoped_(is_scoped), underlying_type_() {}

	std::string_view name() const { return name_; }
	bool is_scoped() const { return is_scoped_; }  // true for enum class, false for enum
	bool has_underlying_type() const { return underlying_type_.has_value(); }
	const std::optional<ASTNode>& underlying_type() const { return underlying_type_; }
	const std::vector<ASTNode>& enumerators() const { return enumerators_; }

	void set_underlying_type(ASTNode type) {
		underlying_type_ = type;
	}

	void add_enumerator(ASTNode enumerator) {
		enumerators_.push_back(enumerator);
	}

private:
	std::string_view name_;                 // Points directly into source text from lexer token
	bool is_scoped_;                        // true for enum class, false for enum
	std::optional<ASTNode> underlying_type_; // Optional underlying type (TypeSpecifierNode)
	std::vector<ASTNode> enumerators_;      // List of EnumeratorNode
};

class MemberAccessNode {
public:
	explicit MemberAccessNode(ASTNode object, Token member_name, bool is_arrow = false)
		: object_(object), member_name_(member_name), is_arrow_(is_arrow) {}

	ASTNode object() const { return object_; }
	std::string_view member_name() const { return member_name_.value(); }
	const Token& member_token() const { return member_name_; }
	bool is_arrow() const { return is_arrow_; }

private:
	ASTNode object_;
	Token member_name_;
	bool is_arrow_;  // True if accessed via -> instead of .
};

// Member function call node (e.g., obj.method(args))
class MemberFunctionCallNode {
public:
	explicit MemberFunctionCallNode(ASTNode object, FunctionDeclarationNode& func_decl,
	                                ChunkedVector<ASTNode>&& arguments, Token called_from_token)
		: object_(object), func_decl_(func_decl), arguments_(std::move(arguments)), called_from_(called_from_token) {}

	ASTNode object() const { return object_; }
	const auto& arguments() const { return arguments_; }
	const auto& function_declaration() const { return func_decl_; }
	Token called_from() const { return called_from_; }

	void add_argument(ASTNode argument) { arguments_.push_back(argument); }

private:
	ASTNode object_;                    // The object on which the method is called
	FunctionDeclarationNode& func_decl_; // The member function declaration
	ChunkedVector<ASTNode> arguments_;   // Arguments to the function call
	Token called_from_;                  // Token for error reporting
};

class ArraySubscriptNode {
public:
	explicit ArraySubscriptNode(ASTNode array_expr, ASTNode index_expr, Token bracket_token)
		: array_expr_(array_expr), index_expr_(index_expr), bracket_token_(bracket_token) {}

	ASTNode array_expr() const { return array_expr_; }
	ASTNode index_expr() const { return index_expr_; }
	const Token& bracket_token() const { return bracket_token_; }

private:
	ASTNode array_expr_;
	ASTNode index_expr_;
	Token bracket_token_;
};

// sizeof operator node - can take either a type or an expression
class SizeofExprNode {
public:
	// Constructor for sizeof(type)
	explicit SizeofExprNode(ASTNode type_node, Token sizeof_token)
		: type_or_expr_(type_node), sizeof_token_(sizeof_token), is_type_(true) {}

	// Constructor for sizeof(expression)
	static SizeofExprNode from_expression(ASTNode expr_node, Token sizeof_token) {
		SizeofExprNode node(expr_node, sizeof_token);
		node.is_type_ = false;
		return node;
	}

	ASTNode type_or_expr() const { return type_or_expr_; }
	const Token& sizeof_token() const { return sizeof_token_; }
	bool is_type() const { return is_type_; }

private:
	ASTNode type_or_expr_;  // Either TypeSpecifierNode or ExpressionNode
	Token sizeof_token_;
	bool is_type_;
};

// sizeof... operator node - returns the number of elements in a parameter pack
class SizeofPackNode {
public:
	explicit SizeofPackNode(std::string_view pack_name, Token sizeof_token)
		: pack_name_(pack_name), sizeof_token_(sizeof_token) {}

	std::string_view pack_name() const { return pack_name_; }
	const Token& sizeof_token() const { return sizeof_token_; }

private:
	std::string_view pack_name_;  // Name of the parameter pack
	Token sizeof_token_;
};

// alignof operator node - returns the alignment requirement of a type
class AlignofExprNode {
public:
	// Constructor for alignof(type)
	explicit AlignofExprNode(ASTNode type_node, Token alignof_token)
		: type_or_expr_(type_node), alignof_token_(alignof_token), is_type_(true) {}

	// Constructor for alignof(expression)
	static AlignofExprNode from_expression(ASTNode expr_node, Token alignof_token) {
		AlignofExprNode node(expr_node, alignof_token);
		node.is_type_ = false;
		return node;
	}

	ASTNode type_or_expr() const { return type_or_expr_; }
	const Token& alignof_token() const { return alignof_token_; }
	bool is_type() const { return is_type_; }

private:
	ASTNode type_or_expr_;  // Either TypeSpecifierNode or ExpressionNode
	Token alignof_token_;
	bool is_type_;
};

// offsetof operator node - offsetof(struct_type, member)
class OffsetofExprNode {
public:
	explicit OffsetofExprNode(ASTNode type_node, Token member_name, Token offsetof_token)
		: type_node_(type_node), member_name_(member_name), offsetof_token_(offsetof_token) {}

	ASTNode type_node() const { return type_node_; }
	std::string_view member_name() const { return member_name_.value(); }
	const Token& offsetof_token() const { return offsetof_token_; }

private:
	ASTNode type_node_;      // TypeSpecifierNode for the struct type
	Token member_name_;      // Name of the member
	Token offsetof_token_;
};

// Type trait intrinsic expression node - __is_void(T), __is_integral(T), etc.
enum class TypeTraitKind {
	// Primary type categories
	IsVoid,
	IsNullptr,
	IsIntegral,
	IsFloatingPoint,
	IsArray,
	IsPointer,
	IsLvalueReference,
	IsRvalueReference,
	IsMemberObjectPointer,
	IsMemberFunctionPointer,
	IsEnum,
	IsUnion,
	IsClass,
	IsFunction,
	// Composite type categories
	IsReference,        // __is_reference - lvalue or rvalue reference
	IsArithmetic,       // __is_arithmetic - integral or floating point
	IsFundamental,      // __is_fundamental - void, nullptr, arithmetic
	IsObject,           // __is_object - not function, not reference, not void
	IsScalar,           // __is_scalar - arithmetic, pointer, enum, member pointer, nullptr
	IsCompound,         // __is_compound - array, function, pointer, reference, class, union, enum, member pointer
	// Type relationships (binary trait - takes 2 types)
	IsBaseOf,
	IsSame,
	IsConvertible,      // __is_convertible(From, To) - check if From can convert to To
	IsAssignable,
	IsTriviallyAssignable,
	IsNothrowAssignable,
	IsLayoutCompatible,
	IsPointerInterconvertibleBaseOf,
	// Type properties
	IsConst,            // __is_const - has const qualifier
	IsVolatile,         // __is_volatile - has volatile qualifier
	IsSigned,           // __is_signed - signed integral type
	IsUnsigned,         // __is_unsigned - unsigned integral type
	IsBoundedArray,     // __is_bounded_array - array with known bound
	IsUnboundedArray,   // __is_unbounded_array - array with unknown bound
	IsPolymorphic,
	IsFinal,
	IsAbstract,
	IsEmpty,
	IsAggregate,         // __is_aggregate - type is an aggregate
	IsStandardLayout,
	HasUniqueObjectRepresentations,
	IsTriviallyCopyable,
	IsTrivial,
	IsPod,
	// Constructibility traits (variadic - takes T + Args...)
	IsConstructible,
	IsTriviallyConstructible,
	IsNothrowConstructible,
	// Destructibility traits (unary)
	IsDestructible,
	IsTriviallyDestructible,
	IsNothrowDestructible,
	// Special traits
	UnderlyingType,      // __underlying_type(T) - returns the underlying type of an enum
	IsConstantEvaluated  // __is_constant_evaluated() - no arguments, returns bool
};

class TypeTraitExprNode {
public:
	// Constructor for unary type traits (single type argument)
	explicit TypeTraitExprNode(TypeTraitKind kind, ASTNode type_node, Token trait_token)
		: kind_(kind), type_node_(type_node), second_type_node_(std::nullopt), additional_type_nodes_(), trait_token_(trait_token) {}

	// Constructor for binary type traits (two type arguments, like __is_base_of, __is_assignable)
	explicit TypeTraitExprNode(TypeTraitKind kind, ASTNode type_node, ASTNode second_type_node, Token trait_token)
		: kind_(kind), type_node_(type_node), second_type_node_(second_type_node), additional_type_nodes_(), trait_token_(trait_token) {}

	// Constructor for variadic type traits (T + Args..., like __is_constructible)
	explicit TypeTraitExprNode(TypeTraitKind kind, ASTNode type_node, std::vector<ASTNode> additional_types, Token trait_token)
		: kind_(kind), type_node_(type_node), second_type_node_(std::nullopt), additional_type_nodes_(std::move(additional_types)), trait_token_(trait_token) {}

	// Constructor for no-argument traits (like __is_constant_evaluated)
	explicit TypeTraitExprNode(TypeTraitKind kind, Token trait_token)
		: kind_(kind), type_node_(), second_type_node_(std::nullopt), additional_type_nodes_(), trait_token_(trait_token) {}

	TypeTraitKind kind() const { return kind_; }
	ASTNode type_node() const { return type_node_; }
	bool has_type() const { return type_node_.is<TypeSpecifierNode>(); }
	bool has_second_type() const { return second_type_node_.has_value(); }
	ASTNode second_type_node() const { return second_type_node_.value_or(ASTNode()); }
	const std::vector<ASTNode>& additional_type_nodes() const { return additional_type_nodes_; }
	const Token& trait_token() const { return trait_token_; }

	// Check if this is a binary trait (takes exactly 2 types)
	bool is_binary_trait() const {
		return kind_ == TypeTraitKind::IsBaseOf ||
		       kind_ == TypeTraitKind::IsSame ||
		       kind_ == TypeTraitKind::IsConvertible ||
		       kind_ == TypeTraitKind::IsAssignable ||
		       kind_ == TypeTraitKind::IsTriviallyAssignable ||
		       kind_ == TypeTraitKind::IsNothrowAssignable ||
		       kind_ == TypeTraitKind::IsLayoutCompatible ||
		       kind_ == TypeTraitKind::IsPointerInterconvertibleBaseOf;
	}

	// Check if this is a variadic trait (takes T + Args...)
	bool is_variadic_trait() const {
		return kind_ == TypeTraitKind::IsConstructible ||
		       kind_ == TypeTraitKind::IsTriviallyConstructible ||
		       kind_ == TypeTraitKind::IsNothrowConstructible;
	}

	// Check if this is a no-argument trait
	bool is_no_arg_trait() const {
		return kind_ == TypeTraitKind::IsConstantEvaluated;
	}

	// Get the string name of the trait for error messages
	std::string_view trait_name() const {
		switch (kind_) {
			case TypeTraitKind::IsVoid: return "__is_void";
			case TypeTraitKind::IsNullptr: return "__is_nullptr";
			case TypeTraitKind::IsIntegral: return "__is_integral";
			case TypeTraitKind::IsFloatingPoint: return "__is_floating_point";
			case TypeTraitKind::IsArray: return "__is_array";
			case TypeTraitKind::IsPointer: return "__is_pointer";
			case TypeTraitKind::IsLvalueReference: return "__is_lvalue_reference";
			case TypeTraitKind::IsRvalueReference: return "__is_rvalue_reference";
			case TypeTraitKind::IsMemberObjectPointer: return "__is_member_object_pointer";
			case TypeTraitKind::IsMemberFunctionPointer: return "__is_member_function_pointer";
			case TypeTraitKind::IsEnum: return "__is_enum";
			case TypeTraitKind::IsUnion: return "__is_union";
			case TypeTraitKind::IsClass: return "__is_class";
			case TypeTraitKind::IsFunction: return "__is_function";
			case TypeTraitKind::IsReference: return "__is_reference";
			case TypeTraitKind::IsArithmetic: return "__is_arithmetic";
			case TypeTraitKind::IsFundamental: return "__is_fundamental";
			case TypeTraitKind::IsObject: return "__is_object";
			case TypeTraitKind::IsScalar: return "__is_scalar";
			case TypeTraitKind::IsCompound: return "__is_compound";
			case TypeTraitKind::IsBaseOf: return "__is_base_of";
			case TypeTraitKind::IsSame: return "__is_same";
			case TypeTraitKind::IsConvertible: return "__is_convertible";
			case TypeTraitKind::IsConst: return "__is_const";
			case TypeTraitKind::IsVolatile: return "__is_volatile";
			case TypeTraitKind::IsSigned: return "__is_signed";
			case TypeTraitKind::IsUnsigned: return "__is_unsigned";
			case TypeTraitKind::IsBoundedArray: return "__is_bounded_array";
			case TypeTraitKind::IsUnboundedArray: return "__is_unbounded_array";
			case TypeTraitKind::IsPolymorphic: return "__is_polymorphic";
			case TypeTraitKind::IsFinal: return "__is_final";
			case TypeTraitKind::IsAbstract: return "__is_abstract";
			case TypeTraitKind::IsEmpty: return "__is_empty";
			case TypeTraitKind::IsAggregate: return "__is_aggregate";
			case TypeTraitKind::IsStandardLayout: return "__is_standard_layout";
			case TypeTraitKind::HasUniqueObjectRepresentations: return "__has_unique_object_representations";
			case TypeTraitKind::IsTriviallyCopyable: return "__is_trivially_copyable";
			case TypeTraitKind::IsTrivial: return "__is_trivial";
			case TypeTraitKind::IsPod: return "__is_pod";
			case TypeTraitKind::IsConstructible: return "__is_constructible";
			case TypeTraitKind::IsTriviallyConstructible: return "__is_trivially_constructible";
			case TypeTraitKind::IsNothrowConstructible: return "__is_nothrow_constructible";
			case TypeTraitKind::IsAssignable: return "__is_assignable";
			case TypeTraitKind::IsTriviallyAssignable: return "__is_trivially_assignable";
			case TypeTraitKind::IsNothrowAssignable: return "__is_nothrow_assignable";
			case TypeTraitKind::IsDestructible: return "__is_destructible";
			case TypeTraitKind::IsTriviallyDestructible: return "__is_trivially_destructible";
			case TypeTraitKind::IsNothrowDestructible: return "__is_nothrow_destructible";
			case TypeTraitKind::UnderlyingType: return "__underlying_type";
			case TypeTraitKind::IsConstantEvaluated: return "__is_constant_evaluated";
			case TypeTraitKind::IsLayoutCompatible: return "__is_layout_compatible";
			case TypeTraitKind::IsPointerInterconvertibleBaseOf: return "__is_pointer_interconvertible_base_of";
			default: return "__unknown_trait";
		}
	}

private:
	TypeTraitKind kind_;
	ASTNode type_node_;      // TypeSpecifierNode for the first type argument
	std::optional<ASTNode> second_type_node_;  // TypeSpecifierNode for the second type argument (for binary traits)
	std::vector<ASTNode> additional_type_nodes_;  // Additional type arguments (for variadic traits like __is_constructible)
	Token trait_token_;      // Token for the trait (for error reporting)
};

// New expression node: new Type, new Type(args), new Type[size], new (address) Type
class NewExpressionNode {
public:
	explicit NewExpressionNode(ASTNode type_node, bool is_array = false,
	                          std::optional<ASTNode> size_expr = std::nullopt,
	                          ChunkedVector<ASTNode, 128, 256> constructor_args = {},
	                          std::optional<ASTNode> placement_address = std::nullopt)
		: type_node_(type_node), is_array_(is_array),
		  size_expr_(size_expr), constructor_args_(std::move(constructor_args)),
		  placement_address_(placement_address) {}

	const ASTNode& type_node() const { return type_node_; }
	bool is_array() const { return is_array_; }
	const std::optional<ASTNode>& size_expr() const { return size_expr_; }
	const ChunkedVector<ASTNode, 128, 256>& constructor_args() const { return constructor_args_; }
	const std::optional<ASTNode>& placement_address() const { return placement_address_; }

private:
	ASTNode type_node_;  // TypeSpecifierNode
	bool is_array_;      // true for new[], false for new
	std::optional<ASTNode> size_expr_;  // For new Type[size], the size expression
	ChunkedVector<ASTNode, 128, 256> constructor_args_;  // For new Type(args)
	std::optional<ASTNode> placement_address_;  // For new (address) Type, the placement address
};

// Delete expression node: delete ptr, delete[] ptr
class DeleteExpressionNode {
public:
	explicit DeleteExpressionNode(ASTNode expr, bool is_array = false)
		: expr_(expr), is_array_(is_array) {}

	const ASTNode& expr() const { return expr_; }
	bool is_array() const { return is_array_; }

private:
	ASTNode expr_;       // Expression to delete
	bool is_array_;      // true for delete[], false for delete
};

// Static cast expression node: static_cast<Type>(expr)
class StaticCastNode {
public:
	explicit StaticCastNode(ASTNode target_type, ASTNode expr, Token cast_token)
		: target_type_(target_type), expr_(expr), cast_token_(cast_token) {}

	const ASTNode& target_type() const { return target_type_; }
	const ASTNode& expr() const { return expr_; }
	const Token& cast_token() const { return cast_token_; }

private:
	ASTNode target_type_;  // TypeSpecifierNode - the type to cast to
	ASTNode expr_;         // ExpressionNode - the expression to cast
	Token cast_token_;     // Token for error reporting
};

// Dynamic cast expression node: dynamic_cast<Type>(expr)
class DynamicCastNode {
public:
	explicit DynamicCastNode(ASTNode target_type, ASTNode expr, Token cast_token)
		: target_type_(target_type), expr_(expr), cast_token_(cast_token) {}

	const ASTNode& target_type() const { return target_type_; }
	const ASTNode& expr() const { return expr_; }
	const Token& cast_token() const { return cast_token_; }

private:
	ASTNode target_type_;  // TypeSpecifierNode - the type to cast to (must be pointer or reference)
	ASTNode expr_;         // ExpressionNode - the expression to cast (must be polymorphic)
	Token cast_token_;     // Token for error reporting
};

// Const cast expression node: const_cast<Type>(expr)
class ConstCastNode {
public:
	explicit ConstCastNode(ASTNode target_type, ASTNode expr, Token cast_token)
		: target_type_(target_type), expr_(expr), cast_token_(cast_token) {}

	const ASTNode& target_type() const { return target_type_; }
	const ASTNode& expr() const { return expr_; }
	const Token& cast_token() const { return cast_token_; }

private:
	ASTNode target_type_;  // TypeSpecifierNode - the type to cast to (adds/removes const/volatile)
	ASTNode expr_;         // ExpressionNode - the expression to cast
	Token cast_token_;     // Token for error reporting
};

// Reinterpret cast expression node: reinterpret_cast<Type>(expr)
class ReinterpretCastNode {
public:
	explicit ReinterpretCastNode(ASTNode target_type, ASTNode expr, Token cast_token)
		: target_type_(target_type), expr_(expr), cast_token_(cast_token) {}

	const ASTNode& target_type() const { return target_type_; }
	const ASTNode& expr() const { return expr_; }
	const Token& cast_token() const { return cast_token_; }

private:
	ASTNode target_type_;  // TypeSpecifierNode - the type to cast to (bit pattern reinterpretation)
	ASTNode expr_;         // ExpressionNode - the expression to cast
	Token cast_token_;     // Token for error reporting
};

// Typeid expression node: typeid(expr) or typeid(Type)
class TypeidNode {
public:
	// Constructor for typeid(expr)
	explicit TypeidNode(ASTNode operand, bool is_type, Token typeid_token)
		: operand_(operand), is_type_(is_type), typeid_token_(typeid_token) {}

	const ASTNode& operand() const { return operand_; }
	bool is_type() const { return is_type_; }  // true if operand is a type, false if expression
	const Token& typeid_token() const { return typeid_token_; }

private:
	ASTNode operand_;      // Either TypeSpecifierNode or ExpressionNode
	bool is_type_;         // true for typeid(Type), false for typeid(expr)
	Token typeid_token_;   // Token for error reporting
};

// Lambda capture node - represents a single capture in a lambda
class LambdaCaptureNode {
public:
	enum class CaptureKind {
		ByValue,      // [x]
		ByReference,  // [&x]
		AllByValue,   // [=]
		AllByReference, // [&]
		This,         // [this]
		CopyThis      // [*this] (C++17)
	};

	explicit LambdaCaptureNode(CaptureKind kind, Token identifier = Token(), std::optional<ASTNode> initializer = std::nullopt)
		: kind_(kind), identifier_(identifier), initializer_(initializer) {}

	CaptureKind kind() const { return kind_; }
	std::string_view identifier_name() const { return identifier_.value(); }
	const Token& identifier_token() const { return identifier_; }
	bool is_capture_all() const {
		return kind_ == CaptureKind::AllByValue || kind_ == CaptureKind::AllByReference;
	}
	bool has_initializer() const { return initializer_.has_value(); }
	const std::optional<ASTNode>& initializer() const { return initializer_; }

private:
	CaptureKind kind_;
	Token identifier_;  // Empty for capture-all and [this]
	std::optional<ASTNode> initializer_;  // For init-captures like [x = expr]
};

// Lambda expression node
class LambdaExpressionNode {
public:
	explicit LambdaExpressionNode(
		std::vector<LambdaCaptureNode> captures,
		std::vector<ASTNode> parameters,
		ASTNode body,
		std::optional<ASTNode> return_type = std::nullopt,
		Token lambda_token = Token())
		: captures_(std::move(captures)),
		  parameters_(std::move(parameters)),
		  body_(body),
		  return_type_(return_type),
		  lambda_token_(lambda_token),
		  lambda_id_(next_lambda_id_++) {}

	const std::vector<LambdaCaptureNode>& captures() const { return captures_; }
	const std::vector<ASTNode>& parameters() const { return parameters_; }
	const ASTNode& body() const { return body_; }
	const std::optional<ASTNode>& return_type() const { return return_type_; }
	const Token& lambda_token() const { return lambda_token_; }
	size_t lambda_id() const { return lambda_id_; }

	// Generate a unique name for the lambda's generated function
	StringHandle generate_lambda_name() const {
		return StringTable::getOrInternStringHandle(StringBuilder().append("__lambda_"sv).append(lambda_id_));
	}

private:
	std::vector<LambdaCaptureNode> captures_;
	std::vector<ASTNode> parameters_;
	ASTNode body_;
	std::optional<ASTNode> return_type_;  // Optional return type (e.g., -> int)
	Token lambda_token_;  // For error reporting
	size_t lambda_id_;    // Unique ID for this lambda

	static inline size_t next_lambda_id_ = 0;  // Counter for generating unique IDs
};

// Template parameter reference node - represents a reference to a template parameter in expressions
class TemplateParameterReferenceNode {
public:
	explicit TemplateParameterReferenceNode(StringHandle param_name, Token token)
		: param_name_(param_name), token_(token) {}

	StringHandle param_name() const { return param_name_; }
	const Token& token() const { return token_; }

private:
	StringHandle param_name_;  // Name of the template parameter being referenced
	Token token_;                  // Token for error reporting
};

using ExpressionNode = std::variant<IdentifierNode, QualifiedIdentifierNode, StringLiteralNode, NumericLiteralNode, BoolLiteralNode,
	BinaryOperatorNode, UnaryOperatorNode, TernaryOperatorNode, FunctionCallNode, ConstructorCallNode, MemberAccessNode, MemberFunctionCallNode,
	ArraySubscriptNode, SizeofExprNode, SizeofPackNode, AlignofExprNode, OffsetofExprNode, TypeTraitExprNode, NewExpressionNode, DeleteExpressionNode, StaticCastNode,
	DynamicCastNode, ConstCastNode, ReinterpretCastNode, TypeidNode, LambdaExpressionNode, TemplateParameterReferenceNode, FoldExpressionNode>;

/*class FunctionDefinitionNode {
public:
		FunctionDefinitionNode(Token definition_token, size_t definition_index,
std::vector<size_t> parameter_indices, size_t body_index) :
definition_token_(definition_token), definition_index_(definition_index),
parameter_indices_(std::move(parameter_indices)), body_index_(body_index) {}

		const Token& definition_token() const { return definition_token_; }
		size_t declaration_index() const { return definition_index_; }
		const std::vector<size_t>& parameter_indices() const { return
parameter_indices_; } size_t body_index() const { return body_index_; }

private:
		Token definition_token_;
		size_t definition_index_;
		std::vector<size_t> parameter_indices_;
		size_t body_index_;
};*/



class LoopStatementNode {
public:
	size_t start_pos;
	size_t end_pos;
};

class WhileLoopNode : public LoopStatementNode {
public:
	explicit WhileLoopNode(size_t start_pos, size_t end_pos, size_t condition,
		size_t body)
		: condition_(condition), body_(body) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	size_t condition() const { return condition_; }
	size_t body() const { return body_; }

private:
	size_t condition_;
	size_t body_;
};

class DoWhileLoopNode : public LoopStatementNode {
public:
	explicit DoWhileLoopNode(size_t start_pos, size_t end_pos, size_t body,
		size_t condition)
		: condition_(condition), body_(body) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	size_t condition() const { return condition_; }
	size_t body() const { return body_; }

private:
	size_t condition_;
	size_t body_;
};

// ForLoopNode removed - use ForStatementNode instead for C++20 compatibility

class ReturnStatementNode {
public:
	explicit ReturnStatementNode(
		std::optional<ASTNode> expression = std::nullopt, Token return_token = Token())
		: expression_(expression), return_token_(return_token) {}

	const std::optional<ASTNode>& expression() const { return expression_; }
	const Token& return_token() const { return return_token_; }

private:
	std::optional<ASTNode>
		expression_; // Optional, as a return statement may not have an expression
	Token return_token_;
};

class InitializerListNode {
public:
	explicit InitializerListNode() {}

	void add_initializer(ASTNode init_expr) {
		initializers_.push_back(init_expr);
		is_designated_.push_back(false);
		member_names_.push_back(StringHandle());  // Invalid StringHandle for positional initializers
	}

	void add_designated_initializer(StringHandle member_name, ASTNode init_expr) {
		initializers_.push_back(init_expr);
		is_designated_.push_back(true);
		member_names_.push_back(member_name);
	}

	const std::vector<ASTNode>& initializers() const {
		return initializers_;
	}

	size_t size() const {
		return initializers_.size();
	}

	bool is_designated(size_t index) const {
		return index < is_designated_.size() && is_designated_[index];
	}

	StringHandle member_name(size_t index) const {
		if (index < member_names_.size()) {
			return member_names_[index];
		}
		return StringHandle();  // Return invalid handle for out of bounds
	}

	bool has_any_designated() const {
		for (bool is_des : is_designated_) {
			if (is_des) return true;
		}
		return false;
	}

private:
	std::vector<ASTNode> initializers_;
	std::vector<bool> is_designated_;
	std::vector<StringHandle> member_names_;
};

class IfStatementNode {
public:
	explicit IfStatementNode(ASTNode condition, ASTNode then_statement,
		std::optional<ASTNode> else_statement = std::nullopt,
		std::optional<ASTNode> init_statement = std::nullopt,
		bool is_constexpr = false)
		: condition_(condition), then_statement_(then_statement),
		  else_statement_(else_statement), init_statement_(init_statement),
		  is_constexpr_(is_constexpr) {}

	auto get_condition() const { return condition_; }
	auto get_then_statement() const { return then_statement_; }
	auto get_else_statement() const { return else_statement_; }
	auto get_init_statement() const { return init_statement_; }
	bool has_else() const { return else_statement_.has_value(); }
	bool has_init() const { return init_statement_.has_value(); }
	bool is_constexpr() const { return is_constexpr_; }

private:
	ASTNode condition_;
	ASTNode then_statement_;
	std::optional<ASTNode> else_statement_;
	std::optional<ASTNode> init_statement_; // C++20 if (init; condition)
	bool is_constexpr_; // C++17 if constexpr
};

class ForStatementNode {
public:
	explicit ForStatementNode(std::optional<ASTNode> init_statement,
		std::optional<ASTNode> condition,
		std::optional<ASTNode> update_expression,
		ASTNode body_statement)
		: init_statement_(init_statement), condition_(condition),
		  update_expression_(update_expression), body_statement_(body_statement) {}

	auto get_init_statement() const { return init_statement_; }
	auto get_condition() const { return condition_; }
	auto get_update_expression() const { return update_expression_; }
	auto get_body_statement() const { return body_statement_; }
	bool has_init() const { return init_statement_.has_value(); }
	bool has_condition() const { return condition_.has_value(); }
	bool has_update() const { return update_expression_.has_value(); }

private:
	std::optional<ASTNode> init_statement_;    // for (init; condition; update)
	std::optional<ASTNode> condition_;
	std::optional<ASTNode> update_expression_;
	ASTNode body_statement_;
};

class WhileStatementNode {
public:
	explicit WhileStatementNode(ASTNode condition, ASTNode body_statement)
		: condition_(condition), body_statement_(body_statement) {}

	auto get_condition() const { return condition_; }
	auto get_body_statement() const { return body_statement_; }

private:
	ASTNode condition_;
	ASTNode body_statement_;
};

class DoWhileStatementNode {
public:
	explicit DoWhileStatementNode(ASTNode body_statement, ASTNode condition)
		: body_statement_(body_statement), condition_(condition) {}

	auto get_body_statement() const { return body_statement_; }
	auto get_condition() const { return condition_; }

private:
	ASTNode body_statement_;
	ASTNode condition_;
};

class RangedForStatementNode {
public:
	explicit RangedForStatementNode(ASTNode loop_variable_decl,
		ASTNode range_expression,
		ASTNode body_statement)
		: loop_variable_decl_(loop_variable_decl),
		  range_expression_(range_expression),
		  body_statement_(body_statement) {}

	auto get_loop_variable_decl() const { return loop_variable_decl_; }
	auto get_range_expression() const { return range_expression_; }
	auto get_body_statement() const { return body_statement_; }

private:
	ASTNode loop_variable_decl_;  // for (int x : range)
	ASTNode range_expression_;     // the array or container to iterate over
	ASTNode body_statement_;
};

class BreakStatementNode {
public:
	explicit BreakStatementNode(Token break_token = Token())
		: break_token_(break_token) {}

	const Token& break_token() const { return break_token_; }

private:
	Token break_token_;
};

class ContinueStatementNode {
public:
	explicit ContinueStatementNode(Token continue_token = Token())
		: continue_token_(continue_token) {}

	const Token& continue_token() const { return continue_token_; }

private:
	Token continue_token_;
};

// Case label node for switch statements
class CaseLabelNode {
public:
	explicit CaseLabelNode(ASTNode case_value, std::optional<ASTNode> statement = std::nullopt)
		: case_value_(case_value), statement_(statement) {}

	auto get_case_value() const { return case_value_; }
	auto get_statement() const { return statement_; }
	bool has_statement() const { return statement_.has_value(); }

private:
	ASTNode case_value_;  // Constant expression for case value
	std::optional<ASTNode> statement_;  // Optional statement (for fall-through cases)
};

// Default label node for switch statements
class DefaultLabelNode {
public:
	explicit DefaultLabelNode(std::optional<ASTNode> statement = std::nullopt)
		: statement_(statement) {}

	auto get_statement() const { return statement_; }
	bool has_statement() const { return statement_.has_value(); }

private:
	std::optional<ASTNode> statement_;  // Optional statement
};

// Switch statement node
class SwitchStatementNode {
public:
	explicit SwitchStatementNode(ASTNode condition, ASTNode body)
		: condition_(condition), body_(body) {}

	auto get_condition() const { return condition_; }
	auto get_body() const { return body_; }

private:
	ASTNode condition_;  // Expression to switch on
	ASTNode body_;       // Body (typically a BlockNode containing case/default labels)
};

// Label statement node (for goto targets)
class LabelStatementNode {
public:
	explicit LabelStatementNode(Token label_token)
		: label_token_(label_token) {}

	std::string_view label_name() const { return label_token_.value(); }
	const Token& label_token() const { return label_token_; }

private:
	Token label_token_;  // The label identifier
};

// Goto statement node
class GotoStatementNode {
public:
	explicit GotoStatementNode(Token label_token, Token goto_token = Token())
		: label_token_(label_token), goto_token_(goto_token) {}

	std::string_view label_name() const { return label_token_.value(); }
	const Token& label_token() const { return label_token_; }
	const Token& goto_token() const { return goto_token_; }

private:
	Token label_token_;  // The target label identifier
	Token goto_token_;   // The goto keyword token (for error reporting)
};

// Typedef declaration node: typedef existing_type new_name;
class TypedefDeclarationNode {
public:
	explicit TypedefDeclarationNode(ASTNode type_node, Token alias_name)
		: type_node_(type_node), alias_name_(alias_name) {}

	const ASTNode& type_node() const { return type_node_; }
	std::string_view alias_name() const { return alias_name_.value(); }
	const Token& alias_token() const { return alias_name_; }

private:
	ASTNode type_node_;  // The underlying type (TypeSpecifierNode)
	Token alias_name_;   // The new type alias name
};

// ============================================================================
// Exception Handling Support
// ============================================================================

// Throw statement node: throw expression; or throw;
class ThrowStatementNode {
public:
	// throw expression;
	explicit ThrowStatementNode(ASTNode expression, Token throw_token)
		: expression_(expression), throw_token_(throw_token), is_rethrow_(false) {}

	// throw; (rethrow)
	explicit ThrowStatementNode(Token throw_token)
		: expression_(), throw_token_(throw_token), is_rethrow_(true) {}

	const std::optional<ASTNode>& expression() const { return expression_; }
	bool is_rethrow() const { return is_rethrow_; }
	const Token& throw_token() const { return throw_token_; }

private:
	std::optional<ASTNode> expression_;  // The expression to throw (nullopt for rethrow)
	Token throw_token_;                   // For error reporting
	bool is_rethrow_;                     // True if this is a rethrow (throw;)
};

// Catch clause node: catch (type identifier) { block }
class CatchClauseNode {
public:
	// catch (type identifier) { block } or catch (type) { block }
	explicit CatchClauseNode(
		std::optional<ASTNode> exception_declaration,  // nullopt for catch(...)
		ASTNode body,
		Token catch_token = Token())
		: exception_declaration_(exception_declaration),
		  body_(body),
		  catch_token_(catch_token),
		  is_catch_all_(false) {}

	// catch(...) { block }
	explicit CatchClauseNode(
		ASTNode body,
		Token catch_token,
		bool catch_all)
		: exception_declaration_(std::nullopt),
		  body_(body),
		  catch_token_(catch_token),
		  is_catch_all_(catch_all) {}

	const std::optional<ASTNode>& exception_declaration() const { return exception_declaration_; }
	const ASTNode& body() const { return body_; }
	const Token& catch_token() const { return catch_token_; }
	bool is_catch_all() const { return is_catch_all_; }

private:
	std::optional<ASTNode> exception_declaration_;  // DeclarationNode for the caught exception, nullopt for catch(...)
	ASTNode body_;                                  // BlockNode for the catch block body
	Token catch_token_;                             // For error reporting
	bool is_catch_all_;                             // True for catch(...)
};

// Try statement node: try { block } catch (...) { block }
class TryStatementNode {
public:
	explicit TryStatementNode(
		ASTNode try_block,
		std::vector<ASTNode> catch_clauses,
		Token try_token = Token())
		: try_block_(try_block),
		  catch_clauses_(std::move(catch_clauses)),
		  try_token_(try_token) {}

	const ASTNode& try_block() const { return try_block_; }
	const std::vector<ASTNode>& catch_clauses() const { return catch_clauses_; }
	const Token& try_token() const { return try_token_; }

private:
	ASTNode try_block_;                   // BlockNode for the try block
	std::vector<ASTNode> catch_clauses_;  // Vector of CatchClauseNode
	Token try_token_;                     // For error reporting
};

// ============================================================================
// C++20 Concepts Support
// ============================================================================

// Compound requirement node: { expression } -> ConceptName
// Used inside requires expressions with return-type-requirements
class CompoundRequirementNode {
public:
	explicit CompoundRequirementNode(
		ASTNode expression,
		std::optional<ASTNode> return_type_constraint = std::nullopt,
		Token lbrace_token = Token())
		: expression_(expression),
		  return_type_constraint_(return_type_constraint),
		  lbrace_token_(lbrace_token) {}

	const ASTNode& expression() const { return expression_; }
	const std::optional<ASTNode>& return_type_constraint() const { return return_type_constraint_; }
	bool has_return_type_constraint() const { return return_type_constraint_.has_value(); }
	const Token& lbrace_token() const { return lbrace_token_; }

private:
	ASTNode expression_;                          // The expression inside { }
	std::optional<ASTNode> return_type_constraint_;  // Optional -> ConceptName or -> Type
	Token lbrace_token_;                          // For error reporting
};

// Requires expression node: requires { expression; }
// Used inside concept definitions and requires clauses
class RequiresExpressionNode {
public:
	explicit RequiresExpressionNode(
		std::vector<ASTNode> requirements,
		Token requires_token = Token())
		: requirements_(std::move(requirements)),
		  requires_token_(requires_token) {}

	const std::vector<ASTNode>& requirements() const { return requirements_; }
	const Token& requires_token() const { return requires_token_; }

private:
	std::vector<ASTNode> requirements_;  // List of requirement expressions
	Token requires_token_;               // For error reporting
};

// Requires clause node: requires constraint
// Used in template declarations to constrain template parameters
class RequiresClauseNode {
public:
	explicit RequiresClauseNode(
		ASTNode constraint_expr,
		Token requires_token = Token())
		: constraint_expr_(constraint_expr),
		  requires_token_(requires_token) {}

	const ASTNode& constraint_expr() const { return constraint_expr_; }
	const Token& requires_token() const { return requires_token_; }

private:
	ASTNode constraint_expr_;  // The constraint expression (can be a concept name or requires expression)
	Token requires_token_;     // For error reporting
};

// Concept declaration node: concept Name = constraint;
// Defines a named concept that can be used to constrain templates
class ConceptDeclarationNode {
public:
	explicit ConceptDeclarationNode(
		Token name,
		std::vector<TemplateParameterNode> template_params,
		ASTNode constraint_expr,
		Token concept_token = Token())
		: name_(name),
		  template_params_(std::move(template_params)),
		  constraint_expr_(constraint_expr),
		  concept_token_(concept_token) {}

	std::string_view name() const { return name_.value(); }
	const Token& name_token() const { return name_; }
	const std::vector<TemplateParameterNode>& template_params() const { return template_params_; }
	const ASTNode& constraint_expr() const { return constraint_expr_; }
	const Token& concept_token() const { return concept_token_; }

private:
	Token name_;                                     // Concept name
	std::vector<TemplateParameterNode> template_params_;  // Template parameters for the concept
	ASTNode constraint_expr_;                        // The constraint expression
	Token concept_token_;                            // For error reporting
};

// Helper to get DeclarationNode from a symbol that could be either DeclarationNode or VariableDeclarationNode
// Returns nullptr if the symbol is neither type
inline const DeclarationNode* get_decl_from_symbol(const ASTNode& symbol) {
	if (symbol.is<DeclarationNode>()) {
		return &symbol.as<DeclarationNode>();
	} else if (symbol.is<VariableDeclarationNode>()) {
		return &symbol.as<VariableDeclarationNode>().declaration();
	}
	return nullptr;
}
