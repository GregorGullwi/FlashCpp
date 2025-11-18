#pragma once

#include "AstNodeTypes.h"
#include "ChunkedString.h"
#include "Lexer.h"  // For TokenPosition
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>

// Transparent string hash for heterogeneous lookup (C++20)
// Allows unordered_map<string, T, TransparentStringHash, equal_to<>> to lookup with string_view
struct TransparentStringHash {
	using is_transparent = void;
	using hash_type = std::hash<std::string_view>;
	
	size_t operator()(const char* str) const { return hash_type{}(str); }
	size_t operator()(std::string_view str) const { return hash_type{}(str); }
	size_t operator()(const std::string& str) const { return hash_type{}(str); }
};

// Full type representation for template arguments
// Captures base type, references, pointers, cv-qualifiers, etc.
// Can also represent non-type template parameters (values)
struct TemplateTypeArg {
	Type base_type;
	TypeIndex type_index;  // For user-defined types
	bool is_reference;
	bool is_rvalue_reference;
	size_t pointer_depth;  // 0 = not pointer, 1 = T*, 2 = T**, etc.
	CVQualifier cv_qualifier;  // const/volatile qualifiers

	// For non-type template parameters
	bool is_value;  // true if this represents a value instead of a type
	int64_t value;  // the value for non-type parameters

	// For variadic templates (parameter packs)
	bool is_pack;  // true if this represents a parameter pack (typename... Args)
	
	TemplateTypeArg()
		: base_type(Type::Invalid)
		, type_index(0)
		, is_reference(false)
		, is_rvalue_reference(false)
		, pointer_depth(0)
		, cv_qualifier(CVQualifier::None)
		, is_value(false)
		, value(0)
		, is_pack(false) {}

	explicit TemplateTypeArg(const TypeSpecifierNode& type_spec)
		: base_type(type_spec.type())
		, type_index(type_spec.type_index())
		, is_reference(type_spec.is_reference())
		, is_rvalue_reference(type_spec.is_rvalue_reference())
		, pointer_depth(type_spec.pointer_depth())
		, cv_qualifier(type_spec.cv_qualifier())
		, is_value(false)
		, value(0)
		, is_pack(false) {}

	// Constructor for non-type template parameters
	explicit TemplateTypeArg(int64_t val)
		: base_type(Type::Int)  // Default to int for values
		, type_index(0)
		, is_reference(false)
		, is_rvalue_reference(false)
		, pointer_depth(0)
		, cv_qualifier(CVQualifier::None)
		, is_value(true)
		, value(val)
		, is_pack(false) {}
	
	bool operator==(const TemplateTypeArg& other) const {
		return base_type == other.base_type &&
		       type_index == other.type_index &&
		       is_reference == other.is_reference &&
		       is_rvalue_reference == other.is_rvalue_reference &&
		       pointer_depth == other.pointer_depth &&
		       cv_qualifier == other.cv_qualifier &&
		       is_value == other.is_value &&
		       (!is_value || value == other.value) &&  // Only compare value if it's a value
		       is_pack == other.is_pack;
	}

	// Helper method to check if this is a parameter pack
	bool isParameterPack() const {
		return is_pack;
	}
	
	// Get string representation for mangling
	std::string toString() const {
		if (is_value) {
			// For values, just return the value as string
			return std::to_string(value);
		}

		std::string result;

		// Add const/volatile prefix if present
		if ((static_cast<uint8_t>(cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0) {
			result += "C";  // const
		}
		if ((static_cast<uint8_t>(cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0) {
			result += "V";  // volatile
		}

		// Add base type name
		switch (base_type) {
			case Type::Int: result += "int"; break;
			case Type::Float: result += "float"; break;
			case Type::Double: result += "double"; break;
			case Type::Bool: result += "bool"; break;
			case Type::Char: result += "char"; break;
			case Type::Long: result += "long"; break;
			case Type::LongLong: result += "longlong"; break;
			case Type::Short: result += "short"; break;
			case Type::UnsignedInt: result += "uint"; break;
			case Type::UnsignedLong: result += "ulong"; break;
			case Type::UnsignedLongLong: result += "ulonglong"; break;
			case Type::UnsignedShort: result += "ushort"; break;
			case Type::UnsignedChar: result += "uchar"; break;
			default: result += "unknown"; break;
		}

		// Add pointer markers
		for (size_t i = 0; i < pointer_depth; ++i) {
			result += "P";  // P for pointer
		}

		// Add reference markers
		if (is_rvalue_reference) {
			result += "RR";  // rvalue reference
		} else if (is_reference) {
			result += "R";   // lvalue reference
		}

		return result;
	}
};

// Hash function for TemplateTypeArg
struct TemplateTypeArgHash {
	size_t operator()(const TemplateTypeArg& arg) const {
		size_t hash = std::hash<int>{}(static_cast<int>(arg.base_type));
		hash ^= std::hash<size_t>{}(arg.type_index) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(arg.is_reference) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(arg.is_rvalue_reference) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<size_t>{}(arg.pointer_depth) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(arg.cv_qualifier)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(arg.is_value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		if (arg.is_value) {
			hash ^= std::hash<int64_t>{}(arg.value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		hash ^= std::hash<bool>{}(arg.is_pack) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		return hash;
	}
};

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
	Type type_value;  // For type arguments (legacy - enum only)
	int64_t int_value;  // For non-type integer arguments
	std::string template_name;  // For template template arguments (name of the template)
	std::optional<TypeSpecifierNode> type_specifier;  // Full type info including references, pointers, CV qualifiers
	
	static TemplateArgument makeType(Type t) {
		TemplateArgument arg;
		arg.kind = Kind::Type;
		arg.type_value = t;
		return arg;
	}
	
	static TemplateArgument makeTypeSpecifier(const TypeSpecifierNode& type_spec) {
		TemplateArgument arg;
		arg.kind = Kind::Type;
		arg.type_value = type_spec.type();  // Keep legacy field populated
		arg.type_specifier = type_spec;
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

// Template specialization pattern - represents a pattern like T&, T*, const T, etc.
struct TemplatePattern {
	std::vector<ASTNode> template_params;  // Template parameters (e.g., typename T)
	std::vector<TemplateTypeArg> pattern_args;  // Pattern like T&, T*, etc.
	ASTNode specialized_node;  // The AST node for the specialized template
	
	// Check if this pattern matches the given concrete arguments
	// For example, pattern T& matches int&, float&, etc.
	// Returns true if match succeeds, and fills param_substitutions with T->int mapping
	bool matches(const std::vector<TemplateTypeArg>& concrete_args, 
	             std::unordered_map<std::string, TemplateTypeArg>& param_substitutions) const
	{
		// Pattern and concrete args must have the same number of arguments
		if (pattern_args.size() != concrete_args.size()) {
			return false;
		}
	
		param_substitutions.clear();
	
		// Check each pattern argument against the corresponding concrete argument
		for (size_t i = 0; i < pattern_args.size(); ++i) {
			const TemplateTypeArg& pattern_arg = pattern_args[i];
			const TemplateTypeArg& concrete_arg = concrete_args[i];
		
			// Find the template parameter name for this pattern position
			// The pattern_arg contains the type from the pattern (e.g., T for pattern T&)
			// We need to check if the base types match and the modifiers match
		
			// Pattern matching rules:
			// 1. If pattern is "T&" and concrete is "int&", then T=int (reference match)
			// 2. If pattern is "T&&" and concrete is "int&&", then T=int (rvalue reference match)
			// 3. If pattern is "T*" and concrete is "int*", then T=int (pointer match)
			// 4. If pattern is "T**" and concrete is "int**", then T=int (double pointer match)
			// 5. If pattern is "const T" and concrete is "const int", then T=int (const match)
			// 6. If pattern is "T" and concrete is "int", then T=int (exact match)
			// 7. Reference/pointer/const modifiers must match
		
			// Check if modifiers match
			if (pattern_arg.is_reference != concrete_arg.is_reference) {
				return false;
			}
			if (pattern_arg.is_rvalue_reference != concrete_arg.is_rvalue_reference) {
				return false;
			}
			if (pattern_arg.pointer_depth != concrete_arg.pointer_depth) {
				return false;
			}
			if (pattern_arg.cv_qualifier != concrete_arg.cv_qualifier) {
				return false;
			}
		
			// For pattern matching, we need to extract the template parameter name
			// The pattern_arg.base_type is UserDefined and represents the template parameter
			// We need to get the parameter name from template_params
		
			if (i >= template_params.size()) {
				return false;  // Mismatch in parameter count
			}
		
			const ASTNode& param_node = template_params[i];
			if (!param_node.is<TemplateParameterNode>()) {
				return false;
			}
		
			const TemplateParameterNode& template_param = param_node.as<TemplateParameterNode>();
			std::string param_name(template_param.name());
		
			// Check if we've already seen this parameter
			auto it = param_substitutions.find(param_name);
			if (it != param_substitutions.end()) {
				// Parameter already bound - check consistency
				if (!(it->second == concrete_arg)) {
					return false;  // Inconsistent substitution
				}
			} else {
				// Bind this parameter to the concrete type
				param_substitutions[param_name] = concrete_arg;
			}
		}
	
		return true;  // All patterns matched
	}
	
	// Calculate specificity score (higher = more specialized)
	// T = 0, T& = 1, T* = 1, const T = 1, const T& = 2, etc.
	int specificity() const
	{
		int score = 0;
	
		for (const auto& arg : pattern_args) {
			// Base score: any pattern parameter = 0
		
			// Pointer modifier adds specificity (T* is more specific than T)
			score += arg.pointer_depth;  // T* = +1, T** = +2, etc.
		
			// Reference modifier adds specificity
			if (arg.is_reference) {
				score += 1;  // T& is more specific than T
			}
			if (arg.is_rvalue_reference) {
				score += 1;  // T&& is more specific than T
			}
		
			// CV-qualifiers add specificity
			if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0) {
				score += 1;  // const T is more specific than T
			}
			if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0) {
				score += 1;  // volatile T is more specific than T
			}
		}
	
		return score;
	}
};

// Key for template specializations
struct SpecializationKey {
	std::string template_name;
	std::vector<TemplateTypeArg> template_args;

	bool operator==(const SpecializationKey& other) const {
		return template_name == other.template_name && template_args == other.template_args;
	}
};

// Hash function for SpecializationKey
struct SpecializationKeyHash {
	size_t operator()(const SpecializationKey& key) const {
		size_t hash = std::hash<std::string>{}(key.template_name);
		TemplateTypeArgHash arg_hasher;
		for (const auto& arg : key.template_args) {
			hash ^= arg_hasher(arg) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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

	// Register template parameter names for a template
	void registerTemplateParameters(std::string_view name, const std::vector<std::string_view>& param_names) {
		std::string key(name);
		template_parameters_[key] = std::vector<std::string_view>(param_names.begin(), param_names.end());
	}

	// Register an alias template: template<typename T> using Ptr = T*;
	void register_alias_template(std::string_view name, ASTNode alias_node) {
		std::string key(name);
		alias_templates_[key] = alias_node;
	}

	// Look up an alias template by name
	std::optional<ASTNode> lookup_alias_template(std::string_view name) const {
		// Heterogeneous lookup - string_view accepted directly without temporary string allocation
		auto it = alias_templates_.find(name);
		if (it != alias_templates_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Get template parameter names for a template
	std::vector<std::string_view> getTemplateParameters(std::string_view name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = template_parameters_.find(name);
		if (it != template_parameters_.end()) {
			return it->second;
		}
		return {};
	}
	
	// Look up a template by name
	std::optional<ASTNode> lookupTemplate(std::string_view name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = templates_.find(name);
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

	// Get out-of-line member functions for a class
	std::vector<OutOfLineMemberFunction> getOutOfLineMemberFunctions(std::string_view class_name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = out_of_line_members_.find(class_name);
		if (it != out_of_line_members_.end()) {
			return it->second;
		}
		return {};
	}

	// Register a template specialization pattern
	void registerSpecializationPattern(std::string_view template_name, 
	                                   const std::vector<ASTNode>& template_params,
	                                   const std::vector<TemplateTypeArg>& pattern_args, 
	                                   ASTNode specialized_node) {
		std::string key(template_name);
		TemplatePattern pattern;
		pattern.template_params = template_params;
		pattern.pattern_args = pattern_args;
		pattern.specialized_node = specialized_node;
		specialization_patterns_[key].push_back(std::move(pattern));
	}

	// Register a template specialization (exact match)
	void registerSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, ASTNode specialized_node) {
		SpecializationKey key{std::string(template_name), template_args};
		specializations_[key] = specialized_node;
	}

	// Look up an exact template specialization (no pattern matching)
	std::optional<ASTNode> lookupExactSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) const {
		SpecializationKey key{std::string(template_name), template_args};
		auto it = specializations_.find(key);
		if (it != specializations_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Look up a template specialization (exact match first, then pattern match)
	std::optional<ASTNode> lookupSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) const {
		// First, try exact match
		auto exact = lookupExactSpecialization(template_name, template_args);
		if (exact.has_value()) {
			return exact;
		}
		
		// No exact match - try pattern matching
		return matchSpecializationPattern(template_name, template_args);
	}
	
	// Find a matching specialization pattern
	std::optional<ASTNode> matchSpecializationPattern(std::string_view template_name, 
	                                                  const std::vector<TemplateTypeArg>& concrete_args) const {
		// Heterogeneous lookup - string_view accepted directly
		auto patterns_it = specialization_patterns_.find(template_name);
		if (patterns_it == specialization_patterns_.end()) {
			return std::nullopt;  // No patterns for this template
		}
		
		const std::vector<TemplatePattern>& patterns = patterns_it->second;
		const TemplatePattern* best_match = nullptr;
		int best_specificity = -1;
		
		// Find the most specific matching pattern
		for (const auto& pattern : patterns) {
			std::unordered_map<std::string, TemplateTypeArg> substitutions;
			if (pattern.matches(concrete_args, substitutions)) {
				int spec = pattern.specificity();
				if (spec > best_specificity) {
					best_match = &pattern;
					best_specificity = spec;
				}
			}
		}
		
		if (best_match) {
			return best_match->specialized_node;
		}
		
		return std::nullopt;
	}

	// Clear all templates and instantiations
	void clear() {
		templates_.clear();
		template_parameters_.clear();
		instantiations_.clear();
		out_of_line_members_.clear();
		specializations_.clear();
		specialization_patterns_.clear();
		alias_templates_.clear();
	}

private:
	// Map from template name to template declaration node (supports heterogeneous lookup)
	std::unordered_map<std::string, ASTNode, TransparentStringHash, std::equal_to<>> templates_;

	// Map from template name to template parameter names (supports heterogeneous lookup)
	std::unordered_map<std::string, std::vector<std::string_view>, TransparentStringHash, std::equal_to<>> template_parameters_;

	// Map from alias template name to TemplateAliasNode (supports heterogeneous lookup)
	std::unordered_map<std::string, ASTNode, TransparentStringHash, std::equal_to<>> alias_templates_;

	// Map from instantiation key to instantiated function node
	std::unordered_map<TemplateInstantiationKey, ASTNode, TemplateInstantiationKeyHash> instantiations_;

	// Map from class name to out-of-line member function definitions (supports heterogeneous lookup)
	std::unordered_map<std::string, std::vector<OutOfLineMemberFunction>, TransparentStringHash, std::equal_to<>> out_of_line_members_;

	// Map from (template_name, template_args) to specialized class node (exact matches)
	std::unordered_map<SpecializationKey, ASTNode, SpecializationKeyHash> specializations_;
	
	// Map from template_name to specialization patterns (for pattern matching, supports heterogeneous lookup)
	std::unordered_map<std::string, std::vector<TemplatePattern>, TransparentStringHash, std::equal_to<>> specialization_patterns_;
};

// Global template registry
extern TemplateRegistry gTemplateRegistry;

