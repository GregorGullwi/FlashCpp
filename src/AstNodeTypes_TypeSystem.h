#pragma once
#include "AstNodeTypes_Core.h"


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
inline CVQualifier operator|(CVQualifier a, CVQualifier b) {
	return static_cast<CVQualifier>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline CVQualifier& operator|=(CVQualifier& a, CVQualifier b) {
	return a = a | b;
}
inline bool hasCVQualifier(CVQualifier cv, CVQualifier flag) {
	return (static_cast<uint8_t>(cv) & static_cast<uint8_t>(flag)) != 0;
}

// Reference qualifiers - mutually exclusive enum (not a bitmask)
enum class ReferenceQualifier : uint8_t {
	None = 0,
	LValueReference = 1 << 0,  // &
	RValueReference = 1 << 1,  // &&
};

// Target data model - controls the size of 'long' and 'wchar_t' types
// Windows uses LLP64: long is 32-bit, wchar_t is 16-bit unsigned
// Linux/Unix uses LP64: long is 64-bit, wchar_t is 32-bit signed
enum class TargetDataModel {
	LLP64,     // Windows x64: long = 32 bits, wchar_t = 16 bits unsigned (COFF)
	LP64       // Linux/Unix x64: long = 64 bits, wchar_t = 32 bits signed (ELF)
};

// Global data model setting - set by main.cpp based on target platform
// Default is platform-dependent
extern TargetDataModel g_target_data_model;

enum class Type : int_fast16_t {
	Invalid = 0,          // Must be 0 so zero-initialized memory is detected as uninitialized
	Void,
	Bool,
	Char,
	UnsignedChar,
	WChar,             // wchar_t - distinct built-in type (mangled as 'w')
	Char8,             // char8_t (C++20) - distinct built-in type (mangled as 'Du')
	Char16,            // char16_t (C++11) - distinct built-in type (mangled as 'Ds')
	Char32,            // char32_t (C++11) - distinct built-in type (mangled as 'Di')
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
		case Type::WChar: return "wchar_t";
		case Type::Char8: return "char8_t";
		case Type::Char16: return "char16_t";
		case Type::Char32: return "char32_t";
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
		// wchar_t is target-dependent: signed on Linux (LP64), unsigned on Windows (LLP64)
		case Type::WChar:
			return g_target_data_model != TargetDataModel::LLP64;  // signed on LP64, unsigned on LLP64
		// Explicitly unsigned types
		case Type::Bool:
		case Type::UnsignedChar:
		case Type::Char8:     // char8_t is always unsigned (C++20)
		case Type::Char16:    // char16_t is always unsigned
		case Type::Char32:    // char32_t is always unsigned
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
	MemberFunction, // friend void Class::func();
	TemplateClass  // template<typename T1, typename T2> friend struct pair;
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

// Deferred static_assert information - stored during template definition, evaluated during instantiation
struct DeferredStaticAssert {
	ASTNode condition_expr;  // The condition expression to evaluate
	StringHandle message;    // The assertion message (interned in StringTable for concatenated literals)
	
	DeferredStaticAssert(ASTNode expr, StringHandle msg)
		: condition_expr(expr), message(msg) {}
};

// Struct member information
struct StructMember {
	StringHandle name;
	Type type;
	TypeIndex type_index;   // Index into gTypeInfo for complex types (structs, etc.)
	size_t offset;          // Offset in bytes from start of struct
	size_t size;            // Size in bytes
	std::optional<size_t> bitfield_width; // Width in bits for bitfield members
	size_t bitfield_bit_offset = 0; // Bit offset within the storage unit for bitfield members
	size_t referenced_size_bits; // Size of the referenced value in bits (for references)
	size_t alignment;       // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;  // None, LValueReference (&), or RValueReference (&&)
	std::optional<ASTNode> default_initializer;  // C++11 default member initializer
	bool is_array;          // True if member is an array
	std::vector<size_t> array_dimensions;  // Dimensions for multidimensional arrays
	int pointer_depth;      // Pointer indirection level (e.g., int* = 1, int** = 2)

	// Convenience helpers for common checks
	bool is_reference() const { return reference_qualifier != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier == ReferenceQualifier::RValueReference; }

	StructMember(StringHandle n, Type t, TypeIndex tidx, size_t off, size_t sz, size_t align,
	            AccessSpecifier acc,
	            std::optional<ASTNode> init,
	            ReferenceQualifier ref_qual,
	            size_t ref_size_bits,
	            bool is_arr,
	            std::vector<size_t> arr_dims,
	            int ptr_depth,
	            std::optional<size_t> bf_width)
		: name(n), type(t), type_index(tidx), offset(off), size(sz),
		  bitfield_width(bf_width), referenced_size_bits(ref_size_bits ? ref_size_bits : sz * 8), alignment(align),
		  access(acc), reference_qualifier(ref_qual),
		  default_initializer(std::move(init)), is_array(is_arr), array_dimensions(std::move(arr_dims)),
		  pointer_depth(ptr_depth) {}
	
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
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;  // None, LValueReference (&), or RValueReference (&&)
	int pointer_depth = 0;  // Pointer indirection level (e.g., int* = 1, int** = 2)

	// Convenience helpers for common checks
	bool is_reference() const { return reference_qualifier != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier == ReferenceQualifier::RValueReference; }

	StructStaticMember(StringHandle n, Type t, TypeIndex tidx, size_t sz, size_t align, AccessSpecifier acc = AccessSpecifier::Public,
	                   std::optional<ASTNode> init = std::nullopt, bool is_const_val = false,
	                   ReferenceQualifier ref_qual = ReferenceQualifier::None, int ptr_depth = 0)
		: name(n), type(t), type_index(tidx), size(sz), alignment(align), access(acc),
		  initializer(init), is_const(is_const_val), reference_qualifier(ref_qual), pointer_depth(ptr_depth) {}
	
	StringHandle getName() const {
		return name;
	}
};

