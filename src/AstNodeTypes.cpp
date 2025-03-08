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
