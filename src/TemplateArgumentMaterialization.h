#pragma once

#include "Parser.h"

inline std::string_view getTemplateArgumentBuiltinTokenText(const TemplateTypeArg& arg) {
	switch (arg.typeEnum()) {
	case TypeCategory::Void: return "void";
	case TypeCategory::Bool: return "bool";
	case TypeCategory::Char: return "char";
	case TypeCategory::UnsignedChar: return "unsigned char";
	case TypeCategory::Short: return "short";
	case TypeCategory::UnsignedShort: return "unsigned short";
	case TypeCategory::Int: return "int";
	case TypeCategory::UnsignedInt: return "unsigned int";
	case TypeCategory::Long: return "long";
	case TypeCategory::UnsignedLong: return "unsigned long";
	case TypeCategory::LongLong: return "long long";
	case TypeCategory::UnsignedLongLong: return "unsigned long long";
	case TypeCategory::Float: return "float";
	case TypeCategory::Double: return "double";
	case TypeCategory::LongDouble: return "long double";
	default: return {};
	}
}

inline ASTNode materializeDependentTemplateArgumentNode(
	const TemplateTypeArg& arg,
	const Token& source_token) {
	Token dep_token(
		Token::Type::Identifier,
		arg.dependent_name.isValid() ? arg.dependent_name.view() : std::string_view{},
		source_token.line(),
		source_token.column(),
		source_token.file_index());
	ExpressionNode& dep_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
		TemplateParameterReferenceNode(arg.dependent_name, dep_token));
	return ASTNode(&dep_expr);
}

inline ASTNode materializeValueTemplateArgumentNode(
	const TemplateTypeArg& arg,
	const Token& source_token) {
	if (arg.typeEnum() == TypeCategory::Bool) {
		Token bool_token(
			Token::Type::Keyword,
			arg.value != 0 ? "true"sv : "false"sv,
			source_token.line(),
			source_token.column(),
			source_token.file_index());
		ExpressionNode& bool_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
			BoolLiteralNode(bool_token, arg.value != 0));
		return ASTNode(&bool_expr);
	}

	StringBuilder text_builder;
	std::string_view literal_text = text_builder.append(arg.value).commit();
	TypeCategory literal_type = arg.typeEnum() == TypeCategory::Invalid
		? TypeCategory::Int
		: arg.typeEnum();
	Token literal_token(
		Token::Type::Literal,
		literal_text,
		source_token.line(),
		source_token.column(),
		source_token.file_index());
	ExpressionNode& literal_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
		NumericLiteralNode(
			literal_token,
			static_cast<unsigned long long>(arg.value),
			literal_type,
			TypeQualifier::None,
			get_type_size_bits(literal_type)));
	return ASTNode(&literal_expr);
}

inline Token makeTemplateArgumentTypeToken(
	const TemplateTypeArg& arg,
	const Token& source_token) {
	if (const TypeInfo* type_info = tryGetTypeInfo(arg.type_index)) {
		std::string_view type_name = StringTable::getStringView(type_info->name());
		if (!type_name.empty()) {
			return Token(
				Token::Type::Identifier,
				type_name,
				source_token.line(),
				source_token.column(),
				source_token.file_index());
		}
	} else if (std::string_view builtin_name = getTemplateArgumentBuiltinTokenText(arg);
			   !builtin_name.empty()) {
		return Token(
			Token::Type::Keyword,
			builtin_name,
			source_token.line(),
			source_token.column(),
			source_token.file_index());
	}

	return source_token;
}

inline int computeTemplateArgumentTypeSizeBits(TypeIndex type_index) {
	int size_in_bits = get_type_size_bits(type_index.category());
	if (size_in_bits != 0) {
		return size_in_bits;
	}

	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	if (!type_info) {
		return 0;
	}

	size_in_bits = type_info->sizeInBits().value;
	if (size_in_bits != 0 || !type_info->isStruct()) {
		return size_in_bits;
	}

	const StructTypeInfo* struct_info = type_info->getStructInfo();
	return struct_info ? struct_info->sizeInBits().value : 0;
}

inline std::vector<ASTNode> materializeTemplateArgumentNodes(
	std::span<const TemplateTypeArg> template_args,
	const Token& source_token) {
	std::vector<ASTNode> result;
	result.reserve(template_args.size());

	for (const TemplateTypeArg& arg : template_args) {
		if (arg.is_dependent && arg.dependent_name.isValid()) {
			result.push_back(materializeDependentTemplateArgumentNode(arg, source_token));
			continue;
		}

		if (arg.is_value) {
			result.push_back(materializeValueTemplateArgumentNode(arg, source_token));
			continue;
		}

		TypeSpecifierNode& type_node = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
			arg.type_index.withCategory(arg.typeEnum()),
			get_type_size_bits(arg.typeEnum()),
			makeTemplateArgumentTypeToken(arg, source_token),
			arg.cv_qualifier,
			arg.ref_qualifier);
		type_node.set_pack_expansion(arg.is_pack);
		for (size_t pointer_index = 0; pointer_index < arg.pointer_depth; ++pointer_index) {
			CVQualifier pointer_cv = pointer_index < arg.pointer_cv_qualifiers.size()
				? arg.pointer_cv_qualifiers[pointer_index]
				: CVQualifier::None;
			type_node.add_pointer_level(pointer_cv);
		}
		result.push_back(ASTNode(&type_node));
	}

	return result;
}
