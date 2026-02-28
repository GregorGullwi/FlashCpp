// Strip namespace prefix from a class name handle (e.g., "ns::Foo$hash" -> "Foo$hash").
// Used by lazy registries so lookups match regardless of qualification.
static StringHandle normalizeClassName(StringHandle handle) {
	std::string_view name = StringTable::getStringView(handle);
	if (size_t pos = name.rfind("::"); pos != std::string_view::npos) {
		return StringTable::getOrInternStringHandle(name.substr(pos + 2));
	}
	return handle;
}

struct LazyMemberFunctionInfo {
	StringHandle class_template_name;          // Original template name (e.g., "vector")
	StringHandle instantiated_class_name;      // Instantiated class name (e.g., "vector_int")
	StringHandle member_function_name;         // Member function name
	ASTNode original_function_node;            // Original function from template
	std::vector<ASTNode> template_params;      // Template parameters from class template
	std::vector<TemplateTypeArg> template_args; // Concrete template arguments used for instantiation
	AccessSpecifier access;                    // Access specifier (public/private/protected)
	bool is_virtual;                           // Virtual function flag
	bool is_pure_virtual;                      // Pure virtual flag
	bool is_override;                          // Override flag
	bool is_final;                             // Final flag
	bool is_const_method;                      // Const member function flag
	bool is_constructor;                       // Constructor flag
	bool is_destructor;                        // Destructor flag
};

// Registry for tracking uninstantiated template member functions
// Allows lazy (on-demand) instantiation for better compilation performance
class LazyMemberInstantiationRegistry {
public:
	static LazyMemberInstantiationRegistry& getInstance() {
		static LazyMemberInstantiationRegistry instance;
		return instance;
	}
	
	// Register a member function for lazy instantiation
	// Key format: "instantiated_class_name::member_function_name"
	void registerLazyMember(LazyMemberFunctionInfo info) {
		StringHandle normalized_class = normalizeClassName(info.instantiated_class_name);
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(normalized_class)
			.append("::")
			.append(info.member_function_name)
			.commit();
		
		lazy_members_[StringTable::getOrInternStringHandle(key)] = std::move(info);
	}
	
	// Check if a member function needs lazy instantiation
	bool needsInstantiation(StringHandle instantiated_class_name, StringHandle member_function_name) const {
		instantiated_class_name = normalizeClassName(instantiated_class_name);
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(instantiated_class_name)
			.append("::")
			.append(member_function_name)
			.commit();  // Changed from preview() to commit()
		
		auto handle = StringTable::getOrInternStringHandle(key);
		return lazy_members_.find(handle) != lazy_members_.end();
	}
	
	// Get lazy member info for instantiation
	std::optional<LazyMemberFunctionInfo> getLazyMemberInfo(StringHandle instantiated_class_name, StringHandle member_function_name) {
		instantiated_class_name = normalizeClassName(instantiated_class_name);
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(instantiated_class_name)
			.append("::")
			.append(member_function_name)
			.commit();
		
		auto handle = StringTable::getOrInternStringHandle(key);
		auto it = lazy_members_.find(handle);
		if (it != lazy_members_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Mark a member function as instantiated (remove from lazy registry)
	void markInstantiated(StringHandle instantiated_class_name, StringHandle member_function_name) {
		instantiated_class_name = normalizeClassName(instantiated_class_name);
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(instantiated_class_name)
			.append("::")
			.append(member_function_name)
			.commit();
		
		auto handle = StringTable::getOrInternStringHandle(key);
		lazy_members_.erase(handle);
	}
	
	// Clear all lazy members (for testing)
	void clear() {
		lazy_members_.clear();
	}
	
	// Get count of uninstantiated members (for diagnostics)
	size_t getUninstantiatedCount() const {
		return lazy_members_.size();
	}

private:
	LazyMemberInstantiationRegistry() = default;
	
	// Map from "instantiated_class::member_function" to lazy instantiation info
	std::unordered_map<StringHandle, LazyMemberFunctionInfo, TransparentStringHash, std::equal_to<>> lazy_members_;
};

// Global lazy member instantiation registry
// Note: Use LazyMemberInstantiationRegistry::getInstance() to access
// (cannot use global variable due to singleton pattern)

// ============================================================================
// Lazy Static Member Instantiation Registry
// ============================================================================

// Information needed to instantiate a template static member on-demand
struct LazyStaticMemberInfo {
	StringHandle class_template_name;          // Original template name (e.g., "integral_constant")
	StringHandle instantiated_class_name;      // Instantiated class name (e.g., "integral_constant_bool_true")
	StringHandle member_name;                  // Static member name (e.g., "value")
	Type type;                                 // Member type
	TypeIndex type_index;                      // Type index for complex types
	size_t size;                               // Size in bytes
	size_t alignment;                          // Alignment requirement
	AccessSpecifier access;                    // Access specifier
	std::optional<ASTNode> initializer;        // Original initializer (may need substitution)
	CVQualifier cv_qualifier = CVQualifier::None; // CV qualifiers (const/volatile)
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None; // Reference qualifier (lvalue/rvalue)
	int pointer_depth = 0;                     // Pointer depth (e.g., 1 for int*, 2 for int**)
	std::vector<ASTNode> template_params;      // Template parameters from class template
	std::vector<TemplateTypeArg> template_args; // Concrete template arguments
	bool needs_substitution;                   // True if initializer contains template parameters
};

// Registry for tracking uninstantiated template static members
// Allows lazy (on-demand) instantiation for better compilation performance
// Particularly beneficial for type_traits which have many static constexpr members
class LazyStaticMemberRegistry {
public:
	static LazyStaticMemberRegistry& getInstance() {
		static LazyStaticMemberRegistry instance;
		return instance;
	}
	
	// Register a static member for lazy instantiation
	// Key format: "instantiated_class_name::member_name"
	void registerLazyStaticMember(const LazyStaticMemberInfo& info) {
		StringHandle key = makeKey(info.instantiated_class_name, info.member_name);
		FLASH_LOG(Templates, Debug, "Registering lazy static member: ", key);
		lazy_static_members_[key] = info;
	}
	
	// Check if a static member needs lazy instantiation
	bool needsInstantiation(StringHandle instantiated_class_name, StringHandle member_name) const {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		return lazy_static_members_.find(key) != lazy_static_members_.end();
	}
	
	// Get lazy static member info for instantiation
	// Returns a pointer to avoid copying; nullptr if not found
	const LazyStaticMemberInfo* getLazyStaticMemberInfo(StringHandle instantiated_class_name, StringHandle member_name) const {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		auto it = lazy_static_members_.find(key);
		if (it != lazy_static_members_.end()) {
			return &it->second;
		}
		return nullptr;
	}
	
	// Mark a static member as instantiated (remove from lazy registry)
	void markInstantiated(StringHandle instantiated_class_name, StringHandle member_name) {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		lazy_static_members_.erase(key);
		FLASH_LOG(Templates, Debug, "Marked lazy static member as instantiated: ", key);
	}
	
	// Clear all lazy static members (for testing)
	void clear() {
		lazy_static_members_.clear();
	}
	
	// Get count of uninstantiated static members (for diagnostics)
	size_t getUninstantiatedCount() const {
		return lazy_static_members_.size();
	}

private:
	LazyStaticMemberRegistry() = default;
	
	// Helper to generate registry key from class name and member name
	// Key format: "instantiated_class_name::member_name"
	static StringHandle makeKey(StringHandle class_name, StringHandle member_name) {
		class_name = normalizeClassName(class_name);
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(class_name)
			.append("::")
			.append(member_name)
			.commit();
		return StringTable::getOrInternStringHandle(key);
	}

	
	// Map from "instantiated_class::static_member" to lazy instantiation info
	std::unordered_map<StringHandle, LazyStaticMemberInfo, TransparentStringHash, std::equal_to<>> lazy_static_members_;
};

// ============================================================================
// Phase 2: Lazy Class Instantiation Registry
// ============================================================================

// Instantiation phases for three-phase class instantiation
// Each phase represents a level of completeness of the instantiation
enum class ClassInstantiationPhase : uint8_t {
	None = 0,           // Not yet instantiated
	Minimal = 1,        // Type entry created, name registered (triggered by any type name use)
	Layout = 2,         // Size/alignment computed (triggered by sizeof, alignof, variable declarations)
	Full = 3            // All members, base classes, and static members instantiated (triggered by member access)
};

// Information needed for lazy (phased) class template instantiation
// Allows deferring complete instantiation until members are actually used
struct LazyClassInstantiationInfo {
	StringHandle template_name;                    // Original template name (e.g., "vector")
	StringHandle instantiated_name;                // Instantiated class name (e.g., "vector_int")
	std::vector<TemplateTypeArg> template_args;    // Concrete template arguments
	std::vector<ASTNode> template_params;          // Template parameters from class template
	ASTNode template_declaration;                  // Reference to primary template declaration
	ClassInstantiationPhase current_phase = ClassInstantiationPhase::None;
	// Flags for tracking what needs to be instantiated in Full phase
	// These are set during Minimal phase to avoid re-parsing template declaration
	bool has_base_classes = false;                 // Does the template have base classes?
	bool has_static_members = false;               // Does the template have static members?
	bool has_member_functions = false;             // Does the template have member functions?
	TypeIndex type_index = 0;                      // Type index once minimal instantiation is done
};

// Registry for tracking partially instantiated template classes
// Enables three-phase instantiation:
// - Minimal: Create type entry, register name - triggered by any type name use
// - Layout: Compute size/alignment - triggered by sizeof, alignof, variable declarations
// - Full: Instantiate all members - triggered by member access, method calls
class LazyClassInstantiationRegistry {
public:
	static LazyClassInstantiationRegistry& getInstance() {
		static LazyClassInstantiationRegistry instance;
		return instance;
	}
	
	// Register a class for lazy instantiation
	void registerLazyClass(const LazyClassInstantiationInfo& info) {
		FLASH_LOG(Templates, Debug, "Registering lazy class: ", info.instantiated_name,
		          " (template: ", info.template_name, ")");
		lazy_classes_[info.instantiated_name] = info;
	}
	
	// Check if a class is registered for lazy instantiation
	bool isRegistered(StringHandle instantiated_name) const {
		return lazy_classes_.find(instantiated_name) != lazy_classes_.end();
	}
	
	// Get the current instantiation phase of a class
	ClassInstantiationPhase getCurrentPhase(StringHandle instantiated_name) const {
		auto it = lazy_classes_.find(instantiated_name);
		if (it != lazy_classes_.end()) {
			return it->second.current_phase;
		}
		return ClassInstantiationPhase::None;
	}
	
	// Check if a class needs instantiation to the specified phase
	// Uses enum underlying values for comparison (None=0 < Minimal=1 < Layout=2 < Full=3)
	bool needsInstantiationTo(StringHandle instantiated_name, ClassInstantiationPhase target_phase) const {
		auto it = lazy_classes_.find(instantiated_name);
		if (it == lazy_classes_.end()) {
			return false;  // Not registered for lazy instantiation
		}
		return static_cast<uint8_t>(it->second.current_phase) < static_cast<uint8_t>(target_phase);
	}
	
	// Get lazy class info for instantiation
	// Returns a pointer to avoid copying; nullptr if not found
	const LazyClassInstantiationInfo* getLazyClassInfo(StringHandle instantiated_name) const {
		auto it = lazy_classes_.find(instantiated_name);
		if (it != lazy_classes_.end()) {
			return &it->second;
		}
		return nullptr;
	}
	
	// Get mutable lazy class info for updating phase
	LazyClassInstantiationInfo* getLazyClassInfoMutable(StringHandle instantiated_name) {
		auto it = lazy_classes_.find(instantiated_name);
		if (it != lazy_classes_.end()) {
			return &it->second;
		}
		return nullptr;
	}
	
	// Update the instantiation phase of a class
	void updatePhase(StringHandle instantiated_name, ClassInstantiationPhase new_phase) {
		auto it = lazy_classes_.find(instantiated_name);
		if (it != lazy_classes_.end()) {
			FLASH_LOG(Templates, Debug, "Updating lazy class phase: ", instantiated_name,
			          " from ", static_cast<int>(it->second.current_phase),
			          " to ", static_cast<int>(new_phase));
			it->second.current_phase = new_phase;
		}
	}
	
	// Mark a class as fully instantiated (remove from lazy registry)
	void markFullyInstantiated(StringHandle instantiated_name) {
		lazy_classes_.erase(instantiated_name);
		FLASH_LOG(Templates, Debug, "Marked lazy class as fully instantiated: ", instantiated_name);
	}
	
	// Clear all lazy classes (for testing)
	void clear() {
		lazy_classes_.clear();
	}
	
	// Get count of partially instantiated classes (for diagnostics)
	size_t getPartiallyInstantiatedCount() const {
		return lazy_classes_.size();
	}

private:
	LazyClassInstantiationRegistry() = default;
	
	// Map from instantiated class name to lazy instantiation info
	std::unordered_map<StringHandle, LazyClassInstantiationInfo, TransparentStringHash, std::equal_to<>> lazy_classes_;
};

// ============================================================================
// Phase 3: Lazy Type Alias Evaluation Registry
// ============================================================================

// Information needed for lazy type alias evaluation
// Allows deferring evaluation of template type aliases until actually accessed
struct LazyTypeAliasInfo {
	StringHandle alias_name;                       // Full alias name (e.g., "remove_const_int::type")
	StringHandle template_name;                    // Original template name (e.g., "remove_const")
	StringHandle instantiated_class_name;          // Instantiated class name (e.g., "remove_const_int")
	StringHandle member_name;                      // Member alias name (e.g., "type")
	ASTNode unevaluated_target;                    // Unevaluated target type expression
	std::vector<ASTNode> template_params;          // Template parameters from class template
	std::vector<TemplateTypeArg> template_args;    // Concrete template arguments
	bool needs_substitution = true;                // True if target contains template parameters
	bool is_evaluated = false;                     // True once evaluation has been performed
	// Cached evaluation result (to avoid re-computation)
	Type evaluated_type = Type::Invalid;
	TypeIndex evaluated_type_index = 0;
};

// Registry for tracking unevaluated template type aliases
// Enables lazy evaluation: aliases are not evaluated until ::type is accessed
// Particularly beneficial for type_traits where many aliases are defined but only some are used
class LazyTypeAliasRegistry {
public:
	static LazyTypeAliasRegistry& getInstance() {
		static LazyTypeAliasRegistry instance;
		return instance;
	}
	
	// Register a type alias for lazy evaluation
	// Key format: "instantiated_class_name::member_name"
	void registerLazyTypeAlias(const LazyTypeAliasInfo& info) {
		StringHandle key = makeKey(info.instantiated_class_name, info.member_name);
		FLASH_LOG(Templates, Debug, "Registering lazy type alias: ", key);
		lazy_aliases_[key] = info;
	}
	
	// Check if a type alias needs lazy evaluation (registered and not yet evaluated)
	bool needsEvaluation(StringHandle instantiated_class_name, StringHandle member_name) const {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		auto it = lazy_aliases_.find(key);
		return it != lazy_aliases_.end() && !it->second.is_evaluated;
	}
	
	// Get lazy type alias info
	// Returns a pointer to avoid copying; nullptr if not found
	// Use this instead of isRegistered() when you need the actual data
	const LazyTypeAliasInfo* getLazyTypeAliasInfo(StringHandle instantiated_class_name, StringHandle member_name) const {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		auto it = lazy_aliases_.find(key);
		if (it != lazy_aliases_.end()) {
			return &it->second;
		}
		return nullptr;
	}
	
	// Get mutable lazy type alias info for updating evaluation result
	LazyTypeAliasInfo* getLazyTypeAliasInfoMutable(StringHandle instantiated_class_name, StringHandle member_name) {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		auto it = lazy_aliases_.find(key);
		if (it != lazy_aliases_.end()) {
			return &it->second;
		}
		return nullptr;
	}
	
	// Mark a type alias as evaluated and cache the result
	// Returns true if the alias was found and marked, false if not registered
	bool markEvaluated(StringHandle instantiated_class_name, StringHandle member_name, 
	                   Type result_type, TypeIndex result_type_index) {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		auto it = lazy_aliases_.find(key);
		if (it != lazy_aliases_.end()) {
			it->second.is_evaluated = true;
			it->second.evaluated_type = result_type;
			it->second.evaluated_type_index = result_type_index;
			FLASH_LOG(Templates, Debug, "Marked lazy type alias as evaluated: ", key);
			return true;
		}
		FLASH_LOG(Templates, Warning, "Attempted to mark unregistered type alias as evaluated: ", key);
		return false;
	}
	
	// Get cached evaluation result (only valid if is_evaluated is true)
	std::optional<std::pair<Type, TypeIndex>> getCachedResult(StringHandle instantiated_class_name, 
	                                                           StringHandle member_name) const {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		auto it = lazy_aliases_.find(key);
		if (it != lazy_aliases_.end() && it->second.is_evaluated) {
			return std::pair<Type, TypeIndex>{it->second.evaluated_type, it->second.evaluated_type_index};
		}
		return std::nullopt;
	}
	
	// Clear all lazy type aliases (for testing)
	void clear() {
		lazy_aliases_.clear();
	}
	
	// Get count of unevaluated type aliases (for diagnostics)
	size_t getUnevaluatedCount() const {
		size_t count = 0;
		for (const auto& [key, info] : lazy_aliases_) {
			if (!info.is_evaluated) {
				count++;
			}
		}
		return count;
	}
	
	// Get total count of registered type aliases (for diagnostics)
	size_t getTotalCount() const {
		return lazy_aliases_.size();
	}

private:
	LazyTypeAliasRegistry() = default;
	
	// Helper to generate registry key from class name and member name
	// Key format: "instantiated_class_name::member_name"
	static StringHandle makeKey(StringHandle class_name, StringHandle member_name) {
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(class_name)
			.append("::")
			.append(member_name)
			.commit();
		return StringTable::getOrInternStringHandle(key);
	}
	
	// Map from "instantiated_class::type_alias" to lazy evaluation info
	std::unordered_map<StringHandle, LazyTypeAliasInfo, TransparentStringHash, std::equal_to<>> lazy_aliases_;
};

// ============================================================================
// Phase 4: Lazy Nested Type Instantiation Registry
// ============================================================================

// Information needed for lazy nested type instantiation
// Allows deferring instantiation of nested types (inner classes/structs) until actually accessed
struct LazyNestedTypeInfo {
	StringHandle parent_class_name;                // Parent instantiated class name (e.g., "outer_int")
	StringHandle nested_type_name;                 // Nested type name (e.g., "inner")
	StringHandle qualified_name;                   // Fully qualified name (e.g., "outer_int::inner")
	ASTNode nested_type_declaration;               // The nested struct/class declaration AST node
	std::vector<ASTNode> parent_template_params;   // Template parameters from parent class
	std::vector<TemplateTypeArg> parent_template_args; // Concrete template arguments for parent
};

// Registry for tracking uninstantiated nested types
// Enables lazy instantiation: nested types are not instantiated until accessed
// Note: Entries are removed from the registry once instantiated (consistent with other lazy registries)
// Example:
//   template<typename T> struct outer { struct inner { T value; }; };
//   outer<int> o;           // inner is NOT instantiated
//   outer<int>::inner i;    // NOW inner is instantiated
class LazyNestedTypeRegistry {
public:
	static LazyNestedTypeRegistry& getInstance() {
		static LazyNestedTypeRegistry instance;
		return instance;
	}
	
	// Register a nested type for lazy instantiation
	// Key format: "parent_class_name::nested_type_name"
	void registerLazyNestedType(const LazyNestedTypeInfo& info) {
		StringHandle key = makeKey(info.parent_class_name, info.nested_type_name);
		FLASH_LOG(Templates, Debug, "Registering lazy nested type: ", key);
		lazy_nested_types_[key] = info;
	}
	
	// Check if a nested type needs lazy instantiation (entry exists in registry)
	// Note: Once instantiated, the entry is removed from the registry
	bool needsInstantiation(StringHandle parent_class_name, StringHandle nested_type_name) const {
		StringHandle key = makeKey(parent_class_name, nested_type_name);
		return lazy_nested_types_.find(key) != lazy_nested_types_.end();
	}
	
	// Get lazy nested type info
	// Returns a pointer to avoid copying; nullptr if not found
	// Use this instead of a separate isRegistered() method
	const LazyNestedTypeInfo* getLazyNestedTypeInfo(StringHandle parent_class_name, StringHandle nested_type_name) const {
		StringHandle key = makeKey(parent_class_name, nested_type_name);
		auto it = lazy_nested_types_.find(key);
		if (it != lazy_nested_types_.end()) {
			return &it->second;
		}
		return nullptr;
	}
	
	// Mark a nested type as instantiated (remove from lazy registry)
	// Consistent with other lazy registries - entries are removed after instantiation
	void markInstantiated(StringHandle parent_class_name, StringHandle nested_type_name) {
		StringHandle key = makeKey(parent_class_name, nested_type_name);
		lazy_nested_types_.erase(key);
		FLASH_LOG(Templates, Debug, "Marked lazy nested type as instantiated: ", key);
	}
	
	// Get all nested types for a parent class that need instantiation
	std::vector<const LazyNestedTypeInfo*> getNestedTypesForParent(StringHandle parent_class_name) const {
		std::vector<const LazyNestedTypeInfo*> result;
		for (const auto& [key, info] : lazy_nested_types_) {
			if (info.parent_class_name == parent_class_name) {
				result.push_back(&info);
			}
		}
		return result;
	}
	
	// Clear all lazy nested types (for testing)
	void clear() {
		lazy_nested_types_.clear();
	}
	
	// Get count of pending (uninstantiated) nested types (for diagnostics)
	// Note: Since entries are removed after instantiation, this is the same as total count
	size_t getPendingCount() const {
		return lazy_nested_types_.size();
	}

private:
	LazyNestedTypeRegistry() = default;
	
	// Helper to generate registry key from parent class name and nested type name
	// Key format: "parent_class_name::nested_type_name"
	static StringHandle makeKey(StringHandle parent_name, StringHandle nested_name) {
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(parent_name)
			.append("::")
			.append(nested_name)
			.commit();
		return StringTable::getOrInternStringHandle(key);
	}
	
	// Map from "parent_class::nested_type" to lazy instantiation info
	std::unordered_map<StringHandle, LazyNestedTypeInfo, TransparentStringHash, std::equal_to<>> lazy_nested_types_;
};

// ============================================================================
// C++20 Concepts Registry (inline with TemplateRegistry since they're related)
// ============================================================================

// Concept registry for storing and looking up C++20 concept declarations
// Concepts are named constraints that can be used to constrain template parameters
class ConceptRegistry {
public:
	ConceptRegistry() = default;

	// Register a concept declaration
	// concept_name: The name of the concept (e.g., "Integral", "Addable")
	// concept_node: The ConceptDeclarationNode AST node
	void registerConcept(std::string_view concept_name, ASTNode concept_node) {
		std::string key(concept_name);
		concepts_[key] = concept_node;
	}

	// Look up a concept by name
	// Returns the ConceptDeclarationNode if found, std::nullopt otherwise
	std::optional<ASTNode> lookupConcept(std::string_view concept_name) const {
		auto it = concepts_.find(concept_name);
		if (it != concepts_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Check if a concept exists
	bool hasConcept(std::string_view concept_name) const {
		return concepts_.find(concept_name) != concepts_.end();
	}

	// Clear all concepts (for testing)
	void clear() {
		concepts_.clear();
	}

	// Get all concept names (for debugging)
	std::vector<std::string> getAllConceptNames() const {
		std::vector<std::string> names;
		names.reserve(concepts_.size());
		for (const auto& pair : concepts_) {
			names.push_back(pair.first);
		}
		return names;
	}

private:
	// Map from concept name to ConceptDeclarationNode
	// Using TransparentStringHash for heterogeneous lookup with string_view
	std::unordered_map<std::string, ASTNode, TransparentStringHash, std::equal_to<>> concepts_;
};

// Global concept registry
extern ConceptRegistry gConceptRegistry;

// ============================================================================
// Concept Subsumption for C++20
// ============================================================================

// Check if constraint A subsumes constraint B
// A subsumes B if whenever A is satisfied, B is also satisfied
// In practice: A subsumes B if A's requirements are a superset of B's
inline bool constraintSubsumes(const ASTNode& constraintA, const ASTNode& constraintB) {
	// Advanced subsumption rules:
	// 1. Identical constraints subsume each other
	// 2. A && B subsumes A (conjunction implies the parts)
	// 3. A && B subsumes B (conjunction implies the parts)
	// 4. A subsumes A || B (A is stronger than disjunction with A)
	// 5. A && !B does not subsume A (negation creates incompatibility)
	// 6. Transitivity: if A subsumes B and B subsumes C, then A subsumes C
	// 7. A && B && C subsumes A && B (more constraints = more specific)
	
	// If constraints are identical, they subsume each other
	// This is a simplified check - full implementation would need deep comparison
	if (constraintA.type_name() == constraintB.type_name()) {
		// Same type - might be the same constraint
		// For full correctness, we'd need to compare the actual expressions
		return true;
	}
	
	// Check if A is a conjunction that includes B
	if (constraintA.is<BinaryOperatorNode>()) {
		const auto& binop = constraintA.as<BinaryOperatorNode>();
		if (binop.op() == "&&") {
			// A = X && Y, check if X or Y subsumes B
			if (constraintSubsumes(binop.get_lhs(), constraintB)) {
				return true;
			}
			if (constraintSubsumes(binop.get_rhs(), constraintB)) {
				return true;
			}
			
			// Check transitive subsumption: (A && B) subsumes C if A subsumes C or B subsumes C
			// Already handled above
		}
		
		// Handle negation: !A does not subsume A
		if (binop.op() == "||") {
			// A = X || Y does not generally subsume anything
			// (disjunction is weaker than either branch)
			return false;
		}
	}
	
	// Check if A is a unary negation operator
	if (constraintA.is<UnaryOperatorNode>()) {
		const auto& unop = constraintA.as<UnaryOperatorNode>();
		if (unop.op() == "!") {
			// !A does not subsume A (they're contradictory)
			// !A subsumes !(A && B) is complex, skip for now
			return false;
		}
	}
	
	// Check if B is a disjunction where A subsumes one branch
	if (constraintB.is<BinaryOperatorNode>()) {
		const auto& binop = constraintB.as<BinaryOperatorNode>();
		if (binop.op() == "||") {
			// B = X || Y, A subsumes B if A subsumes both X and Y
			if (constraintSubsumes(constraintA, binop.get_lhs()) && 
			    constraintSubsumes(constraintA, binop.get_rhs())) {
				return true;
			}
		}
		
		// Check if B is a conjunction where A subsumes the whole conjunction
		if (binop.op() == "&&") {
			// B = X && Y, A subsumes B if A subsumes (X && Y) as a whole
			// This is tricky: A subsumes (X && Y) if A subsumes at least one of them
			// Example: A subsumes (A && B) because A is less restrictive
			// But we already check if constraintA matches constraintB above
			// So skip detailed analysis here
		}
	}
	
	return false;  // Conservative: assume no subsumption
}

// Compare two concepts for subsumption ordering
// Returns: -1 if A subsumes B, 1 if B subsumes A, 0 if neither
inline int compareConceptSubsumption(const ASTNode& conceptA, const ASTNode& conceptB) {
	// Get constraint expressions from concepts
	const ASTNode* exprA = nullptr;
	const ASTNode* exprB = nullptr;
	
	if (conceptA.is<ConceptDeclarationNode>()) {
		exprA = &conceptA.as<ConceptDeclarationNode>().constraint_expr();
	}
	if (conceptB.is<ConceptDeclarationNode>()) {
		exprB = &conceptB.as<ConceptDeclarationNode>().constraint_expr();
	}
	
	if (!exprA || !exprB) {
		return 0;  // Can't compare
	}
	
	bool a_subsumes_b = constraintSubsumes(*exprA, *exprB);
	bool b_subsumes_a = constraintSubsumes(*exprB, *exprA);
	
	if (a_subsumes_b && !b_subsumes_a) {
		return -1;  // A is more specific (subsumes B)
	}
	if (b_subsumes_a && !a_subsumes_b) {
		return 1;   // B is more specific (subsumes A)
	}
	
	return 0;  // Neither subsumes the other (or both do - equivalent)
}

// ============================================================================
// Constraint Evaluation for C++20 Concepts
// ============================================================================

// Result of constraint evaluation
struct ConstraintEvaluationResult {
	bool satisfied;
	std::string error_message;
	std::string failed_requirement;
	std::string suggestion;
	
	static ConstraintEvaluationResult success() {
		return ConstraintEvaluationResult{true, "", "", ""};
	}
	
	static ConstraintEvaluationResult failure(std::string_view error_msg, 
	                                          std::string_view failed_req = "",
	                                          std::string_view suggestion = "") {
		return ConstraintEvaluationResult{
			false, 
			std::string(error_msg), 
			std::string(failed_req),
			std::string(suggestion)
		};
	}
};

// Helper function to check if a type is integral
inline bool isIntegralType(Type type) {
	switch (type) {
		case Type::Bool:
		case Type::Char:
		case Type::Short:
		case Type::Int:
		case Type::Long:
		case Type::LongLong:
		case Type::UnsignedChar:
		case Type::UnsignedShort:
		case Type::UnsignedInt:
		case Type::UnsignedLong:
		case Type::UnsignedLongLong:
			return true;
		default:
			return false;
	}
}

// Helper function to check if a type is floating point
inline bool isFloatingPointType(Type type) {
	switch (type) {
		case Type::Float:
		case Type::Double:
		case Type::LongDouble:
			return true;
		default:
			return false;
	}
}

// Helper function to evaluate type traits like std::is_integral_v<T>
inline bool evaluateTypeTrait(std::string_view trait_name, const std::vector<TemplateTypeArg>& type_args) {
	if (type_args.empty()) {
		return false;  // Type traits need at least one argument
	}
	
	Type arg_type = type_args[0].base_type;
	
	// Handle common type traits
	if (trait_name == "is_integral_v" || trait_name == "is_integral") {
		return isIntegralType(arg_type);
	}
	else if (trait_name == "is_floating_point_v" || trait_name == "is_floating_point") {
		return isFloatingPointType(arg_type);
	}
	else if (trait_name == "is_arithmetic_v" || trait_name == "is_arithmetic") {
		return isIntegralType(arg_type) || isFloatingPointType(arg_type);
	}
	else if (trait_name == "is_signed_v" || trait_name == "is_signed") {
		// Check if type is signed
		switch (arg_type) {
			case Type::Char:  // char signedness is implementation-defined, but typically signed
			case Type::Short:
			case Type::Int:
			case Type::Long:
			case Type::LongLong:
			case Type::Float:
			case Type::Double:
			case Type::LongDouble:
				return true;
			default:
				return false;
		}
	}
	else if (trait_name == "is_unsigned_v" || trait_name == "is_unsigned") {
		switch (arg_type) {
			case Type::Bool:
			case Type::UnsignedChar:
			case Type::UnsignedShort:
			case Type::UnsignedInt:
			case Type::UnsignedLong:
			case Type::UnsignedLongLong:
				return true;
			default:
				return false;
		}
	}
	
	// Unknown type trait - assume satisfied (conservative approach)
	return true;
}

// Helper function to evaluate a constraint expression to a numeric value
// Used for evaluating comparisons in requires clauses like: requires sizeof(T) < 8
inline std::optional<long long> evaluateConstraintExpression(
	const ASTNode& expr,
	const std::vector<TemplateTypeArg>& template_args,
	const std::vector<std::string_view>& template_param_names) {
	
	// Handle ExpressionNode wrapper
	if (expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_variant = expr.as<ExpressionNode>();
		return std::visit([&](const auto& inner) -> std::optional<long long> {
			ASTNode inner_ast_node(&inner);
			return evaluateConstraintExpression(inner_ast_node, template_args, template_param_names);
		}, expr_variant);
	}
	
	// Handle numeric literals
	if (expr.is<NumericLiteralNode>()) {
		const auto& literal = expr.as<NumericLiteralNode>();
		if (std::holds_alternative<unsigned long long>(literal.value())) {
			return static_cast<long long>(std::get<unsigned long long>(literal.value()));
		} else if (std::holds_alternative<double>(literal.value())) {
			return static_cast<long long>(std::get<double>(literal.value()));
		}
	}
	
	// Handle sizeof expression
	if (expr.is<SizeofExprNode>()) {
		const auto& sizeof_expr = expr.as<SizeofExprNode>();
		const ASTNode& type_or_expr = sizeof_expr.type_or_expr();
		
		// If it's a type specifier, get the size
		if (type_or_expr.is<TypeSpecifierNode>()) {
			const auto& type_spec = type_or_expr.as<TypeSpecifierNode>();
			
			// Check if it's a template parameter that needs substitution
			if (type_spec.type() == Type::UserDefined) {
				// Get the type name from the token
				std::string_view type_name = type_spec.token().value();
				
				// Also check the actual type name from gTypeInfo using the type_index
				// This is important for placeholder types like "Op<...>::type"
				std::string_view full_type_name = type_name;
				TypeIndex type_idx = type_spec.type_index();
				if (type_idx > 0 && type_idx < gTypeInfo.size()) {
					full_type_name = StringTable::getStringView(gTypeInfo[type_idx].name_);
				}
				
				FLASH_LOG(Templates, Debug, "evaluateConstraintExpression: sizeof(", type_name, "), full_type_name='", full_type_name, "', type_index=", type_idx);
				
				// Check if the type name is a simple template parameter (e.g., sizeof(T))
				for (size_t i = 0; i < template_param_names.size() && i < template_args.size(); ++i) {
					if (template_param_names[i] == type_name) {
						const auto& arg = template_args[i];
						if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
							return static_cast<long long>((gTypeInfo[arg.type_index].type_size_ + 7) / 8);
						}
						long long size = static_cast<long long>(get_type_size_bits(arg.base_type) / 8);
						if (size > 0) {
							return size;
						}
					}
				}

				// Check if this is a placeholder for a dependent nested type like "Op<...>::type"
				// These are created during parsing as placeholders for template-dependent types
				if (full_type_name.find("::") != std::string_view::npos) {
					// This looks like a nested type access - try to resolve it
					// Format: "Op<...>::type" or similar
					size_t scope_pos = full_type_name.find("::");
					std::string_view base_part = full_type_name.substr(0, scope_pos);
					std::string_view member_part = full_type_name.substr(scope_pos + 2);
					
					FLASH_LOG(Templates, Debug, "  Nested type access: base='", base_part, "', member='", member_part, "'");
					
					// Check if base_part references a template template parameter like "Op<...>"
					std::string_view template_param_name;
					if (base_part.find("<") != std::string_view::npos) {
						// Extract the template parameter name (e.g., "Op" from "Op<...>")
						template_param_name = base_part.substr(0, base_part.find("<"));
					} else {
						template_param_name = base_part;
					}
					
					FLASH_LOG(Templates, Debug, "  Template param name: '", template_param_name, "', template_param_names.size()=", template_param_names.size());
					for (size_t dbg_i = 0; dbg_i < template_param_names.size(); ++dbg_i) {
						FLASH_LOG(Templates, Debug, "    template_param_names[", dbg_i, "] = '", template_param_names[dbg_i], "'");
					}
					
					// Look for the template template parameter in our substitutions
					for (size_t i = 0; i < template_param_names.size() && i < template_args.size(); ++i) {
						if (template_param_names[i] == template_param_name) {
							const auto& arg = template_args[i];
							
							FLASH_LOG(Templates, Debug, "  Found template param at index ", i, ", is_template_template_arg=", arg.is_template_template_arg);
							
							// Check if this is a template template argument
							if (arg.is_template_template_arg && arg.template_name_handle.isValid()) {
								// We have a template template argument like HasType
								std::string_view template_name = arg.template_name_handle.view();
								FLASH_LOG(Templates, Debug, "  Found template template arg: '", template_name, "'");
								
								// Now we need to get the instantiated type's member
								// For HasType<int>::type, we need to get the type alias from HasType<int>
								
								// Look for the pack argument to get the template argument
								for (size_t j = i + 1; j < template_args.size(); ++j) {
									const auto& pack_arg = template_args[j];
									if (!pack_arg.is_template_template_arg && !pack_arg.is_value) {
										// Found a type argument - this is what we instantiate the template with
										FLASH_LOG(Templates, Debug, "  Pack arg type_index=", pack_arg.type_index, ", base_type=", static_cast<int>(pack_arg.base_type));
										
										// For member "type", we need to look up HasType<T>::type which equals T
										// For HasType, the using type = T; means ::type is the template argument
										if (member_part == "type") {
											// For a simple type alias like HasType<T>::type = T,
											// return the size of the template argument
											if (pack_arg.type_index > 0 && pack_arg.type_index < gTypeInfo.size()) {
												long long size = static_cast<long long>((gTypeInfo[pack_arg.type_index].type_size_ + 7) / 8);
												FLASH_LOG(Templates, Debug, "  Resolved sizeof(", template_name, "<...>::type) = ", size);
												return size;
											}
											// For built-in types without type_index
											long long size = static_cast<long long>(get_type_size_bits(pack_arg.base_type) / 8);
											if (size > 0) {
												FLASH_LOG(Templates, Debug, "  Resolved sizeof(", template_name, "<...>::type) = ", size, " (from base_type)");
												return size;
											}
										}
										break;
									}
								}
							}
							break;
						}
					}
				}
				
				// Look for matching template parameter
				for (size_t i = 0; i < template_param_names.size() && i < template_args.size(); ++i) {
					if (template_param_names[i] == type_name) {
						// Found the template parameter - use the substituted type's size
						const auto& arg = template_args[i];
						if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
							// type_size_ is in bits, convert to bytes
							return static_cast<long long>((gTypeInfo[arg.type_index].type_size_ + 7) / 8);
						}
						// Fallback for primitive types without type_index (e.g., int, char, etc.)
						// This handles cases where type_index is 0 but base_type is valid
						long long size = static_cast<long long>(get_type_size_bits(arg.base_type) / 8);
						if (size > 0) {
							return size;
						}
					}
				}
				
				// Try to look up the type directly
				auto type_handle = StringTable::getOrInternStringHandle(type_name);
				auto type_it = gTypesByName.find(type_handle);
				if (type_it != gTypesByName.end()) {
					// type_size_ is in bits, convert to bytes
					return static_cast<long long>((type_it->second->type_size_ + 7) / 8);
				}
			} else {
				// Built-in type - get size from type info
				size_t size_bits = type_spec.size_in_bits();
				if (size_bits > 0) {
					return static_cast<long long>((size_bits + 7) / 8);
				}
			}
		}
		
		// Handle nested type access like typename Op<Args...>::type
		if (type_or_expr.is<QualifiedIdentifierNode>()) {
			// This is a dependent type - try to resolve it
			// For now, if we can't resolve, return nullopt
			return std::nullopt;
		}
	}
	
	// Handle qualified identifier (nested type access)
	if (expr.is<QualifiedIdentifierNode>()) {
		// This is a dependent type - try to resolve the qualified name
		// For now, can't evaluate complex qualified identifiers
		return std::nullopt;
	}
	
	// Can't evaluate this expression
	return std::nullopt;
}

// Enhanced constraint evaluator for C++20 concepts
// Evaluates constraints and provides detailed error messages when they fail
inline ConstraintEvaluationResult evaluateConstraint(
	const ASTNode& constraint_expr, 
	const std::vector<TemplateTypeArg>& template_args,
	const std::vector<std::string_view>& template_param_names = {}) {
	
	FLASH_LOG(Templates, Debug, "evaluateConstraint: constraint type=", constraint_expr.type_name(), ", template_args.size()=", template_args.size());
	for (size_t i = 0; i < template_param_names.size(); ++i) {
		if (i < template_args.size()) {
			const auto& arg = template_args[i];
			FLASH_LOG(Templates, Debug, "  param '", template_param_names[i], "' -> is_template_template_arg=", arg.is_template_template_arg, 
				", base_type=", static_cast<int>(arg.base_type), ", type_index=", arg.type_index);
		}
	}
	
	// Handle ExpressionNode wrapper - unwrap it and evaluate the inner node
	if (constraint_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_variant = constraint_expr.as<ExpressionNode>();
		// ExpressionNode is a variant, visit it to get the actual inner node
		return std::visit([&](const auto& inner) -> ConstraintEvaluationResult {
			// Create an ASTNode wrapper around the inner node
			ASTNode inner_ast_node(&inner);
			return evaluateConstraint(inner_ast_node, template_args, template_param_names);
		}, expr_variant);
	}
	
	// For BoolLiteralNode (true/false keywords parsed as boolean literals)
	if (constraint_expr.is<BoolLiteralNode>()) {
		const auto& literal = constraint_expr.as<BoolLiteralNode>();
		bool value = literal.value();
		
		if (!value) {
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: literal constraint is false",
				"false",
				"use 'true' or a valid concept expression"
			);
		}
		return ConstraintEvaluationResult::success();
	}
	
	// For boolean literals (true/false), evaluate directly
	if (constraint_expr.is<NumericLiteralNode>()) {
		const auto& literal = constraint_expr.as<NumericLiteralNode>();
		// Check if the value is 0 (false) or non-zero (true)
		bool value = true;  // default to true
		if (std::holds_alternative<unsigned long long>(literal.value())) {
			value = std::get<unsigned long long>(literal.value()) != 0;
		} else if (std::holds_alternative<double>(literal.value())) {
			value = std::get<double>(literal.value()) != 0.0;
		}
		
		if (!value) {
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: literal constraint is false",
				"false",
				"use 'true' or a valid concept expression"
			);
		}
		return ConstraintEvaluationResult::success();
	}
	
	// For identifier nodes (concept names or type trait variables)
	if (constraint_expr.is<IdentifierNode>()) {
		const auto& ident = constraint_expr.as<IdentifierNode>();
		std::string_view name = ident.name();
		
		// Check for boolean literals written as identifiers (true/false)
		if (name == "false") {
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: literal constraint is false",
				"false",
				"use 'true' or a valid concept expression"
			);
		}
		if (name == "true") {
			return ConstraintEvaluationResult::success();
		}
		
		// Check if it's a type trait variable (e.g., is_integral_v)
		if (name.find("_v") != std::string_view::npos || 
		    name.find("is_") == 0) {
			// Try to evaluate as type trait
			bool result = evaluateTypeTrait(name, template_args);
			if (!result) {
				return ConstraintEvaluationResult::failure(
					std::string("constraint not satisfied: type trait '") + std::string(name) + "' evaluated to false",
					std::string(name),
					"check that the template argument satisfies the type trait"
				);
			}
			return ConstraintEvaluationResult::success();
		}
		
		// Otherwise, look up as a concept
		auto concept_opt = gConceptRegistry.lookupConcept(name);
		if (!concept_opt.has_value()) {
			return ConstraintEvaluationResult::failure(
				std::string("constraint not satisfied: concept '") + std::string(name) + "' not found",
				std::string(name),
				std::string("declare the concept before using it in a requires clause")
			);
		}
		
		// Concept found - evaluate its constraint expression with the template arguments
		const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
		return evaluateConstraint(concept_node.constraint_expr(), template_args, template_param_names);
	}
	
	// For member access nodes (e.g., std::is_integral_v<T>)
	if (constraint_expr.is<MemberAccessNode>()) {
		[[maybe_unused]] const auto& member = constraint_expr.as<MemberAccessNode>();
		// Try to get the member name for type trait evaluation
		// This handles std::is_integral_v syntax
		// For now, we'll accept these as satisfied
		return ConstraintEvaluationResult::success();
	}
	
	// For function call nodes (concept with template arguments like Integral<T>)
	// This handles the Concept<T> syntax in requires clauses
	if (constraint_expr.is<FunctionCallNode>()) {
		const auto& func_call = constraint_expr.as<FunctionCallNode>();
		std::string_view concept_name = func_call.called_from().value();
		
		// Look up the concept
		auto concept_opt = gConceptRegistry.lookupConcept(concept_name);
		if (!concept_opt.has_value()) {
			// Not a concept - might be a function call, assume satisfied
			return ConstraintEvaluationResult::success();
		}
		
		// Get the concept's constraint expression and evaluate it
		const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
		const auto& concept_params = concept_node.template_params();
		
		// Build the template arguments for the concept from the function call's template arguments
		// Map the concept's template parameters to the actual template arguments
		std::vector<TemplateTypeArg> concept_args;
		std::vector<std::string_view> concept_param_names;
		
		// Get the explicit template arguments from the function call
		const auto& explicit_args = func_call.template_arguments();
		
		// Map concept parameters to the arguments
		for (size_t i = 0; i < concept_params.size(); ++i) {
			concept_param_names.push_back(concept_params[i].name());
			
			if (i < explicit_args.size()) {
				const ASTNode& arg_node = explicit_args[i];
				
				// The argument might be a reference to a template parameter from the enclosing context
				// We need to resolve it using the original template_args
				if (arg_node.is<ExpressionNode>()) {
					const ExpressionNode& expr = arg_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(expr)) {
						const IdentifierNode& ident = std::get<IdentifierNode>(expr);
						std::string_view arg_name = ident.name();
						
						// Look for this name in the enclosing template parameters
						for (size_t j = 0; j < template_param_names.size() && j < template_args.size(); ++j) {
							if (template_param_names[j] == arg_name) {
								// Found the template parameter - use its substituted value
								concept_args.push_back(template_args[j]);
								break;
							}
						}
						
						// If we haven't added an argument yet, try to look up as a type
						if (concept_args.size() == i) {
							auto type_handle = StringTable::getOrInternStringHandle(arg_name);
							auto type_it = gTypesByName.find(type_handle);
							if (type_it != gTypesByName.end()) {
								TemplateTypeArg type_arg;
								type_arg.base_type = type_it->second->type_;
								type_arg.type_index = type_it->second->type_index_;
								concept_args.push_back(type_arg);
							}
						}
					} else if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
						const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
						std::string_view arg_name = tparam_ref.param_name().view();
						
						// Look for this name in the enclosing template parameters
						for (size_t j = 0; j < template_param_names.size() && j < template_args.size(); ++j) {
							if (template_param_names[j] == arg_name) {
								concept_args.push_back(template_args[j]);
								break;
							}
						}
					}
				} else if (arg_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = arg_node.as<TypeSpecifierNode>();
					TemplateTypeArg type_arg;
					type_arg.base_type = type_spec.type();
					type_arg.type_index = type_spec.type_index();
					type_arg.ref_qualifier = type_spec.reference_qualifier();
					type_arg.pointer_depth = type_spec.pointer_depth();
					type_arg.cv_qualifier = type_spec.cv_qualifier();
					concept_args.push_back(type_arg);
				}
			}
			
			// If we still haven't resolved the argument, use a placeholder
			if (concept_args.size() == i) {
				TemplateTypeArg placeholder;
				placeholder.is_dependent = true;
				concept_args.push_back(placeholder);
			}
		}
		
		FLASH_LOG(Templates, Debug, "FunctionCallNode concept evaluation: concept='", concept_name, "', concept_args.size()=", concept_args.size(), ", concept_param_names.size()=", concept_param_names.size());
		for (size_t i = 0; i < concept_param_names.size(); ++i) {
			if (i < concept_args.size()) {
				FLASH_LOG(Templates, Debug, "  param[", i, "] name='", concept_param_names[i], "', is_template_template_arg=", concept_args[i].is_template_template_arg, ", base_type=", static_cast<int>(concept_args[i].base_type));
			}
		}
		
		// Evaluate the concept's constraint with the resolved arguments
		return evaluateConstraint(concept_node.constraint_expr(), concept_args, concept_param_names);
	}
	
	// For binary operators (&&, ||)
	if (constraint_expr.is<BinaryOperatorNode>()) {
		const auto& binop = constraint_expr.as<BinaryOperatorNode>();
		std::string_view op = binop.op();
		
		if (op == "&&") {
			// Conjunction - both must be satisfied
			auto left_result = evaluateConstraint(binop.get_lhs(), template_args, template_param_names);
			if (!left_result.satisfied) {
				return left_result;  // Return first failure
			}
			
			auto right_result = evaluateConstraint(binop.get_rhs(), template_args, template_param_names);
			if (!right_result.satisfied) {
				return right_result;
			}
			
			return ConstraintEvaluationResult::success();
		}
		else if (op == "||") {
			// Disjunction - at least one must be satisfied
			auto left_result = evaluateConstraint(binop.get_lhs(), template_args, template_param_names);
			if (left_result.satisfied) {
				return ConstraintEvaluationResult::success();
			}
			
			auto right_result = evaluateConstraint(binop.get_rhs(), template_args, template_param_names);
			if (right_result.satisfied) {
				return ConstraintEvaluationResult::success();
			}
			
			// Both failed
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: neither alternative of disjunction is satisfied",
				left_result.failed_requirement + " || " + right_result.failed_requirement,
				"ensure at least one of the constraints is met"
			);
		}
		// Handle comparison operators for constraint evaluation (<, >, <=, >=, ==, !=)
		else if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=") {
			// Try to evaluate both sides as constant expressions
			auto lhs_value = evaluateConstraintExpression(binop.get_lhs(), template_args, template_param_names);
			auto rhs_value = evaluateConstraintExpression(binop.get_rhs(), template_args, template_param_names);
			
			if (!lhs_value.has_value() || !rhs_value.has_value()) {
				// Can't evaluate as constants - assume satisfied for unknown expressions
				return ConstraintEvaluationResult::success();
			}
			
			bool result = false;
			if (op == "<") {
				result = *lhs_value < *rhs_value;
			} else if (op == ">") {
				result = *lhs_value > *rhs_value;
			} else if (op == "<=") {
				result = *lhs_value <= *rhs_value;
			} else if (op == ">=") {
				result = *lhs_value >= *rhs_value;
			} else if (op == "==") {
				result = *lhs_value == *rhs_value;
			} else if (op == "!=") {
				result = *lhs_value != *rhs_value;
			}
			
			if (!result) {
				return ConstraintEvaluationResult::failure(
					"constraint not satisfied: comparison evaluated to false",
					std::to_string(*lhs_value) + " " + std::string(op) + " " + std::to_string(*rhs_value),
					"check the constraint expression"
				);
			}
			return ConstraintEvaluationResult::success();
		}
	}
	
	// For unary operators (!)
	if (constraint_expr.is<UnaryOperatorNode>()) {
		const auto& unop = constraint_expr.as<UnaryOperatorNode>();
		if (unop.op() == "!") {
			auto operand_result = evaluateConstraint(unop.get_operand(), template_args, template_param_names);
			if (operand_result.satisfied) {
				return ConstraintEvaluationResult::failure(
					"constraint not satisfied: negated constraint is true",
					"!" + operand_result.failed_requirement,
					"remove the negation or use a different constraint"
				);
			}
			return ConstraintEvaluationResult::success();
		}
	}
	
	// For requires expressions: requires { expr; ... } or requires(params) { expr; ... }
	if (constraint_expr.is<RequiresExpressionNode>()) {
		const auto& requires_expr = constraint_expr.as<RequiresExpressionNode>();
		// Evaluate each requirement in the requires expression
		// For now, we check if each requirement is a valid expression
		// This is a simplified check - full implementation would need to verify
		// that the expressions are well-formed with the substituted template arguments
		for (const auto& requirement : requires_expr.requirements()) {
			// Check different types of requirements
			if (requirement.is<CompoundRequirementNode>()) {
				// Compound requirement: { expression } -> Type
				// For now, assume satisfied - full implementation would check
				// that the expression is valid and the return type matches
				continue;
			}
			// Check for false literal (from SFINAE recovery when expression parsing failed)
			if (requirement.is<BoolLiteralNode>()) {
				if (!requirement.as<BoolLiteralNode>().value()) {
					return ConstraintEvaluationResult::failure(
						"requirement not satisfied: expression is ill-formed",
						"false",
						"the expression is not valid for the substituted types"
					);
				}
				continue;
			}
			if (requirement.is<RequiresClauseNode>()) {
				// Nested requirement: requires constraint
				const auto& nested_req = requirement.as<RequiresClauseNode>();
				auto nested_result = evaluateConstraint(nested_req.constraint_expr(), template_args, template_param_names);
				if (!nested_result.satisfied) {
					return nested_result;
				}
				continue;
			}
			// Simple requirement: expression must be valid
			// For binary operator expressions like a + b, we need to check if the operation is valid for the type
			if (requirement.is<ExpressionNode>()) {
				const ExpressionNode& expr = requirement.as<ExpressionNode>();
				// Check if this is a false literal (SFINAE recovery created this when wrapped in ExpressionNode)
				if (std::holds_alternative<BoolLiteralNode>(expr)) {
					const BoolLiteralNode& bool_lit = std::get<BoolLiteralNode>(expr);
					if (!bool_lit.value()) {
						return ConstraintEvaluationResult::failure(
							"requirement not satisfied: expression is not valid for the given types",
							"false",
							"the expression is ill-formed for the substituted types"
						);
					}
					continue;
				}
				// Check if this is a function call to a constrained template function
				if (std::holds_alternative<FunctionCallNode>(expr)) {
					const FunctionCallNode& call = std::get<FunctionCallNode>(expr);
					std::string_view called_name = call.function_declaration().identifier_token().value();
					// Look up if this function is a template with a requires clause
					const std::vector<ASTNode>* all_templates = gTemplateRegistry.lookupAllTemplates(called_name);
					if (all_templates) {
						for (const auto& tmpl : *all_templates) {
							if (tmpl.is<TemplateFunctionDeclarationNode>()) {
								const auto& tfdn = tmpl.as<TemplateFunctionDeclarationNode>();
								if (tfdn.has_requires_clause()) {
									// Build the called function's own template parameter names
									// and map outer template args to them via positional matching
									std::vector<std::string_view> callee_param_names;
									for (const auto& param_node : tfdn.template_parameters()) {
										if (param_node.is<TemplateParameterNode>()) {
											callee_param_names.push_back(param_node.as<TemplateParameterNode>().name());
										}
									}
									// Evaluate using the callee's param names with the outer args
									// (positional: concept's T maps to callee's T by position)
									auto req_result = evaluateConstraint(
										tfdn.requires_clause()->as<RequiresClauseNode>().constraint_expr(),
										template_args, callee_param_names);
									if (!req_result.satisfied) {
										return ConstraintEvaluationResult::failure(
											"requirement not satisfied: constrained function call failed",
											std::string(called_name),
											"check the constraint on the called function"
										);
									}
								}
							}
						}
					}
				}
				// The expression parsing succeeded, so it's syntactically valid
				continue;
			}
			if (requirement.is<BinaryOperatorNode>()) {
				// Binary operation like a + b
				// For now, assume satisfied - full implementation would check
				// if the types support the operation
				continue;
			}
		}
		// All requirements satisfied
		return ConstraintEvaluationResult::success();
	}
	
	// For TypeTraitExprNode (e.g., __is_same(T, int), __is_integral(T))
	// These can appear either directly or wrapped in ExpressionNode (handled above)
	// After ExpressionNode unwrapping, this handles the inner TypeTraitExprNode
	if (constraint_expr.is<TypeTraitExprNode>()) {
		const TypeTraitExprNode& trait_expr = constraint_expr.as<TypeTraitExprNode>();
		
		// Holds fully resolved type info including indirection and qualifiers
		struct ResolvedTypeInfo {
			Type base_type = Type::Invalid;
			TypeIndex type_index = 0;
			uint8_t pointer_depth = 0;
			ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
			CVQualifier cv_qualifier = CVQualifier::None;
		};
		
		// Helper to resolve a type specifier, substituting template parameters
		auto resolve_type = [&](const ASTNode& type_node) -> ResolvedTypeInfo {
			if (!type_node.is<TypeSpecifierNode>()) return {};
			const TypeSpecifierNode& ts = type_node.as<TypeSpecifierNode>();
			if (ts.type() == Type::UserDefined) {
				std::string_view name = ts.token().value();
				for (size_t i = 0; i < template_param_names.size() && i < template_args.size(); ++i) {
					if (template_param_names[i] == name) {
						const auto& arg = template_args[i];
						return {arg.base_type, arg.type_index, arg.pointer_depth,
						        arg.ref_qualifier, arg.cv_qualifier};
					}
				}
			}
			return {ts.type(), ts.type_index(), static_cast<uint8_t>(ts.pointer_depth()),
			        ts.reference_qualifier(), ts.cv_qualifier()};
		};
		
		auto first = resolve_type(trait_expr.type_node());
		
		bool result = false;
		switch (trait_expr.kind()) {
			case TypeTraitKind::IsSame: {
				if (trait_expr.has_second_type()) {
					auto second = resolve_type(trait_expr.second_type_node());
					FLASH_LOG(Templates, Debug, "IsSame comparison: first={type=", static_cast<int>(first.base_type), 
						", idx=", first.type_index, ", ptr=", static_cast<int>(first.pointer_depth),
						", ref_qual=", static_cast<int>(first.ref_qualifier),
						", cv=", static_cast<int>(first.cv_qualifier), "} second={type=", static_cast<int>(second.base_type),
						", idx=", second.type_index, ", ptr=", static_cast<int>(second.pointer_depth),
						", ref_qual=", static_cast<int>(second.ref_qualifier),
						", cv=", static_cast<int>(second.cv_qualifier), "}");
					result = (first.base_type == second.base_type &&
					          first.type_index == second.type_index &&
					          first.pointer_depth == second.pointer_depth &&
					          first.ref_qualifier == second.ref_qualifier &&
					          first.cv_qualifier == second.cv_qualifier);
				}
				break;
			}
			case TypeTraitKind::IsIntegral:
				result = (first.base_type == Type::Bool || first.base_type == Type::Char ||
				         first.base_type == Type::Short || first.base_type == Type::Int ||
				         first.base_type == Type::Long || first.base_type == Type::LongLong ||
				         first.base_type == Type::UnsignedChar || first.base_type == Type::UnsignedShort ||
				         first.base_type == Type::UnsignedInt || first.base_type == Type::UnsignedLong ||
				         first.base_type == Type::UnsignedLongLong)
				         && first.ref_qualifier == ReferenceQualifier::None && first.pointer_depth == 0;
				break;
			case TypeTraitKind::IsFloatingPoint:
				result = (first.base_type == Type::Float || first.base_type == Type::Double || first.base_type == Type::LongDouble)
				         && first.ref_qualifier == ReferenceQualifier::None && first.pointer_depth == 0;
				break;
			case TypeTraitKind::IsVoid:
				result = (first.base_type == Type::Void && first.ref_qualifier == ReferenceQualifier::None && first.pointer_depth == 0);
				break;
			case TypeTraitKind::IsPointer:
				result = (first.pointer_depth > 0) && first.ref_qualifier == ReferenceQualifier::None;
				break;
			case TypeTraitKind::IsReference:
				result = first.ref_qualifier != ReferenceQualifier::None;
				break;
			case TypeTraitKind::IsLvalueReference:
				result = first.ref_qualifier == ReferenceQualifier::LValueReference;
				break;
			case TypeTraitKind::IsRvalueReference:
				result = first.ref_qualifier == ReferenceQualifier::RValueReference;
				break;
			case TypeTraitKind::IsConst:
				result = (static_cast<uint8_t>(first.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0;
				break;
			case TypeTraitKind::IsVolatile:
				result = (static_cast<uint8_t>(first.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0;
				break;
			default:
				// For unhandled type traits, assume satisfied
				return ConstraintEvaluationResult::success();
		}
		
		if (!result) {
			return ConstraintEvaluationResult::failure(
				std::string("constraint not satisfied: type trait '") + std::string(trait_expr.trait_name()) + "' evaluated to false",
				std::string(trait_expr.trait_name()),
				"check that the template argument satisfies the type trait"
			);
		}
		return ConstraintEvaluationResult::success();
	}
	
	// Default: assume satisfied for unknown expressions
	// This allows templates to compile even with complex constraints
	return ConstraintEvaluationResult::success();
}
