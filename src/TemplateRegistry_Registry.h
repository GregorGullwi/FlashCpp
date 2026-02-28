#pragma once
#include "TemplateRegistry_Pattern.h"

class TemplateRegistry {
public:
	// Register a template function declaration
	void registerTemplate(std::string_view name, ASTNode template_node) {
		registerTemplate(StringTable::getOrInternStringHandle(name), template_node);
	}

	void registerTemplate(StringHandle name, ASTNode template_node) {
		// Track class template names separately so callers can ask "is this name a class
		// template?" without matching unrelated function templates that share the same
		// unqualified name.
		if (template_node.is<TemplateClassDeclarationNode>()) {
			class_template_names_.insert(name);

			// When registering a class template full definition, replace any existing
			// forward declaration (empty body) for the same name instead of appending.
			// This ensures lookupTemplate returns the full definition, not the forward decl.
			auto& entries = templates_[name];
			if (!entries.empty() && isClassTemplateForwardDecl(template_node) == false) {
				for (size_t i = 0; i < entries.size(); ++i) {
					if (entries[i].is<TemplateClassDeclarationNode>() && isClassTemplateForwardDecl(entries[i])) {
						entries[i] = template_node;
						return;
					}
				}
			}
		}
		templates_[name].push_back(template_node);
	}

	// Returns true if the given node is a TemplateClassDeclarationNode whose
	// underlying StructDeclarationNode was parsed from a forward declaration
	// like `template<typename T> struct Foo;` (semicolon instead of body).
	static bool isClassTemplateForwardDecl(const ASTNode& node) {
		if (!node.is<TemplateClassDeclarationNode>()) return false;
		return node.as<TemplateClassDeclarationNode>().class_decl_node().is_forward_declaration();
	}

	// Returns true if 'name' (exact StringHandle) was registered as a class template.
	// Used in codegen to skip uninstantiated class template pattern structs in
	// gTypesByName without accidentally skipping non-template structs that share an
	// unqualified name with a template in a different namespace.
	bool isClassTemplate(StringHandle name) const {
		return class_template_names_.count(name) > 0;
	}

	// Register a template using QualifiedIdentifier (Phase 2).
	// Stores under the unqualified name for backward-compatible lookups.
	// If the identifier has a non-global namespace, also stores under the
	// fully-qualified name (e.g. "std::vector") so that namespace-qualified
	// lookups work without manual dual registration by the caller.
	void registerTemplate(QualifiedIdentifier qi, ASTNode template_node) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			registerTemplate(name, template_node);
		});
	}

	// Register template parameter names for a template
	void registerTemplateParameters(StringHandle key, const std::vector<StringHandle>& param_names) {
		template_parameters_[key] = std::vector<StringHandle>(param_names.begin(), param_names.end());
	}

	// Register an alias template: template<typename T> using Ptr = T*;
	void register_alias_template(std::string_view name, ASTNode alias_node) {
		StringHandle key = StringTable::getOrInternStringHandle(name);
		alias_templates_[key] = alias_node;
	}

	void register_alias_template(StringHandle name, ASTNode alias_node) {
		alias_templates_[name] = alias_node;
	}

	// Register an alias template using QualifiedIdentifier (Phase 2).
	void register_alias_template(QualifiedIdentifier qi, ASTNode alias_node) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			register_alias_template(name, alias_node);
		});
	}

	// Register a variable template: template<typename T> constexpr T pi = T(3.14159...);
	void registerVariableTemplate(std::string_view name, ASTNode variable_template_node) {
		StringHandle key = StringTable::getOrInternStringHandle(name);
		variable_templates_[key] = variable_template_node;
	}

	void registerVariableTemplate(StringHandle name, ASTNode variable_template_node) {
		variable_templates_[name] = variable_template_node;
	}

	// Register a variable template using QualifiedIdentifier (Phase 2).
	void registerVariableTemplate(QualifiedIdentifier qi, ASTNode variable_template_node) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			registerVariableTemplate(name, variable_template_node);
		});
	}

	// Look up a variable template by name
	std::optional<ASTNode> lookupVariableTemplate(std::string_view name) const {
		return lookupVariableTemplate(StringTable::getOrInternStringHandle(name));
	}

	std::optional<ASTNode> lookupVariableTemplate(StringHandle name) const {
		auto it = variable_templates_.find(name);
		if (it != variable_templates_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Register a variable template partial specialization with its pattern args
	void registerVariableTemplateSpecialization(std::string_view base_name,
	                                             const std::vector<ASTNode>& template_params,
	                                             const std::vector<TemplateTypeArg>& pattern_args,
	                                             ASTNode specialized_node) {
		StringHandle key = StringTable::getOrInternStringHandle(base_name);
		variable_template_specializations_[key].push_back(
			TemplatePattern{template_params, pattern_args, specialized_node, std::nullopt});
	}

	// Find the best matching variable template partial specialization for concrete args.
	// Returns the specialized ASTNode and the deduced parameter substitutions.
	struct VarTemplateSpecMatch {
		ASTNode node;
		std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>> substitutions;
	};

	std::optional<VarTemplateSpecMatch> findVariableTemplateSpecialization(
	    std::string_view base_name,
	    const std::vector<TemplateTypeArg>& concrete_args) const
	{
		StringHandle key = StringTable::getOrInternStringHandle(base_name);
		auto it = variable_template_specializations_.find(key);
		if (it == variable_template_specializations_.end()) {
			return std::nullopt;
		}
		
		const TemplatePattern* best_match = nullptr;
		int best_specificity = -1;
		std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>> best_subs;
		
		for (const auto& pattern : it->second) {
			std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>> subs;
			if (pattern.matches(concrete_args, subs)) {
				int spec = pattern.specificity();
				if (spec > best_specificity) {
					best_specificity = spec;
					best_match = &pattern;
					best_subs = std::move(subs);
				}
			}
		}
		
		if (best_match) {
			return VarTemplateSpecMatch{best_match->specialized_node, std::move(best_subs)};
		}
		return std::nullopt;
	}

	// Look up an alias template by name
	std::optional<ASTNode> lookup_alias_template(std::string_view name) const {
		return lookup_alias_template(StringTable::getOrInternStringHandle(name));
	}

	std::optional<ASTNode> lookup_alias_template(StringHandle name) const {
		auto it = alias_templates_.find(name);
		if (it != alias_templates_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Get all alias template names with a given prefix (for template instantiation)
	// Used to copy member template aliases from primary template to instantiated template
	std::vector<std::string_view> get_alias_templates_with_prefix(std::string_view prefix) const {
		std::vector<std::string_view> result;
		for (const auto& [name_handle, node] : alias_templates_) {
			std::string_view name = StringTable::getStringView(name_handle);
			if (name.starts_with(prefix)) {
				result.push_back(name);
			}
		}
		return result;
	}

	// Register a deduction guide: template<typename T> ClassName(T) -> ClassName<T>;
	void register_deduction_guide(std::string_view class_name, ASTNode guide_node) {
		register_deduction_guide(StringTable::getOrInternStringHandle(class_name), guide_node);
	}

	void register_deduction_guide(StringHandle class_name, ASTNode guide_node) {
		deduction_guides_[class_name].push_back(guide_node);
	}

	// Look up deduction guides for a class template
	std::vector<ASTNode> lookup_deduction_guides(std::string_view class_name) const {
		return lookup_deduction_guides(StringTable::getOrInternStringHandle(class_name));
	}

	std::vector<ASTNode> lookup_deduction_guides(StringHandle class_name) const {
		auto it = deduction_guides_.find(class_name);
		if (it != deduction_guides_.end()) {
			return it->second;
		}
		return {};
	}

	// Get template parameter names for a template
	std::vector<StringHandle> getTemplateParameters(StringHandle name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = template_parameters_.find(name);
		if (it != template_parameters_.end()) {
			return it->second;
		}
		return {};
	}
	
	// Look up a template by name
	// If multiple overloads exist, returns the first one registered
	// For all overloads, use lookupAllTemplates()
	std::optional<ASTNode> lookupTemplate(std::string_view name) const {
		return lookupTemplate(StringTable::getOrInternStringHandle(name));
	}
	
	std::optional<ASTNode> lookupTemplate(StringHandle name) const {
		auto it = templates_.find(name);
		if (it != templates_.end() && !it->second.empty()) {
			return it->second.front();
		}
		return std::nullopt;
	}

	// Look up a template using QualifiedIdentifier (Phase 2).
	// Tries the qualified name first, then falls back to unqualified.
	std::optional<ASTNode> lookupTemplate(QualifiedIdentifier qi) const {
		if (qi.hasNamespace()) {
			StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(
				qi.namespace_handle, qi.identifier_handle);
			auto result = lookupTemplate(qualified);
			if (result.has_value()) return result;
		}
		return lookupTemplate(qi.identifier_handle);
	}

	// Look up all template overloads for a given name
	const std::vector<ASTNode>* lookupAllTemplates(std::string_view name) const {
		return lookupAllTemplates(StringTable::getOrInternStringHandle(name));
	}

	const std::vector<ASTNode>* lookupAllTemplates(StringHandle name) const {
		auto it = templates_.find(name);
		if (it != templates_.end()) {
			return &it->second;
		}
		return nullptr;
	}
	
	// Get all registered template names (for smart re-instantiation)
	std::vector<std::string_view> getAllTemplateNames() const {
		std::vector<std::string_view> result;
		result.reserve(templates_.size());
		for (const auto& [name_handle, _] : templates_) {
			result.push_back(StringTable::getStringView(name_handle));
		}
		return result;
	}
	
	// Check if a template instantiation already exists
	bool hasInstantiation(const FlashCpp::TemplateInstantiationKey& key) const {
		return instantiations_.find(key) != instantiations_.end();
	}
	
	// Get an existing instantiation
	std::optional<ASTNode> getInstantiation(const FlashCpp::TemplateInstantiationKey& key) const {
		auto it = instantiations_.find(key);
		if (it != instantiations_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Register a new instantiation
	void registerInstantiation(const FlashCpp::TemplateInstantiationKey& key, ASTNode instantiated_node) {
		instantiations_[key] = instantiated_node;
	}
	
	// Convenience method: register instantiation using template name and TemplateTypeArg args
	void registerInstantiation(StringHandle template_name, 
	                            const std::vector<TemplateTypeArg>& args,
	                            ASTNode instantiated_node) {
		auto key = FlashCpp::makeInstantiationKey(template_name, args);
		instantiations_[key] = instantiated_node;
	}
	
	// Convenience method: lookup instantiation using template name and TemplateTypeArg args
	std::optional<ASTNode> getInstantiation(StringHandle template_name,
	                                         const std::vector<TemplateTypeArg>& args) const {
		auto key = FlashCpp::makeInstantiationKey(template_name, args);
		return getInstantiation(key);
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
			default: return "?";
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

	// Generate a mangled name for a template instantiation using hash-based naming
	// Example: max<int> -> max$a1b2c3d4, max<int, 5> -> max$e5f6g7h8
	// Build a unique hash-based name for a template instantiation and register it.
	// This avoids collisions from underscore-based naming (e.g., type names with underscores)
	std::string_view mangleTemplateName(std::string_view base_name, const std::vector<TemplateArgument>& args) {
		// Convert TemplateArgument to TemplateTypeArg for hash-based naming
		std::vector<TemplateTypeArg> type_args;
		type_args.reserve(args.size());
		
		for (const auto& arg : args) {
			TemplateTypeArg ta;
			if (arg.kind == TemplateArgument::Kind::Type) {
				ta.base_type = arg.type_value;
				ta.type_index = arg.type_index;
				if (arg.type_specifier.has_value()) {
					const auto& ts = *arg.type_specifier;
					ta.ref_qualifier = ts.reference_qualifier();
					ta.cv_qualifier = ts.cv_qualifier();
					ta.pointer_depth = static_cast<uint8_t>(ts.pointer_levels().size());
				}
			} else if (arg.kind == TemplateArgument::Kind::Value) {
				ta.is_value = true;
				ta.value = arg.int_value;
				ta.base_type = arg.value_type;
			} else if (arg.kind == TemplateArgument::Kind::Template) {
				// For template template arguments, mark as template template arg
				ta.is_template_template_arg = true;
				ta.template_name_handle = arg.template_name;
			}
			type_args.push_back(ta);
		}
		
		auto result = FlashCpp::generateInstantiatedNameFromArgs(base_name, type_args);
		return result;
	}

	// Register an out-of-line template member function definition (StringHandle overload)
	void registerOutOfLineMember(StringHandle class_name, OutOfLineMemberFunction member_func) {
		out_of_line_members_[class_name].push_back(std::move(member_func));
	}

	// Register an out-of-line template member function definition (string_view overload)
	void registerOutOfLineMember(std::string_view class_name, OutOfLineMemberFunction member_func) {
		StringHandle key = StringTable::getOrInternStringHandle(class_name);
		registerOutOfLineMember(key, std::move(member_func));
	}

	// Get out-of-line member functions for a class (StringHandle overload)
	std::vector<OutOfLineMemberFunction> getOutOfLineMemberFunctions(StringHandle class_name) const {
		auto it = out_of_line_members_.find(class_name);
		if (it != out_of_line_members_.end()) {
			return it->second;
		}
		return {};
	}

	// Get out-of-line member functions for a class (string_view overload)
	std::vector<OutOfLineMemberFunction> getOutOfLineMemberFunctions(std::string_view class_name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = out_of_line_members_.find(class_name);
		if (it != out_of_line_members_.end()) {
			return it->second;
		}
		return {};
	}

	// Register an out-of-line template static member variable definition (StringHandle overload)
	void registerOutOfLineMemberVariable(StringHandle class_name, OutOfLineMemberVariable member_var) {
		out_of_line_variables_[class_name].push_back(std::move(member_var));
	}

	// Register an out-of-line template static member variable definition (string_view overload)
	void registerOutOfLineMemberVariable(std::string_view class_name, OutOfLineMemberVariable member_var) {
		StringHandle key = StringTable::getOrInternStringHandle(class_name);
		registerOutOfLineMemberVariable(key, std::move(member_var));
	}

	// Get out-of-line member variables for a class (StringHandle overload)
	std::vector<OutOfLineMemberVariable> getOutOfLineMemberVariables(StringHandle class_name) const {
		auto it = out_of_line_variables_.find(class_name);
		if (it != out_of_line_variables_.end()) {
			return it->second;
		}
		return {};
	}

	// Get out-of-line member variables for a class (string_view overload)
	std::vector<OutOfLineMemberVariable> getOutOfLineMemberVariables(std::string_view class_name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = out_of_line_variables_.find(class_name);
		if (it != out_of_line_variables_.end()) {
			return it->second;
		}
		return {};
	}

	// Register an out-of-line template nested class definition (StringHandle overload)
	void registerOutOfLineNestedClass(StringHandle class_name, OutOfLineNestedClass nested_class) {
		out_of_line_nested_classes_[class_name].push_back(std::move(nested_class));
	}

	// Register an out-of-line template nested class definition (string_view overload)
	void registerOutOfLineNestedClass(std::string_view class_name, OutOfLineNestedClass nested_class) {
		StringHandle key = StringTable::getOrInternStringHandle(class_name);
		registerOutOfLineNestedClass(key, std::move(nested_class));
	}

	// Get out-of-line nested classes for a class (StringHandle overload)
	std::vector<OutOfLineNestedClass> getOutOfLineNestedClasses(StringHandle class_name) const {
		auto it = out_of_line_nested_classes_.find(class_name);
		if (it != out_of_line_nested_classes_.end()) {
			return it->second;
		}
		return {};
	}

	// Get out-of-line nested classes for a class (string_view overload)
	std::vector<OutOfLineNestedClass> getOutOfLineNestedClasses(std::string_view class_name) const {
		auto it = out_of_line_nested_classes_.find(class_name);
		if (it != out_of_line_nested_classes_.end()) {
			return it->second;
		}
		return {};
	}

	// Register outer template parameter bindings for a member function template
	// of an instantiated class template (e.g., Container<int>::convert has T→int)
	void registerOuterTemplateBinding(std::string_view qualified_name, OuterTemplateBinding binding) {
		registerOuterTemplateBinding(StringTable::getOrInternStringHandle(qualified_name), std::move(binding));
	}

	void registerOuterTemplateBinding(StringHandle qualified_name, OuterTemplateBinding binding) {
		outer_template_bindings_[qualified_name] = std::move(binding);
	}

	// Get outer template parameter bindings for a member function template
	const OuterTemplateBinding* getOuterTemplateBinding(std::string_view qualified_name) const {
		return getOuterTemplateBinding(StringTable::getOrInternStringHandle(qualified_name));
	}

	const OuterTemplateBinding* getOuterTemplateBinding(StringHandle qualified_name) const {
		auto it = outer_template_bindings_.find(qualified_name);
		if (it != outer_template_bindings_.end()) {
			return &it->second;
		}
		return nullptr;
	}

	// Register a template specialization pattern (StringHandle overload)
	void registerSpecializationPattern(StringHandle template_name, 
	                                   const std::vector<ASTNode>& template_params,
	                                   const std::vector<TemplateTypeArg>& pattern_args, 
	                                   ASTNode specialized_node,
	                                   std::optional<SfinaeCondition> sfinae_cond = std::nullopt) {
		FLASH_LOG(Templates, Debug, "registerSpecializationPattern: template_name='", StringTable::getStringView(template_name), 
		          "', num_template_params=", template_params.size(), ", num_pattern_args=", pattern_args.size());
		
		// Debug: log each pattern arg
		for (size_t i = 0; i < pattern_args.size(); ++i) {
			const auto& arg = pattern_args[i];
			std::string_view dep_name_view = arg.dependent_name.isValid() ? StringTable::getStringView(arg.dependent_name) : "";
			FLASH_LOG(Templates, Debug, "  pattern_arg[", i, "]: base_type=", static_cast<int>(arg.base_type),
			          ", type_index=", arg.type_index, ", is_dependent=", arg.is_dependent,
			          ", is_value=", arg.is_value, ", dependent_name='", dep_name_view, "'");
		}
		
		// Debug: log each template param type
		for (size_t i = 0; i < template_params.size(); ++i) {
			FLASH_LOG(Templates, Debug, "  template_param[", i, "]: type_name=", template_params[i].type_name(), 
			          ", is_TemplateParameterNode=", template_params[i].is<TemplateParameterNode>());
		}
		
		TemplatePattern pattern;
		pattern.template_params = template_params;
		pattern.pattern_args = pattern_args;
		pattern.specialized_node = specialized_node;
		pattern.sfinae_condition = sfinae_cond;
		
		// Auto-detect void_t SFINAE patterns if no explicit condition provided.
		// Heuristic: patterns with 2 args where first is dependent and second is void
		// indicate void_t<...> usage. The member name to check is extracted from the
		// first arg's dependent_name if available, otherwise defaults to "type".
		if (!sfinae_cond.has_value() && pattern_args.size() == 2) {
			const auto& first_arg = pattern_args[0];
			const auto& second_arg = pattern_args[1];
			
			// Check: first arg is dependent (template param), second arg is void (from void_t expansion)
			if (first_arg.is_dependent && !second_arg.is_dependent && 
			    second_arg.base_type == Type::Void) {
				// This looks like a void_t SFINAE pattern.
				// Try to extract the member name from available information.
				StringHandle member_name;
				
				// Check if the first arg's dependent_name contains a qualified name like "T::type"
				if (first_arg.dependent_name.isValid()) {
					std::string_view dep_name = StringTable::getStringView(first_arg.dependent_name);
					size_t scope_pos = dep_name.rfind("::");
					if (scope_pos != std::string_view::npos && scope_pos + 2 < dep_name.size()) {
						// Extract the member name after "::"
						std::string_view extracted_member = dep_name.substr(scope_pos + 2);
						member_name = StringTable::getOrInternStringHandle(extracted_member);
						FLASH_LOG(Templates, Debug, "Extracted SFINAE member name '", extracted_member, "' from dependent_name '", dep_name, "'");
					}
				}
				
				// If no member name was extracted, check the type name via type_index
				if (!member_name.isValid() && first_arg.type_index > 0 && first_arg.type_index < gTypeInfo.size()) {
					std::string_view type_name = StringTable::getStringView(gTypeInfo[first_arg.type_index].name());
					size_t scope_pos = type_name.rfind("::");
					if (scope_pos != std::string_view::npos && scope_pos + 2 < type_name.size()) {
						std::string_view extracted_member = type_name.substr(scope_pos + 2);
						member_name = StringTable::getOrInternStringHandle(extracted_member);
						FLASH_LOG(Templates, Debug, "Extracted SFINAE member name '", extracted_member, "' from type_name '", type_name, "'");
					}
				}
				
				// Default to "type" if no member name could be extracted
				// This is the most common pattern (e.g., void_t<typename T::type>)
				if (!member_name.isValid()) {
					member_name = StringTable::getOrInternStringHandle("type");
					FLASH_LOG(Templates, Debug, "Using default SFINAE member name 'type'");
				}
				
				pattern.sfinae_condition = SfinaeCondition(0, member_name);
				FLASH_LOG(Templates, Debug, "Auto-detected void_t SFINAE pattern: checking for ::", 
				          StringTable::getStringView(member_name), " member");
			}
		}
		
		specialization_patterns_[template_name].push_back(std::move(pattern));
		const auto& stored_pattern = specialization_patterns_[template_name].back();
		FLASH_LOG(Templates, Debug, "  Total patterns for '", StringTable::getStringView(template_name), "': ", specialization_patterns_[template_name].size());
		if (stored_pattern.sfinae_condition.has_value()) {
			FLASH_LOG(Templates, Debug, "  SFINAE condition set: check param[", stored_pattern.sfinae_condition->template_param_index, 
			          "]::", StringTable::getStringView(stored_pattern.sfinae_condition->member_name));
		}
	}

	// Register a template specialization pattern (string_view overload)
	void registerSpecializationPattern(std::string_view template_name, 
	                                   const std::vector<ASTNode>& template_params,
	                                   const std::vector<TemplateTypeArg>& pattern_args, 
	                                   ASTNode specialized_node,
	                                   std::optional<SfinaeCondition> sfinae_cond = std::nullopt) {
		StringHandle key = StringTable::getOrInternStringHandle(template_name);
		registerSpecializationPattern(key, template_params, pattern_args, specialized_node, sfinae_cond);
	}

	// Register a template specialization pattern using QualifiedIdentifier (Phase 4).
	void registerSpecializationPattern(QualifiedIdentifier qi,
	                                   const std::vector<ASTNode>& template_params,
	                                   const std::vector<TemplateTypeArg>& pattern_args,
	                                   ASTNode specialized_node,
	                                   std::optional<SfinaeCondition> sfinae_cond = std::nullopt) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			registerSpecializationPattern(name, template_params, pattern_args, specialized_node, sfinae_cond);
		});
	}

	// Register a template specialization (exact match)
	void registerSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, ASTNode specialized_node) {
		SpecializationKey key{std::string(template_name), template_args};
		specializations_[key] = specialized_node;
		FLASH_LOG(Templates, Debug, "registerSpecialization: '", template_name, "' with ", template_args.size(), " args");
	}

	// Register a template specialization using QualifiedIdentifier (Phase 4).
	void registerSpecialization(QualifiedIdentifier qi, const std::vector<TemplateTypeArg>& template_args, ASTNode specialized_node) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			registerSpecialization(name, template_args, specialized_node);
		});
	}

	// Look up an exact template specialization (no pattern matching)
	std::optional<ASTNode> lookupExactSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) const {
		SpecializationKey key{std::string(template_name), template_args};
		
		FLASH_LOG(Templates, Debug, "lookupExactSpecialization: '", template_name, "' with ", template_args.size(), " args");
		
		auto it = specializations_.find(key);
		if (it != specializations_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Look up a template specialization (exact match first, then pattern match)
	std::optional<ASTNode> lookupSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) const {
		FLASH_LOG(Templates, Debug, "lookupSpecialization: template_name='", template_name, "', num_args=", template_args.size());
		
		// First, try exact match
		auto exact = lookupExactSpecialization(template_name, template_args);
		if (exact.has_value()) {
			FLASH_LOG(Templates, Debug, "  Found exact specialization match");
			return exact;
		}
		
		// No exact match - try pattern matching
		FLASH_LOG(Templates, Debug, "  No exact match, trying pattern matching...");
		auto pattern_result = matchSpecializationPattern(template_name, template_args);
		if (pattern_result.has_value()) {
			FLASH_LOG(Templates, Debug, "  Found pattern match!");
		} else {
			FLASH_LOG(Templates, Debug, "  No pattern match found");
		}
		return pattern_result;
	}

	// Look up a template specialization using QualifiedIdentifier (Phase 4).
	// Tries qualified name first, then falls back to unqualified.
	std::optional<ASTNode> lookupSpecialization(QualifiedIdentifier qi, const std::vector<TemplateTypeArg>& template_args) const {
		if (qi.hasNamespace()) {
			StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(
				qi.namespace_handle, qi.identifier_handle);
			auto result = lookupSpecialization(StringTable::getStringView(qualified), template_args);
			if (result.has_value()) return result;
		}
		return lookupSpecialization(StringTable::getStringView(qi.identifier_handle), template_args);
	}
	
	// Find a matching specialization pattern (StringHandle overload)
	std::optional<ASTNode> matchSpecializationPattern(StringHandle template_name, 
	                                                  const std::vector<TemplateTypeArg>& concrete_args) const {
		auto patterns_it = specialization_patterns_.find(template_name);
		if (patterns_it == specialization_patterns_.end()) {
			FLASH_LOG(Templates, Debug, "    No patterns registered for template '", StringTable::getStringView(template_name), "'");
			return std::nullopt;  // No patterns for this template
		}
		
		const std::vector<TemplatePattern>& patterns = patterns_it->second;
		FLASH_LOG(Templates, Debug, "    Found ", patterns.size(), " pattern(s) for template '", StringTable::getStringView(template_name), "'");
		
		const TemplatePattern* best_match = nullptr;
		int best_specificity = -1;
		
		// Find the most specific matching pattern
		for (size_t i = 0; i < patterns.size(); ++i) {
			const auto& pattern = patterns[i];
			FLASH_LOG(Templates, Debug, "    Checking pattern #", i, " (specificity=", pattern.specificity(), ")");
			std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>> substitutions;
			if (pattern.matches(concrete_args, substitutions)) {
				FLASH_LOG(Templates, Debug, "      Pattern #", i, " MATCHES!");
				int spec = pattern.specificity();
				if (spec > best_specificity) {
					best_match = &pattern;
					best_specificity = spec;
					FLASH_LOG(Templates, Debug, "      New best match (specificity=", spec, ")");
				}
			} else {
				FLASH_LOG(Templates, Debug, "      Pattern #", i, " does not match");
			}
		}
		
		if (best_match) {
			FLASH_LOG(Templates, Debug, "    Selected best pattern (specificity=", best_specificity, ")");
			return best_match->specialized_node;
		}
		
		FLASH_LOG(Templates, Debug, "    No matching pattern found");
		return std::nullopt;
	}

	// Find a matching specialization pattern (string_view overload)
	std::optional<ASTNode> matchSpecializationPattern(std::string_view template_name, 
	                                                  const std::vector<TemplateTypeArg>& concrete_args) const {
		// Heterogeneous lookup - string_view accepted directly
		auto patterns_it = specialization_patterns_.find(template_name);
		if (patterns_it == specialization_patterns_.end()) {
			FLASH_LOG(Templates, Debug, "    No patterns registered for template '", template_name, "'");
			return std::nullopt;  // No patterns for this template
		}
		
		const std::vector<TemplatePattern>& patterns = patterns_it->second;
		FLASH_LOG(Templates, Debug, "    Found ", patterns.size(), " pattern(s) for template '", template_name, "'");
		
		const TemplatePattern* best_match = nullptr;
		int best_specificity = -1;
		
		// Find the most specific matching pattern
		for (size_t i = 0; i < patterns.size(); ++i) {
			const auto& pattern = patterns[i];
			FLASH_LOG(Templates, Debug, "    Checking pattern #", i, " (specificity=", pattern.specificity(), ")");
			std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>> substitutions;
			if (pattern.matches(concrete_args, substitutions)) {
				FLASH_LOG(Templates, Debug, "      Pattern #", i, " MATCHES!");
				int spec = pattern.specificity();
				if (spec > best_specificity) {
					best_match = &pattern;
					best_specificity = spec;
					FLASH_LOG(Templates, Debug, "      New best match (specificity=", spec, ")");
				}
			} else {
				FLASH_LOG(Templates, Debug, "      Pattern #", i, " does not match");
			}
		}
		
		if (best_match) {
			FLASH_LOG(Templates, Debug, "    Selected best pattern (specificity=", best_specificity, ")");
			return best_match->specialized_node;
		}
		
		FLASH_LOG(Templates, Debug, "    No matching pattern found");
		return std::nullopt;
	}

	// Clear all templates and instantiations
	void clear() {
		templates_.clear();
		template_parameters_.clear();
		instantiations_.clear();
		out_of_line_variables_.clear();
		out_of_line_members_.clear();
		out_of_line_nested_classes_.clear();
		specializations_.clear();
		specialization_patterns_.clear();
		alias_templates_.clear();
		variable_templates_.clear();
		variable_template_specializations_.clear();
		deduction_guides_.clear();
		instantiation_to_pattern_.clear();
		class_template_names_.clear();
		pattern_struct_names_.clear();
		outer_template_bindings_.clear();
	}

	// Public access to specialization patterns for pattern matching in Parser
	std::unordered_map<StringHandle, std::vector<TemplatePattern>, TransparentStringHash, TransparentStringEqual> specialization_patterns_;
	
	// Register a pattern struct name (for partial specializations)
	void registerPatternStructName(StringHandle pattern_name) {
		pattern_struct_names_.insert(pattern_name);
	}

	// Returns true if 'name' was registered as a pattern struct name
	// (partial specialization pattern like "Container_pattern_TP")
	bool isPatternStructName(StringHandle name) const {
		return pattern_struct_names_.count(name) > 0;
	}

	// Register mapping from instantiated name to pattern name (for partial specializations)
	void register_instantiation_pattern(StringHandle instantiated_name, StringHandle pattern_name) {
		instantiation_to_pattern_[instantiated_name] = pattern_name;
		pattern_struct_names_.insert(pattern_name);
	}

	
	// Look up which pattern was used for an instantiation
	std::optional<StringHandle> get_instantiation_pattern(StringHandle instantiated_name) const {
		auto it = instantiation_to_pattern_.find(instantiated_name);
		if (it != instantiation_to_pattern_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

private:
	// Helper: Given a QualifiedIdentifier, call `fn` with both the unqualified name
	// and (if the identifier has a non-global namespace) the fully-qualified name.
	// Used by all QualifiedIdentifier registration overloads to eliminate duplication.
	template<typename Fn>
	void forEachQualifiedName(QualifiedIdentifier qi, Fn&& fn) {
		std::string_view simple = StringTable::getStringView(qi.identifier_handle);
		fn(simple);
		if (qi.hasNamespace()) {
			StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(
				qi.namespace_handle, qi.identifier_handle);
			std::string_view qualified_name = StringTable::getStringView(qualified);
			if (qualified_name != simple) {
				fn(qualified_name);
			}
		}
	}

	// Map from template name to template declaration nodes (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, std::vector<ASTNode>, StringHandleHash, std::equal_to<>> templates_;

	// Map from template name to template parameter names (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, std::vector<StringHandle>, StringHandleHash, std::equal_to<>> template_parameters_;

	// Map from alias template name to TemplateAliasNode (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, ASTNode, StringHandleHash, std::equal_to<>> alias_templates_;

	// Map from variable template name to TemplateVariableDeclarationNode (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, ASTNode, StringHandleHash, std::equal_to<>> variable_templates_;

	// Map from variable template base name to partial specialization patterns (structural matching)
	std::unordered_map<StringHandle, std::vector<TemplatePattern>, StringHandleHash, std::equal_to<>> variable_template_specializations_;

	// Map from class template name to deduction guides (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, std::vector<ASTNode>, StringHandleHash, std::equal_to<>> deduction_guides_;

	// TypeIndex-based template instantiation cache
	// Uses TemplateInstantiationKey for O(1) lookup without string concatenation
	std::unordered_map<FlashCpp::TemplateInstantiationKey, ASTNode, FlashCpp::TemplateInstantiationKeyHash> instantiations_;

	// Map from class name to out-of-line member function definitions (StringHandle key for efficient lookup)
	std::unordered_map<StringHandle, std::vector<OutOfLineMemberFunction>, TransparentStringHash, TransparentStringEqual> out_of_line_members_;

	// Map from class name to out-of-line static member variable definitions (StringHandle key for efficient lookup)
	std::unordered_map<StringHandle, std::vector<OutOfLineMemberVariable>, TransparentStringHash, TransparentStringEqual> out_of_line_variables_;

	// Map from class name to out-of-line nested class definitions (StringHandle key for efficient lookup)
	std::unordered_map<StringHandle, std::vector<OutOfLineNestedClass>, TransparentStringHash, TransparentStringEqual> out_of_line_nested_classes_;

	// Map from qualified member function template name (e.g., "Container$hash::convert") to
	// outer template parameter bindings (e.g., T→int). Used during nested template instantiation.
	std::unordered_map<StringHandle, OuterTemplateBinding, StringHandleHash, std::equal_to<>> outer_template_bindings_;

	// Map from (template_name, template_args) to specialized class node (exact matches)
	std::unordered_map<SpecializationKey, ASTNode, SpecializationKeyHash> specializations_;
	
	// Map from instantiated struct name to the pattern struct name used (for partial specializations)
	// Example: "Wrapper_int_0" -> "Wrapper_pattern__"
	// This allows looking up member aliases from the correct specialization
	std::unordered_map<StringHandle, StringHandle, StringHandleHash, std::equal_to<>> instantiation_to_pattern_;

	// Set of StringHandles that were registered as class templates (TemplateClassDeclarationNode).
	// Used by isClassTemplate() for O(1) exact-name lookup, avoiding substring searches
	// and false positives from unqualified-name fallbacks in lookupTemplate().
	std::unordered_set<StringHandle, StringHandleHash> class_template_names_;

	// Set of StringHandles that are pattern struct names (partial specialization patterns).
	// Used by isPatternStructName() for O(1) lookup, replacing find("_pattern_") substring searches.
	std::unordered_set<StringHandle, StringHandleHash> pattern_struct_names_;


};

// Global template registry
extern TemplateRegistry gTemplateRegistry;

// ============================================================================
// Template name extraction helper
// ============================================================================

/**
 * Extract the base template name from an instantiated name.
 *
 * Checks gTypesByName for the name — if the TypeInfo has
 * isTemplateInstantiation() metadata, returns baseTemplateName() directly.
 * Returns empty string_view if the name is not a template instantiation.
 */
inline std::string_view extractBaseTemplateName(std::string_view name) {
	auto name_handle = StringTable::getOrInternStringHandle(name);
	auto type_it = gTypesByName.find(name_handle);
	if (type_it != gTypesByName.end() && type_it->second->isTemplateInstantiation()) {
		return StringTable::getStringView(type_it->second->baseTemplateName());
	}
	return {};
}

// ============================================================================
// Lazy Template Member Function Instantiation Registry
// ============================================================================

// Information needed to instantiate a template member function on-demand

// Lazy registries and ConceptRegistry split out for maintainability.
#include "TemplateRegistry_Lazy.cpp"  // LazyMemberInstantiationRegistry, LazyClassInstantiationRegistry, ConceptRegistry, etc.
