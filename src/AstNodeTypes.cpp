#include "AstNodeTypes.h"
#include "StringBuilder.h"
#include "NameMangling.h"
#include "Log.h"
#include <sstream>
#include <set>
#include <unordered_set>
#include <functional>
#include <cstdlib>
#include <cstring>

// Helper class for cycle detection in recursive member lookup
// Uses RAII to manage the resolution stack
class RecursionGuard {
private:
    static thread_local std::unordered_set<const StructTypeInfo*> resolution_stack_;
    static thread_local int recursion_depth_;
    static constexpr int MAX_RECURSION_DEPTH = 100;
    
    const StructTypeInfo* type_;
    bool is_active_;
    
public:
    // Constructor: check for cycles and register this type
    explicit RecursionGuard(const StructTypeInfo* type) 
        : type_(type), is_active_(false) {
        // Check if we're already resolving this type (cycle detection)
        if (resolution_stack_.contains(type_)) {
            return;  // Cycle detected, leave is_active_ as false
        }
        
        // Check depth limit
        if (recursion_depth_ >= MAX_RECURSION_DEPTH) {
            return;  // Depth limit exceeded, leave is_active_ as false
        }
        
        // Add this type to the resolution stack
        resolution_stack_.insert(type_);
        ++recursion_depth_;
        is_active_ = true;
    }
    
    // Destructor: cleanup
    ~RecursionGuard() {
        if (is_active_) {
            resolution_stack_.erase(type_);
            --recursion_depth_;
        }
    }
    
    // Check if this guard is active (no cycle/depth limit detected)
    bool isActive() const { return is_active_; }
    
    // Disable copy and move
    RecursionGuard(const RecursionGuard&) = delete;
    RecursionGuard& operator=(const RecursionGuard&) = delete;
};

// Initialize static members
thread_local std::unordered_set<const StructTypeInfo*> RecursionGuard::resolution_stack_;
thread_local int RecursionGuard::recursion_depth_ = 0;

// Global target data model - default is platform-dependent
#if defined(_WIN32) || defined(_WIN64)
TargetDataModel g_target_data_model = TargetDataModel::LLP64;  // Windows: long = 32 bits
#else
TargetDataModel g_target_data_model = TargetDataModel::LP64;   // Linux/Unix: long = 64 bits
#endif

std::deque<TypeInfo> gTypeInfo;
std::unordered_map<StringHandle, TypeInfo*, StringHash, StringEqual> gTypesByName;
std::unordered_map<TypeCategory, const TypeInfo*> gNativeTypes;

TypeCreationResult add_user_type(StringHandle name, int type_size_in_bits, NamespaceHandle ns) {
    TypeIndex idx{gTypeInfo.size(), TypeCategory::UserDefined};
    auto& type_info = gTypeInfo.emplace_back(std::move(name), idx, type_size_in_bits);
    type_info.setNamespaceHandle(ns);
    gTypesByName.emplace(type_info.name(), &type_info);
    return TypeCreationResult{type_info, idx};
}

TypeCreationResult add_function_type(StringHandle name, [[maybe_unused]] Type return_type, NamespaceHandle ns) {
    TypeIndex idx{gTypeInfo.size(), TypeCategory::Function};
    auto& type_info = gTypeInfo.emplace_back(std::move(name), idx, 0);
    type_info.setNamespaceHandle(ns);
    gTypesByName.emplace(type_info.name(), &type_info);
    return TypeCreationResult{type_info, idx};
}

TypeCreationResult add_struct_type(StringHandle name, NamespaceHandle ns) {
    // Check if type already exists (forward declaration case)
    auto existing_it = gTypesByName.find(name);
    if (existing_it != gTypesByName.end()) {
        // Type already exists - return the existing one
        // This handles the case where we have a forward declaration followed by a full definition
        // Update namespace if not yet explicitly set (forward declaration may not have had context).
        // We check !isValid() (INVALID_HANDLE) which means "not yet assigned", as opposed to
        // isGlobal() which is a legitimate namespace (index 0) for types at file scope.
        if (!existing_it->second->namespaceHandle().isValid()) {
            existing_it->second->setNamespaceHandle(ns);
        }
        return TypeCreationResult{*existing_it->second, existing_it->second->type_index_};
    }

    TypeIndex idx{gTypeInfo.size(), TypeCategory::Struct};
    auto& type_info = gTypeInfo.emplace_back(name, idx, 0);
    type_info.setNamespaceHandle(ns);
    gTypesByName.emplace(type_info.name(), &type_info);
    return TypeCreationResult{type_info, idx};
}

TypeCreationResult add_enum_type(StringHandle name, NamespaceHandle ns) {
    TypeIndex idx{gTypeInfo.size(), TypeCategory::Enum};
    auto& type_info = gTypeInfo.emplace_back(std::move(name), idx, 0);
    type_info.setNamespaceHandle(ns);
    gTypesByName.emplace(type_info.name(), &type_info);
    return TypeCreationResult{type_info, idx};
}

TypeCreationResult register_type_alias(StringHandle name, const TypeSpecifierNode& type_spec, NamespaceHandle ns) {
    TypeIndex alias_idx{gTypeInfo.size(), TypeCategory::TypeAlias};
    auto& info = gTypeInfo.emplace_back(name, type_spec.type_index(), type_spec.size_in_bits());
    info.setNamespaceHandle(ns);
    info.is_type_alias_ = true;
    info.pointer_depth_ = type_spec.pointer_depth();
    info.reference_qualifier_ = type_spec.reference_qualifier();
    if (type_spec.has_function_signature()) {
        info.function_signature_ = type_spec.function_signature();
    }
    if (type_spec.type() == Type::Enum && type_spec.type_index().index() < gTypeInfo.size()) {
        if (const EnumTypeInfo* enum_info = gTypeInfo[type_spec.type_index().index()].getEnumInfo()) {
            info.setEnumInfo(std::make_unique<EnumTypeInfo>(*enum_info));
        }
    }
    gTypesByName.emplace(info.name(), &info);
    return TypeCreationResult{info, alias_idx};
}

// --- Type table accessor API (Milestone 6 / Option D Step 0) ---

const TypeInfo& getTypeInfo(TypeIndex idx) {
    assert(idx.index() < gTypeInfo.size() && "TypeIndex out of range");
    return gTypeInfo[idx.index()];
}

TypeInfo& getTypeInfoMut(TypeIndex idx) {
    assert(idx.index() < gTypeInfo.size() && "TypeIndex out of range");
    return gTypeInfo[idx.index()];
}

const TypeInfo* findTypeByName(StringHandle name) {
    auto it = gTypesByName.find(name);
    return it != gTypesByName.end() ? it->second : nullptr;
}

const TypeInfo* findNativeType(TypeCategory cat) {
    auto it = gNativeTypes.find(cat);
    return it != gNativeTypes.end() ? it->second : nullptr;
}

size_t getTypeInfoCount() {
    return gTypeInfo.size();
}

TypeInfo& add_template_param_type(StringHandle name, TypeCategory kind, uint32_t size_bits) {
    auto& type_info = gTypeInfo.emplace_back(name, TypeIndex{static_cast<uint32_t>(gTypeInfo.size()), kind}, size_bits);
    gTypesByName.emplace(type_info.name(), &type_info);
    return type_info;
}

TypeInfo& add_instantiated_type(StringHandle name, TypeCategory type, uint32_t size_bits) {
    auto& type_info = gTypeInfo.emplace_back(name, TypeIndex{static_cast<uint32_t>(gTypeInfo.size()), type}, size_bits);
    gTypesByName.emplace(type_info.name(), &type_info);
    return type_info;
}

TypeInfo& add_type_alias_copy(StringHandle name, [[maybe_unused]] Type type, TypeIndex source_type_index, uint32_t size_bits) {
    auto& type_info = gTypeInfo.emplace_back(name, source_type_index, size_bits);
    gTypesByName.emplace(type_info.name(), &type_info);
    return type_info;
}

TypeCreationResult add_empty_type_entry() {
    TypeIndex idx{gTypeInfo.size(), TypeCategory::UserDefined};
    auto& type_info = gTypeInfo.emplace_back();
    type_info.type_index_ = idx;
    return TypeCreationResult{type_info, idx};
}

std::unordered_map<StringHandle, TypeInfo*, StringHash, StringEqual>& getTypesByNameMap() {
    return gTypesByName;
}

const std::unordered_map<TypeCategory, const TypeInfo*>& getNativeTypesMap() {
    return gNativeTypes;
}

void initialize_native_types() {
    // Initialize native types if not already done
    if (!gNativeTypes.empty()) {
        return;
    }

    // Add basic native types
    auto& void_type = gTypeInfo.emplace_back(StringTable::createStringHandle("void"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Void}, 0);
    gNativeTypes[TypeCategory::Void] = &void_type;

    auto& bool_type = gTypeInfo.emplace_back(StringTable::createStringHandle("bool"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Bool}, get_type_size_bits(Type::Bool));
    gNativeTypes[TypeCategory::Bool] = &bool_type;

    auto& char_type = gTypeInfo.emplace_back(StringTable::createStringHandle("char"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Char}, get_type_size_bits(Type::Char));
    gNativeTypes[TypeCategory::Char] = &char_type;
    
    auto& wchar_type = gTypeInfo.emplace_back(StringTable::createStringHandle("wchar_t"sv), TypeIndex{gTypeInfo.size(), TypeCategory::WChar}, get_wchar_size_bits());
    gNativeTypes[TypeCategory::WChar] = &wchar_type;
    
    auto& char8_type = gTypeInfo.emplace_back(StringTable::createStringHandle("char8_t"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Char8}, get_type_size_bits(Type::Char8));
    gNativeTypes[TypeCategory::Char8] = &char8_type;
    
    auto& char16_type = gTypeInfo.emplace_back(StringTable::createStringHandle("char16_t"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Char16}, get_type_size_bits(Type::Char16));
    gNativeTypes[TypeCategory::Char16] = &char16_type;
    
    auto& char32_type = gTypeInfo.emplace_back(StringTable::createStringHandle("char32_t"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Char32}, get_type_size_bits(Type::Char32));
    gNativeTypes[TypeCategory::Char32] = &char32_type;

    auto& uchar_type = gTypeInfo.emplace_back(StringTable::createStringHandle("uchar"sv), TypeIndex{gTypeInfo.size(), TypeCategory::UnsignedChar}, get_type_size_bits(Type::UnsignedChar));
    gNativeTypes[TypeCategory::UnsignedChar] = &uchar_type;

    auto& short_type = gTypeInfo.emplace_back(StringTable::createStringHandle("short"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Short}, get_type_size_bits(Type::Short));
    gNativeTypes[TypeCategory::Short] = &short_type;

    auto& ushort_type = gTypeInfo.emplace_back(StringTable::createStringHandle("ushort"sv), TypeIndex{gTypeInfo.size(), TypeCategory::UnsignedShort}, get_type_size_bits(Type::UnsignedShort));
    gNativeTypes[TypeCategory::UnsignedShort] = &ushort_type;

    auto& int_type = gTypeInfo.emplace_back(StringTable::createStringHandle("int"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Int}, get_type_size_bits(Type::Int));
    gNativeTypes[TypeCategory::Int] = &int_type;

    auto& uint_type = gTypeInfo.emplace_back(StringTable::createStringHandle("uint"sv), TypeIndex{gTypeInfo.size(), TypeCategory::UnsignedInt}, get_type_size_bits(Type::UnsignedInt));
    gNativeTypes[TypeCategory::UnsignedInt] = &uint_type;

	auto& long_type = gTypeInfo.emplace_back(StringTable::createStringHandle("long"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Long}, get_type_size_bits(Type::Long));
	gNativeTypes[TypeCategory::Long] = &long_type;

	auto& ulong_type = gTypeInfo.emplace_back(StringTable::createStringHandle("ulong"sv), TypeIndex{gTypeInfo.size(), TypeCategory::UnsignedLong}, get_type_size_bits(Type::UnsignedLong));
	gNativeTypes[TypeCategory::UnsignedLong] = &ulong_type;

    auto& longlong_type = gTypeInfo.emplace_back(StringTable::createStringHandle("longlong"sv), TypeIndex{gTypeInfo.size(), TypeCategory::LongLong}, get_type_size_bits(Type::LongLong));
    gNativeTypes[TypeCategory::LongLong] = &longlong_type;

    auto& ulonglong_type = gTypeInfo.emplace_back(StringTable::createStringHandle("ulonglong"sv), TypeIndex{gTypeInfo.size(), TypeCategory::UnsignedLongLong}, get_type_size_bits(Type::UnsignedLongLong));
    gNativeTypes[TypeCategory::UnsignedLongLong] = &ulonglong_type;

    auto& float_type = gTypeInfo.emplace_back(StringTable::createStringHandle("float"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Float}, get_type_size_bits(Type::Float));
    gNativeTypes[TypeCategory::Float] = &float_type;

    auto& double_type = gTypeInfo.emplace_back(StringTable::createStringHandle("double"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Double}, get_type_size_bits(Type::Double));
    gNativeTypes[TypeCategory::Double] = &double_type;

    auto& longdouble_type = gTypeInfo.emplace_back(StringTable::createStringHandle("longdouble"sv), TypeIndex{gTypeInfo.size(), TypeCategory::LongDouble}, get_type_size_bits(Type::LongDouble));
    gNativeTypes[TypeCategory::LongDouble] = &longdouble_type;

    auto& auto_type = gTypeInfo.emplace_back(StringTable::createStringHandle("auto"sv), TypeIndex{gTypeInfo.size(), TypeCategory::Auto}, 0);
    gNativeTypes[TypeCategory::Auto] = &auto_type;

    auto& decltype_auto_type = gTypeInfo.emplace_back(StringTable::createStringHandle("decltype(auto)"sv), TypeIndex{gTypeInfo.size(), TypeCategory::DeclTypeAuto}, 0);
    gNativeTypes[TypeCategory::DeclTypeAuto] = &decltype_auto_type;

    auto& function_pointer_type = gTypeInfo.emplace_back(StringTable::createStringHandle("function_pointer"sv), TypeIndex{gTypeInfo.size(), TypeCategory::FunctionPointer}, get_type_size_bits(Type::FunctionPointer));
    gNativeTypes[TypeCategory::FunctionPointer] = &function_pointer_type;

    auto& member_function_pointer_type = gTypeInfo.emplace_back(StringTable::createStringHandle("member_function_pointer"sv), TypeIndex{gTypeInfo.size(), TypeCategory::MemberFunctionPointer}, get_type_size_bits(Type::MemberFunctionPointer));
    gNativeTypes[TypeCategory::MemberFunctionPointer] = &member_function_pointer_type;

    // Register GCC builtin types used by libstdc++ headers
    // __builtin_va_list is the compiler's internal va_list type (a struct array on x86-64).
    // We register it as a user-defined type so lookupTypeInCurrentContext finds it,
    // allowing declarations like '__builtin_va_list args;' to parse correctly.
    auto va_list_handle = StringTable::createStringHandle("__builtin_va_list"sv);
    auto& va_list_type_info = gTypeInfo.emplace_back(va_list_handle, TypeIndex{gTypeInfo.size(), TypeCategory::UserDefined}, 64); // Pointer-sized opaque handle
    gTypesByName.emplace(va_list_handle, &va_list_type_info);

    auto gnuc_va_list_handle = StringTable::createStringHandle("__gnuc_va_list"sv);
    auto& gnuc_va_list_type_info = gTypeInfo.emplace_back(gnuc_va_list_handle, TypeIndex{gTypeInfo.size(), TypeCategory::UserDefined}, 64);
    gTypesByName.emplace(gnuc_va_list_handle, &gnuc_va_list_type_info);

}

bool is_integer_type(Type type) {
    switch (type) {
        case Type::Char:
        case Type::UnsignedChar:
        case Type::WChar:      // wchar_t is an integer type
        case Type::Char8:      // char8_t is an integer type (C++20)
        case Type::Char16:     // char16_t is an integer type
        case Type::Char32:     // char32_t is an integer type
        case Type::Short:
        case Type::UnsignedShort:
        case Type::Int:
        case Type::UnsignedInt:
        case Type::Long:
        case Type::UnsignedLong:
        case Type::LongLong:
        case Type::UnsignedLongLong:
            return true;
        default:
            return false;
    }
}

bool is_bool_type(Type type) {
    return type == Type::Bool;
}

bool is_floating_point_type(Type type) {
    switch (type) {
        case Type::Float:
        case Type::Double:
        case Type::LongDouble:
            return true;
        default:
            return false;
    }
}

bool is_struct_type(Type type) {
    return type == Type::Struct || type == Type::UserDefined;
}

bool is_signed_integer_type(Type type) {
    switch (type) {
        case Type::Char:  // char is signed by default in most implementations
        case Type::Short:
        case Type::Int:
        case Type::Long:
        case Type::LongLong:
            return true;
        case Type::WChar:
            return g_target_data_model != TargetDataModel::LLP64;  // signed on Linux
        default:
            return false;
    }
}

bool is_unsigned_integer_type(Type type) {
    switch (type) {
        case Type::UnsignedChar:
        case Type::UnsignedShort:
        case Type::UnsignedInt:
        case Type::UnsignedLong:
        case Type::UnsignedLongLong:
            return true;
        case Type::Char8:
        case Type::Char16:
        case Type::Char32:
            return true;
        case Type::WChar:
            return g_target_data_model == TargetDataModel::LLP64;  // unsigned on Windows
        default:
            return false;
    }
}

int get_integer_rank(Type type) {
    // C++20 integer conversion rank (higher rank = larger type)
    // [conv.rank] specifies the conversion rank:
    // - bool has the lowest rank
    // - signed char/unsigned char have the same rank (less than short)
    // - short/unsigned short have the same rank (less than int)
    // - int/unsigned int have the same rank
    // - long/unsigned long have the same rank
    // - long long/unsigned long long have the highest standard integer rank
    switch (type) {
        case Type::Bool:
            return 0;  // Bool has lowest rank
        case Type::Char:
        case Type::UnsignedChar:
            return 1;
        case Type::Short:
        case Type::UnsignedShort:
            return 2;
        case Type::Int:
        case Type::UnsignedInt:
            return 3;
        case Type::Long:
        case Type::UnsignedLong:
            return 4;
        case Type::LongLong:
        case Type::UnsignedLongLong:
            return 5;
        case Type::Char8:
            return 1;  // Same rank as unsigned char
        case Type::Char16:
            return 2;  // Same rank as uint_least16_t (short)
        case Type::Char32:
            return 3;  // Same rank as uint_least32_t (int)
        case Type::WChar:
            // wchar_t rank is target-dependent: 16-bit on LLP64/Windows x64 (rank 2), 32-bit on LP64/Unix-like systems (rank 3)
            return (g_target_data_model == TargetDataModel::LLP64) ? 2 : 3;
        default:
            return -1;  // Invalid/unknown type
    }
}

int get_floating_point_rank(Type type) {
    // Floating-point conversion rank (higher rank = larger type)
    switch (type) {
        case Type::Float:
            return 1;
        case Type::Double:
            return 2;
        case Type::LongDouble:
            return 3;
        default:
            return 0;
    }
}

int get_type_size_bits(Type type) {
    switch (type) {
        case Type::Bool:
            return 8;
        case Type::Char:
        case Type::UnsignedChar:
        case Type::Char8:      // char8_t is always 8 bits
            return 8;
        case Type::Char16:     // char16_t is always 16 bits
            return 16;
        case Type::Char32:     // char32_t is always 32 bits
            return 32;
        case Type::WChar:
            return get_wchar_size_bits();  // Target-dependent: 16 bits (LLP64) or 32 bits (LP64)
        case Type::Short:
        case Type::UnsignedShort:
            return 16;
        case Type::Int:
        case Type::UnsignedInt:
            return 32;
        case Type::Long:
        case Type::UnsignedLong:
            return get_long_size_bits();  // Target-dependent: 32 bits (LLP64) or 64 bits (LP64)
        case Type::LongLong:
        case Type::UnsignedLongLong:
            return 64;
        case Type::Float:
            return 32;
        case Type::Double:
            return 64;
        case Type::LongDouble:
            return 80;  // x87 extended precision
        case Type::Enum:
            // Fallback only: when code still carries Type::Enum but lost the concrete
            // enum metadata, assume the common default underlying type (int, 32 bits).
            // This is expected only for still-buggy dependent/template instantiation paths;
            // normal enum sizing should come from the enum's TypeIndex/type_size_.
            return 32;
        case Type::FunctionPointer:
        case Type::MemberFunctionPointer:
        case Type::MemberObjectPointer:
            return 64;  // Function and member pointers are 64-bit (sizeof(void*))
        default:
            return 0;
    }
}

int get_type_size_bits(TypeCategory cat) {
    return get_type_size_bits(categoryToType(cat));
}

int get_type_size_bits(TypeIndex type_index) {
	if (type_index.index() > 0 && type_index.index() < getTypeInfoCount()) {
		const TypeInfo& ti = getTypeInfo(type_index);
		if (ti.type_size_ > 0)
			return ti.type_size_;
	}
	return get_type_size_bits(type_index.category());
}

Type promote_integer_type(Type type) {
    // C++ integer promotion rules: bool, char, and short promote to int
    switch (type) {
        case Type::Bool:
        case Type::Char:
        case Type::Short:
        case Type::Char8:      // char8_t (8-bit unsigned) promotes to int
        case Type::Char16:     // char16_t (16-bit unsigned) promotes to int
            return Type::Int;
        case Type::UnsignedChar:
        case Type::UnsignedShort:
            // If int can represent all values of the original type, promote to int
            // Otherwise promote to unsigned int (but for char/short, int is always sufficient)
            return Type::Int;
        case Type::WChar:
            // wchar_t promotion is target-dependent:
            // On Windows (16-bit): promotes to int
            // On Linux (32-bit): doesn't promote (same size as int)
            return (get_wchar_size_bits() < 32) ? Type::Int : Type::WChar;
        case Type::Char32:
            // char32_t (32-bit) doesn't promote - same size as int
            return Type::Char32;
        default:
            // Types int and larger don't get promoted
            return type;
    }
}

Type promote_floating_point_type(Type type) {
    // Floating-point promotions: float promotes to double in some contexts
    // For now, keep types as-is (no automatic promotion)
    return type;
}

// Helper function to get the unsigned version of an integer type
static Type get_unsigned_version(Type type) {
    switch (type) {
        case Type::Char:
        case Type::UnsignedChar:
            return Type::UnsignedChar;
        case Type::Short:
        case Type::UnsignedShort:
            return Type::UnsignedShort;
        case Type::Int:
        case Type::UnsignedInt:
            return Type::UnsignedInt;
        case Type::Long:
        case Type::UnsignedLong:
            return Type::UnsignedLong;
        case Type::LongLong:
        case Type::UnsignedLongLong:
            return Type::UnsignedLongLong;
        default:
            return type;  // Keep as-is for non-integer types
    }
}

// Check if a signed type can represent all values of an unsigned type
// This depends on platform-specific type sizes.
// Per C++20 [conv.rank], a signed type can represent all values of an unsigned type
// only if the signed type has strictly more bits. When bits are equal, the signed
// type cannot represent the upper half of the unsigned range (since half its range is negative).
// Example: signed long (64 bits) can represent all unsigned int (32 bits) values
// because max(long) = 2^63-1 > max(uint) = 2^32-1.
// But: signed int (32 bits) CANNOT represent all unsigned int (32 bits) values
// because max(int) = 2^31-1 < max(uint) = 2^32-1.
static bool can_represent_all_values(Type signed_type, Type unsigned_type) {
    // Get the bit sizes for comparison
    int signed_bits = get_type_size_bits(signed_type);
    int unsigned_bits = get_type_size_bits(unsigned_type);
    
    // Strictly greater means all values can be represented
    return signed_bits > unsigned_bits;
}

Type get_common_type(Type left, Type right) {
    // C++20 usual arithmetic conversions [conv.arith]
    
    // Floating-point types have higher precedence than integer types
    bool left_is_fp = is_floating_point_type(left);
    bool right_is_fp = is_floating_point_type(right);

    if (left_is_fp && right_is_fp) {
        // Both floating-point: higher rank wins
        int left_fp_rank = get_floating_point_rank(left);
        int right_fp_rank = get_floating_point_rank(right);
        return (left_fp_rank > right_fp_rank) ? left : right;
    }

    if (left_is_fp) {
        return left;  // Floating-point wins over integer
    }

    if (right_is_fp) {
        return right;  // Floating-point wins over integer
    }

    // Both are integer types: apply integer promotions first
    // This handles bool -> int, char -> int, short -> int, unsigned char/short -> int
    left = promote_integer_type(left);
    right = promote_integer_type(right);

    // After promotion, check if types are the same
    if (left == right) {
        return left;
    }

    // Get signedness and ranks
    bool left_unsigned = is_unsigned_integer_type(left);
    bool right_unsigned = is_unsigned_integer_type(right);
    int left_rank = get_integer_rank(left);
    int right_rank = get_integer_rank(right);

    // Case 1: Same signedness - higher rank wins
    if (left_unsigned == right_unsigned) {
        return (left_rank > right_rank) ? left : right;
    }

    // Case 2: One is signed, one is unsigned - apply C++20 rules
    // Identify which is signed and which is unsigned
    Type signed_type = left_unsigned ? right : left;
    Type unsigned_type = left_unsigned ? left : right;
    int signed_rank = left_unsigned ? right_rank : left_rank;
    int unsigned_rank = left_unsigned ? left_rank : right_rank;

    // Rule 1: If unsigned type's rank >= signed type's rank, convert to unsigned
    if (unsigned_rank >= signed_rank) {
        return unsigned_type;
    }

    // Rule 2: If signed type can represent all values of unsigned type, convert to signed
    if (can_represent_all_values(signed_type, unsigned_type)) {
        return signed_type;
    }

    // Rule 3: Convert both to unsigned version of the signed type
    return get_unsigned_version(signed_type);
}

bool requires_conversion(Type from, Type to) {
    return from != to && is_integer_type(from) && is_integer_type(to);
}

// Helper function to get CV-qualifier string
static std::string cv_qualifier_to_string(CVQualifier cv) {
    std::string result;
    if ((static_cast<uint8_t>(cv) & static_cast<uint8_t>(CVQualifier::Const)) != 0) {
        result += "const";
    }
    if ((static_cast<uint8_t>(cv) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0) {
        if (!result.empty()) result += " ";
        result += "volatile";
    }
    return result;
}

std::string TypeSpecifierNode::getReadableString() const {
    std::ostringstream oss;

    // Start with base type CV-qualifiers
    std::string base_cv = cv_qualifier_to_string(cv_qualifier_);
    if (!base_cv.empty()) {
        oss << base_cv << " ";
    }

    // Add sign qualifier if present
    if (qualifier_ == TypeQualifier::Unsigned) {
        oss << "unsigned ";
    } else if (qualifier_ == TypeQualifier::Signed) {
        oss << "signed ";
    }

    // Add base type name
    std::string_view name = getTypeName(type());
    if (!name.empty()) {
        oss << name;
    } else {
        // getTypeName returns "" for non-primitive types; provide fallback names
        switch (type()) {
            case Type::UserDefined: oss << "user_defined"; break;
            case Type::Auto: oss << "auto"; break;
            case Type::DeclTypeAuto: oss << "decltype(auto)"; break;
            case Type::Function: oss << "function"; break;
            case Type::Struct: oss << "struct"; break;
            case Type::Enum: oss << "enum"; break;
            case Type::FunctionPointer: oss << "function_pointer"; break;
            case Type::MemberFunctionPointer: oss << "member_function_pointer"; break;
            case Type::MemberObjectPointer: oss << "member_object_pointer"; break;
            case Type::Nullptr: oss << "nullptr_t"; break;
            case Type::Template: oss << "template"; break;
            case Type::Invalid: oss << "invalid"; break;
            default: oss << "unknown"; break;
        }
    }

    // Add pointer levels
    for (const auto& ptr_level : pointer_levels_) {
        oss << "*";
        std::string ptr_cv = cv_qualifier_to_string(ptr_level.cv_qualifier);
        if (!ptr_cv.empty()) {
            oss << " " << ptr_cv;
        }
    }

    return oss.str();
}


// StructTypeInfo method implementations

const StructMemberFunction* StructTypeInfo::findDefaultConstructor() const {
    for (const auto& func : member_functions) {
        if (func.is_constructor) {
            // Check if it's a default constructor:
            // - Either has no parameters, OR
            // - All parameters have default values
            const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
            const auto& params = ctor_node.parameter_nodes();
            
            if (params.empty()) {
                // No parameters - this is a default constructor
                return &func;
            }
            
            // Check if all parameters have default values
            bool all_params_have_defaults = true;
            for (const auto& param : params) {
                if (param.is<DeclarationNode>()) {
                    if (!param.as<DeclarationNode>().has_default_value()) {
                        all_params_have_defaults = false;
                        break;
                    }
                } else {
                    // Parameter is not a DeclarationNode - treat as not having a default
                    all_params_have_defaults = false;
                    break;
                }
            }
            
            if (all_params_have_defaults) {
                // All parameters have defaults - this can be called as a default constructor
                return &func;
            }
        }
    }
    return nullptr;
}

InlineVector<const StructMemberFunction*, 4> StructTypeInfo::getConstructorsByParameterCount(
	size_t parameter_count,
	bool skip_implicit) const {
	InlineVector<const StructMemberFunction*, 4> matches;
	bool hasNonImplicitMatch = false;
	for (const auto& func : member_functions) {
		if (!func.is_constructor) {
			continue;
		}
		if (!func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}
		const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
		bool ctor_is_implicit = ctor_node.is_implicit();
		if (skip_implicit && ctor_is_implicit) {
			continue;
		}
		if (ctor_node.parameter_nodes().size() == parameter_count) {
			if (!ctor_is_implicit) {
				hasNonImplicitMatch = true;
			}
			matches.push_back(&func);
		}
	}

	if (!skip_implicit && hasNonImplicitMatch) {
		// Prefer user-declared constructors over implicit ones when both
		// match the same parameter count.
		InlineVector<const StructMemberFunction*, 4> filtered_matches;
		for (const StructMemberFunction* match : matches) {
			const auto& ctor_node = match->function_decl.as<ConstructorDeclarationNode>();
			if (!ctor_node.is_implicit()) {
				filtered_matches.push_back(match);
			}
		}
		return filtered_matches;
	}

	return matches;
}

bool StructTypeInfo::hasUserDefinedConstructor() const {
	for (const auto& func : member_functions) {
		if (!func.is_constructor) continue;
		const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
		if (ctor_node.is_implicit()) continue;
		return true;
	}
	return false;
}

// Auto-extract is_noexcept and cv_qualifier from the AST node stored in a
// StructMemberFunction. This centralises property propagation so that every
// addMemberFunction / addConstructor / addDestructor / addOperatorOverload call
// automatically picks up the flags — callers never need to do it manually.
// All parse and instantiation paths must call set_is_const_member_function()
// on the FunctionDeclarationNode before calling add*().
void StructTypeInfo::propagateAstProperties(StructMemberFunction& mf) {
    if (const auto* fn = get_function_decl_node(mf.function_decl)) {
        mf.is_noexcept = fn->is_noexcept();
        // Auto-derive cv_qualifier from the stored const/volatile member function flags.
        // All parse and instantiation paths must call set_is_const_member_function() and
        // set_is_volatile_member_function() on the FunctionDeclarationNode before calling
        // addMemberFunction / addOperatorOverload.
        const bool is_c = fn->is_const_member_function();
        const bool is_v = fn->is_volatile_member_function();
        if (is_c && is_v) mf.cv_qualifier = CVQualifier::ConstVolatile;
        else if (is_c) mf.cv_qualifier = CVQualifier::Const;
        else if (is_v) mf.cv_qualifier = CVQualifier::Volatile;
        else mf.cv_qualifier = CVQualifier::None;
    } else if (mf.function_decl.is<ConstructorDeclarationNode>()) {
        const auto& ctor = mf.function_decl.as<ConstructorDeclarationNode>();
        mf.is_noexcept = ctor.is_noexcept();
    } else if (mf.function_decl.is<DestructorDeclarationNode>()) {
        const auto& dtor = mf.function_decl.as<DestructorDeclarationNode>();
        mf.is_noexcept = dtor.is_noexcept();
    }
}

// Helper to check if a parameter's type_index matches this struct's own type_index.
// Uses own_type_index_ cached by TypeInfo::setStructInfo() — no map lookup needed.
// For template instantiations (e.g., Wrapper<int>), also checks if the param_type_index
// refers to the base template pattern (e.g., Wrapper) that this struct was instantiated from,
// since self-referential parameters like operator=(const Wrapper&) may not have the
// instantiated type_index substituted.
bool StructTypeInfo::isOwnTypeIndex(TypeIndex param_type_index) const {
    if (!own_type_index_.has_value()) return false;
    // Direct match (works for non-template types and properly substituted template params)
    if (param_type_index == *own_type_index_) return true;
    // Template instantiation fallback: check if param refers to our base template pattern
    if ((*own_type_index_).index() >= gTypeInfo.size() || param_type_index.index() >= gTypeInfo.size())
        return false;
    const TypeInfo& own_info = gTypeInfo[(*own_type_index_).index()];
    if (!own_info.isTemplateInstantiation()) return false;
    const TypeInfo& param_info = gTypeInfo[param_type_index.index()];
    // Param is the base template pattern itself (e.g., Wrapper vs Wrapper<int>)
    if (own_info.baseTemplateName() == param_info.name()) return true;
    // Note: We intentionally do NOT match different instantiations of the same template
    // (e.g., Wrapper<double> should not match for Wrapper<int>'s own type check).
    // The base pattern check above handles the unsubstituted case.
    return false;
}

// ── Shared core for copy/move constructor lookup ────────────────────────
//
// Matches a constructor whose *first* parameter is a reference (lvalue or
// rvalue, selected by want_move) to the struct's own type.  Constructors
// with additional defaulted parameters also qualify (min-required == 1).
//
// This single helper replaces the three ad-hoc loops that previously lived
// in findCopyConstructor, findMoveConstructor, and
// findPreferredSameTypeConstructor, fixing two latent issues:
//   1. findCopyConstructor used is_reference() which matched rvalue-refs.
//   2. None of the finders accepted ctors with default arguments
//      (e.g. Foo(const Foo&, int = 0)).
const StructMemberFunction* StructTypeInfo::findSameTypeConstructorCore(
	bool want_move,
	bool include_implicit) const {
	for (const auto& func : member_functions) {
		if (!func.is_constructor || !func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}

		const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
		if (!include_implicit && ctor_node.is_implicit()) {
			continue;
		}

		const auto& params = ctor_node.parameter_nodes();
		if (params.empty() || !params[0].is<DeclarationNode>()) {
			continue;
		}

		// Accept ctors where only the first arg is required (rest have defaults).
		size_t min_required = computeMinRequiredArgs(params);
		if (min_required > 1) {
			continue;
		}

		const auto& param_decl = params[0].as<DeclarationNode>();
		if (!param_decl.type_node().is<TypeSpecifierNode>()) {
			continue;
		}
		const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
		if (param_type.type() != Type::Struct || !isOwnTypeIndex(param_type.type_index())) {
			continue;
		}

		if (want_move) {
			if (param_type.is_rvalue_reference()) {
				return &func;
			}
		} else {
			if (param_type.is_lvalue_reference()) {
				return &func;
			}
		}
	}

	return nullptr;
}

const StructMemberFunction* StructTypeInfo::findCopyConstructor(bool include_implicit) const {
	return findSameTypeConstructorCore(/*want_move=*/false, include_implicit);
}

const StructMemberFunction* StructTypeInfo::findMoveConstructor(bool include_implicit) const {
	return findSameTypeConstructorCore(/*want_move=*/true, include_implicit);
}

// Preferred same-type constructor: try move (if not deleted) then copy (if not
// deleted), with optional implicit-ctor participation.
const StructMemberFunction* StructTypeInfo::findPreferredSameTypeConstructor(
	bool prefer_move,
	bool include_implicit) const {
	if (prefer_move && !isMoveConstructorDeleted()) {
		if (const StructMemberFunction* move_ctor =
				findSameTypeConstructorCore(true, include_implicit)) {
			return move_ctor;
		}
	}

	if (!isCopyConstructorDeleted()) {
		if (const StructMemberFunction* copy_ctor =
				findSameTypeConstructorCore(false, include_implicit)) {
			return copy_ctor;
		}
	}

	return nullptr;
}

namespace {
	bool isMatchingSameTypeAssignmentOperator(const StructTypeInfo& struct_info,
		const StructMemberFunction& func,
		bool want_move,
		bool include_implicit) {
		if (!isAssignOperator(func.operator_kind)) {
			return false;
		}

		const auto* func_node = get_function_decl_node(func.function_decl);
		if (!func_node) {
			return false;
		}
		if (!include_implicit && func_node->is_implicit()) {
			return false;
		}

		const OverloadableOperator expected_kind =
			want_move ? OverloadableOperator::MoveAssign : OverloadableOperator::CopyAssign;
		if (func.operator_kind == expected_kind) {
			return true;
		}
		if (func.operator_kind != OverloadableOperator::Assign) {
			return false;
		}

		const auto& params = func_node->parameter_nodes();
		if (params.empty() || !params[0].is<DeclarationNode>()) {
			return false;
		}

		if (computeMinRequiredArgs(params) > 1) {
			return false;
		}

		const auto& param_decl = params[0].as<DeclarationNode>();
		if (!param_decl.type_node().is<TypeSpecifierNode>()) {
			return false;
		}

		const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
		const bool matches_reference = want_move
			? param_type.is_rvalue_reference()
			: param_type.is_lvalue_reference();
		return matches_reference
			&& param_type.type() == Type::Struct
			&& struct_info.isOwnTypeIndex(param_type.type_index());
	}
}

const StructMemberFunction* StructTypeInfo::findCopyAssignmentOperator(bool include_implicit) const {
	for (const auto& func : member_functions) {
		if (isMatchingSameTypeAssignmentOperator(*this, func, false, include_implicit)) {
			return &func;
		}
	}
	return nullptr;
}

const StructMemberFunction* StructTypeInfo::findMoveAssignmentOperator(bool include_implicit) const {
	for (const auto& func : member_functions) {
		if (isMatchingSameTypeAssignmentOperator(*this, func, true, include_implicit)) {
			return &func;
		}
	}
	return nullptr;
}

// Finalize struct layout with base classes
// Returns false if semantic errors were detected
bool StructTypeInfo::finalizeWithBases() {
    // Step 0: Build vtable first (before layout)
    if (!buildVTable()) {
        return false;  // Semantic error during vtable building
    }

    // Step 0.1: Build RTTI information (after vtable, before layout)
    buildRTTI();

    size_t current_offset = 0;
    size_t max_alignment = 1;

    // Step 0.5: Add vptr if this struct has virtual functions
    // Note: If base class has vtable, we inherit its vptr (at offset 0 in base subobject)
    // Only add vptr if we introduce virtual functions and have no polymorphic base
    bool base_has_vtable = false;
    for (const auto& base : base_classes) {
        if (base.is_virtual) continue;  // Skip virtual bases for now
        if (base.type_index.index() < gTypeInfo.size()) {
            const TypeInfo& base_type = gTypeInfo[base.type_index.index()];
            const StructTypeInfo* base_info = base_type.getStructInfo();
            if (base_info && base_info->has_vtable) {
                base_has_vtable = true;
                break;
            }
        }
    }

    // If we have virtual functions but no polymorphic base, add vptr
    if (has_vtable && !base_has_vtable) {
        // vptr is at offset 0, size 8 (pointer size on x64)
        current_offset = 8;
        max_alignment = 8;  // Pointer alignment
    }

    // Step 1: Layout non-virtual base class subobjects
    for (auto& base : base_classes) {
        if (base.is_virtual) {
            continue;  // Virtual bases are laid out at the end
        }

        if (base.type_index.index() >= gTypeInfo.size()) {
            continue;  // Invalid base class index
        }

        const TypeInfo& base_type = gTypeInfo[base.type_index.index()];
        const StructTypeInfo* base_info = base_type.getStructInfo();

        if (!base_info) {
            continue;  // Not a struct type
        }

        // Align to base class alignment
        size_t base_alignment = base_info->alignment;
        current_offset = (current_offset + base_alignment - 1) & ~(base_alignment - 1);

        // Store base class offset
        base.offset = current_offset;

        // Advance offset by base class size
        current_offset += base_info->total_size;

        // Track maximum alignment
        max_alignment = std::max(max_alignment, base_alignment);
    }

    // Step 2: Layout derived class members
    for (auto& member : members) {
        // Apply pack alignment if specified
        size_t effective_alignment = member.alignment;
        if (pack_alignment > 0 && pack_alignment < member.alignment) {
            effective_alignment = pack_alignment;
        }

        // Align to member alignment
        current_offset = (current_offset + effective_alignment - 1) & ~(effective_alignment - 1);

        // Update member offset
        member.offset = current_offset;

        // Advance offset by member size
        current_offset += member.size;

        // Track maximum alignment
        max_alignment = std::max(max_alignment, effective_alignment);
    }

    // Step 3: Layout virtual base class subobjects (at the end, shared across inheritance paths)
    // Collect all unique virtual bases (including those from base classes)
    std::vector<BaseClassSpecifier*> all_virtual_bases;
    std::set<TypeIndex> seen_virtual_bases;

    // Helper function to collect virtual bases recursively
    std::function<void(const StructTypeInfo*)> collectVirtualBases = [&](const StructTypeInfo* struct_info) {
        if (!struct_info) return;

        // Check direct base classes
        for (auto& base : struct_info->base_classes) {
            if (base.is_virtual && seen_virtual_bases.find(base.type_index) == seen_virtual_bases.end()) {
                seen_virtual_bases.insert(base.type_index);
                // Find the corresponding base in our base_classes list
                for (auto& our_base : base_classes) {
                    if (our_base.type_index == base.type_index && our_base.is_virtual) {
                        all_virtual_bases.push_back(&our_base);
                        break;
                    }
                }
            }

            // Recursively collect from non-virtual bases
            if (!base.is_virtual && base.type_index.index() < gTypeInfo.size()) {
                const TypeInfo& base_type = gTypeInfo[base.type_index.index()];
                const StructTypeInfo* base_info = base_type.getStructInfo();
                collectVirtualBases(base_info);
            }
        }
    };

    collectVirtualBases(this);

    // Layout virtual bases
    for (auto* vbase : all_virtual_bases) {
        if (vbase->type_index.index() >= gTypeInfo.size()) {
            continue;
        }

        const TypeInfo& base_type = gTypeInfo[vbase->type_index.index()];
        const StructTypeInfo* base_info = base_type.getStructInfo();

        if (!base_info) {
            continue;
        }

        // Align to base class alignment
        size_t base_alignment = base_info->alignment;
        current_offset = (current_offset + base_alignment - 1) & ~(base_alignment - 1);

        // Store virtual base class offset
        vbase->offset = current_offset;

        // Advance offset by base class size
        current_offset += base_info->total_size;

        // Track maximum alignment
        max_alignment = std::max(max_alignment, base_alignment);
    }

    // Step 4: Apply custom alignment if specified
    if (custom_alignment > 0) {
        max_alignment = custom_alignment;
    }

    // Step 5: Pad to alignment
    alignment = max_alignment;
    total_size = (current_offset + alignment - 1) & ~(alignment - 1);
    
    return true;
}

// Build vtable for virtual functions
// Returns false if semantic errors were detected
bool StructTypeInfo::buildVTable() {
    bool success = true;
    
    // Step 1: Copy base class vtable entries (if any)
    bool any_base_potentially_incomplete = has_deferred_base_classes;
    for (const auto& base : base_classes) {
        if (base.type_index.index() >= gTypeInfo.size()) {
            continue;
        }

        const TypeInfo& base_type = gTypeInfo[base.type_index.index()];
        const StructTypeInfo* base_info = base_type.getStructInfo();

        if (base_info && base_info->has_vtable) {
            // Copy all base vtable entries
            for (const auto* base_func : base_info->vtable) {
                if (base_func != nullptr) {  // Safety check
                    vtable.push_back(base_func);
                }
            }
            has_vtable = true;
        } else if (base_info == nullptr) {
            // Base class type info not yet available. Override checks must be deferred.
            any_base_potentially_incomplete = true;
        } else if (!base_info->has_vtable) {
            // Base class exists but vtable not built. Check if it has virtual functions
            // that suggest its vtable hasn't been built yet (e.g., template instantiation
            // whose finalization is deferred).
            for (const auto& func : base_info->member_functions) {
                if (func.is_virtual || func.is_override) {
                    any_base_potentially_incomplete = true;
                    break;
                }
            }
        }
    }

    // Step 2: Process this class's virtual functions
    if (member_functions.empty()) {
        return true;  // No member functions to process
    }

    for (auto& func : member_functions) {
        // Skip constructors (they can't be virtual in the vtable sense)
        if (func.is_constructor) {
            continue;
        }

        // Skip non-virtual functions
        // Note: A function with 'override' is implicitly virtual even without 'virtual' keyword
        if (!func.is_virtual && !func.is_override) {
            continue;  // Not a virtual function
        }

        // Mark struct as having a vtable
        has_vtable = true;

        // Get function name for matching
        StringHandle func_name = func.getName();

        // Check if this function overrides a base class virtual function
        int override_index = -1;
        const StructMemberFunction* base_func_ptr = nullptr;
        for (size_t i = 0; i < vtable.size(); ++i) {
            const StructMemberFunction* base_func = vtable[i];
            if (base_func != nullptr && base_func->getName() == func_name) {
                override_index = static_cast<int>(i);
                base_func_ptr = base_func;
                break;
            }
        }

        if (override_index >= 0) {
            // Check if base function is final
            if (base_func_ptr && base_func_ptr->is_final) {
                // Error: attempting to override a final function
                std::string error_msg = "cannot override final function '" + 
                    std::string(StringTable::getStringView(func_name)) + "' in class '" +
                    std::string(StringTable::getStringView(getName())) + "'";
                FLASH_LOG(Parser, Error, error_msg);
                finalization_error_ = error_msg;
                success = false;
            }
            
            // Override existing vtable entry
            vtable[override_index] = &func;
            func.vtable_index = override_index;
        } else {
            // Add new vtable entry
            func.vtable_index = static_cast<int>(vtable.size());
            vtable.push_back(&func);
        }

        // Validate override keyword usage
        if (func.is_override && override_index < 0) {
            if (any_base_potentially_incomplete) {
                // Base class vtable may be incomplete (template base classes are deferred
                // during parsing). Defer the check to instantiation time.
            } else {
                // Error: 'override' specified but no base function to override
                // Enforces [class.virtual]/4: ill-formed if override doesn't match
                std::string error_msg = "function '" + 
                    std::string(StringTable::getStringView(func_name)) + 
                    "' marked 'override' but does not override any base class function in class '" +
                    std::string(StringTable::getStringView(getName())) + "'";
                FLASH_LOG(Parser, Error, error_msg);
                finalization_error_ = error_msg;
                success = false;
            }
        }
    }

    // Update abstract flag after building vtable
    updateAbstractFlag();
    
    // Generate vtable symbol name if this struct has a vtable
    if (has_vtable) {
        StringBuilder vtable_sb;
        
        // Use platform-appropriate vtable mangling
        if (NameMangling::g_mangling_style == NameMangling::ManglingStyle::Itanium) {
            // Itanium C++ ABI: _ZTV + length + name
            // e.g., class "Base" -> "_ZTV4Base"
            vtable_sb.append("_ZTV");
            std::string_view name_sv = StringTable::getStringView(getName());
            vtable_sb.append(static_cast<uint64_t>(name_sv.size()));
            vtable_sb.append(name_sv);
        } else {
            // MSVC: ??_7 + name + @@6B@
            // e.g., class "Base" -> "??_7Base@@6B@"
            vtable_sb.append("??_7");
            vtable_sb.append(StringTable::getStringView(getName()));
            vtable_sb.append("@@6B@");
        }
        
        vtable_symbol = vtable_sb.commit();
    }
    
    return success;
}

// Update abstract flag based on pure virtual functions in vtable
void StructTypeInfo::updateAbstractFlag() {
    is_abstract = false;

    // Check if any function in the vtable is pure virtual
    for (const auto* func : vtable) {
        if (func && func->is_pure_virtual) {
            is_abstract = true;
            break;
        }
    }
}

// Find member recursively through base classes
std::optional<StructMember> StructTypeInfo::findMemberRecursive(StringHandle member_name) const {
    // Use RecursionGuard to prevent infinite recursion in variadic template patterns
    RecursionGuard guard(this);
    if (!guard.isActive()) {
        return std::nullopt;  // Cycle or depth limit detected
    }
    
    // First, check own members
    for (const auto& member : members) {
        if (member.getName() == member_name) {
            return member;
        }
    }

    // Then, check base class members
    for (const auto& base : base_classes) {
        if (base.type_index.index() >= gTypeInfo.size()) {
            continue;
        }

        const TypeInfo& base_type = gTypeInfo[base.type_index.index()];
        const StructTypeInfo* base_info = base_type.getStructInfo();

        if (base_info) {
            auto base_member = base_info->findMemberRecursive(member_name);
            if (base_member) {
                // Found in base class - adjust offset by base class offset
                base_member->offset += base.offset;
                return base_member;
            }
        }
    }

    return std::nullopt;  // Not found
}

std::pair<const StructStaticMember*, const StructTypeInfo*> StructTypeInfo::findStaticMemberRecursive(StringHandle member_name) const {
    // Use RecursionGuard to prevent infinite recursion in variadic template patterns
    RecursionGuard guard(this);
    if (!guard.isActive()) {
        return { nullptr, nullptr };  // Cycle or depth limit detected
    }
    
    // First, check own static members
    for (const auto& static_member : static_members) {
        if (static_member.getName() == member_name) {
            return { &static_member, this };
        }
    }

    // Then, check base class static members
    for (const auto& base : base_classes) {
		if (base.type_index.index() >= gTypeInfo.size()) {
			continue;
		}

		const TypeInfo* base_type = &gTypeInfo[base.type_index.index()];
		const StructTypeInfo* base_info = base_type->getStructInfo();

		// Follow typedef/alias chains to find the underlying struct info if needed
		if (!base_info && base_type->isStruct()) {
			constexpr size_t MAX_ALIAS_DEPTH = 64;
			size_t depth = 0;
			while (depth < MAX_ALIAS_DEPTH) {
				if (!base_type->type_index_.is_valid() || base_type->type_index_.index() >= gTypeInfo.size()) {
					break;
				}
				const TypeInfo* next = &gTypeInfo[base_type->type_index_.index()];
				if (next == base_type) {
					break;
				}
				base_type = next;
				base_info = base_type->getStructInfo();
				if (base_info) {
					break;
				}
				++depth;
			}
		}

		if (base_info) {
			auto [base_static_member, owner_struct] = base_info->findStaticMemberRecursive(member_name);
			if (base_static_member) {
				// Found in base class - return it with its owner
                return { base_static_member, owner_struct };
            }
        }
    }

    return { nullptr, nullptr };  // Not found
}

// Build RTTI information for polymorphic classes
void StructTypeInfo::buildRTTI() {
    // Only build RTTI for polymorphic classes (those with vtables)
    if (!has_vtable) {
        return;
    }

    // Create RTTI info with MSVC multi-structure format
    // Use std::deque instead of std::vector to avoid pointer invalidation on resize
    static std::deque<RTTITypeInfo> rtti_storage;
    static std::vector<MSVCTypeDescriptor*> type_descriptor_storage;
    static std::deque<MSVCCompleteObjectLocator> col_storage;
    static std::deque<MSVCClassHierarchyDescriptor> chd_storage;
    static std::vector<MSVCBaseClassArray*> bca_storage;
    static std::deque<MSVCBaseClassDescriptor> bcd_storage;

    // Create mangled and demangled names
    std::string name_str(StringTable::getStringView(getName()));
    std::string mangled_name = ".?AV" + name_str + "@@";  // MSVC-style mangling for classes

    // Allocate RTTI info
    rtti_storage.emplace_back(mangled_name.c_str(), name_str.c_str(), base_classes.size());
    rtti_info = &rtti_storage.back();

    // Build ??_R0 - Type Descriptor
    // Note: Memory is allocated in static storage and persists for program lifetime (RTTI data)
    size_t name_len = mangled_name.length() + 1;
    MSVCTypeDescriptor* type_desc = (MSVCTypeDescriptor*)malloc(sizeof(MSVCTypeDescriptor) + name_len);
    if (!type_desc) {
        // Allocation failed - skip RTTI generation for this class
        return;
    }
    type_desc->vtable = nullptr;  // No vtable pointer needed for our purposes
    type_desc->spare = nullptr;
    // Use safe string copy
    strncpy(type_desc->name, mangled_name.c_str(), name_len);
    type_desc->name[name_len - 1] = '\0';  // Ensure null termination
    type_descriptor_storage.push_back(type_desc);
    rtti_info->type_descriptor = type_desc;

    // Build legacy base class array for compatibility
    if (!base_classes.empty()) {
        static std::vector<const RTTITypeInfo*> base_array_storage;
        size_t base_array_start = base_array_storage.size();

        for (const auto& base : base_classes) {
            if (base.type_index.index() >= gTypeInfo.size()) {
                base_array_storage.push_back(nullptr);
                continue;
            }

            const TypeInfo& base_type = gTypeInfo[base.type_index.index()];
            const StructTypeInfo* base_info = base_type.getStructInfo();

            if (base_info && base_info->rtti_info) {
                base_array_storage.push_back(base_info->rtti_info);
            } else {
                base_array_storage.push_back(nullptr);
            }
        }

        rtti_info->base_types = &base_array_storage[base_array_start];
    }

    // Build ??_R1 - Base Class Descriptors (one for each base, plus one for self)
    [[maybe_unused]] size_t total_bases = base_classes.size() + 1;  // +1 for self
    
    // Self descriptor (always first)
    MSVCBaseClassDescriptor self_bcd;
    self_bcd.type_descriptor = type_desc;
    self_bcd.num_contained_bases = static_cast<uint32_t>(base_classes.size());
    self_bcd.mdisp = 0;      // No displacement for self
    self_bcd.pdisp = -1;     // Not a virtual base
    self_bcd.vdisp = 0;
    self_bcd.attributes = 0; // No special attributes for self
    bcd_storage.push_back(self_bcd);
    rtti_info->base_descriptors.push_back(&bcd_storage.back());

    // Base class descriptors
    for (size_t i = 0; i < base_classes.size(); ++i) {
        const auto& base = base_classes[i];
        
        if (base.type_index.index() >= gTypeInfo.size()) {
            continue;
        }

        const TypeInfo& base_type = gTypeInfo[base.type_index.index()];
        const StructTypeInfo* base_info = base_type.getStructInfo();

        if (base_info && base_info->rtti_info && base_info->rtti_info->type_descriptor) {
            MSVCBaseClassDescriptor base_bcd;
            base_bcd.type_descriptor = base_info->rtti_info->type_descriptor;
            base_bcd.num_contained_bases = static_cast<uint32_t>(base_info->base_classes.size());
            base_bcd.mdisp = static_cast<int32_t>(base.offset);  // Offset of base in derived class
            base_bcd.pdisp = base.is_virtual ? 0 : -1;  // Virtual base handling
            base_bcd.vdisp = 0;
            base_bcd.attributes = base.is_virtual ? 1 : 0;  // Mark virtual bases
            bcd_storage.push_back(base_bcd);
            rtti_info->base_descriptors.push_back(&bcd_storage.back());
        }
    }

    // Build ??_R2 - Base Class Array
    // Note: Memory is allocated in static storage and persists for program lifetime (RTTI data)
    MSVCBaseClassArray* bca = (MSVCBaseClassArray*)malloc(
        sizeof(MSVCBaseClassArray) + (rtti_info->base_descriptors.size() - 1) * sizeof(void*)
    );
    if (!bca) {
        // Allocation failed - skip Base Class Array for this class
        rtti_info->bca = nullptr;
        return;
    }
    for (size_t i = 0; i < rtti_info->base_descriptors.size(); ++i) {
        bca->base_class_descriptors[i] = rtti_info->base_descriptors[i];
    }
    bca_storage.push_back(bca);
    rtti_info->bca = bca;

    // Build ??_R3 - Class Hierarchy Descriptor
    MSVCClassHierarchyDescriptor chd;
    chd.signature = 0;
    chd.attributes = 0;  // Can be extended for multiple/virtual inheritance flags
    chd.num_base_classes = static_cast<uint32_t>(rtti_info->base_descriptors.size());
    chd.base_class_array = bca;
    chd_storage.push_back(chd);
    rtti_info->chd = &chd_storage.back();

    // Build ??_R4 - Complete Object Locator
    MSVCCompleteObjectLocator col;
    col.signature = 1;  // 1 for 64-bit
    col.offset = 0;     // Offset of vtable in complete class (0 for primary base)
    col.cd_offset = 0;  // Constructor displacement offset
    col.type_descriptor = type_desc;
    col.hierarchy = rtti_info->chd;
    col_storage.push_back(col);
    rtti_info->col = &col_storage.back();

    // ========== Build Itanium C++ ABI RTTI structures ==========
    // These are used for Linux/Unix targets (ELF format)
    
    // Storage for Itanium structures (static lifetime - RTTI data persists for program lifetime)
    // Note: Memory allocated here is intentionally never freed as RTTI must remain valid
    // throughout the program execution. This is the same pattern as MSVC RTTI above.
    // Use std::deque instead of std::vector to avoid pointer invalidation on resize
    static std::deque<ItaniumClassTypeInfo> itanium_class_storage;
    static std::deque<ItaniumSIClassTypeInfo> itanium_si_storage;
    static std::vector<char*> itanium_vmi_storage;  // Variable-sized, use malloc
    static std::vector<char*> itanium_name_storage;
    
    // Create Itanium-style mangled name
    // Itanium uses length-prefixed names: "3Foo" for class Foo
    std::string_view name_sv = StringTable::getStringView(getName());
    
    // Calculate required size for "LENGTH" + name + null terminator
    size_t length_digits = std::to_string(name_sv.length()).length();
    size_t itanium_total_size = length_digits + name_sv.length() + 1;
    
    // Allocate permanent storage for the name string (persists for program lifetime)
    char* itanium_name = (char*)malloc(itanium_total_size);
    if (!itanium_name) {
        // Allocation failed - Itanium RTTI won't be available
        return;
    }
    
    // Write length prefix and name directly to buffer
    int written = snprintf(itanium_name, itanium_total_size, "%zu%.*s", 
                          name_sv.length(), 
                          static_cast<int>(name_sv.length()), 
                          name_sv.data());
    if (written < 0 || static_cast<size_t>(written) >= itanium_total_size) {
        free(itanium_name);
        return;
    }
    itanium_name_storage.push_back(itanium_name);
    
    if (base_classes.empty()) {
        // __class_type_info - No base classes
        ItaniumClassTypeInfo class_ti;
        class_ti.vtable = nullptr;  // Will be set to vtable for __class_type_info at link time
        class_ti.name = itanium_name;
        itanium_class_storage.push_back(class_ti);
        rtti_info->itanium_type_info = &itanium_class_storage.back();
        rtti_info->itanium_kind = RTTITypeInfo::ItaniumTypeInfoKind::ClassTypeInfo;
    } else if (base_classes.size() == 1 && !base_classes[0].is_virtual) {
        // __si_class_type_info - Single, non-virtual base class
        ItaniumSIClassTypeInfo si_ti;
        si_ti.vtable = nullptr;  // Will be set to vtable for __si_class_type_info at link time
        si_ti.name = itanium_name;
        
        // Get base class type info
        if (base_classes[0].type_index.index() < gTypeInfo.size()) {
            const TypeInfo& base_type = gTypeInfo[base_classes[0].type_index.index()];
            const StructTypeInfo* base_info = base_type.getStructInfo();
            if (base_info && base_info->rtti_info && base_info->rtti_info->itanium_type_info) {
                si_ti.base_type = base_info->rtti_info->itanium_type_info;
            } else {
                si_ti.base_type = nullptr;
            }
        } else {
            si_ti.base_type = nullptr;
        }
        
        itanium_si_storage.push_back(si_ti);
        rtti_info->itanium_type_info = &itanium_si_storage.back();
        rtti_info->itanium_kind = RTTITypeInfo::ItaniumTypeInfoKind::SIClassTypeInfo;
    } else {
        // __vmi_class_type_info - Multiple or virtual base classes
        size_t vmi_size = sizeof(ItaniumVMIClassTypeInfo) + 
                         (base_classes.size() - 1) * sizeof(ItaniumBaseClassTypeInfo);
        // Allocate permanent storage (persists for program lifetime - RTTI data)
        ItaniumVMIClassTypeInfo* vmi_ti = (ItaniumVMIClassTypeInfo*)malloc(vmi_size);
        if (!vmi_ti) {
            // Allocation failed
            return;
        }
        
        vmi_ti->vtable = nullptr;  // Will be set to vtable for __vmi_class_type_info at link time
        vmi_ti->name = itanium_name;
        vmi_ti->base_count = static_cast<uint32_t>(base_classes.size());
        
        // Calculate flags
        vmi_ti->flags = 0;
        [[maybe_unused]] bool has_virtual_bases = false;
        for (const auto& base : base_classes) {
            if (base.is_virtual) {
                has_virtual_bases = true;
                break;
            }
        }
        // Set diamond flag if multiple inheritance (conservative approach)
        if (base_classes.size() > 1) {
            vmi_ti->flags |= 0x2;  // __diamond_shaped_mask
        }
        
        // Fill base class info
        for (size_t i = 0; i < base_classes.size(); ++i) {
            const auto& base = base_classes[i];
            
            // Get base class type info
            void* base_type_info = nullptr;
            if (base.type_index.index() < gTypeInfo.size()) {
                const TypeInfo& base_type = gTypeInfo[base.type_index.index()];
                const StructTypeInfo* base_info = base_type.getStructInfo();
                if (base_info && base_info->rtti_info) {
                    base_type_info = base_info->rtti_info->itanium_type_info;
                }
            }
            
            vmi_ti->base_info[i].base_type = base_type_info;
            
            // Encode offset and flags
            int64_t offset_flags = static_cast<int64_t>(base.offset) << 8;
            if (base.is_virtual) {
                offset_flags |= 0x1;  // __virtual_mask
            }
            // Assume public inheritance (set bit 1)
            offset_flags |= 0x2;  // __public_mask
            
            vmi_ti->base_info[i].offset_flags = offset_flags;
        }
        
        itanium_vmi_storage.push_back((char*)vmi_ti);
        rtti_info->itanium_type_info = vmi_ti;
        rtti_info->itanium_kind = RTTITypeInfo::ItaniumTypeInfoKind::VMIClassTypeInfo;
    }
}
