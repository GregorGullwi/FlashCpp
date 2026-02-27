#pragma once
#include "TemplateRegistry_Types.h"

// Template argument - can be a type, a value, or a template
struct TemplateArgument {
	enum class Kind {
		Type,
		Value,
		Template   // For template template parameters
	};
	
	Kind kind;
	Type type_value;  // For type arguments (legacy - enum only, kept for backwards compatibility)
	TypeIndex type_index = 0;  // For type arguments - index into gTypeInfo for complex types (NEW in Task 2)
	int64_t int_value;  // For non-type integer arguments
	Type value_type;  // For non-type arguments: the type of the value (bool, int, etc.)
	StringHandle template_name;  // For template template arguments (name of the template)
	std::optional<TypeSpecifierNode> type_specifier;  // Full type info including references, pointers, CV qualifiers
	
	static TemplateArgument makeType(Type t, TypeIndex idx = 0) {
		TemplateArgument arg;
		arg.kind = Kind::Type;
		arg.type_value = t;
		arg.type_index = idx;  // Store TypeIndex for complex types
		return arg;
	}
	
	static TemplateArgument makeTypeSpecifier(const TypeSpecifierNode& type_spec) {
		TemplateArgument arg;
		arg.kind = Kind::Type;
		arg.type_value = type_spec.type();  // Keep legacy field populated
		arg.type_index = type_spec.type_index();  // Extract and store TypeIndex
		arg.type_specifier = type_spec;
		return arg;
	}
	
	static TemplateArgument makeValue(int64_t v, Type type = Type::Int) {
		TemplateArgument arg;
		arg.kind = Kind::Value;
		arg.int_value = v;
		arg.value_type = type;
		return arg;
	}
	
	static TemplateArgument makeTemplate(StringHandle template_name) {
		TemplateArgument arg;
		arg.kind = Kind::Template;
		arg.template_name = template_name;
		return arg;
	}
	
	// Hash for use in maps (needed for InstantiationQueue)
	size_t hash() const {
		size_t h = std::hash<int>{}(static_cast<int>(kind));
		h ^= std::hash<int>{}(static_cast<int>(type_value)) << 1;
		h ^= std::hash<TypeIndex>{}(type_index) << 2;
		h ^= std::hash<int64_t>{}(int_value) << 3;
		return h;
	}
	
	// Equality operator (needed for InstantiationQueue)
	bool operator==(const TemplateArgument& other) const {
		if (kind != other.kind) return false;
		switch (kind) {
			case Kind::Type:
				return type_value == other.type_value && type_index == other.type_index;
			case Kind::Value:
				return int_value == other.int_value && value_type == other.value_type;
			case Kind::Template:
				return template_name == other.template_name;
		}
		return false;
	}
};

/**
 * Conversion Helper Functions
 * ============================
 * 
 * These functions provide explicit, type-safe conversions between TemplateArgument
 * and TemplateTypeArg. They preserve as much type information as possible during
 * the conversion.
 * 
 * Usage Examples:
 *   // Convert TemplateArgument to TemplateTypeArg
 *   TemplateArgument arg = TemplateArgument::makeType(Type::Int, 0);
 *   TemplateTypeArg type_arg = toTemplateTypeArg(arg);
 * 
 *   // Convert TemplateTypeArg to TemplateArgument
 *   TemplateTypeArg type_arg;
 *   type_arg.base_type = Type::Float;
 *   TemplateArgument arg = toTemplateArgument(type_arg);
 */

/**
 * Convert TemplateArgument to TemplateTypeArg
 * 
 * Extracts type information from TemplateArgument and creates a TemplateTypeArg.
 * - If arg has type_specifier (modern path): Extracts full type info including
 *   references, pointers, cv-qualifiers, and arrays
 * - If arg lacks type_specifier (legacy path): Uses basic type_value and type_index
 * - For value arguments: Sets is_value=true and copies the value
 * - Template template parameters are not directly supported in TemplateTypeArg
 * 
 * @param arg The TemplateArgument to convert
 * @return TemplateTypeArg with extracted type information
 */
inline TemplateTypeArg toTemplateTypeArg(const TemplateArgument& arg) {
	TemplateTypeArg result;
	
	if (arg.kind == TemplateArgument::Kind::Type) {
		if (arg.type_specifier.has_value()) {
			// Modern path: use full type info from TypeSpecifierNode
			const auto& ts = *arg.type_specifier;
			result.base_type = ts.type();
			result.type_index = ts.type_index();
			result.ref_qualifier = ts.reference_qualifier();
			result.pointer_depth = ts.pointer_levels().size();
			result.pointer_cv_qualifiers.reserve(ts.pointer_levels().size());
			for (const auto& level : ts.pointer_levels()) {
				result.pointer_cv_qualifiers.push_back(level.cv_qualifier);
			}
			result.cv_qualifier = ts.cv_qualifier();
			result.is_array = ts.is_array();
			if (ts.is_array() && ts.array_size().has_value()) {
				result.array_size = ts.array_size();
			}
			// Note: member_pointer_kind not stored in TypeSpecifierNode, defaults to None
		} else {
			// Legacy path: use basic type info only
			result.base_type = arg.type_value;
			result.type_index = arg.type_index;
			// Other fields remain at default values
		}
	} else if (arg.kind == TemplateArgument::Kind::Value) {
		result.is_value = true;
		result.value = arg.int_value;
		result.base_type = arg.value_type;
	}
	// Template template parameters: not directly supported in TemplateTypeArg
	
	return result;
}

/**
 * Convert TemplateTypeArg to TemplateArgument
 * 
 * Creates a TemplateArgument with a TypeSpecifierNode containing complete type
 * information from the TemplateTypeArg.
 * - For value arguments: Creates TemplateArgument with makeValue()
 * - For type arguments: Creates TypeSpecifierNode with all qualifiers:
 *   - CV-qualifiers (const, volatile)
 *   - Pointer levels
 *   - Reference type (lvalue or rvalue)
 *   - Array dimensions
 * - Returns TemplateArgument with embedded TypeSpecifierNode (modern representation)
 * 
 * @param arg The TemplateTypeArg to convert
 * @return TemplateArgument with complete type information
 */
inline TemplateArgument toTemplateArgument(const TemplateTypeArg& arg) {
	if (arg.is_value) {
		// Non-type template parameter
		return TemplateArgument::makeValue(arg.value, arg.base_type);
	} else {
		// Type template parameter - create TypeSpecifierNode for full info
		TypeSpecifierNode ts(arg.base_type, TypeQualifier::None, 
		                    get_type_size_bits(arg.base_type), Token(), arg.cv_qualifier);
		ts.set_type_index(arg.type_index);
		
		// Add pointer levels
		if (!arg.pointer_cv_qualifiers.empty()) {
			for (const auto cv : arg.pointer_cv_qualifiers) {
				ts.add_pointer_level(cv);
			}
		} else {
			ts.add_pointer_levels(arg.pointer_depth);
		}
		
		// Set reference type
		ts.set_reference_qualifier(arg.reference_qualifier());
		
		// Set array info if present
		if (arg.is_array) {
			ts.set_array(true, arg.array_size);
		}
		
		return TemplateArgument::makeTypeSpecifier(ts);
	}
}

/**
 * Create a TemplateInstantiationKey from template name and TemplateArgument vector.
 * Overload of makeInstantiationKey(StringHandle, const std::vector<TemplateTypeArg>&)
 * that accepts TemplateArgument (the parser-level representation) instead of TemplateTypeArg.
 * Each TemplateArgument is converted to TypeIndexArg via toTemplateTypeArg().
 */
namespace FlashCpp {
inline TemplateInstantiationKey makeInstantiationKey(
	StringHandle template_name,
	const std::vector<TemplateArgument>& args) {
	
	TemplateInstantiationKey key(template_name);
	
	for (const auto& arg : args) {
		if (arg.kind == TemplateArgument::Kind::Value) {
			key.value_args.push_back(arg.int_value);
		} else if (arg.kind == TemplateArgument::Kind::Template) {
			key.template_template_args.push_back(arg.template_name);
		} else {
			// Type argument — convert via toTemplateTypeArg then to TypeIndexArg
			key.type_args.push_back(makeTypeIndexArg(toTemplateTypeArg(arg)));
		}
	}
	
	return key;
}
} // namespace FlashCpp

// Out-of-line template member function definition
struct OutOfLineMemberFunction {
	std::vector<ASTNode> template_params;  // Template parameters (e.g., <typename T>)
	ASTNode function_node;                  // FunctionDeclarationNode
	SaveHandle body_start;                  // Handle to saved position of function body for re-parsing
	std::vector<StringHandle> template_param_names;  // Names of template parameters
	// For nested templates (member function templates of class templates):
	// template<typename T> template<typename U> T Container<T>::convert(U u) { ... }
	// inner_template_params stores the inner template params (U), while template_params stores the outer (T)
	std::vector<ASTNode> inner_template_params;
	std::vector<StringHandle> inner_template_param_names;
	// Function specifiers from out-of-line definition (= default, = delete)
	bool is_defaulted = false;
	bool is_deleted = false;
};

// Outer template parameter bindings for member function templates of class templates.
// Stored when a TemplateFunctionDeclarationNode is copied during class template instantiation.
// Used during inner template instantiation to resolve outer template params (e.g., T→int).
struct OuterTemplateBinding {
	std::vector<StringHandle> param_names;  // Outer param names (e.g., ["T"])
	std::vector<TemplateTypeArg> param_args;  // Concrete types (e.g., [int])
};

// Out-of-line template static member variable definition
struct OutOfLineMemberVariable {
	std::vector<ASTNode> template_params;       // Template parameters (e.g., <typename T>)
	StringHandle member_name;               // Name of the static member variable
	ASTNode type_node;                          // Type of the variable (TypeSpecifierNode)
	std::optional<ASTNode> initializer;         // Initializer expression
	std::vector<StringHandle> template_param_names;  // Names of template parameters
};

// Out-of-line template nested class definition
// Stores information about patterns like:
//   template<typename T> struct Outer<T>::Inner { ... };     (partial — applies to all instantiations)
//   template<> struct Wrapper<int>::Nested { int x; };       (full — applies only when args match)
struct OutOfLineNestedClass {
	std::vector<ASTNode> template_params;           // Outer template parameters (e.g., <typename T>)
	StringHandle nested_class_name;                 // Name of the nested class (e.g., "Inner")
	SaveHandle body_start;                          // Saved position at the struct/class keyword for re-parsing via parse_struct_declaration()
	std::vector<StringHandle> template_param_names; // Names of template parameters
	bool is_class = false;                          // true if 'class', false if 'struct'
	std::vector<TemplateTypeArg> specialization_args; // For full specializations: concrete args (e.g., <int>). Empty for partial specs.
};

// SFINAE condition for void_t patterns
// Stores information about dependent member type checks like "typename T::type"
struct SfinaeCondition {
	size_t template_param_index;  // Which template parameter (e.g., 0 for T in has_type<T>)
	StringHandle member_name;     // The member type name to check (e.g., "type")
	
	SfinaeCondition() : template_param_index(0), member_name() {}
	SfinaeCondition(size_t idx, StringHandle name) : template_param_index(idx), member_name(name) {}
};

// Template specialization pattern - represents a pattern like T&, T*, const T, etc.
struct TemplatePattern {
	std::vector<ASTNode> template_params;  // Template parameters (e.g., typename T)
	std::vector<TemplateTypeArg> pattern_args;  // Pattern like T&, T*, etc.
	ASTNode specialized_node;  // The AST node for the specialized template
	std::optional<SfinaeCondition> sfinae_condition;  // Optional SFINAE check for void_t patterns
	
	// Constructor to avoid aggregate initialization issues with mutable cache fields
	TemplatePattern() = default;
	TemplatePattern(std::vector<ASTNode> tp, std::vector<TemplateTypeArg> pa,
	                ASTNode sn, std::optional<SfinaeCondition> sc)
		: template_params(std::move(tp)), pattern_args(std::move(pa)),
		  specialized_node(std::move(sn)), sfinae_condition(std::move(sc)) {}
	
	// Cached set of template parameter names for O(1) lookup in matches()/specificity().
	// Built lazily on first access. Assumes template_params is not modified after construction.
	mutable std::unordered_set<StringHandle, StringHandleHash> cached_template_param_names_;
	mutable bool template_param_names_valid_ = false;
	
	const std::unordered_set<StringHandle, StringHandleHash>& getTemplateParamNames() const {
		if (!template_param_names_valid_) {
			cached_template_param_names_.clear();
			cached_template_param_names_.reserve(template_params.size());
			for (const auto& tp : template_params) {
				if (tp.is<TemplateParameterNode>()) {
					cached_template_param_names_.insert(tp.as<TemplateParameterNode>().nameHandle());
				}
			}
			template_param_names_valid_ = true;
		}
		return cached_template_param_names_;
	}
	
	// Check if this pattern matches the given concrete arguments
	// For example, pattern T& matches int&, float&, etc.
	// Returns true if match succeeds, and fills param_substitutions with T->int mapping
	bool matches(const std::vector<TemplateTypeArg>& concrete_args, 
	             std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>>& param_substitutions) const
	{
		FLASH_LOG(Templates, Trace, "      matches(): pattern has ", pattern_args.size(), " args, concrete has ", concrete_args.size(), " args");
		
		// Handle variadic templates: pattern may have fewer args if last template param is a pack
		// Check if the last template parameter is variadic (a pack)
		bool has_variadic_pack = false;
		[[maybe_unused]] size_t pack_param_index = 0;
		for (size_t i = 0; i < template_params.size(); ++i) {
			if (template_params[i].is<TemplateParameterNode>()) {
				const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
				if (param.is_variadic()) {
					has_variadic_pack = true;
					pack_param_index = i;
					break;
				}
			}
		}
		
		// For non-variadic patterns, sizes must match exactly
		// For variadic patterns, concrete_args.size() >= pattern_args.size() - 1 
		// (pack can be empty, matching 0 or more args)
		if (!has_variadic_pack) {
			if (pattern_args.size() != concrete_args.size()) {
				FLASH_LOG(Templates, Trace, "      Size mismatch: pattern_args.size()=", pattern_args.size(), 
				          " != concrete_args.size()=", concrete_args.size());
				return false;
			}
		} else {
			// With variadic pack: need at least (pattern_args.size() - 1) concrete args
			// Pattern <First, Rest...> has 2 pattern_args, but can match 1+ concrete args
			// (Rest can be empty matching 0 args, or Rest can match 1+ args)
			if (concrete_args.size() < pattern_args.size() - 1) {
				return false;  // Not enough args for non-pack parameters
			}
		}
	
		param_substitutions.clear();

		// Use cached hash set of template parameter names for O(1) lookup
		const auto& template_param_names = getTemplateParamNames();
	
		// Check each pattern argument against the corresponding concrete argument
		// Track template parameter index separately from pattern argument index
		size_t param_index = 0;  // Tracks which template parameter we're binding
		for (size_t i = 0; i < pattern_args.size(); ++i) {
			const TemplateTypeArg& pattern_arg = pattern_args[i];
			
			// Handle variadic pack case: if i >= concrete_args.size(), 
			// this pattern arg corresponds to a pack that matches 0 args (empty pack)
			if (i >= concrete_args.size()) {
				// This should only happen for the variadic pack parameter
				// Check if this pattern position corresponds to a variadic pack
				if (param_index < template_params.size() && template_params[param_index].is<TemplateParameterNode>()) {
					const TemplateParameterNode& param = template_params[param_index].as<TemplateParameterNode>();
					if (param.is_variadic()) {
						// Empty pack is valid - continue without error
						continue;
					}
				}
				// Not a variadic pack but no concrete arg - pattern doesn't match
				return false;
			}
			
			const TemplateTypeArg& concrete_arg = concrete_args[i];
		
			FLASH_LOG(Templates, Trace, "Matching pattern arg[", i, "] against concrete arg[", i, "]");
		
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
			if (pattern_arg.ref_qualifier != concrete_arg.ref_qualifier) {
				FLASH_LOG(Templates, Trace, "  FAILED: ref_qualifier mismatch");
				return false;
			}
			if (pattern_arg.pointer_depth != concrete_arg.pointer_depth) {
				FLASH_LOG(Templates, Trace, "  FAILED: pointer_depth mismatch");
				return false;
			}
			if (pattern_arg.cv_qualifier != concrete_arg.cv_qualifier) {
				FLASH_LOG(Templates, Trace, "  FAILED: cv_qualifier mismatch");
				return false;
			}
			if (pattern_arg.is_array != concrete_arg.is_array) {
				FLASH_LOG(Templates, Trace, "  FAILED: array-ness mismatch");
				return false;
			}
			// Check array size matching
			// - If pattern has no size (T[]), it matches any array
			// - If pattern has SIZE_MAX (T[N] where N is template param), it matches any sized array but not unsized arrays
			// - If pattern has a specific size (T[3]), it must match exactly
			if (pattern_arg.is_array && pattern_arg.array_size.has_value() && concrete_arg.array_size.has_value()) {
				// Both have sizes - check if they match
				// SIZE_MAX in pattern means "any size" (template parameter like N)
				if (*pattern_arg.array_size != SIZE_MAX && *pattern_arg.array_size != *concrete_arg.array_size) {
					FLASH_LOG(Templates, Trace, "  FAILED: array size mismatch");
					return false;
				}
			} else if (pattern_arg.is_array && pattern_arg.array_size.has_value() && *pattern_arg.array_size == SIZE_MAX) {
				// Pattern has SIZE_MAX (like T[N]) but concrete has no size (like int[])
				// This should not match - T[N] requires a sized array
				if (!concrete_arg.array_size.has_value()) {
					FLASH_LOG(Templates, Trace, "  FAILED: pattern requires sized array but concrete is unsized");
					return false;
				}
			}
			if (pattern_arg.member_pointer_kind != concrete_arg.member_pointer_kind) {
				FLASH_LOG(Templates, Trace, "  FAILED: member pointer kind mismatch");
				return false;
			}
		
			// For pattern matching, we need to extract the template parameter name
			// The pattern_arg.base_type is UserDefined and represents the template parameter
			// We need to get the parameter name from template_params
		
			// The pattern_arg.base_type tells us which template parameter this is
			// For partial specialization Derived<T*, T>, both pattern args refer to the SAME
			// template parameter T, so we can't use position i
		
			// Find which template parameter this pattern arg refers to
			// base_type == Type::UserDefined (15) means it's a template parameter reference
			// BUT it could also be a dependent template instantiation placeholder
			// (e.g., ratio<_Num, _Den> stored as UserDefined with isTemplateInstantiation())
			if (pattern_arg.base_type != Type::UserDefined) {
				// This is a concrete type or value in the pattern
				// (e.g., partial specialization Container<int, T> or enable_if<true, T>)
				// The concrete type/value must match exactly
				FLASH_LOG(Templates, Trace, "  Pattern arg[", i, "]: concrete type/value check");
				FLASH_LOG(Templates, Trace, "    pattern_arg.base_type=", static_cast<int>(pattern_arg.base_type), 
				          " concrete_arg.base_type=", static_cast<int>(concrete_arg.base_type));
				FLASH_LOG(Templates, Trace, "    pattern_arg.is_value=", pattern_arg.is_value, 
				          " concrete_arg.is_value=", concrete_arg.is_value);
				if (pattern_arg.is_value && concrete_arg.is_value) {
					FLASH_LOG(Templates, Trace, "    pattern_arg.value=", pattern_arg.value, 
					          " concrete_arg.value=", concrete_arg.value);
				}
				if (pattern_arg.base_type != concrete_arg.base_type) {
					// For non-type value parameters, Bool and Int are interchangeable
					// (e.g., template<bool B> with default false stored as Bool vs Int)
					bool compatible_value_types = pattern_arg.is_value && concrete_arg.is_value &&
						((pattern_arg.base_type == Type::Bool && concrete_arg.base_type == Type::Int) ||
						 (pattern_arg.base_type == Type::Int && concrete_arg.base_type == Type::Bool));
					if (!compatible_value_types) {
						FLASH_LOG(Templates, Trace, "    FAILED: base types don't match");
						return false;
					}
				}
				// For non-type template parameters, also check the value matches
				if (pattern_arg.is_value && concrete_arg.is_value) {
					if (pattern_arg.value != concrete_arg.value) {
						FLASH_LOG(Templates, Trace, "    FAILED: values don't match");
						return false;  // Different values - no match
					}
				} else if (pattern_arg.is_value != concrete_arg.is_value) {
					FLASH_LOG(Templates, Trace, "    FAILED: is_value flags don't match");
					return false;  // One is value, one is type - no match
				}
				FLASH_LOG(Templates, Trace, "    SUCCESS: concrete type/value matches");
				continue;  // No substitution needed for concrete types/values - don't increment param_index
			}
		
			// Check if this UserDefined pattern arg is a dependent template instantiation
			// (e.g., ratio<_Num, _Den> stored as a placeholder like ratio$hash)
			// If so, the concrete arg must be a template instantiation of the same base template
			if (pattern_arg.type_index > 0 && pattern_arg.type_index < gTypeInfo.size()) {
				const TypeInfo& pattern_type_info = gTypeInfo[pattern_arg.type_index];
				if (pattern_type_info.isTemplateInstantiation()) {
					// Pattern is a template instantiation — concrete must match base template
					StringHandle pattern_base = pattern_type_info.baseTemplateName();
					if (concrete_arg.base_type != Type::UserDefined && concrete_arg.base_type != Type::Struct) {
						FLASH_LOG(Templates, Trace, "  FAILED: pattern is template instantiation '",
						          StringTable::getStringView(pattern_base), 
						          "' but concrete is fundamental type");
						return false;
					}
					if (concrete_arg.type_index >= gTypeInfo.size()) {
						return false;
					}
					const TypeInfo& concrete_type_info = gTypeInfo[concrete_arg.type_index];
					StringHandle concrete_base = concrete_type_info.isTemplateInstantiation() 
						? concrete_type_info.baseTemplateName() 
						: concrete_type_info.name();
					if (pattern_base != concrete_base) {
						FLASH_LOG(Templates, Trace, "  FAILED: template base mismatch: pattern='",
						          StringTable::getStringView(pattern_base), "' concrete='",
						          StringTable::getStringView(concrete_base), "'");
						return false;
					}
					FLASH_LOG(Templates, Trace, "  SUCCESS: template instantiation base matches '",
					          StringTable::getStringView(pattern_base), "'");
					// Recursively match inner template args to deduce parameters
					// e.g., for pattern pair<T,U> and concrete pair<int,float>, deduce T=int, U=float
					const size_t subs_before_inner = param_substitutions.size();
					{
						const auto& pattern_inner_args = pattern_type_info.templateArgs();
						const auto& concrete_inner_args = concrete_type_info.templateArgs();
						
						if (pattern_inner_args.size() != concrete_inner_args.size()) {
							FLASH_LOG(Templates, Trace, "  FAILED: inner arg count mismatch: pattern=",
							          pattern_inner_args.size(), " concrete=", concrete_inner_args.size());
							return false;
						}
						
						bool inner_match_ok = true;
						for (size_t j = 0; j < pattern_inner_args.size(); ++j) {
							const auto& p_inner = pattern_inner_args[j];
							const auto& c_inner = concrete_inner_args[j];
							
							FLASH_LOG(Templates, Trace, "  Inner arg[", j, "]: p_inner.is_value=", p_inner.is_value,
							          " base_type=", static_cast<int>(p_inner.base_type),
							          " type_index=", p_inner.type_index,
							          " | c_inner.is_value=", c_inner.is_value,
							          " base_type=", static_cast<int>(c_inner.base_type));
							
							// First, check if pattern inner arg is a dependent template parameter
							// This handles both type params (T, U) and non-type params (_Num, _Den)
							// where the pattern stores them as type references (is_value=false)
							bool bound_param = false;
							StringHandle inner_name;
							if (!p_inner.is_value && (p_inner.base_type == Type::UserDefined || p_inner.base_type == Type::Struct)) {
								// Try to get the parameter name from type_index or dependent_name
								if (p_inner.type_index > 0 && p_inner.type_index < gTypeInfo.size()) {
									inner_name = gTypeInfo[p_inner.type_index].name();
								} else if (p_inner.dependent_name.isValid()) {
									inner_name = p_inner.dependent_name;
								}
								if (inner_name.isValid() && template_param_names.count(inner_name)) {
									// Build TemplateTypeArg from concrete inner arg
									TemplateTypeArg deduced;
									if (c_inner.is_value) {
										// Non-type parameter: concrete is a value
										deduced.is_value = true;
										deduced.value = c_inner.intValue();
										deduced.base_type = c_inner.base_type != Type::Invalid ? c_inner.base_type : Type::Int;
									} else {
										// Type parameter: concrete is a type
										deduced.base_type = c_inner.base_type;
										deduced.type_index = c_inner.type_index;
										deduced.cv_qualifier = c_inner.cv_qualifier;
										deduced.pointer_depth = static_cast<uint8_t>(c_inner.pointer_depth);
										deduced.ref_qualifier = c_inner.ref_qualifier;
										deduced.pointer_cv_qualifiers = c_inner.pointer_cv_qualifiers;
										deduced.is_array = c_inner.is_array;
										deduced.array_size = c_inner.array_size;
									}
									
									auto sub_it = param_substitutions.find(inner_name);
									if (sub_it != param_substitutions.end()) {
										if (!(sub_it->second == deduced)) {
											FLASH_LOG(Templates, Trace, "  FAILED: inconsistent inner deduction for '",
											          StringTable::getStringView(inner_name), "'");
											inner_match_ok = false;
										}
									} else {
										param_substitutions[inner_name] = deduced;
										FLASH_LOG(Templates, Trace, "  Deduced inner param '",
										          StringTable::getStringView(inner_name), "' from inner arg[", j, "]");
									}
									bound_param = true;
									if (!inner_match_ok) break;
								}
							}
							
							if (!inner_match_ok) break;
							if (bound_param) continue;
							
							// Handle non-type value arguments (both pattern and concrete are values)
							if (p_inner.is_value && c_inner.is_value) {
								bool bound_value = false;
								// Check if this value arg has a dependent_name (e.g., _Num in ratio<_Num, _Den>)
								if (p_inner.dependent_name.isValid()) {
									if (template_param_names.count(p_inner.dependent_name)) {
										TemplateTypeArg deduced;
										deduced.is_value = true;
										deduced.value = c_inner.intValue();
										deduced.base_type = c_inner.base_type != Type::Invalid ? c_inner.base_type : Type::Int;
										auto sub_it = param_substitutions.find(p_inner.dependent_name);
										if (sub_it != param_substitutions.end()) {
											if (!(sub_it->second == deduced)) { inner_match_ok = false; }
										} else {
											param_substitutions[p_inner.dependent_name] = deduced;
										}
										bound_value = true;
										if (!inner_match_ok) break;
									}
								}
								if (!bound_value) {
									if (p_inner.intValue() != c_inner.intValue()) {
										FLASH_LOG(Templates, Trace, "  FAILED: inner value mismatch at index ", j);
										inner_match_ok = false; break;
									}
								}
								continue;
							}
							
							if (p_inner.is_value != c_inner.is_value) {
								FLASH_LOG(Templates, Trace, "  FAILED: inner arg value/type mismatch at index ", j);
								inner_match_ok = false; break;
							}
							
							// Concrete type in pattern - must match exactly
							if (p_inner.base_type != c_inner.base_type) {
								FLASH_LOG(Templates, Trace, "  FAILED: inner concrete type mismatch at index ", j);
								inner_match_ok = false; break;
							}
							if ((p_inner.base_type == Type::UserDefined || p_inner.base_type == Type::Struct ||
							     p_inner.base_type == Type::Enum) && p_inner.type_index != c_inner.type_index) {
								FLASH_LOG(Templates, Trace, "  FAILED: inner concrete type_index mismatch at index ", j);
								inner_match_ok = false; break;
							}
						}
						if (!inner_match_ok) return false;
					}
					// Advance param_index past inner-deduced parameters so that
					// subsequent pattern args use the correct fallback index.
					param_index += param_substitutions.size() - subs_before_inner;
					continue;
				}
			}
		
			// Find the template parameter name for this pattern arg
			// First, try to get the name from the pattern arg's type_index (for reused parameters)
			// For is_same<T, T>, both pattern args point to the same TypeInfo for T
			StringHandle param_name;
			bool found_param = false;
			
			if (pattern_arg.type_index > 0 && pattern_arg.type_index < gTypeInfo.size()) {
				const TypeInfo& param_type_info = gTypeInfo[pattern_arg.type_index];
				param_name = param_type_info.name();
				found_param = true;
				FLASH_LOG(Templates, Trace, "  Found parameter name '", StringTable::getStringView(param_name), "' from pattern_arg.type_index=", pattern_arg.type_index);
			}
			
			if (!found_param) {
				// Fallback: use param_index to get the template parameter
				// This is needed when type_index isn't set properly
				if (param_index >= template_params.size()) {
					FLASH_LOG(Templates, Trace, "  FAILED: param_index ", param_index, " >= template_params.size() ", template_params.size());
					return false;  // More template params needed than available - invalid pattern
				}
				
				if (template_params[param_index].is<TemplateParameterNode>()) {
					const TemplateParameterNode& template_param = template_params[param_index].as<TemplateParameterNode>();
					param_name = template_param.nameHandle();
					found_param = true;
				}
			
				if (!found_param) {
					FLASH_LOG(Templates, Trace, "  FAILED: Template parameter at param_index ", param_index, " is not a TemplateParameterNode");
					return false;  // Template parameter at position param_index is not a TemplateParameterNode
				}
			}
		
			// Check if we've already seen this parameter
			// For consistency checking, we need to compare the BASE TYPE only,
			// because Derived<T*, T> means both args bind to the same T, but with different modifiers
			auto it = param_substitutions.find(param_name);
			if (it != param_substitutions.end()) {
				// Parameter already bound - check consistency of BASE TYPE only
				if (it->second.base_type != concrete_arg.base_type) {
					FLASH_LOG(Templates, Trace, "  FAILED: Inconsistent substitution for parameter ", StringTable::getStringView(param_name));
					return false;  // Inconsistent substitution (different base types)
				}
				FLASH_LOG(Templates, Trace, "  SUCCESS: Reused parameter ", StringTable::getStringView(param_name), " - consistency check passed");
				// Don't increment param_index - we reused an existing parameter binding
			} else {
				// Bind this parameter to the concrete type, stripping pattern qualifiers.
				// Per C++ deduction rules: for pattern T&, T is deduced as int (not int&).
				TemplateTypeArg deduced_arg = concrete_arg;
				if (pattern_arg.is_reference()) deduced_arg.ref_qualifier = ReferenceQualifier::None;
				if (pattern_arg.pointer_depth > 0 && deduced_arg.pointer_depth >= pattern_arg.pointer_depth) {
					deduced_arg.pointer_depth -= pattern_arg.pointer_depth;
					// Strip the first pattern_arg.pointer_depth CV qualifiers by rebuilding the vector
					InlineVector<CVQualifier, 4> remaining;
					for (size_t pd = pattern_arg.pointer_depth; pd < deduced_arg.pointer_cv_qualifiers.size(); ++pd) {
						remaining.push_back(deduced_arg.pointer_cv_qualifiers[pd]);
					}
					deduced_arg.pointer_cv_qualifiers = std::move(remaining);
				}
				if (pattern_arg.is_array) {
					deduced_arg.is_array = false;
					deduced_arg.array_size = std::nullopt;
				}
				// Strip cv_qualifier contributed by the pattern (e.g., const T → T=int, not T=const int)
				if (pattern_arg.cv_qualifier != CVQualifier::None) {
					deduced_arg.cv_qualifier = static_cast<CVQualifier>(
						static_cast<uint8_t>(deduced_arg.cv_qualifier) & ~static_cast<uint8_t>(pattern_arg.cv_qualifier));
				}
				param_substitutions[param_name] = deduced_arg;
				FLASH_LOG(Templates, Trace, "  SUCCESS: Bound parameter ", StringTable::getStringView(param_name), " to concrete type (qualifiers stripped)");
				// Increment param_index since we bound a new template parameter
				++param_index;
			}
		}
		
		// SFINAE check: If this pattern has a SFINAE condition (e.g., void_t<typename T::type>),
		// verify that the condition is satisfied with the substituted types.
		// This enables proper void_t detection behavior.
		if (sfinae_condition.has_value()) {
			const SfinaeCondition& cond = *sfinae_condition;
			
			// Get the concrete type for the template parameter
			if (cond.template_param_index < concrete_args.size()) {
				const TemplateTypeArg& concrete_arg = concrete_args[cond.template_param_index];
				
				// Check if the concrete type has the required member type
				if (concrete_arg.type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[concrete_arg.type_index];
					
					// Build the qualified member name (e.g., "WithType::type")
					StringBuilder qualified_name;
					qualified_name.append(type_info.name());
					qualified_name.append("::");
					qualified_name.append(cond.member_name);
					StringHandle qualified_handle = StringTable::getOrInternStringHandle(qualified_name.commit());
					
					// Check if this member type exists
					auto type_it = gTypesByName.find(qualified_handle);
					if (type_it == gTypesByName.end()) {
						FLASH_LOG(Templates, Debug, "SFINAE condition failed: ", 
						          StringTable::getStringView(qualified_handle), " does not exist");
						return false;  // SFINAE failure - pattern doesn't match
					}
					FLASH_LOG(Templates, Debug, "SFINAE condition passed: ", 
					          StringTable::getStringView(qualified_handle), " exists");
				}
			}
		}
	
		return true;  // All patterns matched
	}
	
	// Calculate specificity score (higher = more specialized)
	// T = 0, T& = 1, T* = 1, const T = 1, const T& = 2, T[N] = 2, T[] = 1, etc.
	int specificity() const
	{
		int score = 0;

		// Use cached hash set of template parameter names for O(1) lookup
		const auto& template_param_names = getTemplateParamNames();
	
		for (const auto& arg : pattern_args) {
			// Base score: any pattern parameter = 0
		
			// Template instantiation pattern (e.g., pair<T,U>) is more specific than bare T
			if (arg.base_type == Type::UserDefined && arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
				const TypeInfo& ti = gTypeInfo[arg.type_index];
				if (ti.isTemplateInstantiation()) {
					score += 2 + static_cast<int>(ti.templateArgs().size());
					// Concrete inner args (not template parameters) add extra specificity
					// e.g., pair<int,U> is more specific than pair<T,U>
					for (const auto& inner_arg : ti.templateArgs()) {
						bool is_dependent = false;
						if (!inner_arg.is_value &&
						    (inner_arg.base_type == Type::UserDefined || inner_arg.base_type == Type::Struct)) {
							StringHandle iname;
							if (inner_arg.type_index > 0 && inner_arg.type_index < gTypeInfo.size()) {
								iname = gTypeInfo[inner_arg.type_index].name();
							} else if (inner_arg.dependent_name.isValid()) {
								iname = inner_arg.dependent_name;
							}
							if (iname.isValid()) {
								is_dependent = template_param_names.count(iname) > 0;
							}
						} else if (inner_arg.is_value && inner_arg.dependent_name.isValid()) {
							// Non-type value inner arg: check if dependent_name matches a template parameter
							is_dependent = template_param_names.count(inner_arg.dependent_name) > 0;
						}
						if (!is_dependent) {
							score += 1;  // concrete inner arg adds specificity
						}
					}
				}
			}
		
			// Pointer modifier adds specificity (T* is more specific than T)
			score += arg.pointer_depth;  // T* = +1, T** = +2, etc.
		
			if (arg.is_lvalue_reference()) {
				score += 1;  // T& is more specific than T
			}
			if (arg.is_rvalue_reference()) {
				score += 1;  // T&& is more specific than T
			}
		
			// Array modifiers add specificity
			if (arg.is_array) {
				if (arg.array_size.has_value()) {
					// SIZE_MAX indicates "array with size expression but value unknown" (like T[N])
					// Concrete sizes (like T[3]) and template parameter sizes (like T[N]) both get score of 2
					score += 2;  // T[N] or T[3] is more specific than T[]
				} else {
					score += 1;  // T[] is more specific than T
				}
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
