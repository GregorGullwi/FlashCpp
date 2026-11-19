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

// [expr.prim.lambda.closure]/8 requires a capture-default-free lambda, not just
// an empty generated member list: an unused [=] or [&] still has a capture
// default and therefore no conversion to pointer-to-function.
inline bool isCapturelessLambdaDeclaration(const LambdaExpressionNode& lambda) {
	return lambda.captures().empty() && !lambda.has_capture_default();
}

// Implemented by SemanticAnalysis; the header-only conversion selector uses it
// to obtain the call operator's actual return type after substituting the
// function-pointer target's parameter types into a generic lambda.
std::optional<TypeSpecifierNode> tryDeduceLambdaConversionReturnType(
	const LambdaExpressionNode& lambda, std::span<const TypeSpecifierNode> parameter_types,
	std::span<const std::optional<TypeSpecifierNode>> explicit_template_arguments);
std::optional<bool> tryEvaluateLambdaConversionConstraints(
	const LambdaExpressionNode& lambda,
	std::span<const std::optional<TypeSpecifierNode>> explicit_template_arguments);

inline std::optional<LambdaStructSignature> getFunctionSignatureFromLambdaStruct(const StructTypeInfo& struct_info) {
	if (!isLambdaClosureStruct(struct_info)) {
		return std::nullopt;
	}

	for (const auto& member_func : struct_info.member_functions) {
		if (member_func.getName() == StringTable::getOrInternStringHandle("operator()") &&
			member_func.function_decl.is<FunctionDeclarationNode>()) {
			const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
			TypeSpecifierNode return_type = func_decl.decl_node().type_specifier_node();
			if (isPlaceholderAutoType(return_type.type())) {
				const SizeInBits void_size_bits{get_type_size_bits(TypeCategory::Void)};
				return_type = TypeSpecifierNode(TypeCategory::Void, TypeQualifier::None, void_size_bits, func_decl.decl_node().identifier_token(), CVQualifier::None);
			}

			LambdaStructSignature sig{return_type, {}};
			for (const auto& param_node : func_decl.parameter_nodes()) {
				if (param_node.is<DeclarationNode>()) {
					sig.param_types.push_back(param_node.as<DeclarationNode>().type_specifier_node());
				}
			}
			return sig;
		}
	}

	return std::nullopt;
}
