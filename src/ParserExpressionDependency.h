#pragma once

#include "AstNodeTypes.h"

namespace ParserExpressionDependency {

bool argsHaveDeferredTemplateDependency(
	const ChunkedVector<ASTNode>& args,
	const InlineVector<StringHandle, 4>& current_template_param_names);

bool argTypesAreDeferredTemplateDependent(
	std::span<const TypeSpecifierNode> arg_types,
	const InlineVector<StringHandle, 4>& current_template_param_names);

} // namespace ParserExpressionDependency
