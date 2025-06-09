#include "AstNodeTypes.h"

std::deque<TypeInfo> gTypeInfo;
std::unordered_map<std::string, const TypeInfo*> gTypesByName;
std::unordered_map<Type, const TypeInfo*> gNativeTypes;

TypeInfo& add_user_type(std::string name) {
    auto& type_info = gTypeInfo.emplace_back(std::move(name), Type::UserDefined, gTypeInfo.size());
    gTypesByName.emplace(type_info.name_, &type_info);
    return type_info;
}

TypeInfo& add_function_type(std::string name, Type return_type) {
    auto& type_info = gTypeInfo.emplace_back(std::move(name), Type::Function, gTypeInfo.size());
    gTypesByName.emplace(type_info.name_, &type_info);
    return type_info;
}

void initialize_native_types() {
    // Initialize native types if not already done
    if (!gNativeTypes.empty()) {
        return;
    }

    // Add basic native types
    auto& void_type = gTypeInfo.emplace_back("void", Type::Void, gTypeInfo.size());
    gNativeTypes[Type::Void] = &void_type;

    auto& bool_type = gTypeInfo.emplace_back("bool", Type::Bool, gTypeInfo.size());
    gNativeTypes[Type::Bool] = &bool_type;

    auto& char_type = gTypeInfo.emplace_back("char", Type::Char, gTypeInfo.size());
    gNativeTypes[Type::Char] = &char_type;

    auto& uchar_type = gTypeInfo.emplace_back("uchar", Type::UnsignedChar, gTypeInfo.size());
    gNativeTypes[Type::UnsignedChar] = &uchar_type;

    auto& short_type = gTypeInfo.emplace_back("short", Type::Short, gTypeInfo.size());
    gNativeTypes[Type::Short] = &short_type;

    auto& ushort_type = gTypeInfo.emplace_back("ushort", Type::UnsignedShort, gTypeInfo.size());
    gNativeTypes[Type::UnsignedShort] = &ushort_type;

    auto& int_type = gTypeInfo.emplace_back("int", Type::Int, gTypeInfo.size());
    gNativeTypes[Type::Int] = &int_type;

    auto& uint_type = gTypeInfo.emplace_back("uint", Type::UnsignedInt, gTypeInfo.size());
    gNativeTypes[Type::UnsignedInt] = &uint_type;

    auto& long_type = gTypeInfo.emplace_back("long", Type::Long, gTypeInfo.size());
    gNativeTypes[Type::Long] = &long_type;

    auto& ulong_type = gTypeInfo.emplace_back("ulong", Type::UnsignedLong, gTypeInfo.size());
    gNativeTypes[Type::UnsignedLong] = &ulong_type;

    auto& longlong_type = gTypeInfo.emplace_back("longlong", Type::LongLong, gTypeInfo.size());
    gNativeTypes[Type::LongLong] = &longlong_type;

    auto& ulonglong_type = gTypeInfo.emplace_back("ulonglong", Type::UnsignedLongLong, gTypeInfo.size());
    gNativeTypes[Type::UnsignedLongLong] = &ulonglong_type;

    auto& float_type = gTypeInfo.emplace_back("float", Type::Float, gTypeInfo.size());
    gNativeTypes[Type::Float] = &float_type;

    auto& auto_type = gTypeInfo.emplace_back("auto", Type::Auto, gTypeInfo.size());
    gNativeTypes[Type::Auto] = &auto_type;
}

bool is_integer_type(Type type) {
    switch (type) {
        case Type::Char:
        case Type::UnsignedChar:
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

bool is_signed_integer_type(Type type) {
    switch (type) {
        case Type::Char:  // char is signed by default in most implementations
        case Type::Short:
        case Type::Int:
        case Type::Long:
        case Type::LongLong:
            return true;
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
        default:
            return false;
    }
}

int get_integer_rank(Type type) {
    // C++ integer conversion rank (higher rank = larger type)
    switch (type) {
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
        default:
            return 0;
    }
}

int get_type_size_bits(Type type) {
    switch (type) {
        case Type::Char:
        case Type::UnsignedChar:
            return 8;
        case Type::Short:
        case Type::UnsignedShort:
            return 16;
        case Type::Int:
        case Type::UnsignedInt:
            return 32;
        case Type::Long:
        case Type::UnsignedLong:
            return sizeof(long) * 8;  // Platform dependent
        case Type::LongLong:
        case Type::UnsignedLongLong:
            return 64;
        default:
            return 0;
    }
}

Type promote_integer_type(Type type) {
    // C++ integer promotion rules: char and short promote to int
    switch (type) {
        case Type::Char:
        case Type::Short:
            return Type::Int;
        case Type::UnsignedChar:
        case Type::UnsignedShort:
            // If int can represent all values of the original type, promote to int
            // Otherwise promote to unsigned int (but for char/short, int is always sufficient)
            return Type::Int;
        default:
            // Types int and larger don't get promoted
            return type;
    }
}

Type get_common_type(Type left, Type right) {
    // Apply integer promotions first
    left = promote_integer_type(left);
    right = promote_integer_type(right);

    // If both types are the same, return that type
    if (left == right) {
        return left;
    }

    // If one is signed and the other unsigned, and they have the same rank
    int left_rank = get_integer_rank(left);
    int right_rank = get_integer_rank(right);

    if (left_rank == right_rank) {
        // Same rank: unsigned wins
        if (is_unsigned_integer_type(left)) return left;
        if (is_unsigned_integer_type(right)) return right;
    }

    // Different ranks: higher rank wins
    if (left_rank > right_rank) {
        return left;
    } else {
        return right;
    }
}

bool requires_conversion(Type from, Type to) {
    return from != to && is_integer_type(from) && is_integer_type(to);
}
