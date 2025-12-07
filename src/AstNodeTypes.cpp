#include "AstNodeTypes.h"
#include "ChunkedString.h"
#include "NameMangling.h"
#include <sstream>
#include <set>
#include <functional>
#include <cstdlib>
#include <cstring>

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

TypeInfo& add_enum_type(std::string name) {
    auto& type_info = gTypeInfo.emplace_back(std::move(name), Type::Enum, gTypeInfo.size());
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

    auto& function_pointer_type = gTypeInfo.emplace_back("function_pointer", Type::FunctionPointer, gTypeInfo.size());
    gNativeTypes[Type::FunctionPointer] = &function_pointer_type;

    auto& member_function_pointer_type = gTypeInfo.emplace_back("member_function_pointer", Type::MemberFunctionPointer, gTypeInfo.size());
    gNativeTypes[Type::MemberFunctionPointer] = &member_function_pointer_type;
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
        case Type::Auto:  // Treat auto as integer type for now (generic lambdas)
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
        case Type::Bool:
            return 8;
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
    // C++ integer promotion rules: bool, char, and short promote to int
    switch (type) {
        case Type::Bool:
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
    // This handles bool -> int, char -> int, short -> int
    left = promote_integer_type(left);
    right = promote_integer_type(right);

    // After promotion, check if types are the same
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
        case Type::Enum: result += "enum"; break;
        case Type::FunctionPointer: result += "function_pointer"; break;
        case Type::MemberFunctionPointer: result += "member_function_pointer"; break;
        case Type::MemberObjectPointer: result += "member_object_pointer"; break;
        case Type::Nullptr: result += "nullptr_t"; break;
        case Type::Invalid: result += "invalid"; break;
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

const StructMemberFunction* StructTypeInfo::findCopyAssignmentOperator() const {
    for (const auto& func : member_functions) {
        if (func.is_operator_overload && func.operator_symbol == "=") {
            // Check if this is a copy assignment operator
            // Copy assignment operator has signature: Type& operator=(const Type& other)
            // or Type& operator=(Type& other)
            const auto& func_node = func.function_decl.as<FunctionDeclarationNode>();
            const auto& params = func_node.parameter_nodes();

            // Copy assignment operator has exactly one parameter (by reference)
            if (params.size() == 1) {
                const auto& param_decl = params[0].as<DeclarationNode>();
                const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

                // Check if it's a reference to the same struct type (not rvalue reference)
                if (param_type.is_reference() && !param_type.is_rvalue_reference() && param_type.type() == Type::Struct) {
                    return &func;
                }
            }
        }
    }
    return nullptr;
}

const StructMemberFunction* StructTypeInfo::findMoveAssignmentOperator() const {
    for (const auto& func : member_functions) {
        if (func.is_operator_overload && func.operator_symbol == "=") {
            // Check if this is a move assignment operator
            // Move assignment operator has signature: Type& operator=(Type&& other)
            const auto& func_node = func.function_decl.as<FunctionDeclarationNode>();
            const auto& params = func_node.parameter_nodes();

            // Move assignment operator has exactly one parameter (by rvalue reference)
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

// Finalize struct layout with base classes
void StructTypeInfo::finalizeWithBases() {
    // Step 0: Build vtable first (before layout)
    buildVTable();

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
        if (base.type_index < gTypeInfo.size()) {
            const TypeInfo& base_type = gTypeInfo[base.type_index];
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

        if (base.type_index >= gTypeInfo.size()) {
            continue;  // Invalid base class index
        }

        const TypeInfo& base_type = gTypeInfo[base.type_index];
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
            if (!base.is_virtual && base.type_index < gTypeInfo.size()) {
                const TypeInfo& base_type = gTypeInfo[base.type_index];
                const StructTypeInfo* base_info = base_type.getStructInfo();
                collectVirtualBases(base_info);
            }
        }
    };

    collectVirtualBases(this);

    // Layout virtual bases
    for (auto* vbase : all_virtual_bases) {
        if (vbase->type_index >= gTypeInfo.size()) {
            continue;
        }

        const TypeInfo& base_type = gTypeInfo[vbase->type_index];
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
}

// Build vtable for virtual functions
void StructTypeInfo::buildVTable() {
    // Step 1: Copy base class vtable entries (if any)
    for (const auto& base : base_classes) {
        if (base.type_index >= gTypeInfo.size()) {
            continue;
        }

        const TypeInfo& base_type = gTypeInfo[base.type_index];
        const StructTypeInfo* base_info = base_type.getStructInfo();

        if (base_info) {
            if (base_info->has_vtable) {
                // Copy all base vtable entries
                for (const auto* base_func : base_info->vtable) {
                    if (base_func != nullptr) {  // Safety check
                        vtable.push_back(base_func);
                    }
                }
                has_vtable = true;
            }
        }
    }

    // Step 2: Process this class's virtual functions
    if (member_functions.empty()) {
        return;  // No member functions to process
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
        const std::string& func_name = func.name;

        // Check if this function overrides a base class virtual function
        int override_index = -1;
        const StructMemberFunction* base_func_ptr = nullptr;
        for (size_t i = 0; i < vtable.size(); ++i) {
            const StructMemberFunction* base_func = vtable[i];
            if (base_func != nullptr && base_func->name == func_name) {
                override_index = static_cast<int>(i);
                base_func_ptr = base_func;
                break;
            }
        }

        if (override_index >= 0) {
            // Check if base function is final
            if (base_func_ptr && base_func_ptr->is_final) {
                // Error: attempting to override a final function
                // For now, we'll just ignore this - proper error handling would require
                // access to the parser's error reporting mechanism
                // TODO: Report error "cannot override final function"
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
            // Error: 'override' specified but no base function to override
            // For now, we'll just ignore this - proper error handling would require
            // access to the parser's error reporting mechanism
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
            vtable_sb.append(static_cast<uint64_t>(name.size()));
            vtable_sb.append(name);
        } else {
            // MSVC: ??_7 + name + @@6B@
            // e.g., class "Base" -> "??_7Base@@6B@"
            vtable_sb.append("??_7");
            vtable_sb.append(name);
            vtable_sb.append("@@6B@");
        }
        
        vtable_symbol = vtable_sb.commit();
    }
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
const StructMember* StructTypeInfo::findMemberRecursive(std::string_view member_name) const {
    // First, check own members
    for (const auto& member : members) {
        if (member.name == member_name) {
            return &member;
        }
    }

    // Then, check base class members
    for (const auto& base : base_classes) {
        if (base.type_index >= gTypeInfo.size()) {
            continue;
        }

        const TypeInfo& base_type = gTypeInfo[base.type_index];
        const StructTypeInfo* base_info = base_type.getStructInfo();

        if (base_info) {
            const StructMember* base_member = base_info->findMemberRecursive(member_name);
            if (base_member) {
                // Found in base class - need to return adjusted member
                // Note: We can't modify the base_member, so we use a thread_local static
                static thread_local StructMember adjusted_member("", Type::Void, 0, 0, 0, 0);
                adjusted_member = *base_member;
                adjusted_member.offset += base.offset;  // Adjust offset by base class offset
                return &adjusted_member;
            }
        }
    }

    return nullptr;  // Not found
}

// Build RTTI information for polymorphic classes
void StructTypeInfo::buildRTTI() {
    // Only build RTTI for polymorphic classes (those with vtables)
    if (!has_vtable) {
        return;
    }

    // Create RTTI info with MSVC multi-structure format
    static std::vector<RTTITypeInfo> rtti_storage;
    static std::vector<MSVCTypeDescriptor*> type_descriptor_storage;
    static std::vector<MSVCCompleteObjectLocator> col_storage;
    static std::vector<MSVCClassHierarchyDescriptor> chd_storage;
    static std::vector<MSVCBaseClassArray*> bca_storage;
    static std::vector<MSVCBaseClassDescriptor> bcd_storage;

    // Create mangled and demangled names
    std::string mangled_name = ".?AV" + name + "@@";  // MSVC-style mangling for classes

    // Allocate RTTI info
    rtti_storage.emplace_back(mangled_name.c_str(), name.c_str(), base_classes.size());
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
            if (base.type_index >= gTypeInfo.size()) {
                base_array_storage.push_back(nullptr);
                continue;
            }

            const TypeInfo& base_type = gTypeInfo[base.type_index];
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
    size_t total_bases = base_classes.size() + 1;  // +1 for self
    
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
        
        if (base.type_index >= gTypeInfo.size()) {
            continue;
        }

        const TypeInfo& base_type = gTypeInfo[base.type_index];
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
    static std::vector<ItaniumClassTypeInfo> itanium_class_storage;
    static std::vector<ItaniumSIClassTypeInfo> itanium_si_storage;
    static std::vector<char*> itanium_vmi_storage;  // Variable-sized, use malloc
    static std::vector<char*> itanium_name_storage;
    
    // Create Itanium-style mangled name
    // Itanium uses length-prefixed names: "3Foo" for class Foo
    StringBuilder builder;
    builder.append(name.length()).append(name);
    std::string_view itanium_mangled = builder.commit();
    
    // Allocate permanent storage for the name string (persists for program lifetime)
    char* itanium_name = (char*)malloc(itanium_mangled.length() + 1);
    if (!itanium_name) {
        // Allocation failed - Itanium RTTI won't be available
        return;
    }
    strncpy(itanium_name, itanium_mangled.data(), itanium_mangled.length());
    itanium_name[itanium_mangled.length()] = '\0';  // Ensure null termination
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
        if (base_classes[0].type_index < gTypeInfo.size()) {
            const TypeInfo& base_type = gTypeInfo[base_classes[0].type_index];
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
        bool has_virtual_bases = false;
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
            if (base.type_index < gTypeInfo.size()) {
                const TypeInfo& base_type = gTypeInfo[base.type_index];
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
