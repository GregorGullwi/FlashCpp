#pragma once

#include "AstNodeTypes.h"
#include "StringTable.h"

struct LambdaStructSignature {
	TypeSpecifierNode return_type;
	std::vector<TypeSpecifierNode> param_types;
};

inline bool isLambdaClosureStruct(const StructTypeInfo& struct_info) {
	return struct_info.members.empty() &&
		   StringTable::getStringView(struct_info.getName()).starts_with("__lambda_");
}

inline std::optional<LambdaStructSignature> getFunctionSignatureFromLambdaStruct(const StructTypeInfo& struct_info) {
	if (!isLambdaClosureStruct(struct_info)) {
		return std::nullopt;
	}

	for (const auto& member_func : struct_info.member_functions) {
		if (member_func.getName() == StringTable::getOrInternStringHandle("operator()") &&
			member_func.function_decl.is<FunctionDeclarationNode>()) {
			const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
			TypeSpecifierNode return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
			if (isPlaceholderAutoType(return_type.type())) {
				return_type = TypeSpecifierNode(TypeCategory::Void, TypeQualifier::None, get_type_size_bits(TypeCategory::Void), func_decl.decl_node().identifier_token(), CVQualifier::None);
			}

			LambdaStructSignature sig{return_type, {}};
			for (const auto& param_node : func_decl.parameter_nodes()) {
				if (param_node.is<DeclarationNode>()) {
					sig.param_types.push_back(param_node.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
				}
			}
			return sig;
		}
	}

	return std::nullopt;
}
