#include "AstNodeTypes.h"
#include <sstream>

std::deque<TypeInfo> gTypeInfo;
std::unordered_map<std::string, const TypeInfo*, StringHash, StringEqual> gTypesByName;
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

TypeInfo& add_struct_type(std::string name) {
    auto& type_info = gTypeInfo.emplace_back(std::move(name), Type::Struct, gTypeInfo.size());
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

    auto& double_type = gTypeInfo.emplace_back("double", Type::Double, gTypeInfo.size());
    gNativeTypes[Type::Double] = &double_type;

    auto& longdouble_type = gTypeInfo.emplace_back("longdouble", Type::LongDouble, gTypeInfo.size());
    gNativeTypes[Type::LongDouble] = &longdouble_type;

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
        case Type::Float:
            return 32;
        case Type::Double:
            return 64;
        case Type::LongDouble:
            return 80;  // x87 extended precision
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

Type promote_floating_point_type(Type type) {
    // Floating-point promotions: float promotes to double in some contexts
    // For now, keep types as-is (no automatic promotion)
    return type;
}

Type get_common_type(Type left, Type right) {
    // If both types are the same, return that type
    if (left == right) {
        return left;
    }

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
    left = promote_integer_type(left);
    right = promote_integer_type(right);

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

// Helper function to get base type string
static std::string type_to_string(Type type, TypeQualifier qualifier) {
    std::string result;

    // Add sign qualifier if present
    if (qualifier == TypeQualifier::Unsigned) {
        result += "unsigned ";
    } else if (qualifier == TypeQualifier::Signed) {
        result += "signed ";
    }

    // Add base type
    switch (type) {
        case Type::Void: result += "void"; break;
        case Type::Bool: result += "bool"; break;
        case Type::Char: result += "char"; break;
        case Type::UnsignedChar: result += "unsigned char"; break;
        case Type::Short: result += "short"; break;
        case Type::UnsignedShort: result += "unsigned short"; break;
        case Type::Int: result += "int"; break;
        case Type::UnsignedInt: result += "unsigned int"; break;
        case Type::Long: result += "long"; break;
        case Type::UnsignedLong: result += "unsigned long"; break;
        case Type::LongLong: result += "long long"; break;
        case Type::UnsignedLongLong: result += "unsigned long long"; break;
        case Type::Float: result += "float"; break;
        case Type::Double: result += "double"; break;
        case Type::LongDouble: result += "long double"; break;
        case Type::UserDefined: result += "user_defined"; break;
        case Type::Auto: result += "auto"; break;
        case Type::Function: result += "function"; break;
        case Type::Struct: result += "struct"; break;
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

    // Add base type
    oss << type_to_string(type_, qualifier_);

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
            // Check if it's a default constructor (no parameters)
            const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
            if (ctor_node.parameter_nodes().empty()) {
                return &func;
            }
        }
    }
    return nullptr;
}

const StructMemberFunction* StructTypeInfo::findCopyConstructor() const {
    for (const auto& func : member_functions) {
        if (func.is_constructor) {
            const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
            const auto& params = ctor_node.parameter_nodes();

            // Copy constructor has exactly one parameter of the same type (by reference)
            if (params.size() == 1) {
                const auto& param_decl = params[0].as<DeclarationNode>();
                const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

                // Check if it's a reference to the same struct type
                if (param_type.is_reference() && param_type.type() == Type::Struct) {
                    // TODO: Also check that the type_index matches this struct
                    return &func;
                }
            }
        }
    }
    return nullptr;
}

const StructMemberFunction* StructTypeInfo::findMoveConstructor() const {
    for (const auto& func : member_functions) {
        if (func.is_constructor) {
            const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
            const auto& params = ctor_node.parameter_nodes();

            // Move constructor has exactly one parameter of the same type (by rvalue reference)
            if (params.size() == 1) {
                const auto& param_decl = params[0].as<DeclarationNode>();
                const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

                // Check if it's an rvalue reference to the same struct type
                if (param_type.is_rvalue_reference() && param_type.type() == Type::Struct) {
                    return &func;
                }
            }
        }
    }
    return nullptr;
}
