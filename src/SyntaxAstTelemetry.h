#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <typeindex>

// Coarse syntax-AST families for migration arena telemetry (architecture boundary 1).
enum class SyntaxAstFamily : uint8_t {
	Declaration,
	Statement,
	Expression,
	TypeSpecifier,
	Template,
	Other,
	Count,
};

inline std::string_view syntaxAstFamilyLabel(SyntaxAstFamily family) {
	switch (family) {
	case SyntaxAstFamily::Declaration:
		return "declaration";
	case SyntaxAstFamily::Statement:
		return "statement";
	case SyntaxAstFamily::Expression:
		return "expression";
	case SyntaxAstFamily::TypeSpecifier:
		return "type-specifier";
	case SyntaxAstFamily::Template:
		return "template";
	case SyntaxAstFamily::Other:
		return "other";
	case SyntaxAstFamily::Count:
		break;
	}
	return "unknown";
}

inline std::string_view demangledTypeLeafName(std::string_view type_name) {
	if (type_name.starts_with("class ")) {
		type_name.remove_prefix(sizeof("class ") - 1);
	} else if (type_name.starts_with("struct ")) {
		type_name.remove_prefix(sizeof("struct ") - 1);
	} else if (type_name.starts_with("enum ")) {
		type_name.remove_prefix(sizeof("enum ") - 1);
	} else if (type_name.starts_with("union ")) {
		type_name.remove_prefix(sizeof("union ") - 1);
	}

	const std::size_t scope_separator = type_name.rfind("::");
	if (scope_separator != std::string_view::npos) {
		type_name.remove_prefix(scope_separator + 2);
	}

	std::size_t pos = 0;
	while (pos < type_name.size() && type_name[pos] >= '0' && type_name[pos] <= '9') {
		++pos;
	}
	return type_name.substr(pos);
}

inline SyntaxAstFamily classifySyntaxAstFamily(std::type_index type) {
	const std::string_view name = demangledTypeLeafName(type.name());
	if (name.empty()) {
		return SyntaxAstFamily::Other;
	}
	if (name.starts_with("Template")) {
		return SyntaxAstFamily::Template;
	}
	if (name.ends_with("StatementNode") || name == "BlockNode") {
		return SyntaxAstFamily::Statement;
	}
	if (name.ends_with("ExpressionNode") || name.ends_with("ExprNode")) {
		return SyntaxAstFamily::Expression;
	}
	if (name.ends_with("DeclarationNode") || name == "DeclarationNode") {
		return SyntaxAstFamily::Declaration;
	}
	if (name.ends_with("TypeSpecifierNode") || name.ends_with("TypeNode") ||
		name == "PointerNode" || name == "ReferenceNode" || name == "ArrayTypeNode") {
		return SyntaxAstFamily::TypeSpecifier;
	}
	return SyntaxAstFamily::Other;
}

template<typename Storage>
std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)> countSyntaxAstFamilies(
	const Storage& storage) {
	std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)> counts{};
	storage.visit([&](void*, std::type_index type) {
		const SyntaxAstFamily family = classifySyntaxAstFamily(type);
		++counts[static_cast<std::size_t>(family)];
	});
	return counts;
}
