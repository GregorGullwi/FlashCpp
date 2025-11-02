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
	UserDefined,
	Auto,
	Function,
	Struct,
	Enum,
	FunctionPointer,
	MemberFunctionPointer,
};

using TypeIndex = size_t;

// Linkage specification for functions (C vs C++)
enum class Linkage : uint8_t {
	None,           // Default C++ linkage (with name mangling)
	C,              // C linkage (no name mangling)
	CPlusPlus,      // Explicit C++ linkage
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

	BaseClassSpecifier(std::string_view n, TypeIndex tidx, AccessSpecifier acc, bool virt = false, size_t off = 0)
		: name(n), type_index(tidx), access(acc), is_virtual(virt), offset(off) {}
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
	std::string name;
	Type type;
	TypeIndex type_index;   // Index into gTypeInfo for complex types (structs, etc.)
	size_t offset;          // Offset in bytes from start of struct
	size_t size;            // Size in bytes
	size_t alignment;       // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)
	std::optional<ASTNode> default_initializer;  // C++11 default member initializer

	StructMember(std::string n, Type t, TypeIndex tidx, size_t off, size_t sz, size_t align, AccessSpecifier acc = AccessSpecifier::Public, std::optional<ASTNode> init = std::nullopt)
		: name(std::move(n)), type(t), type_index(tidx), offset(off), size(sz), alignment(align), access(acc), default_initializer(std::move(init)) {}
};

// Forward declaration for member function support
class FunctionDeclarationNode;

// Struct member function information
struct StructMemberFunction {
	std::string name;
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

	StructMemberFunction(std::string n, ASTNode func_decl, AccessSpecifier acc = AccessSpecifier::Public,
	                     bool is_ctor = false, bool is_dtor = false, bool is_op_overload = false, std::string_view op_symbol = "")
		: name(std::move(n)), function_decl(func_decl), access(acc), is_constructor(is_ctor), is_destructor(is_dtor),
		  is_operator_overload(is_op_overload), operator_symbol(op_symbol) {}
};

// Runtime Type Information (RTTI) structure
struct RTTITypeInfo {
	const char* type_name;           // Mangled type name
	const char* demangled_name;      // Human-readable type name
	size_t num_bases;                // Number of base classes
	const RTTITypeInfo** base_types; // Array of pointers to base class type_info

	RTTITypeInfo(const char* mangled, const char* demangled, size_t num_base = 0)
		: type_name(mangled), demangled_name(demangled), num_bases(num_base), base_types(nullptr) {}

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

// Struct type information
struct StructTypeInfo {
	std::string name;
	std::vector<StructMember> members;
	std::vector<StructMemberFunction> member_functions;
	std::vector<BaseClassSpecifier> base_classes;  // Base classes for inheritance
	size_t total_size = 0;      // Total size of struct in bytes
	size_t alignment = 1;       // Alignment requirement of struct
	size_t custom_alignment = 0; // Custom alignment from alignas(n), 0 = use natural alignment
	size_t pack_alignment = 0;   // Pack alignment from #pragma pack(n), 0 = no packing
	AccessSpecifier default_access; // Default access for struct (public) vs class (private)

	// Virtual function support (Phase 2)
	bool has_vtable = false;    // True if this struct has virtual functions
	bool is_abstract = false;   // True if this struct has pure virtual functions
	std::vector<const StructMemberFunction*> vtable;  // Virtual function table (pointers to member functions)

	// Virtual base class support (Phase 3)
	std::vector<const BaseClassSpecifier*> virtual_bases;  // Virtual base classes (shared across inheritance paths)

	// RTTI support (Phase 5)
	RTTITypeInfo* rtti_info = nullptr;  // Runtime type information (for polymorphic classes)

	// Friend declarations support (Phase 2)
	std::vector<std::string> friend_functions_;      // Friend function names
	std::vector<std::string> friend_classes_;        // Friend class names
	std::vector<std::pair<std::string, std::string>> friend_member_functions_;  // (class, function)

	// Nested class support (Phase 2)
	std::vector<StructTypeInfo*> nested_classes_;    // Nested classes
	StructTypeInfo* enclosing_class_ = nullptr;      // Enclosing class (if this is nested)

	StructTypeInfo(std::string n, AccessSpecifier default_acc = AccessSpecifier::Public)
		: name(std::move(n)), default_access(default_acc) {}

	void addMember(const std::string& member_name, Type member_type, TypeIndex type_index,
	               size_t member_size, size_t member_alignment, AccessSpecifier access = AccessSpecifier::Public,
	               std::optional<ASTNode> default_initializer = std::nullopt) {
		// Apply pack alignment if specified
		// Pack alignment limits the maximum alignment of members
		size_t effective_alignment = member_alignment;
		if (pack_alignment > 0 && pack_alignment < member_alignment) {
			effective_alignment = pack_alignment;
		}

		// Calculate offset with effective alignment
		size_t offset = (total_size + effective_alignment - 1) & ~(effective_alignment - 1);

		members.emplace_back(member_name, member_type, type_index, offset, member_size, effective_alignment, access, std::move(default_initializer));

		// Update struct size and alignment
		total_size = offset + member_size;
		alignment = std::max(alignment, effective_alignment);
	}

	void addMemberFunction(const std::string& function_name, ASTNode function_decl, AccessSpecifier access = AccessSpecifier::Public,
	                       bool is_virtual = false, bool is_pure_virtual = false, bool is_override = false, bool is_final = false) {
		auto& func = member_functions.emplace_back(function_name, function_decl, access, false, false);
		func.is_virtual = is_virtual;
		func.is_pure_virtual = is_pure_virtual;
		func.is_override = is_override;
		func.is_final = is_final;
	}

	void addConstructor(ASTNode constructor_decl, AccessSpecifier access = AccessSpecifier::Public) {
		member_functions.emplace_back(name, constructor_decl, access, true, false);
	}

	void addDestructor(ASTNode destructor_decl, AccessSpecifier access = AccessSpecifier::Public, bool is_virtual = false) {
		auto& dtor = member_functions.emplace_back("~" + name, destructor_decl, access, false, true, false, "");
		dtor.is_virtual = is_virtual;
	}

	void addOperatorOverload(std::string_view operator_symbol, ASTNode function_decl, AccessSpecifier access = AccessSpecifier::Public,
	                         bool is_virtual = false, bool is_pure_virtual = false, bool is_override = false, bool is_final = false) {
		std::string op_name = "operator";
		op_name += operator_symbol;
		auto& func = member_functions.emplace_back(op_name, function_decl, access, false, false, true, operator_symbol);
		func.is_virtual = is_virtual;
		func.is_pure_virtual = is_pure_virtual;
		func.is_override = is_override;
		func.is_final = is_final;
	}

	void finalize() {
		// Build vtable first (if struct has virtual functions)
		buildVTable();

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
	void addBaseClass(std::string_view base_name, TypeIndex base_type_index, AccessSpecifier access, bool is_virtual = false) {
		base_classes.emplace_back(base_name, base_type_index, access, is_virtual, 0);
	}

	// Find member recursively through base classes
	const StructMember* findMemberRecursive(const std::string& member_name) const;

	void set_custom_alignment(size_t align) {
		custom_alignment = align;
	}

	void set_pack_alignment(size_t align) {
		pack_alignment = align;
	}

	const StructMember* findMember(const std::string& name) const {
		for (const auto& member : members) {
			if (member.name == name) {
				return &member;
			}
		}
		return nullptr;
	}

	const StructMemberFunction* findMemberFunction(const std::string& name) const {
		for (const auto& func : member_functions) {
			if (func.name == name) {
				return &func;
			}
		}
		return nullptr;
	}

	// Friend declaration support methods
	void addFriendFunction(const std::string& func_name) {
		friend_functions_.push_back(func_name);
	}

	void addFriendClass(const std::string& class_name) {
		friend_classes_.push_back(class_name);
	}

	void addFriendMemberFunction(const std::string& class_name, const std::string& func_name) {
		friend_member_functions_.emplace_back(class_name, func_name);
	}

	bool isFriendFunction(const std::string& func_name) const {
		return std::find(friend_functions_.begin(), friend_functions_.end(), func_name) != friend_functions_.end();
	}

	bool isFriendClass(const std::string& class_name) const {
		return std::find(friend_classes_.begin(), friend_classes_.end(), class_name) != friend_classes_.end();
	}

	bool isFriendMemberFunction(const std::string& class_name, const std::string& func_name) const {
		auto it = std::find(friend_member_functions_.begin(), friend_member_functions_.end(),
		                    std::make_pair(class_name, func_name));
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
	std::string getQualifiedName() const {
		if (enclosing_class_) {
			return enclosing_class_->getQualifiedName() + "::" + name;
		}
		return name;
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
		return findDefaultConstructor() != nullptr;
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
};

// Enumerator information
struct Enumerator {
	std::string name;
	long long value;  // Enumerator value (always an integer)

	Enumerator(std::string n, long long v)
		: name(std::move(n)), value(v) {}
};

// Enum type information
struct EnumTypeInfo {
	std::string name;
	bool is_scoped;                  // true for enum class, false for enum
	Type underlying_type;            // Underlying type (default: int)
	unsigned char underlying_size;   // Size in bits of underlying type
	std::vector<Enumerator> enumerators;

	EnumTypeInfo(std::string n, bool scoped = false, Type underlying = Type::Int, unsigned char size = 32)
		: name(std::move(n)), is_scoped(scoped), underlying_type(underlying), underlying_size(size) {}

	void addEnumerator(const std::string& enumerator_name, long long value) {
		enumerators.emplace_back(enumerator_name, value);
	}

	const Enumerator* findEnumerator(const std::string& name) const {
		for (const auto& enumerator : enumerators) {
			if (enumerator.name == name) {
				return &enumerator;
			}
		}
		return nullptr;
	}

	long long getEnumeratorValue(const std::string& name) const {
		const Enumerator* e = findEnumerator(name);
		return e ? e->value : 0;
	}
};

struct TypeInfo
{
	TypeInfo() : type_(Type::Void), type_index_(0) {}
	TypeInfo(std::string name, Type type, TypeIndex idx) : name_(std::move(name)), type_(type), type_index_(idx) {}

	std::string name_;
	Type type_;
	TypeIndex type_index_;

	// For struct types, store additional information
	std::unique_ptr<StructTypeInfo> struct_info_;

	// For enum types, store additional information
	std::unique_ptr<EnumTypeInfo> enum_info_;

	// For typedef, store the size in bits (for primitive types)
	unsigned char type_size_ = 0;

	const char* name() { return name_.c_str(); };

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
	using is_transparent = void;
	size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
	size_t operator()(const std::string& s) const { return std::hash<std::string>{}(s); }
};

struct StringEqual {
	using is_transparent = void;
	bool operator()(std::string_view lhs, std::string_view rhs) const { return lhs == rhs; }
};

extern std::unordered_map<std::string, const TypeInfo*, StringHash, StringEqual> gTypesByName;

extern std::unordered_map<Type, const TypeInfo*> gNativeTypes;

TypeInfo& add_user_type(std::string name);

TypeInfo& add_function_type(std::string name, Type /*return_type*/);

TypeInfo& add_struct_type(std::string name);

TypeInfo& add_enum_type(std::string name);

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
int get_integer_rank(Type type);
int get_floating_point_rank(Type type);
int get_type_size_bits(Type type);
Type promote_integer_type(Type type);
Type promote_floating_point_type(Type type);
Type get_common_type(Type left, Type right);
bool requires_conversion(Type from, Type to);

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
	TypeSpecifierNode(Type type, TypeQualifier qualifier, unsigned char sizeInBits,
		const Token& token = {}, CVQualifier cv_qualifier = CVQualifier::None)
		: type_(type), size_(sizeInBits), qualifier_(qualifier), cv_qualifier_(cv_qualifier), token_(token), type_index_(0) {}

	// Constructor for struct types
	TypeSpecifierNode(Type type, TypeIndex type_index, unsigned char sizeInBits,
		const Token& token = {}, CVQualifier cv_qualifier = CVQualifier::None)
		: type_(type), size_(sizeInBits), qualifier_(TypeQualifier::None), cv_qualifier_(cv_qualifier), token_(token), type_index_(type_index) {}

	auto type() const { return type_; }
	auto size_in_bits() const { return size_; }
	auto qualifier() const { return qualifier_; }
	auto cv_qualifier() const { return cv_qualifier_; }
	auto type_index() const { return type_index_; }
	bool is_const() const { return (static_cast<uint8_t>(cv_qualifier_) & static_cast<uint8_t>(CVQualifier::Const)) != 0; }
	bool is_volatile() const { return (static_cast<uint8_t>(cv_qualifier_) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0; }

	// Pointer support
	bool is_pointer() const { return !pointer_levels_.empty(); }
	size_t pointer_depth() const { return pointer_levels_.empty() ? 0 : pointer_levels_.size(); }
	const std::vector<PointerLevel>& pointer_levels() const { return pointer_levels_; }
	void add_pointer_level(CVQualifier cv = CVQualifier::None) { pointer_levels_.push_back(PointerLevel(cv)); }
	void remove_pointer_level() { if (!pointer_levels_.empty()) pointer_levels_.pop_back(); }

	// Reference support
	bool is_reference() const { return is_reference_; }
	bool is_rvalue_reference() const { return is_rvalue_reference_; }
	void set_reference(bool is_rvalue = false) {
		is_reference_ = true;
		is_rvalue_reference_ = is_rvalue;
	}

	// Function pointer support
	bool is_function_pointer() const { return type_ == Type::FunctionPointer; }
	bool is_member_function_pointer() const { return type_ == Type::MemberFunctionPointer; }
	void set_function_signature(const FunctionSignature& sig) { function_signature_ = sig; }
	const FunctionSignature& function_signature() const { return *function_signature_; }
	bool has_function_signature() const { return function_signature_.has_value(); }

	// Get readable string representation
	std::string getReadableString() const;

private:
	Type type_;
	unsigned char size_;
	TypeQualifier qualifier_;
	CVQualifier cv_qualifier_;  // CV-qualifier for the base type
	Token token_;
	TypeIndex type_index_;      // Index into gTypeInfo for user-defined types (structs, etc.)
	std::vector<PointerLevel> pointer_levels_;  // Empty if not a pointer, one entry per * level
	bool is_reference_ = false;  // True if this is a reference type (&)
	bool is_rvalue_reference_ = false;  // True if this is an rvalue reference (&&)
	std::optional<FunctionSignature> function_signature_;  // For function pointers
};

class DeclarationNode {
public:
	DeclarationNode() = default;
	DeclarationNode(ASTNode type_node, Token identifier)
		: type_node_(type_node), identifier_(std::move(identifier)), array_size_(std::nullopt), custom_alignment_(0) {}
	DeclarationNode(ASTNode type_node, Token identifier, std::optional<ASTNode> array_size)
		: type_node_(type_node), identifier_(std::move(identifier)), array_size_(array_size), custom_alignment_(0) {}

	ASTNode type_node() const { return type_node_; }
	const Token& identifier_token() const { return identifier_; }
	uint32_t line_number() const { return identifier_.line(); }
	bool is_array() const { return array_size_.has_value(); }
	std::optional<ASTNode> array_size() const { return array_size_; }

	// Alignment support
	size_t custom_alignment() const { return custom_alignment_; }
	void set_custom_alignment(size_t alignment) { custom_alignment_ = alignment; }

private:
	ASTNode type_node_;
	Token identifier_;
	std::optional<ASTNode> array_size_;  // For array declarations like int arr[10]
	size_t custom_alignment_;            // Custom alignment from alignas(n), 0 = use natural alignment
};

class IdentifierNode {
public:
	explicit IdentifierNode(Token identifier) : identifier_(identifier) {}

	std::optional<Token> try_get_parent_token() { return parent_token_; }
	std::string_view name() const { return identifier_.value(); }

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
	explicit UnaryOperatorNode(Token identifier, ASTNode operand_node, bool is_prefix = true)
		: identifier_(identifier), operand_node_(operand_node), is_prefix_(is_prefix) {}

	std::string_view op() const { return identifier_.value(); }
	auto get_operand() const { return operand_node_; }
	bool is_prefix() const { return is_prefix_; }

private:
	Token identifier_;
	ASTNode operand_node_;
	bool is_prefix_;
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
		: decl_node_(decl_node), parent_struct_name_(""), is_member_function_(false), is_implicit_(false), linkage_(Linkage::None) {}
	FunctionDeclarationNode(DeclarationNode& decl_node, std::string_view parent_struct_name)
		: decl_node_(decl_node), parent_struct_name_(parent_struct_name), is_member_function_(true), is_implicit_(false), linkage_(Linkage::None) {}
	FunctionDeclarationNode(DeclarationNode& decl_node, Linkage linkage)
		: decl_node_(decl_node), parent_struct_name_(""), is_member_function_(false), is_implicit_(false), linkage_(linkage) {}

	const DeclarationNode& decl_node() const {
		return decl_node_;
	}
	const std::vector<ASTNode>& parameter_nodes() const {
		return parameter_nodes_;
	}
	void add_parameter_node(ASTNode parameter_node) {
		parameter_nodes_.push_back(parameter_node);
	}
	auto get_definition() const {
		return definition_block_;
	}
	bool set_definition(BlockNode& block_node) {
		if (definition_block_.has_value())
			return false;

		definition_block_.emplace(&block_node);
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

private:
	DeclarationNode& decl_node_;
	std::vector<ASTNode> parameter_nodes_;
	std::optional<BlockNode*> definition_block_;
	std::string_view parent_struct_name_;  // Points directly into source text from lexer token
	bool is_member_function_;
	bool is_implicit_;  // True if this is an implicitly generated function (e.g., operator=)
	Linkage linkage_;  // Linkage specification (C, C++, or None)
};

class FunctionCallNode {
public:
	explicit FunctionCallNode(DeclarationNode& func_decl, ChunkedVector<ASTNode>&& arguments, Token called_from_token)
		: func_decl_(func_decl), arguments_(std::move(arguments)), called_from_(called_from_token) {}

	const auto& arguments() const { return arguments_; }
	const auto& function_declaration() const { return func_decl_; }

	void add_argument(ASTNode argument) { arguments_.push_back(argument); }

	Token called_from() const { return called_from_; }

private:
	DeclarationNode& func_decl_;
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
	TemplateParameterNode(std::string_view name, Token token)
		: kind_(TemplateParameterKind::Type), name_(name), token_(token) {}

	// Non-type parameter: template<int N>
	TemplateParameterNode(std::string_view name, ASTNode type_node, Token token)
		: kind_(TemplateParameterKind::NonType), name_(name), type_node_(type_node), token_(token) {}

	TemplateParameterKind kind() const { return kind_; }
	std::string_view name() const { return name_; }
	Token token() const { return token_; }

	// For non-type parameters
	bool has_type() const { return type_node_.has_value(); }
	ASTNode type_node() const { return type_node_.value(); }

	// For default arguments (future enhancement)
	bool has_default() const { return default_value_.has_value(); }
	ASTNode default_value() const { return default_value_.value(); }
	void set_default_value(ASTNode value) { default_value_ = value; }

private:
	TemplateParameterKind kind_;
	std::string_view name_;  // Points directly into source text from lexer token
	std::optional<ASTNode> type_node_;  // For non-type parameters (e.g., int N)
	std::optional<ASTNode> default_value_;  // Default argument (e.g., typename T = int)
	Token token_;  // For error reporting
};

// Template function declaration node - represents a function template
class TemplateFunctionDeclarationNode {
public:
	TemplateFunctionDeclarationNode() = delete;
	TemplateFunctionDeclarationNode(std::vector<ASTNode> template_params, ASTNode function_decl)
		: template_parameters_(std::move(template_params)), function_declaration_(function_decl) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	ASTNode function_declaration() const { return function_declaration_; }

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
};

// Forward declaration for TemplateClassDeclarationNode (defined after StructDeclarationNode)
class TemplateClassDeclarationNode;

// Member initializer for constructor initializer lists
struct MemberInitializer {
	std::string_view member_name;
	ASTNode initializer_expr;

	MemberInitializer(std::string_view name, ASTNode expr)
		: member_name(name), initializer_expr(expr) {}
};

// Base class initializer for constructor initializer lists
struct BaseInitializer {
	std::string base_class_name;
	std::vector<ASTNode> arguments;

	BaseInitializer(std::string name, std::vector<ASTNode> args)
		: base_class_name(std::move(name)), arguments(std::move(args)) {}
};

// Constructor declaration node
class ConstructorDeclarationNode {
public:
	ConstructorDeclarationNode() = delete;
	ConstructorDeclarationNode(std::string_view struct_name, std::string_view name)
		: struct_name_(struct_name), name_(name), is_implicit_(false) {}

	std::string_view struct_name() const { return struct_name_; }
	std::string_view name() const { return name_; }
	Token name_token() const { return Token(Token::Type::Identifier, name_, 0, 0, 0); }  // Create token on demand
	const std::vector<ASTNode>& parameter_nodes() const { return parameter_nodes_; }
	const std::vector<MemberInitializer>& member_initializers() const { return member_initializers_; }
	const std::vector<BaseInitializer>& base_initializers() const { return base_initializers_; }
	bool is_implicit() const { return is_implicit_; }

	void add_parameter_node(ASTNode parameter_node) {
		parameter_nodes_.push_back(parameter_node);
	}

	void add_member_initializer(std::string_view member_name, ASTNode initializer_expr) {
		member_initializers_.emplace_back(member_name, initializer_expr);
	}

	void add_base_initializer(std::string base_name, std::vector<ASTNode> args) {
		base_initializers_.emplace_back(std::move(base_name), std::move(args));
	}

	void set_is_implicit(bool implicit) {
		is_implicit_ = implicit;
	}

	auto get_definition() const { return definition_block_; }

	bool set_definition(BlockNode& block_node) {
		if (definition_block_.has_value())
			return false;
		definition_block_.emplace(&block_node);
		return true;
	}

private:
	std::string_view struct_name_;  // Points directly into source text from lexer token
	std::string_view name_;         // Points directly into source text from lexer token
	std::vector<ASTNode> parameter_nodes_;
	std::vector<MemberInitializer> member_initializers_;
	std::vector<BaseInitializer> base_initializers_;  // Base class initializers
	std::optional<BlockNode*> definition_block_;
	bool is_implicit_;  // True if this is an implicitly generated default constructor
};

// Destructor declaration node
class DestructorDeclarationNode {
public:
	DestructorDeclarationNode() = delete;
	DestructorDeclarationNode(std::string_view struct_name, std::string_view name)
		: struct_name_(struct_name), name_(name) {}

	std::string_view struct_name() const { return struct_name_; }
	std::string_view name() const { return name_; }
	Token name_token() const { return Token(Token::Type::Identifier, name_, 0, 0, 0); }  // Create token on demand

	auto get_definition() const { return definition_block_; }

	bool set_definition(BlockNode& block_node) {
		if (definition_block_.has_value())
			return false;
		definition_block_.emplace(&block_node);
		return true;
	}

private:
	std::string_view struct_name_;  // Points directly into source text from lexer token
	std::string_view name_;         // Points directly into source text from lexer token
	std::optional<BlockNode*> definition_block_;
};

// Struct member with access specifier
struct StructMemberDecl {
	ASTNode declaration;
	AccessSpecifier access;
	std::optional<ASTNode> default_initializer;  // C++11 default member initializer

	StructMemberDecl(ASTNode decl, AccessSpecifier acc, std::optional<ASTNode> init = std::nullopt)
		: declaration(decl), access(acc), default_initializer(std::move(init)) {}
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

	StructMemberFunctionDecl(ASTNode func_decl, AccessSpecifier acc, bool is_ctor = false, bool is_dtor = false,
	                         bool is_op_overload = false, std::string_view op_symbol = "")
		: function_declaration(func_decl), access(acc), is_constructor(is_ctor), is_destructor(is_dtor),
		  is_operator_overload(is_op_overload), operator_symbol(op_symbol) {}
};

// Friend declaration node
class FriendDeclarationNode {
public:
	// Friend class declaration: friend class ClassName;
	explicit FriendDeclarationNode(FriendKind kind, std::string_view name)
		: kind_(kind), name_(name), class_name_("") {}

	// Friend member function declaration: friend void ClassName::functionName();
	FriendDeclarationNode(FriendKind kind, std::string_view name, std::string_view class_name)
		: kind_(kind), name_(name), class_name_(class_name) {}

	FriendKind kind() const { return kind_; }
	std::string_view name() const { return name_; }
	std::string_view class_name() const { return class_name_; }

	// For friend functions, store the function declaration
	void set_function_declaration(ASTNode decl) { function_decl_ = decl; }
	std::optional<ASTNode> function_declaration() const { return function_decl_; }

private:
	FriendKind kind_;
	std::string_view name_;           // Function or class name
	std::string_view class_name_;     // For member functions: the class name
	std::optional<ASTNode> function_decl_;  // For friend functions
};

class StructDeclarationNode {
public:
	explicit StructDeclarationNode(std::string_view name, bool is_class = false)
		: name_(name), is_class_(is_class) {}

	std::string_view name() const { return name_; }
	const std::vector<StructMemberDecl>& members() const { return members_; }
	const std::vector<StructMemberFunctionDecl>& member_functions() const { return member_functions_; }
	const std::vector<BaseClassSpecifier>& base_classes() const { return base_classes_; }
	bool is_class() const { return is_class_; }
	AccessSpecifier default_access() const {
		return is_class_ ? AccessSpecifier::Private : AccessSpecifier::Public;
	}

	void add_member(ASTNode member, AccessSpecifier access, std::optional<ASTNode> default_initializer = std::nullopt) {
		members_.emplace_back(member, access, std::move(default_initializer));
	}

	void add_base_class(std::string_view base_name, TypeIndex base_type_index, AccessSpecifier access, bool is_virtual = false) {
		base_classes_.emplace_back(base_name, base_type_index, access, is_virtual, 0);
	}

	void add_member_function(ASTNode function_decl, AccessSpecifier access,
	                         bool is_virtual = false, bool is_pure_virtual = false,
	                         bool is_override = false, bool is_final = false) {
		auto& func_decl = member_functions_.emplace_back(function_decl, access, false, false);
		func_decl.is_virtual = is_virtual;
		func_decl.is_pure_virtual = is_pure_virtual;
		func_decl.is_override = is_override;
		func_decl.is_final = is_final;
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
	                           bool is_override = false, bool is_final = false) {
		auto& func_decl = member_functions_.emplace_back(function_decl, access, false, false, true, operator_symbol);
		func_decl.is_virtual = is_virtual;
		func_decl.is_pure_virtual = is_pure_virtual;
		func_decl.is_override = is_override;
		func_decl.is_final = is_final;
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
	std::string qualified_name() const {
		if (enclosing_class_) {
			return enclosing_class_->qualified_name() + "::" + std::string(name_);
		}
		return std::string(name_);
	}

private:
	std::string_view name_;  // Points directly into source text from lexer token
	std::vector<StructMemberDecl> members_;
	std::vector<StructMemberFunctionDecl> member_functions_;
	std::vector<BaseClassSpecifier> base_classes_;  // Base classes for inheritance
	std::vector<ASTNode> friend_declarations_;  // Friend declarations
	std::vector<ASTNode> nested_classes_;  // Nested classes
	StructDeclarationNode* enclosing_class_ = nullptr;  // Enclosing class (if nested)
	bool is_class_;  // true for class, false for struct
};

// Template class declaration node - represents a class template
class TemplateClassDeclarationNode {
public:
	TemplateClassDeclarationNode() = delete;
	TemplateClassDeclarationNode(std::vector<ASTNode> template_params, ASTNode class_decl)
		: template_parameters_(std::move(template_params)), class_declaration_(class_decl) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	ASTNode class_declaration() const { return class_declaration_; }

	// Get the underlying StructDeclarationNode
	StructDeclarationNode& class_decl_node() {
		return class_declaration_.as<StructDeclarationNode>();
	}
	const StructDeclarationNode& class_decl_node() const {
		return class_declaration_.as<StructDeclarationNode>();
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	ASTNode class_declaration_;  // StructDeclarationNode
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
	explicit EnumDeclarationNode(std::string_view name, bool is_scoped = false)
		: name_(name), is_scoped_(is_scoped), underlying_type_() {}

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
	explicit MemberAccessNode(ASTNode object, Token member_name)
		: object_(object), member_name_(member_name) {}

	ASTNode object() const { return object_; }
	std::string_view member_name() const { return member_name_.value(); }

private:
	ASTNode object_;
	Token member_name_;
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
		AllByReference // [&]
	};

	explicit LambdaCaptureNode(CaptureKind kind, Token identifier = Token())
		: kind_(kind), identifier_(identifier) {}

	CaptureKind kind() const { return kind_; }
	std::string_view identifier_name() const { return identifier_.value(); }
	const Token& identifier_token() const { return identifier_; }
	bool is_capture_all() const {
		return kind_ == CaptureKind::AllByValue || kind_ == CaptureKind::AllByReference;
	}

private:
	CaptureKind kind_;
	Token identifier_;  // Empty for capture-all
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
	std::string generate_lambda_name() const {
		return "__lambda_" + std::to_string(lambda_id_);
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

using ExpressionNode = std::variant<IdentifierNode, QualifiedIdentifierNode, StringLiteralNode, NumericLiteralNode,
	BinaryOperatorNode, UnaryOperatorNode, FunctionCallNode, MemberAccessNode, MemberFunctionCallNode,
	ArraySubscriptNode, SizeofExprNode, OffsetofExprNode, NewExpressionNode, DeleteExpressionNode, StaticCastNode,
	DynamicCastNode, TypeidNode, LambdaExpressionNode>;

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

	std::optional<ASTNode> expression() const { return expression_; }
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
		member_names_.push_back("");  // Empty for positional initializers
	}

	void add_designated_initializer(std::string member_name, ASTNode init_expr) {
		initializers_.push_back(init_expr);
		is_designated_.push_back(true);
		member_names_.push_back(std::move(member_name));
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

	const std::string& member_name(size_t index) const {
		if (index < member_names_.size()) {
			return member_names_[index];
		}
		static const std::string empty;
		return empty;
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
	std::vector<std::string> member_names_;
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
		: declaration_node_(declaration_node), initializer_(initializer), storage_class_(storage_class) {}

	const DeclarationNode& declaration() const { return declaration_node_.as<DeclarationNode>(); }
	const ASTNode& declaration_node() const { return declaration_node_; }
	const std::optional<ASTNode>& initializer() const { return initializer_; }
	StorageClass storage_class() const { return storage_class_; }

private:
	ASTNode declaration_node_;
	std::optional<ASTNode> initializer_;
	StorageClass storage_class_;
};

class IfStatementNode {
public:
	explicit IfStatementNode(ASTNode condition, ASTNode then_statement,
		std::optional<ASTNode> else_statement = std::nullopt,
		std::optional<ASTNode> init_statement = std::nullopt)
		: condition_(condition), then_statement_(then_statement),
		  else_statement_(else_statement), init_statement_(init_statement) {}

	auto get_condition() const { return condition_; }
	auto get_then_statement() const { return then_statement_; }
	auto get_else_statement() const { return else_statement_; }
	auto get_init_statement() const { return init_statement_; }
	bool has_else() const { return else_statement_.has_value(); }
	bool has_init() const { return init_statement_.has_value(); }

private:
	ASTNode condition_;
	ASTNode then_statement_;
	std::optional<ASTNode> else_statement_;
	std::optional<ASTNode> init_statement_; // C++20 if (init; condition)
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
