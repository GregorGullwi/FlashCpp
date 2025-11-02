#pragma once

#include "AstNodeTypes.h"
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
	
	bool operator==(const TemplateInstantiationKey& other) const {
		return template_name == other.template_name &&
		       type_arguments == other.type_arguments &&
		       value_arguments == other.value_arguments;
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
		return hash;
	}
};

// Template argument - can be a type or a value
struct TemplateArgument {
	enum class Kind {
		Type,
		Value
	};
	
	Kind kind;
	Type type_value;  // For type arguments
	int64_t int_value;  // For non-type integer arguments
	
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
	
	// Generate a mangled name for a template instantiation
	// Example: max<int> -> max_int, max<int, 5> -> max_int_5
	static std::string mangleTemplateName(std::string_view base_name, const std::vector<TemplateArgument>& args) {
		std::string mangled(base_name);
		mangled += "_";
		
		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0) mangled += "_";
			
			if (args[i].kind == TemplateArgument::Kind::Type) {
				mangled += typeToString(args[i].type_value);
			} else {
				mangled += std::to_string(args[i].int_value);
			}
		}
		
		return mangled;
	}
	
	// Clear all templates and instantiations
	void clear() {
		templates_.clear();
		instantiations_.clear();
	}
	
private:
	// Helper to convert Type to string for mangling
	static std::string typeToString(Type type) {
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
	
	// Map from template name to template declaration node
	std::unordered_map<std::string, ASTNode> templates_;
	
	// Map from instantiation key to instantiated function node
	std::unordered_map<TemplateInstantiationKey, ASTNode, TemplateInstantiationKeyHash> instantiations_;
};

// Global template registry
extern TemplateRegistry gTemplateRegistry;

