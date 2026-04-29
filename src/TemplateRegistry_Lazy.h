#pragma once

#include "TemplateRegistry.h"

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
	DeferredMemberIdentity identity;
	InlineVector<ASTNode, 4> template_params;		  // Template parameters from class template
	InlineVector<TemplateTypeArg, 4> template_args; // Concrete template arguments used for instantiation
	AccessSpecifier access;						// Access specifier (public/private/protected)
	bool is_virtual = false;					 // Virtual function flag
	bool is_pure_virtual = false;				  // Pure virtual flag
	bool is_override = false;					  // Override flag
	bool is_final = false;					   // Final flag
};

struct LazyMemberKey {
	StringHandle instantiated_class_name;
	StringHandle member_function_name;
	std::optional<bool> is_const_method;

	static LazyMemberKey exact(
		StringHandle instantiated_class_name,
		StringHandle member_function_name,
		bool is_const_method) {
		return {instantiated_class_name, member_function_name, is_const_method};
	}

	static LazyMemberKey anyConst(
		StringHandle instantiated_class_name,
		StringHandle member_function_name) {
		return {instantiated_class_name, member_function_name, std::nullopt};
	}

	bool hasExactConstness() const {
		return is_const_method.has_value();
	}
};

// Registry for tracking uninstantiated template member functions
// Allows lazy (on-demand) instantiation for better compilation performance
class LazyMemberInstantiationRegistry {
public:
	static LazyMemberInstantiationRegistry& getInstance() {
		static LazyMemberInstantiationRegistry instance;
		return instance;
	}

	// Append the shared "instantiated_class_name::member_function_name" prefix.
	// Prefer makeKey(...) for finished lookup keys.
	static StringBuilder& appendMemberKeyPrefix(StringBuilder& key_builder, StringHandle class_name, StringHandle member_name) {
		class_name = normalizeClassName(class_name);
		return key_builder.append(class_name).append("::").append(member_name);
	}

	// Helper to generate registry key from class name, member name, and const-ness.
	// Key format: "instantiated_class_name::member_function_name[$const]"
	// Shared by lazy_members_ operations and odr_used_ operations.
	static StringHandle makeKey(StringHandle class_name, StringHandle member_name, bool is_const) {
		StringBuilder key_builder;
		appendMemberKeyPrefix(key_builder, class_name, member_name);
		if (is_const)
			key_builder.append("$const");
		return StringTable::getOrInternStringHandle(key_builder);
	}

	// Register a member function for lazy instantiation
	// Key format: "instantiated_class_name::member_function_name[$const]"
	void registerLazyMember(LazyMemberFunctionInfo info) {
		StringHandle lookup_name = effectiveLookupName(info.identity);
		StringHandle key = makeKey(info.identity.instantiated_owner_name, lookup_name, info.identity.is_const_method);
		lazy_members_[key] = std::move(info);
	}

	// Check if a member function needs lazy instantiation
	bool needsInstantiation(const LazyMemberKey& key) const {
		if (key.is_const_method.has_value()) {
			auto handle = makeKey(
				key.instantiated_class_name,
				key.member_function_name,
				*key.is_const_method);
			return lazy_members_.find(handle) != lazy_members_.end();
		}

		auto non_const_handle = makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			false);
		if (lazy_members_.find(non_const_handle) != lazy_members_.end()) {
			return true;
		}

		auto const_handle = makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			true);
		return lazy_members_.find(const_handle) != lazy_members_.end();
	}

	bool needsInstantiation(StringHandle instantiated_class_name, StringHandle member_function_name, bool is_const) const {
		return needsInstantiation(
			LazyMemberKey::exact(
				instantiated_class_name,
				member_function_name,
				is_const));
	}

	// Check either the non-const or const variant — for call sites that don't yet know is_const.
	bool needsInstantiationAny(StringHandle instantiated_class_name, StringHandle member_function_name) const {
		return needsInstantiation(
			LazyMemberKey::anyConst(
				instantiated_class_name,
				member_function_name));
	}

	// Get lazy member info for instantiation
	std::optional<LazyMemberFunctionInfo> getLazyMemberInfo(const LazyMemberKey& key) {
		if (key.is_const_method.has_value()) {
			auto handle = makeKey(
				key.instantiated_class_name,
				key.member_function_name,
				*key.is_const_method);
			auto it = lazy_members_.find(handle);
			if (it != lazy_members_.end()) {
				return it->second;
			}
			return std::nullopt;
		}

		auto non_const_handle = makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			false);
		auto it = lazy_members_.find(non_const_handle);
		if (it != lazy_members_.end()) {
			return it->second;
		}

		auto const_handle = makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			true);
		it = lazy_members_.find(const_handle);
		if (it != lazy_members_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	std::optional<LazyMemberFunctionInfo> getLazyMemberInfo(StringHandle instantiated_class_name, StringHandle member_function_name, bool is_const) {
		return getLazyMemberInfo(
			LazyMemberKey::exact(
				instantiated_class_name,
				member_function_name,
				is_const));
	}

	// Get lazy member info without knowing is_const — tries non-const first, then const.
	// For call sites that haven't yet determined which overload they need.
	std::optional<LazyMemberFunctionInfo> getLazyMemberInfoAny(StringHandle instantiated_class_name, StringHandle member_function_name) {
		return getLazyMemberInfo(
			LazyMemberKey::anyConst(
				instantiated_class_name,
				member_function_name));
	}

	// Mark a member function as instantiated (remove from lazy registry)
	void markInstantiated(const LazyMemberKey& key) {
		if (!key.is_const_method.has_value()) {
			return;
		}
		auto handle = makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			*key.is_const_method);
		lazy_members_.erase(handle);
	}

	void markInstantiated(StringHandle instantiated_class_name, StringHandle member_function_name, bool is_const) {
		markInstantiated(
			LazyMemberKey::exact(
				instantiated_class_name,
				member_function_name,
				is_const));
	}

	// ------------------------------------------------------------------
	// Explicit ODR-use tracking (Phase 5 Slice G)
	// ------------------------------------------------------------------
	//
	// `odr_used_` records lazy-member keys that sema has identified as
	// actually used by user code (the C++ notion of ODR-use: named in a
	// potentially-evaluated expression, selected as a call target, chosen
	// by overload resolution, etc.). This set is intentionally *independent*
	// of `lazy_members_` so the signal persists across materialization:
	// `markInstantiated` erases the map entry, but the ODR-use record stays
	// so later passes can still answer "was this member ever ODR-used?".
	//
	// Invariants:
	//   * `markOdrUsed` is only called from non-speculative, non-SFINAE
	//     sema sites that have proven the member is actually needed.
	//   * `ensureMemberFunctionMaterialized` does *not* auto-mark — it is a
	//     generic materialization helper reachable from codegen/constexpr
	//     forwarders whose semantics are "make the body exist", which is
	//     weaker than "sema proved ODR-use".
	//   * The SFINAE-probed instantiation hazard (e.g. `pair<const int, int>`
	//     inside `is_swappable<...>`) is preserved: those paths never run
	//     through sema annotation sites, so the ODR-use bit stays false and
	//     any drain that filters by `isOdrUsed` safely skips them.
	//
	// Note on const/non-const variants: overload sets are per-const-ness,
	// so `markOdrUsed(..., is_const=false)` does NOT also imply the const
	// variant. Callers that truly don't know const-ness (ctor/dtor, or
	// early lookup before overload resolution) may use the `...Any` helpers
	// which record/query both variants.
	void markOdrUsed(const LazyMemberKey& key) {
		if (key.is_const_method.has_value()) {
			odr_used_.insert(makeKey(
				key.instantiated_class_name,
				key.member_function_name,
				*key.is_const_method));
			return;
		}

		odr_used_.insert(makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			false));
		odr_used_.insert(makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			true));
	}

	void markOdrUsed(StringHandle instantiated_class_name, StringHandle member_function_name, bool is_const) {
		markOdrUsed(
			LazyMemberKey::exact(
				instantiated_class_name,
				member_function_name,
				is_const));
	}

	// Mark both const and non-const variants as ODR-used. Prefer the precise
	// overload above; use this only when the member kind is inherently
	// const-agnostic (ctor/dtor) or when the call site has not yet
	// determined const-ness.
	void markOdrUsedAny(StringHandle instantiated_class_name, StringHandle member_function_name) {
		markOdrUsed(
			LazyMemberKey::anyConst(
				instantiated_class_name,
				member_function_name));
	}

	// Phase 5 Slice G item #4: mark every registered lazy member whose
	// `identity.instantiated_owner_name` matches `instantiated_class_name`
	// as ODR-used. This is a conservative escape hatch for call sites that
	// have determined the instantiated class is ODR-reached but cannot
	// reliably reproduce the stub's internal member-name key (notably
	// conversion operators where `computeInstantiatedLookupName` may fail
	// to canonicalize enum template arguments, leaving the stub registered
	// under a template-parameter-dependent name like "operator value_type"
	// instead of "operator Color"). See
	// `tests/test_lazy_conv_op_enum_type_ret0.cpp` for the motivating case.
	//
	// Semantically this is a superset of the old pass-1 wholesale drain for
	// the given struct only. Structs that are never ODR-reached (e.g.,
	// SFINAE-probed instantiations) stay untouched.
	void markOdrUsedAllInClass(StringHandle instantiated_class_name) {
		for (const auto& [key_handle, info] : lazy_members_) {
			if (info.identity.instantiated_owner_name == instantiated_class_name) {
				odr_used_.insert(key_handle);
			}
		}
	}


	bool isOdrUsed(const LazyMemberKey& key) const {
		if (key.is_const_method.has_value()) {
			return odr_used_.find(makeKey(
				key.instantiated_class_name,
				key.member_function_name,
				*key.is_const_method)) != odr_used_.end();
		}

		auto non_const_handle = makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			false);
		if (odr_used_.find(non_const_handle) != odr_used_.end()) {
			return true;
		}

		auto const_handle = makeKey(
			key.instantiated_class_name,
			key.member_function_name,
			true);
		return odr_used_.find(const_handle) != odr_used_.end();
	}

	bool isOdrUsed(StringHandle instantiated_class_name, StringHandle member_function_name, bool is_const) const {
		return isOdrUsed(
			LazyMemberKey::exact(
				instantiated_class_name,
				member_function_name,
				is_const));
	}

	bool isOdrUsedAny(StringHandle instantiated_class_name, StringHandle member_function_name) const {
		return isOdrUsed(
			LazyMemberKey::anyConst(
				instantiated_class_name,
				member_function_name));
	}

	// Clear all lazy members (for testing)
	void clear() {
		lazy_members_.clear();
		odr_used_.clear();
	}

	// Get count of uninstantiated members (for diagnostics)
	size_t getUninstantiatedCount() const {
		return lazy_members_.size();
	}

	// Get count of members marked ODR-used (for diagnostics/audits)
	size_t getOdrUsedCount() const {
		return odr_used_.size();
	}

	// Snapshot a list of (instantiated_owner_name, member_name, is_const_method)
	// triples for every lazy entry whose key is currently in `odr_used_`.
	//
	// Returns a snapshot rather than yielding live iterators because the caller
	// will typically invoke materialization, which mutates `lazy_members_`
	// (via `markInstantiated`) and may register new entries. The triples use
	// the *unnormalized* owner name as stored on the LazyMemberFunctionInfo's
	// identity, so the caller can feed them back through
	// `ensureMemberFunctionMaterialized` without needing to re-derive scope.
	struct OdrUsedLazyEntry {
		StringHandle instantiated_owner_name;
		StringHandle member_name;
		bool is_const_method;
	};
	std::vector<OdrUsedLazyEntry> snapshotOdrUsedLazyEntries() const {
		std::vector<OdrUsedLazyEntry> out;
		out.reserve(lazy_members_.size());
		for (const auto& [key_handle, info] : lazy_members_) {
			if (odr_used_.find(key_handle) != odr_used_.end()) {
				OdrUsedLazyEntry entry{};
				entry.instantiated_owner_name = info.identity.instantiated_owner_name;
				entry.member_name = effectiveLookupName(info.identity);
				entry.is_const_method = info.identity.is_const_method;
				out.push_back(entry);
			}
		}
		return out;
	}

private:
	LazyMemberInstantiationRegistry() = default;

	// Map from "instantiated_class::member_function" to lazy instantiation info
	std::unordered_map<StringHandle, LazyMemberFunctionInfo, TransparentStringHash, std::equal_to<>> lazy_members_;

	// Keys (same format as `lazy_members_`) that sema has proven to be
	// ODR-used. Persists across `markInstantiated` — see the block comment
	// above `markOdrUsed` for the invariants this set upholds.
	std::unordered_set<StringHandle, TransparentStringHash, std::equal_to<>> odr_used_;
};

// Global lazy member instantiation registry
// Note: Use LazyMemberInstantiationRegistry::getInstance() to access
// (cannot use global variable due to singleton pattern)

// ============================================================================
// Lazy Static Member Instantiation Registry
// ============================================================================

// Information needed to instantiate a template static member on-demand
struct LazyStaticMemberInfo {
	StringHandle class_template_name;		  // Original template name (e.g., "integral_constant")
	StringHandle instantiated_class_name;	  // Instantiated class name (e.g., "integral_constant_bool_true")
	StringHandle member_name;				  // Static member name (e.g., "value")
	TypeIndex type_index;					  // Type index; category encodes Type
	size_t size;							   // Size in bytes
	size_t alignment;						  // Alignment requirement
	AccessSpecifier access;					// Access specifier
	std::optional<ASTNode> declaration;		// Original declaration AST for replay-based substitution
	std::optional<ASTNode> initializer;		// Original initializer (may need substitution)
	std::optional<SaveHandle> initializer_position; // Saved lexer position at '=' or '{' for replay
	CVQualifier cv_qualifier = CVQualifier::None; // CV qualifiers (const/volatile)
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None; // Reference qualifier (lvalue/rvalue)
	bool is_array = false;
	std::vector<size_t> array_dimensions;
	int pointer_depth = 0;					   // Pointer depth (e.g., 1 for int*, 2 for int**)
	std::vector<ASTNode> template_params;	  // Template parameters from class template
	std::vector<TemplateTypeArg> template_args; // Concrete template arguments
	bool needs_substitution;					 // True if initializer contains template parameters

	TypeCategory memberType() const { return type_index.category(); }
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
// Lazy Class Instantiation Registry
// ============================================================================

// Instantiation phases for three-phase class instantiation
// Each phase represents a level of completeness of the instantiation
enum class ClassInstantiationPhase : uint8_t {
	None = 0,			  // Not yet instantiated
	Minimal = 1,		 // Type entry created, name registered (triggered by any type name use)
	Layout = 2,			// Size/alignment computed (triggered by sizeof, alignof, variable declarations)
	Full = 3			 // All members, base classes, and static members instantiated (triggered by member access)
};

// Information needed for lazy (phased) class template instantiation
// Allows deferring complete instantiation until members are actually used
struct LazyClassInstantiationInfo {
	StringHandle template_name;					// Original template name (e.g., "vector")
	StringHandle instantiated_name;				// Instantiated class name (e.g., "vector_int")
	std::vector<TemplateTypeArg> template_args;	// Concrete template arguments
	std::vector<ASTNode> template_params;		  // Template parameters from class template
	ASTNode template_declaration;				  // Reference to primary template declaration
	ClassInstantiationPhase current_phase = ClassInstantiationPhase::None;
	// Flags for tracking what needs to be instantiated in Full phase
	// These are set during Minimal phase to avoid re-parsing template declaration
	bool has_base_classes = false;				   // Does the template have base classes?
	bool has_static_members = false;				 // Does the template have static members?
	bool has_member_functions = false;			   // Does the template have member functions?
	TypeIndex type_index{};						// Type index once minimal instantiation is done
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
// Lazy Type Alias Evaluation Registry
// ============================================================================

// Information needed for lazy type alias evaluation
// Allows deferring evaluation of template type aliases until actually accessed
struct LazyTypeAliasInfo {
	StringHandle alias_name;						 // Full alias name (e.g., "remove_const_int::type")
	StringHandle template_name;					// Original template name (e.g., "remove_const")
	StringHandle instantiated_class_name;		  // Instantiated class name (e.g., "remove_const_int")
	StringHandle member_name;					  // Member alias name (e.g., "type")
	ASTNode unevaluated_target;					// Unevaluated target type expression
	std::vector<ASTNode> template_params;		  // Template parameters from class template
	std::vector<TemplateTypeArg> template_args;	// Concrete template arguments
	bool needs_substitution = true;				// True if target contains template parameters
	bool is_evaluated = false;					   // True once evaluation has been performed
	// Cached evaluation result (category embedded in evaluated_type_index)
	TypeIndex evaluated_type_index{};
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
					   TypeIndex result_type_index) {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		auto it = lazy_aliases_.find(key);
		if (it != lazy_aliases_.end()) {
			it->second.is_evaluated = true;
			it->second.evaluated_type_index = result_type_index;
			FLASH_LOG(Templates, Debug, "Marked lazy type alias as evaluated: ", key);
			return true;
		}
		FLASH_LOG(Templates, Warning, "Attempted to mark unregistered type alias as evaluated: ", key);
		return false;
	}

	// Get cached evaluation result (only valid if is_evaluated is true)
	std::optional<TypeIndex> getCachedResult(StringHandle instantiated_class_name,
											 StringHandle member_name) const {
		StringHandle key = makeKey(instantiated_class_name, member_name);
		auto it = lazy_aliases_.find(key);
		if (it != lazy_aliases_.end() && it->second.is_evaluated) {
			return it->second.evaluated_type_index;
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
// Lazy Nested Type Instantiation Registry
// ============================================================================

// Information needed for lazy nested type instantiation
// Allows deferring instantiation of nested types (inner classes/structs) until actually accessed
struct LazyNestedTypeInfo {
	StringHandle parent_class_name;				// Parent instantiated class name (e.g., "outer_int")
	StringHandle nested_type_name;				   // Nested type name (e.g., "inner")
	StringHandle qualified_name;					 // Fully qualified name (e.g., "outer_int::inner")
	ASTNode nested_type_declaration;				 // The nested struct/class declaration AST node
	std::vector<ASTNode> parent_template_params;	 // Template parameters from parent class
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
		return 1;	  // B is more specific (subsumes A)
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
			std::string(suggestion)};
	}
};

// isIntegralType and isFloatingPointType moved to AstNodeTypes_TypeSystem.h

// Helper function to evaluate type traits like std::is_integral_v<T>
inline bool evaluateTypeTrait(std::string_view trait_name, const std::vector<TemplateTypeArg>& type_args) {
	if (type_args.empty()) {
		return false;  // Type traits need at least one argument
	}

	TypeCategory arg_type = type_args[0].category();

	// Handle common type traits
	if (trait_name == "is_integral_v" || trait_name == "is_integral") {
		return isIntegralType(arg_type);
	} else if (trait_name == "is_floating_point_v" || trait_name == "is_floating_point") {
		return isFloatingPointType(arg_type);
	} else if (trait_name == "is_arithmetic_v" || trait_name == "is_arithmetic") {
		return isIntegralType(arg_type) || isFloatingPointType(arg_type);
	} else if (trait_name == "is_signed_v" || trait_name == "is_signed") {
		// Check if type is signed
		switch (arg_type) {
		case TypeCategory::Char:	 // char signedness is implementation-defined, but typically signed
		case TypeCategory::Short:
		case TypeCategory::Int:
		case TypeCategory::Long:
		case TypeCategory::LongLong:
		case TypeCategory::Float:
		case TypeCategory::Double:
		case TypeCategory::LongDouble:
			return true;
		default:
			return false;
		}
	} else if (trait_name == "is_unsigned_v" || trait_name == "is_unsigned") {
		switch (arg_type) {
		case TypeCategory::Bool:
		case TypeCategory::UnsignedChar:
		case TypeCategory::UnsignedShort:
		case TypeCategory::UnsignedInt:
		case TypeCategory::UnsignedLong:
		case TypeCategory::UnsignedLongLong:
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
		},
						  expr_variant);
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
			if (type_spec.category() == TypeCategory::UserDefined || type_spec.category() == TypeCategory::TypeAlias || type_spec.category() == TypeCategory::Template) {
				std::string_view type_name = type_spec.token().value();

				// Also check the actual type name from gTypeInfo using the type_index
				// This is important for placeholder types like "Op<...>::type"
				std::string_view full_type_name = type_name;
				TypeIndex type_idx = type_spec.type_index();
				if (const TypeInfo* type_idx_ti = tryGetTypeInfo(type_idx)) {
					full_type_name = StringTable::getStringView(type_idx_ti->name_);
				}

				FLASH_LOG(Templates, Debug, "evaluateConstraintExpression: sizeof(", type_name, "), full_type_name='", full_type_name, "', type_index=", type_idx);

				// Check if the type name is a simple template parameter (e.g., sizeof(T))
				for (size_t i = 0; i < template_param_names.size() && i < template_args.size(); ++i) {
					if (template_param_names[i] == type_name) {
						const auto& arg = template_args[i];
						if (const TypeInfo* arg_ti = tryGetTypeInfo(arg.type_index)) {
							return static_cast<long long>(toSizeT(arg_ti->sizeInBytes()));
						}
						long long size = static_cast<long long>(get_type_size_bits(arg.category()) / 8);
						if (size > 0) {
							return size;
						}
					}
				}

				auto findConstraintArgByName = [&](std::string_view param_name) -> const TemplateTypeArg* {
					for (size_t i = 0; i < template_param_names.size() && i < template_args.size(); ++i) {
						if (template_param_names[i] == param_name) {
							return &template_args[i];
						}
					}
					return nullptr;
				};

				auto materializeConstraintPlaceholderArgs = [&](const TypeInfo& placeholder_info) {
					std::vector<TemplateTypeArg> concrete_args;
					concrete_args.reserve(placeholder_info.templateArgs().size());
					for (const auto& arg_info : placeholder_info.templateArgs()) {
						TemplateTypeArg concrete_arg = toTemplateTypeArg(arg_info);
						std::string_view dependent_arg_name;
						if (arg_info.dependent_name.isValid()) {
							dependent_arg_name = StringTable::getStringView(arg_info.dependent_name);
						} else if (const TypeInfo* dependent_arg_info = tryGetTypeInfo(arg_info.type_index)) {
							dependent_arg_name = StringTable::getStringView(dependent_arg_info->name());
						}
						if (!dependent_arg_name.empty()) {
							if (const TemplateTypeArg* substituted_arg =
									findConstraintArgByName(dependent_arg_name)) {
								if (!arg_info.is_value && !substituted_arg->is_value) {
									concrete_arg = rebindDependentTemplateTypeArg(*substituted_arg, arg_info);
								} else {
									concrete_arg = *substituted_arg;
								}
							}
						}
						concrete_args.push_back(std::move(concrete_arg));
					}
					return concrete_args;
				};

				struct MemberAliasSizeResolver {
					enum { kMaxDepth = 64 };
					int depth_ = 0;

					struct DepthGuard {
						int& depth_ref;
						DepthGuard(int& depth) : depth_ref(depth) { ++depth_ref; }
						~DepthGuard() { --depth_ref; }
					};

					static std::optional<long long> sizeFromConcreteTemplateArg(
						const TemplateTypeArg& concrete_arg) {
						if (concrete_arg.pointer_depth > 0 ||
							concrete_arg.function_signature.has_value() ||
							concrete_arg.category() == TypeCategory::FunctionPointer ||
							concrete_arg.category() == TypeCategory::MemberFunctionPointer ||
							concrete_arg.category() == TypeCategory::MemberObjectPointer) {
							return 8;
						}

						long long size = 0;
						if (const TypeInfo* concrete_ti = tryGetTypeInfo(concrete_arg.type_index)) {
							if (const StructTypeInfo* struct_info = concrete_ti->getStructInfo();
								struct_info && !struct_info->hasCompleteObjectLayout()) {
								return std::nullopt;
							}
							size = static_cast<long long>(toSizeT(concrete_ti->sizeInBytes()));
						}

						if (size == 0) {
							size = static_cast<long long>(get_type_size_bits(concrete_arg.category()) / 8);
						}
						if (size <= 0) {
							return std::nullopt;
						}

						if (concrete_arg.is_array) {
							if (concrete_arg.array_dimensions.empty()) {
								return std::nullopt;
							}
							for (size_t dim : concrete_arg.array_dimensions) {
								size *= static_cast<long long>(dim);
							}
						}
						return size;
					}

					static std::optional<long long> applyTypeSpecSizeAdjustments(
						const TypeSpecifierNode& type_spec, long long base_size) {
						if (type_spec.has_function_signature() && type_spec.pointer_depth() == 0) {
							return std::nullopt;
						}
						if (type_spec.pointer_depth() > 0 ||
							type_spec.type() == TypeCategory::FunctionPointer ||
							type_spec.type() == TypeCategory::MemberFunctionPointer ||
							type_spec.type() == TypeCategory::MemberObjectPointer) {
							return 8;
						}
						if (type_spec.is_array()) {
							if (type_spec.array_dimensions().empty()) {
								if (auto single_dim = type_spec.array_size(); single_dim.has_value()) {
									base_size *= static_cast<long long>(*single_dim);
								} else {
									return std::nullopt;
								}
							} else {
								for (size_t dim : type_spec.array_dimensions()) {
									base_size *= static_cast<long long>(dim);
								}
							}
						}
						return base_size;
					}

					static std::optional<long long> resolveConcreteTypeMemberAliasSize(
						const TemplateTypeArg& concrete_base_arg,
						std::string_view member_name) {
						if (concrete_base_arg.is_value ||
							concrete_base_arg.is_template_template_arg ||
							!concrete_base_arg.type_index.is_valid()) {
							return std::nullopt;
						}

						const TypeInfo* concrete_base_info =
							tryGetTypeInfo(concrete_base_arg.type_index);
						if (!concrete_base_info) {
							return std::nullopt;
						}

						StringHandle qualified_member_handle =
							StringTable::getOrInternStringHandle(
								StringBuilder()
									.append(concrete_base_info->name())
									.append("::")
									.append(member_name)
									.commit());
						auto member_it = getTypesByNameMap().find(qualified_member_handle);
						if (member_it == getTypesByNameMap().end() || member_it->second == nullptr) {
							return std::nullopt;
						}

						TypeSpecifierNode member_spec(
							member_it->second->registeredTypeIndex().withCategory(member_it->second->typeEnum()),
							member_it->second->hasStoredSize() ? member_it->second->sizeInBits().value : 0,
							Token(),
							CVQualifier::None,
							ReferenceQualifier::None);
						int size_bits = getTypeSpecSizeBits(member_spec);
						if (size_bits <= 0 && member_it->second->hasStoredSize()) {
							size_bits = member_it->second->sizeInBits().value;
						}
						return size_bits > 0
							? std::optional<long long>(static_cast<long long>(size_bits / 8))
							: std::nullopt;
					}

					std::optional<long long> resolveAliasTypeSpecSize(
						const TypeSpecifierNode& alias_type_spec,
						std::span<const TemplateParameterNode> primary_params,
						std::span<const TemplateTypeArg> concrete_member_args) {
						if (depth_ >= kMaxDepth) return std::nullopt;
						DepthGuard guard{depth_};

						auto findConcreteArgByName =
							[&](std::string_view param_name) -> const TemplateTypeArg* {
							for (size_t i = 0; i < primary_params.size() &&
									i < concrete_member_args.size();
								 ++i) {
								const auto& param = primary_params[i];
								if (param.name() == param_name) {
									return &concrete_member_args[i];
								}
							}
							return nullptr;
						};

						auto tryResolveBoundTemplateParam =
							[&](std::string_view candidate_name) -> std::optional<long long> {
							const TemplateTypeArg* concrete_arg =
								findConcreteArgByName(candidate_name);
							if (!concrete_arg ||
								concrete_arg->is_value ||
								concrete_arg->is_template_template_arg) {
								return std::nullopt;
							}

							if (std::optional<long long> base_size =
									sizeFromConcreteTemplateArg(*concrete_arg);
								base_size.has_value()) {
								return applyTypeSpecSizeAdjustments(alias_type_spec, *base_size);
							}
							return std::nullopt;
						};

						if (alias_type_spec.type_index().is_valid()) {
							if (const TypeInfo* alias_type_info =
									tryGetTypeInfo(alias_type_spec.type_index())) {
								if (std::optional<long long> bound_param_size =
										tryResolveBoundTemplateParam(
											StringTable::getStringView(alias_type_info->name()));
									bound_param_size.has_value()) {
									return bound_param_size;
								}

								if (alias_type_info->isDependentMemberType()) {
									std::string_view qualified_alias_name =
										StringTable::getStringView(alias_type_info->name());
									size_t member_sep =
										qualified_alias_name.rfind("::");
									if (member_sep != std::string_view::npos) {
										std::string_view dependent_member_name =
											qualified_alias_name.substr(member_sep + 2);
										std::vector<TemplateTypeArg> nested_args;
										if (alias_type_info->isTemplateInstantiation()) {
											nested_args = materializeTemplateArgs(
												*alias_type_info,
												primary_params,
												concrete_member_args);
										}

										std::string_view nested_template_name =
											StringTable::getStringView(
												alias_type_info->baseTemplateName());
										if (!nested_template_name.empty()) {
											if (std::optional<long long> nested_alias_size =
													resolvePrimaryTemplateMemberAliasSize(
														nested_template_name,
														dependent_member_name,
														nested_args);
												nested_alias_size.has_value()) {
												return applyTypeSpecSizeAdjustments(
													alias_type_spec,
													*nested_alias_size);
											}
										}

										if (!nested_template_name.empty()) {
											if (const TemplateTypeArg* nested_template_arg =
													findConcreteArgByName(
														nested_template_name);
												nested_template_arg &&
												nested_template_arg->is_template_template_arg &&
												nested_template_arg->template_name_handle.isValid()) {
												if (std::optional<long long> nested_alias_size =
														resolvePrimaryTemplateMemberAliasSize(
															nested_template_arg->template_name_handle.view(),
															dependent_member_name,
															nested_args);
													nested_alias_size.has_value()) {
													return applyTypeSpecSizeAdjustments(
														alias_type_spec,
														*nested_alias_size);
												}
											}
										}

										std::string_view base_candidate =
											qualified_alias_name.substr(0, member_sep);
										if (size_t angle_pos = base_candidate.find("<");
											angle_pos != std::string_view::npos) {
											base_candidate =
												base_candidate.substr(0, angle_pos);
										}
										if (const TemplateTypeArg* concrete_base_arg =
												findConcreteArgByName(base_candidate)) {
											if (std::optional<long long> concrete_member_size =
													resolveConcreteTypeMemberAliasSize(
														*concrete_base_arg,
														dependent_member_name);
												concrete_member_size.has_value()) {
												return applyTypeSpecSizeAdjustments(
													alias_type_spec,
													*concrete_member_size);
											}
										}
									}
								}
							}
						}

						if (std::optional<long long> bound_param_size =
								tryResolveBoundTemplateParam(alias_type_spec.token().value());
							bound_param_size.has_value()) {
							return bound_param_size;
						}

						int size_bits = getTypeSpecSizeBits(alias_type_spec);
						return size_bits > 0
							? std::optional<long long>(static_cast<long long>(size_bits / 8))
							: std::nullopt;
					}

					std::optional<long long> resolvePrimaryTemplateMemberAliasSize(
						std::string_view template_name,
						std::string_view member_name,
						std::span<const TemplateTypeArg> concrete_member_args) {
						if (depth_ >= kMaxDepth) return std::nullopt;
						DepthGuard guard{depth_};

						auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
						if (!template_opt.has_value() || !template_opt->is<TemplateClassDeclarationNode>()) {
							return std::nullopt;
						}

						const auto& primary_template =
							template_opt->as<TemplateClassDeclarationNode>();
						const auto& primary_params =
							primary_template.template_parameters();
						const auto& type_aliases =
							primary_template.class_decl_node().type_aliases();

						for (const auto& type_alias : type_aliases) {
							if (StringTable::getStringView(type_alias.alias_name) != member_name ||
								!type_alias.type_node.is<TypeSpecifierNode>()) {
								continue;
							}

							return resolveAliasTypeSpecSize(
								type_alias.type_node.as<TypeSpecifierNode>(),
								primary_params,
								concrete_member_args);
						}

						return std::nullopt;
					}
				};

				// Check if this is a placeholder for a dependent nested type like "Op<...>::type"
				// These are created during parsing as placeholders for template-dependent types
				// Phase 4: use explicit placeholder_kind_ instead of string heuristic
				const TypeInfo* dependent_member_info = nullptr;
				if (const ResolvedAliasTypeInfo resolved_type_info = resolveAliasTypeInfo(type_idx);
					resolved_type_info.terminal_type_info != nullptr &&
					resolved_type_info.terminal_type_info->isDependentMemberType()) {
					dependent_member_info = resolved_type_info.terminal_type_info;
				} else if (const TypeInfo* full_ti = tryGetTypeInfo(type_idx);
						   full_ti && full_ti->isDependentMemberType()) {
					dependent_member_info = full_ti;
				}

				if (dependent_member_info != nullptr) {
					MemberAliasSizeResolver resolver;
					full_type_name = StringTable::getStringView(dependent_member_info->name());
					size_t scope_pos = full_type_name.rfind("::");
					if (scope_pos != std::string_view::npos) {
						std::string_view base_part = full_type_name.substr(0, scope_pos);
						std::string_view member_part = full_type_name.substr(scope_pos + 2);

						FLASH_LOG(Templates, Debug, "  Nested type access: base='", base_part, "', member='", member_part, "'");

						std::string_view template_name =
							dependent_member_info->isTemplateInstantiation()
								? StringTable::getStringView(dependent_member_info->baseTemplateName())
								: std::string_view{};
						std::vector<TemplateTypeArg> concrete_member_args =
							dependent_member_info->isTemplateInstantiation()
								? materializeConstraintPlaceholderArgs(*dependent_member_info)
								: std::vector<TemplateTypeArg>{};

						if (!template_name.empty()) {
							if (std::optional<long long> alias_size =
									resolver.resolvePrimaryTemplateMemberAliasSize(
										template_name,
										member_part,
										concrete_member_args);
								alias_size.has_value()) {
								FLASH_LOG(Templates, Debug, "  Resolved sizeof(", template_name, "<...>::", member_part, ") = ", *alias_size);
								return *alias_size;
							}
						}

						std::string_view base_param_name = base_part;
						if (size_t angle_pos = base_param_name.find("<");
							angle_pos != std::string_view::npos) {
							base_param_name = base_param_name.substr(0, angle_pos);
						}

						if (const TemplateTypeArg* bound_base_arg =
								findConstraintArgByName(base_param_name);
							bound_base_arg != nullptr) {
							if (bound_base_arg->is_template_template_arg &&
								bound_base_arg->template_name_handle.isValid()) {
								if (std::optional<long long> alias_size =
										resolver.resolvePrimaryTemplateMemberAliasSize(
											bound_base_arg->template_name_handle.view(),
											member_part,
											concrete_member_args);
									alias_size.has_value()) {
									FLASH_LOG(Templates, Debug, "  Resolved sizeof(", bound_base_arg->template_name_handle.view(), "<...>::", member_part, ") = ", *alias_size);
									return *alias_size;
								}
							} else if (std::optional<long long> concrete_member_size =
									MemberAliasSizeResolver::resolveConcreteTypeMemberAliasSize(
										*bound_base_arg,
										member_part);
								concrete_member_size.has_value()) {
								FLASH_LOG(Templates, Debug, "  Resolved sizeof(", base_param_name, "::", member_part, ") = ", *concrete_member_size);
								return *concrete_member_size;
							}
						}
					}
				}

				// Look for matching template parameter
				for (size_t i = 0; i < template_param_names.size() && i < template_args.size(); ++i) {
					if (template_param_names[i] == type_name) {
						// Found the template parameter - use the substituted type's size
						const auto& arg = template_args[i];
						if (const TypeInfo* arg_ti = tryGetTypeInfo(arg.type_index)) {
							// fallback_size_bits_ is in bits, convert to bytes
							return static_cast<long long>(toSizeT(arg_ti->sizeInBytes()));
						}
						// Fallback for primitive types without type_index (e.g., int, char, etc.)
						// This handles cases where type_index is 0 but base_type is valid
						long long size = static_cast<long long>(get_type_size_bits(arg.category()) / 8);
						if (size > 0) {
							return size;
						}
					}
				}

				// Try to look up the type directly
				auto type_handle = StringTable::getOrInternStringHandle(type_name);
				auto type_it = getTypesByNameMap().find(type_handle);
				if (type_it != getTypesByNameMap().end()) {
					// fallback_size_bits_ is in bits, convert to bytes
					return static_cast<long long>(toSizeT(type_it->second->sizeInBytes()));
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
		if (tryGetQualifiedIdentifier(type_or_expr)) {
			// This is a dependent type - try to resolve it
			// For now, if we can't resolve, return nullopt
			return std::nullopt;
		}
	}

	// Handle qualified identifier (nested type access)
	if (tryGetQualifiedIdentifier(expr)) {
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
	const InlineVector<TemplateTypeArg, 4>& template_args,
	const InlineVector<std::string_view, 4>& template_param_names = {}) {

	FLASH_LOG(Templates, Debug, "evaluateConstraint: constraint type=", constraint_expr.type_name(), ", template_args.size()=", template_args.size());
	for (size_t i = 0; i < template_param_names.size(); ++i) {
		if (i < template_args.size()) {
			const auto& arg = template_args[i];
			FLASH_LOG(Templates, Debug, "  param '", template_param_names[i], "' -> is_template_template_arg=", arg.is_template_template_arg,
					  ", base_type=", static_cast<int>(arg.typeEnum()), ", type_index=", arg.type_index);
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
		},
						  expr_variant);
	}

	// For BoolLiteralNode (true/false keywords parsed as boolean literals)
	if (constraint_expr.is<BoolLiteralNode>()) {
		const auto& literal = constraint_expr.as<BoolLiteralNode>();
		bool value = literal.value();

		if (!value) {
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: literal constraint is false",
				"false",
				"use 'true' or a valid concept expression");
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
				"use 'true' or a valid concept expression");
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
				"use 'true' or a valid concept expression");
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
					"check that the template argument satisfies the type trait");
			}
			return ConstraintEvaluationResult::success();
		}

		// Otherwise, look up as a concept
		auto concept_opt = gConceptRegistry.lookupConcept(name);
		if (!concept_opt.has_value()) {
			return ConstraintEvaluationResult::failure(
				std::string("constraint not satisfied: concept '") + std::string(name) + "' not found",
				std::string(name),
				std::string("declare the concept before using it in a requires clause"));
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
	if (constraint_expr.is<CallExprNode>()) {
		const auto& func_call = constraint_expr.as<CallExprNode>();
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
							auto type_it = getTypesByNameMap().find(type_handle);
							if (type_it != getTypesByNameMap().end()) {
								TemplateTypeArg type_arg;
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

		FLASH_LOG(Templates, Debug, "CallExprNode concept evaluation: concept='", concept_name, "', concept_args.size()=", concept_args.size(), ", concept_param_names.size()=", concept_param_names.size());
		for (size_t i = 0; i < concept_param_names.size(); ++i) {
			if (i < concept_args.size()) {
				FLASH_LOG(Templates, Debug, "  param[", i, "] name='", concept_param_names[i], "', is_template_template_arg=", concept_args[i].is_template_template_arg, ", base_type=", static_cast<int>(concept_args[i].typeEnum()));
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
				return left_result;	// Return first failure
			}

			auto right_result = evaluateConstraint(binop.get_rhs(), template_args, template_param_names);
			if (!right_result.satisfied) {
				return right_result;
			}

			return ConstraintEvaluationResult::success();
		} else if (op == "||") {
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
				"ensure at least one of the constraints is met");
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
					"check the constraint expression");
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
					"remove the negation or use a different constraint");
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
						"the expression is not valid for the substituted types");
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
							"the expression is ill-formed for the substituted types");
					}
					continue;
				}
				// Check if this is a function call to a constrained template function
				if (std::holds_alternative<CallExprNode>(expr)) {
					const CallExprNode& call = std::get<CallExprNode>(expr);
					std::string_view called_name = call.called_from().value();
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
										callee_param_names.push_back(param_node.name());
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
											"check the constraint on the called function");
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
			TypeCategory base_type_cat = TypeCategory::Invalid;
			TypeIndex type_index{};
			uint8_t pointer_depth = 0;
			ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
			CVQualifier cv_qualifier = CVQualifier::None;
			TypeCategory category() const noexcept { return base_type_cat; }
		};

		// Helper to resolve a type specifier, substituting template parameters
		auto resolve_type = [&](const ASTNode& type_node) -> ResolvedTypeInfo {
			if (!type_node.is<TypeSpecifierNode>())
				return {};
			const TypeSpecifierNode& ts = type_node.as<TypeSpecifierNode>();
			if (ts.category() == TypeCategory::UserDefined || ts.category() == TypeCategory::TypeAlias || ts.category() == TypeCategory::Template) {
				std::string_view name = ts.token().value();
				for (size_t i = 0; i < template_param_names.size() && i < template_args.size(); ++i) {
					if (template_param_names[i] == name) {
						const auto& arg = template_args[i];
						return {arg.typeEnum(), arg.type_index, arg.pointer_depth,
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
				FLASH_LOG(Templates, Debug, "IsSame comparison: first={type=", static_cast<int>(first.base_type_cat),
						  ", idx=", first.type_index, ", ptr=", static_cast<int>(first.pointer_depth),
						  ", ref_qual=", static_cast<int>(first.ref_qualifier),
						  ", cv=", static_cast<int>(first.cv_qualifier), "} second={type=", static_cast<int>(second.base_type_cat),
						  ", idx=", second.type_index, ", ptr=", static_cast<int>(second.pointer_depth),
						  ", ref_qual=", static_cast<int>(second.ref_qualifier),
						  ", cv=", static_cast<int>(second.cv_qualifier), "}");
				result = (first.category() == second.category() &&
						  first.type_index == second.type_index &&
						  first.pointer_depth == second.pointer_depth &&
						  first.ref_qualifier == second.ref_qualifier &&
						  first.cv_qualifier == second.cv_qualifier);
			}
			break;
		}
		case TypeTraitKind::IsIntegral:
			result = isIntegralType(first.category()) && first.ref_qualifier == ReferenceQualifier::None && first.pointer_depth == 0;
			break;
		case TypeTraitKind::IsFloatingPoint:
			result = (first.category() == TypeCategory::Float || first.category() == TypeCategory::Double || first.category() == TypeCategory::LongDouble) && first.ref_qualifier == ReferenceQualifier::None && first.pointer_depth == 0;
			break;
		case TypeTraitKind::IsVoid:
			result = (first.category() == TypeCategory::Void && first.ref_qualifier == ReferenceQualifier::None && first.pointer_depth == 0);
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
				"check that the template argument satisfies the type trait");
		}
		return ConstraintEvaluationResult::success();
	}

	// Default: assume satisfied for unknown expressions
	// This allows templates to compile even with complex constraints
	return ConstraintEvaluationResult::success();
}
