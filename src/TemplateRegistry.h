#pragma once

#include "AstNodeTypes.h"
#include "ChunkedString.h"
#include "Lexer.h"  // For TokenPosition
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>

// Template instantiation key - uniquely identifies a template instantiation
struct TemplateInstantiationKey {
	std::string template_name;
	std::vector<Type> type_arguments;  // For type parameters
	std::vector<int64_t> value_arguments;  // For non-type parameters
	std::vector<std::string> template_arguments;  // For template template parameters
	
	bool operator==(const TemplateInstantiationKey& other) const {
		return template_name == other.template_name &&
		       type_arguments == other.type_arguments &&
		       value_arguments == other.value_arguments &&
		       template_arguments == other.template_arguments;
	}
};

// Hash function for TemplateInstantiationKey
struct TemplateInstantiationKeyHash {
	std::size_t operator()(const TemplateInstantiationKey& key) const {
		std::size_t hash = std::hash<std::string>{}(key.template_name);
		for (const auto& type : key.type_arguments) {
			hash ^= std::hash<int>{}(static_cast<int>(type)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		for (const auto& value : key.value_arguments) {
			hash ^= std::hash<int64_t>{}(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		for (const auto& tmpl : key.template_arguments) {
			hash ^= std::hash<std::string>{}(tmpl) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		return hash;
	}
};

// Template argument - can be a type, a value, or a template
struct TemplateArgument {
	enum class Kind {
		Type,
		Value,
		Template   // For template template parameters
	};
	
	Kind kind;
	Type type_value;  // For type arguments
	int64_t int_value;  // For non-type integer arguments
	std::string template_name;  // For template template arguments (name of the template)
	
	static TemplateArgument makeType(Type t) {
		TemplateArgument arg;
		arg.kind = Kind::Type;
		arg.type_value = t;
		return arg;
	}
	
	static TemplateArgument makeValue(int64_t v) {
		TemplateArgument arg;
		arg.kind = Kind::Value;
		arg.int_value = v;
		return arg;
	}
	
	static TemplateArgument makeTemplate(std::string_view template_name) {
		TemplateArgument arg;
		arg.kind = Kind::Template;
		arg.template_name = std::string(template_name);
		return arg;
	}
};

// Out-of-line template member function definition
struct OutOfLineMemberFunction {
	std::vector<ASTNode> template_params;  // Template parameters (e.g., <typename T>)
	ASTNode function_node;                  // FunctionDeclarationNode
	TokenPosition body_start;               // Position of function body for re-parsing
	std::vector<std::string_view> template_param_names;  // Names of template parameters
};

// Key for template specializations
struct SpecializationKey {
	std::string template_name;
	std::vector<Type> template_args;

	bool operator==(const SpecializationKey& other) const {
		return template_name == other.template_name && template_args == other.template_args;
	}
};

// Hash function for SpecializationKey
struct SpecializationKeyHash {
	size_t operator()(const SpecializationKey& key) const {
		size_t hash = std::hash<std::string>{}(key.template_name);
		for (const auto& arg : key.template_args) {
			hash ^= std::hash<int>{}(static_cast<int>(arg)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		return hash;
	}
};

// Template registry - stores template declarations and manages instantiations
class TemplateRegistry {
public:
	// Register a template function declaration
	void registerTemplate(std::string_view name, ASTNode template_node) {
		std::string key(name);
		templates_[key] = template_node;
	}
	
	// Look up a template by name
	std::optional<ASTNode> lookupTemplate(std::string_view name) const {
		auto it = templates_.find(std::string(name));
		if (it != templates_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Check if a template instantiation already exists
	bool hasInstantiation(const TemplateInstantiationKey& key) const {
		return instantiations_.find(key) != instantiations_.end();
	}
	
	// Get an existing instantiation
	std::optional<ASTNode> getInstantiation(const TemplateInstantiationKey& key) const {
		auto it = instantiations_.find(key);
		if (it != instantiations_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Register a new instantiation
	void registerInstantiation(const TemplateInstantiationKey& key, ASTNode instantiated_node) {
		instantiations_[key] = instantiated_node;
	}
	
	// Helper to convert Type to string for mangling
	static std::string_view typeToString(Type type) {
		switch (type) {
			case Type::Int: return "int";
			case Type::Float: return "float";
			case Type::Double: return "double";
			case Type::Bool: return "bool";
			case Type::Char: return "char";
			case Type::Long: return "long";
			case Type::LongLong: return "longlong";
			case Type::Short: return "short";
			case Type::UnsignedInt: return "uint";
			case Type::UnsignedLong: return "ulong";
			case Type::UnsignedLongLong: return "ulonglong";
			case Type::UnsignedShort: return "ushort";
			case Type::UnsignedChar: return "uchar";
			default: return "unknown";
		}
	}

	// Helper to convert string to Type for parsing mangled names
	static Type stringToType(std::string_view str) {
		if (str == "int") return Type::Int;
		if (str == "float") return Type::Float;
		if (str == "double") return Type::Double;
		if (str == "bool") return Type::Bool;
		if (str == "char") return Type::Char;
		if (str == "long") return Type::Long;
		if (str == "longlong") return Type::LongLong;
		if (str == "short") return Type::Short;
		if (str == "uint") return Type::UnsignedInt;
		if (str == "ulong") return Type::UnsignedLong;
		if (str == "ulonglong") return Type::UnsignedLongLong;
		if (str == "ushort") return Type::UnsignedShort;
		if (str == "uchar") return Type::UnsignedChar;
		return Type::Invalid;
	}

	// Generate a mangled name for a template instantiation
	// Example: max<int> -> max_int, max<int, 5> -> max_int_5, max<vector> -> max_vector
	static std::string_view mangleTemplateName(std::string_view base_name, const std::vector<TemplateArgument>& args) {
		StringBuilder& mangled = StringBuilder().append(base_name).append("_");

		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0) mangled.append("_");

			if (args[i].kind == TemplateArgument::Kind::Type) {
				mangled.append(typeToString(args[i].type_value));
			} else if (args[i].kind == TemplateArgument::Kind::Value) {
				mangled.append(args[i].int_value);
			} else if (args[i].kind == TemplateArgument::Kind::Template) {
				// For template template arguments, use the template name
				mangled.append(args[i].template_name);
			}
		}

		return mangled.commit();
	}

	// Register an out-of-line template member function definition
	void registerOutOfLineMember(std::string_view class_name, OutOfLineMemberFunction member_func) {
		std::string key(class_name);
		out_of_line_members_[key].push_back(std::move(member_func));
	}

	// Get all out-of-line member functions for a class template
	std::vector<OutOfLineMemberFunction> getOutOfLineMembers(std::string_view class_name) const {
		auto it = out_of_line_members_.find(std::string(class_name));
		if (it != out_of_line_members_.end()) {
			return it->second;
		}
		return {};
	}

	// Register a template specialization
	void registerSpecialization(std::string_view template_name, const std::vector<Type>& template_args, ASTNode specialized_node) {
		SpecializationKey key{std::string(template_name), template_args};
		specializations_[key] = specialized_node;
	}

	// Look up a template specialization
	std::optional<ASTNode> lookupSpecialization(std::string_view template_name, const std::vector<Type>& template_args) const {
		SpecializationKey key{std::string(template_name), template_args};
		auto it = specializations_.find(key);
		if (it != specializations_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Clear all templates and instantiations
	void clear() {
		templates_.clear();
		instantiations_.clear();
		out_of_line_members_.clear();
		specializations_.clear();
	}

private:
	// Map from template name to template declaration node
	std::unordered_map<std::string, ASTNode> templates_;

	// Map from instantiation key to instantiated function node
	std::unordered_map<TemplateInstantiationKey, ASTNode, TemplateInstantiationKeyHash> instantiations_;

	// Map from class name to out-of-line member function definitions
	std::unordered_map<std::string, std::vector<OutOfLineMemberFunction>> out_of_line_members_;

	// Map from (template_name, template_args) to specialized class node
	std::unordered_map<SpecializationKey, ASTNode, SpecializationKeyHash> specializations_;
};

// Global template registry
extern TemplateRegistry gTemplateRegistry;

