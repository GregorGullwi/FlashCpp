#pragma once
#include "TemplateRegistry_Types.h"

/**
 * Convert TypeInfo::TemplateArgInfo to TemplateTypeArg
 *
 * Provides a single canonical conversion from the TypeInfo-embedded metadata
 * form to TemplateTypeArg.  Supersedes the local static helper that previously
 * lived in ExpressionSubstitutor.cpp.
 *
 * @param arg The TypeInfo::TemplateArgInfo to convert
 * @return TemplateTypeArg with all available type information populated
 */
inline TemplateTypeArg toTemplateTypeArg(const TypeInfo::TemplateArgInfo& arg) {
	TemplateTypeArg ta;
	ta.base_type = arg.base_type;
	ta.type_index = arg.type_index;
	ta.is_value = arg.is_value;
	ta.cv_qualifier = arg.cv_qualifier;
	ta.ref_qualifier = arg.ref_qualifier;
	ta.pointer_depth = static_cast<uint8_t>(arg.pointer_depth);
	ta.is_array = arg.is_array;
	ta.array_size = arg.array_size;
	ta.pointer_cv_qualifiers = arg.pointer_cv_qualifiers;
	ta.dependent_name = arg.dependent_name;
	if (arg.is_value) {
		ta.value = arg.intValue();
	}
	return ta;
}

// Out-of-line template member function definition
struct OutOfLineMemberFunction {
	InlineVector<ASTNode, 4> template_params;  // Template parameters (e.g., <typename T>)
	ASTNode function_node;                  // FunctionDeclarationNode
	SaveHandle body_start;                  // Handle to saved position of function body for re-parsing
	InlineVector<StringHandle, 4> template_param_names;  // Names of template parameters
	// For nested templates (member function templates of class templates):
	// template<typename T> template<typename U> T Container<T>::convert(U u) { ... }
	// inner_template_params stores the inner template params (U), while template_params stores the outer (T)
	InlineVector<ASTNode, 4> inner_template_params;
	InlineVector<StringHandle, 4> inner_template_param_names;
	// Function specifiers from out-of-line definition (= default, = delete)
	bool is_defaulted = false;
	bool is_deleted = false;
};

// Outer template parameter bindings for member function templates of class templates.
// Stored when a TemplateFunctionDeclarationNode is copied during class template instantiation.
// Used during inner template instantiation to resolve outer template params (e.g., T→int).
struct OuterTemplateBinding {
	InlineVector<StringHandle, 4> param_names;  // Outer param names (e.g., ["T"])
	InlineVector<TemplateTypeArg, 4> param_args;  // Concrete types (e.g., [int])
};

// Out-of-line template static member variable definition
struct OutOfLineMemberVariable {
	InlineVector<ASTNode, 4> template_params;       // Template parameters (e.g., <typename T>)
	StringHandle member_name;               // Name of the static member variable
	ASTNode type_node;                          // Type of the variable (TypeSpecifierNode)
	std::optional<ASTNode> initializer;         // Initializer expression
	InlineVector<StringHandle, 4> template_param_names;  // Names of template parameters
};

// Out-of-line template nested class definition
// Stores information about patterns like:
//   template<typename T> struct Outer<T>::Inner { ... };     (partial — applies to all instantiations)
//   template<> struct Wrapper<int>::Nested { int x; };       (full — applies only when args match)
struct OutOfLineNestedClass {
	InlineVector<ASTNode, 4> template_params;           // Outer template parameters (e.g., <typename T>)
	StringHandle nested_class_name;                 // Name of the nested class (e.g., "Inner")
	SaveHandle body_start;                          // Saved position at the struct/class keyword for re-parsing via parse_struct_declaration()
	InlineVector<StringHandle, 4> template_param_names; // Names of template parameters
	bool is_class = false;                          // true if 'class', false if 'struct'
	InlineVector<TemplateTypeArg, 4> specialization_args; // For full specializations: concrete args (e.g., <int>). Empty for partial specs.
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
	InlineVector<ASTNode, 4> template_params;  // Template parameters (e.g., typename T)
	InlineVector<TemplateTypeArg, 4> pattern_args;  // Pattern like T&, T*, etc.
	ASTNode specialized_node;  // The AST node for the specialized template
	std::optional<SfinaeCondition> sfinae_condition;  // Optional SFINAE check for void_t patterns
	
	// Constructor to avoid aggregate initialization issues with mutable cache fields
	TemplatePattern() = default;
	TemplatePattern(InlineVector<ASTNode, 4> tp, InlineVector<TemplateTypeArg, 4> pa,
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
	
	// Builds a TemplateTypeArg from a concrete TypeInfo::TemplateArgInfo for use in deduction.
	// Unlike toTemplateTypeArg() (which is a general-purpose 1:1 conversion that copies all
	// fields including dependent_name), this helper is specifically for building *resolved*
	// parameter bindings (e.g., T=int). It intentionally omits dependent_name because the
	// result represents a concrete type, not a dependent placeholder. It also defaults
	// base_type to Type::Int for value args with Type::Invalid, which toTemplateTypeArg does not.
	static TemplateTypeArg createDeducedArgFromConcrete(const TypeInfo::TemplateArgInfo& c) {
		TemplateTypeArg deduced;
		if (c.is_value) {
			deduced.is_value = true;
			deduced.value = c.intValue();
			deduced.base_type = c.base_type != Type::Invalid ? c.base_type : Type::Int;
		} else {
			deduced.base_type = c.base_type;
			deduced.type_index = c.type_index;
			deduced.cv_qualifier = c.cv_qualifier;
			deduced.pointer_depth = static_cast<uint8_t>(c.pointer_depth);
			deduced.ref_qualifier = c.ref_qualifier;
			deduced.pointer_cv_qualifiers = c.pointer_cv_qualifiers;
			deduced.is_array = c.is_array;
			deduced.array_size = c.array_size;
		}
		return deduced;
	}

	// Records a deduction for a single template parameter name, checking consistency
	// if the parameter was already deduced. Returns false on inconsistency.
	static bool recordDeduction(
		StringHandle param_name,
		const TemplateTypeArg& deduced,
		std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>>& param_substitutions)
	{
		auto sub_it = param_substitutions.find(param_name);
		if (sub_it != param_substitutions.end()) {
			if (!(sub_it->second == deduced)) {
				FLASH_LOG(Templates, Trace, "  FAILED: inconsistent deduction for '",
				          StringTable::getStringView(param_name), "'");
				return false;
			}
		} else {
			param_substitutions[param_name] = deduced;
			FLASH_LOG(Templates, Trace, "  Deduced param '",
			          StringTable::getStringView(param_name), "'");
		}
		return true;
	}

	// Maximum nesting depth for recursive template argument matching/scoring.
	// Prevents stack overflow on pathological or circular TypeInfo chains.
	// Real-world C++ template nesting is shallow; 64 is generous.
	static constexpr int MAX_NESTED_ARG_DEPTH = 64;

	// Recursive helper: matches a single inner template argument (possibly itself a nested
	// template instantiation) against a concrete inner argument, deducing template parameter
	// substitutions. Handles arbitrarily deep nesting such as Pair<Pair<A,B>, Pair<C,D>>.
	static bool matchNestedArg(
		const TypeInfo::TemplateArgInfo& p,
		const TypeInfo::TemplateArgInfo& c,
		const std::unordered_set<StringHandle, StringHandleHash>& template_param_names,
		std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>>& param_substitutions,
		int depth = 0)
	{
		if (depth >= MAX_NESTED_ARG_DEPTH) {
			FLASH_LOG(Templates, Trace, "  FAILED: matchNestedArg recursion depth limit exceeded (", depth, ")");
			return false;
		}
		// Check if p is a UserDefined type — could be a param name or a nested template instantiation
		if (!p.is_value && (is_struct_type(p.base_type))) {
			if (p.type_index.is_valid() && p.type_index.value < gTypeInfo.size()) {
				const TypeInfo& p_ti = gTypeInfo[p.type_index.value];
				if (p_ti.isTemplateInstantiation()) {
					// Nested template instantiation (e.g., Pair<A,B>): verify same base template and recurse
					if (!is_struct_type(c.base_type)) {
						FLASH_LOG(Templates, Trace, "  FAILED: nested pattern is template instantiation but concrete is not UserDefined/Struct");
						return false;
					}
					if (c.type_index.value >= gTypeInfo.size()) return false;
					const TypeInfo& c_ti = gTypeInfo[c.type_index.value];
					StringHandle p_base = p_ti.baseTemplateName();
					StringHandle c_base = c_ti.isTemplateInstantiation() ? c_ti.baseTemplateName() : c_ti.name();
					if (p_base != c_base) {
						FLASH_LOG(Templates, Trace, "  FAILED: nested template base mismatch");
						return false;
					}
					const auto& np = p_ti.templateArgs();
					const auto& nc = c_ti.templateArgs();
					if (np.size() != nc.size()) {
						FLASH_LOG(Templates, Trace, "  FAILED: nested inner arg count mismatch: pattern=",
						          np.size(), " concrete=", nc.size());
						return false;
					}
					for (size_t k = 0; k < np.size(); ++k) {
						if (!matchNestedArg(np[k], nc[k], template_param_names, param_substitutions, depth + 1))
							return false;
					}
					return true;
				}
				// Simple template parameter name (e.g., T, U)
				StringHandle inner_name = p_ti.name();
				if (inner_name.isValid() && template_param_names.count(inner_name)) {
					return recordDeduction(inner_name, createDeducedArgFromConcrete(c), param_substitutions);
				}
			} else if (p.dependent_name.isValid() && template_param_names.count(p.dependent_name)) {
				return recordDeduction(p.dependent_name, createDeducedArgFromConcrete(c), param_substitutions);
			}
		}
		// Value parameter deduction
		if (p.is_value && c.is_value) {
			if (p.dependent_name.isValid() && template_param_names.count(p.dependent_name)) {
				return recordDeduction(p.dependent_name, createDeducedArgFromConcrete(c), param_substitutions);
			}
			if (p.intValue() != c.intValue()) {
				FLASH_LOG(Templates, Trace, "  FAILED: inner value mismatch");
				return false;
			}
			return true;
		}
		if (p.is_value != c.is_value) {
			FLASH_LOG(Templates, Trace, "  FAILED: inner arg value/type mismatch");
			return false;
		}
		// Concrete type must match exactly
		if (p.base_type != c.base_type) {
			FLASH_LOG(Templates, Trace, "  FAILED: inner concrete type mismatch");
			return false;
		}
		if (needs_type_index(p.base_type) && p.type_index != c.type_index) {
			FLASH_LOG(Templates, Trace, "  FAILED: inner concrete type_index mismatch");
			return false;
		}
		return true;
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
			// Struct-type template instantiation patterns (e.g., Pair<A,B> where Pair is a struct
			// template) must reach the template instantiation handler below, not the concrete
			// type check. Detect that case up front.
			bool is_struct_template_inst = (pattern_arg.base_type == Type::Struct &&
				pattern_arg.type_index.is_valid() && pattern_arg.type_index.value < gTypeInfo.size() &&
				gTypeInfo[pattern_arg.type_index.value].isTemplateInstantiation());

			if (pattern_arg.base_type != Type::UserDefined && !is_struct_template_inst) {
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
		
			// Check if this UserDefined/Struct pattern arg is a dependent template instantiation
			// (e.g., ratio<_Num, _Den> stored as UserDefined, or Pair<A,B> stored as Struct)
			// If so, the concrete arg must be a template instantiation of the same base template
			if (pattern_arg.type_index.is_valid() && pattern_arg.type_index.value < gTypeInfo.size()) {
				const TypeInfo& pattern_type_info = gTypeInfo[pattern_arg.type_index.value];
				if (pattern_type_info.isTemplateInstantiation()) {
					// Pattern is a template instantiation — concrete must match base template
					StringHandle pattern_base = pattern_type_info.baseTemplateName();
					if (!is_struct_type(concrete_arg.base_type)) {
						FLASH_LOG(Templates, Trace, "  FAILED: pattern is template instantiation '",
						          StringTable::getStringView(pattern_base), 
						          "' but concrete is fundamental type");
						return false;
					}
					if (concrete_arg.type_index.value >= gTypeInfo.size()) {
						return false;
					}
					const TypeInfo& concrete_type_info = gTypeInfo[concrete_arg.type_index.value];
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
					// Recursively match inner template args to deduce parameters.
					// Uses matchNestedArg which handles arbitrary nesting depth,
					// e.g., Pair<Pair<A,B>, Pair<C,D>> against pair<pair<int,float>, pair<double,bool>>.
					const size_t subs_before_inner = param_substitutions.size();
					{
						const auto& pattern_inner_args = pattern_type_info.templateArgs();
						const auto& concrete_inner_args = concrete_type_info.templateArgs();
						
						if (pattern_inner_args.size() != concrete_inner_args.size()) {
							FLASH_LOG(Templates, Trace, "  FAILED: inner arg count mismatch: pattern=",
							          pattern_inner_args.size(), " concrete=", concrete_inner_args.size());
							return false;
						}
						
						for (size_t j = 0; j < pattern_inner_args.size(); ++j) {
							const auto& p_inner = pattern_inner_args[j];
							const auto& c_inner = concrete_inner_args[j];
							
							FLASH_LOG(Templates, Trace, "  Inner arg[", j, "]: p_inner.is_value=", p_inner.is_value,
							          " base_type=", static_cast<int>(p_inner.base_type),
							          " type_index=", p_inner.type_index,
							          " | c_inner.is_value=", c_inner.is_value,
							          " base_type=", static_cast<int>(c_inner.base_type));
							
							if (!matchNestedArg(p_inner, c_inner, template_param_names, param_substitutions))
								return false;
						}
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
			
			if (pattern_arg.type_index.is_valid() && pattern_arg.type_index.value < gTypeInfo.size()) {
				const TypeInfo& param_type_info = gTypeInfo[pattern_arg.type_index.value];
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
				TemplateTypeArg deduced_arg = deduceArgFromPattern(concrete_arg, pattern_arg);
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
				if (concrete_arg.type_index.value < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[concrete_arg.type_index.value];
					
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

		// Helper: recursively compute the specificity contribution of a single inner template arg.
		// A dependent type param (like T) contributes 0; a concrete type (like int) contributes 1;
		// a nested template instantiation (like Pair<A,B>) contributes 2 + args + recursion,
		// reflecting the structural constraint it imposes.
		auto innerArgScore = [&](const TypeInfo::TemplateArgInfo& inner_arg, auto& self, int depth = 0) -> int {
			if (depth >= MAX_NESTED_ARG_DEPTH) {
				return 0;  // Depth limit reached; stop adding specificity
			}
			if (!inner_arg.is_value &&
			    (is_struct_type(inner_arg.base_type))) {
				if (inner_arg.type_index.is_valid() && inner_arg.type_index.value < gTypeInfo.size()) {
					const TypeInfo& inner_ti = gTypeInfo[inner_arg.type_index.value];
					if (inner_ti.isTemplateInstantiation()) {
						// Nested template instantiation: structural constraint adds specificity.
						// e.g., Pair<A,B> is more specific than a bare T.
						int nested = 2 + static_cast<int>(inner_ti.templateArgs().size());
						for (const auto& nested_arg : inner_ti.templateArgs()) {
							nested += self(nested_arg, self, depth + 1);
						}
						return nested;
					}
					// Simple template parameter name → dependent, no extra specificity
					StringHandle iname = inner_ti.name();
					if (iname.isValid() && template_param_names.count(iname)) {
						return 0;
					}
				} else if (inner_arg.dependent_name.isValid() &&
				           template_param_names.count(inner_arg.dependent_name)) {
					return 0;  // dependent name
				}
				return 1;  // concrete UserDefined/Struct type
			}
			if (inner_arg.is_value && inner_arg.dependent_name.isValid() &&
			    template_param_names.count(inner_arg.dependent_name)) {
				return 0;  // dependent non-type param
			}
			if (inner_arg.is_value) {
				return 1;  // concrete non-type value
			}
			// Fundamental (non-UserDefined, non-value) concrete type
			return 1;
		};
	
		for (const auto& arg : pattern_args) {
			// Base score: any pattern parameter = 0
		
			// Template instantiation pattern (e.g., pair<T,U> or Pair<Pair<A,B>,Pair<C,D>>) is more specific than bare T
			if ((is_struct_type(arg.base_type)) &&
			    arg.type_index.is_valid() && arg.type_index.value < gTypeInfo.size()) {
				const TypeInfo& ti = gTypeInfo[arg.type_index.value];
				if (ti.isTemplateInstantiation()) {
					score += 2 + static_cast<int>(ti.templateArgs().size());
					// Each inner arg contributes to specificity, including nested instantiations
					for (const auto& inner_arg : ti.templateArgs()) {
						score += innerArgScore(inner_arg, innerArgScore);
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
	InlineVector<TemplateTypeArg, 4> template_args;

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
