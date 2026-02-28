#pragma once
#include "AstNodeTypes_DeclNodes.h"

// Template parameter kinds
enum class TemplateParameterKind {
	Type,      // typename T or class T
	NonType,   // int N, bool B, etc.
	Template   // template<typename> class Container (template template parameter)
};

// Template parameter node - represents a single template parameter
class TemplateParameterNode {
public:
	// Type parameter: template<typename T> or template<class T>
	TemplateParameterNode(StringHandle name, Token token)
		: kind_(TemplateParameterKind::Type), name_(name), token_(token) {}

	// Non-type parameter: template<int N>
	TemplateParameterNode(StringHandle name, ASTNode type_node, Token token)
		: kind_(TemplateParameterKind::NonType), name_(name), type_node_(type_node), token_(token) {}

	// Template template parameter: template<template<typename> class Container>
	TemplateParameterNode(StringHandle name, std::vector<ASTNode> nested_params, Token token)
		: kind_(TemplateParameterKind::Template), name_(name), nested_params_(std::move(nested_params)), token_(token) {}

	TemplateParameterKind kind() const { return kind_; }
	std::string_view name() const { return name_.view(); }
	StringHandle nameHandle() const { return name_; }
	Token token() const { return token_; }

	// For non-type parameters
	bool has_type() const { return type_node_.has_value(); }
	ASTNode type_node() const { return type_node_.value(); }

	// For template template parameters
	const std::vector<ASTNode>& nested_parameters() const { return nested_params_; }

	// For default arguments (future enhancement)
	bool has_default() const { return default_value_.has_value(); }
	ASTNode default_value() const { return default_value_.value(); }
	void set_default_value(ASTNode value) { default_value_ = value; }

	// For variadic templates (parameter packs)
	bool is_variadic() const { return is_variadic_; }
	void set_variadic(bool variadic) { is_variadic_ = variadic; }

	// For concept constraints (C++20)
	bool has_concept_constraint() const { return concept_constraint_.has_value(); }
	std::string_view concept_constraint() const { return concept_constraint_.value(); }
	void set_concept_constraint(std::string_view constraint) { concept_constraint_ = constraint; }

private:
	TemplateParameterKind kind_;
	StringHandle name_;  // Points directly into source text from lexer token
	std::optional<ASTNode> type_node_;  // For non-type parameters (e.g., int N)
	std::vector<ASTNode> nested_params_;  // For template template parameters (nested template parameters)
	std::optional<ASTNode> default_value_;  // Default argument (e.g., typename T = int)
	Token token_;  // For error reporting
	bool is_variadic_ = false;  // True for parameter packs (typename... Args)
	std::optional<std::string_view> concept_constraint_;  // Concept name for constrained parameters (e.g., Addable T)
};

// Template function declaration node - represents a function template
class TemplateFunctionDeclarationNode {
public:
	TemplateFunctionDeclarationNode() = delete;
	TemplateFunctionDeclarationNode(std::vector<ASTNode> template_params, ASTNode function_decl, 
	                                std::optional<ASTNode> requires_clause = std::nullopt)
		: template_parameters_(std::move(template_params)), 
		  function_declaration_(function_decl),
		  requires_clause_(requires_clause) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	ASTNode function_declaration() const { return function_declaration_; }
	const std::optional<ASTNode>& requires_clause() const { return requires_clause_; }
	bool has_requires_clause() const { return requires_clause_.has_value(); }

	// Get the underlying FunctionDeclarationNode
	FunctionDeclarationNode& function_decl_node() {
		return function_declaration_.as<FunctionDeclarationNode>();
	}
	const FunctionDeclarationNode& function_decl_node() const {
		return function_declaration_.as<FunctionDeclarationNode>();
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	ASTNode function_declaration_;  // FunctionDeclarationNode
	std::optional<ASTNode> requires_clause_;  // Optional RequiresClauseNode
};

// Helper functions to safely extract FunctionDeclarationNode from either
// FunctionDeclarationNode or TemplateFunctionDeclarationNode
// This is needed because many places store ASTNode that could be either type

/// Check if an ASTNode contains a function declaration (direct or template)
inline bool is_function_or_template_function(const ASTNode& node) {
	return node.is<FunctionDeclarationNode>() || node.is<TemplateFunctionDeclarationNode>();
}

/// Get the FunctionDeclarationNode from an ASTNode that is either a 
/// FunctionDeclarationNode or TemplateFunctionDeclarationNode
/// Returns nullptr if the node is neither type
inline const FunctionDeclarationNode* get_function_decl_node(const ASTNode& node) {
	if (node.is<FunctionDeclarationNode>()) {
		return &node.as<FunctionDeclarationNode>();
	} else if (node.is<TemplateFunctionDeclarationNode>()) {
		return &node.as<TemplateFunctionDeclarationNode>().function_decl_node();
	}
	return nullptr;
}

/// Non-const version of get_function_decl_node
inline FunctionDeclarationNode* get_function_decl_node_mut(ASTNode& node) {
	if (node.is<FunctionDeclarationNode>()) {
		return &node.as<FunctionDeclarationNode>();
	} else if (node.is<TemplateFunctionDeclarationNode>()) {
		return &node.as<TemplateFunctionDeclarationNode>().function_decl_node();
	}
	return nullptr;
}

// Template alias declaration: template<typename T> using Ptr = T*;
class TemplateAliasNode {
public:
	TemplateAliasNode() = delete;
	TemplateAliasNode(std::vector<ASTNode> template_params,
	                  std::vector<StringHandle> param_names,
	                  StringHandle alias_name,
	                  ASTNode target_type)
		: template_parameters_(std::move(template_params))
		, template_param_names_(std::move(param_names))
		, alias_name_(alias_name)
		, target_type_(target_type)
		, is_deferred_(false) {}
	
	// Constructor for deferred template instantiation (Option 1)
	TemplateAliasNode(std::vector<ASTNode> template_params,
	                  std::vector<StringHandle> param_names,
	                  StringHandle alias_name,
	                  ASTNode target_type,
	                  StringHandle target_template_name,
	                  std::vector<ASTNode> target_template_args)
		: template_parameters_(std::move(template_params))
		, template_param_names_(std::move(param_names))
		, alias_name_(alias_name)
		, target_type_(target_type)
		, is_deferred_(true)
		, target_template_name_(target_template_name)
		, target_template_args_(std::move(target_template_args)) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	const std::vector<StringHandle>& template_param_names() const { return template_param_names_; }
	std::string_view alias_name() const { return alias_name_.view(); }
	ASTNode target_type() const { return target_type_; }
	
	// Deferred instantiation support
	bool is_deferred() const { return is_deferred_; }
	std::string_view target_template_name() const { return target_template_name_.view(); }
	const std::vector<ASTNode>& target_template_args() const { return target_template_args_; }

	// Get the underlying TypeSpecifierNode
	TypeSpecifierNode& target_type_node() {
		return target_type_.as<TypeSpecifierNode>();
	}
	const TypeSpecifierNode& target_type_node() const {
		return target_type_.as<TypeSpecifierNode>();
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	std::vector<StringHandle> template_param_names_;  // Parameter names for lookup
	StringHandle alias_name_;  // The name of the alias (e.g., "Ptr")
	ASTNode target_type_;  // TypeSpecifierNode - the target type (e.g., T*)
	
	// Deferred instantiation (Option 1: cleaner than string parsing)
	bool is_deferred_;  // True if target is a template with unresolved parameters
	StringHandle target_template_name_;  // Template name (e.g., "integral_constant")
	std::vector<ASTNode> target_template_args_;  // Unevaluated argument AST nodes
};

// Deduction guide declaration: template<typename T> ClassName(T) -> ClassName<T>;
// Enables class template argument deduction (CTAD) in C++17
class DeductionGuideNode {
public:
	DeductionGuideNode() = delete;
	DeductionGuideNode(std::vector<ASTNode> template_params,
	                   std::string_view class_name,
	                   std::vector<ASTNode> guide_params,
	                   std::vector<ASTNode> deduced_template_args)
		: template_parameters_(std::move(template_params))
		, class_name_(class_name)
		, guide_parameters_(std::move(guide_params))
		, deduced_template_args_(std::move(deduced_template_args)) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	std::string_view class_name() const { return class_name_; }
	const std::vector<ASTNode>& guide_parameters() const { return guide_parameters_; }
	const std::vector<ASTNode>& deduced_template_args_nodes() const { return deduced_template_args_; }

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances for the guide's template params
	std::string_view class_name_;  // Name of the class template
	std::vector<ASTNode> guide_parameters_;  // Parameters of the guide (like constructor params)
	std::vector<ASTNode> deduced_template_args_;  // RHS nodes for template arguments (TypeSpecifierNode instances)
};

// Forward declarations
class TemplateClassDeclarationNode;
class VariableDeclarationNode;

// Variable template declaration: template<typename T> constexpr T pi = T(3.14159265358979323846);
class TemplateVariableDeclarationNode {
public:
	TemplateVariableDeclarationNode() = delete;
	TemplateVariableDeclarationNode(std::vector<ASTNode> template_params, ASTNode variable_decl)
		: template_parameters_(std::move(template_params)), variable_declaration_(variable_decl) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	ASTNode variable_declaration() const { return variable_declaration_; }

	// Get the underlying VariableDeclarationNode
	VariableDeclarationNode& variable_decl_node() {
		return variable_declaration_.as<VariableDeclarationNode>();
	}
	const VariableDeclarationNode& variable_decl_node() const {
		return variable_declaration_.as<VariableDeclarationNode>();
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	ASTNode variable_declaration_;  // VariableDeclarationNode
};

// Storage class specifiers
enum class StorageClass {
	None,      // No storage class specified (automatic for local, external for global)
	Static,    // static keyword
	Extern,    // extern keyword
	Register,  // register keyword (deprecated in C++17)
	Mutable    // mutable keyword (for class members)
};

class VariableDeclarationNode {
public:
	explicit VariableDeclarationNode(ASTNode declaration_node, std::optional<ASTNode> initializer = std::nullopt, StorageClass storage_class = StorageClass::None)
		: declaration_node_(declaration_node), initializer_(initializer), storage_class_(storage_class), is_constexpr_(false), is_constinit_(false) {}

	const DeclarationNode& declaration() const { return declaration_node_.as<DeclarationNode>(); }
	const ASTNode& declaration_node() const { return declaration_node_; }
	const std::optional<ASTNode>& initializer() const { return initializer_; }
	StorageClass storage_class() const { return storage_class_; }

	void set_is_constexpr(bool is_constexpr) { is_constexpr_ = is_constexpr; }
	bool is_constexpr() const { return is_constexpr_; }

	void set_is_constinit(bool is_constinit) { is_constinit_ = is_constinit; }
	bool is_constinit() const { return is_constinit_; }

private:
	ASTNode declaration_node_;
	std::optional<ASTNode> initializer_;
	StorageClass storage_class_;
	bool is_constexpr_;
	bool is_constinit_;
};

// Structured binding declaration node (C++17 feature)
// Represents: auto [a, b, c] = expr;
class StructuredBindingNode {
public:
	StructuredBindingNode() = delete;
	StructuredBindingNode(std::vector<StringHandle> identifiers, 
	                      ASTNode initializer,
	                      CVQualifier cv_qualifiers,
	                      ReferenceQualifier ref_qualifier)
		: identifiers_(std::move(identifiers))
		, initializer_(initializer)
		, cv_qualifiers_(cv_qualifiers)
		, ref_qualifier_(ref_qualifier) {}
	
	const std::vector<StringHandle>& identifiers() const { return identifiers_; }
	const ASTNode& initializer() const { return initializer_; }
	CVQualifier cv_qualifiers() const { return cv_qualifiers_; }
	ReferenceQualifier ref_qualifier() const { return ref_qualifier_; }
	
	bool is_const() const { 
		return (static_cast<uint8_t>(cv_qualifiers_) & static_cast<uint8_t>(CVQualifier::Const)) != 0;
	}
	
	bool is_lvalue_reference() const { 
		return ref_qualifier_ == ReferenceQualifier::LValueReference;
	}
	
	bool is_rvalue_reference() const { 
		return ref_qualifier_ == ReferenceQualifier::RValueReference;
	}

private:
	std::vector<StringHandle> identifiers_;  // Binding names: [a, b, c]
	ASTNode initializer_;                     // Expression to decompose
	CVQualifier cv_qualifiers_;               // const/volatile qualifiers
	ReferenceQualifier ref_qualifier_;        // &, &&, or none
};

// Member initializer for constructor initializer lists
struct MemberInitializer {
	std::string_view member_name;
	ASTNode initializer_expr;

	MemberInitializer(std::string_view name, ASTNode expr)
		: member_name(name), initializer_expr(expr) {}
};

// Base class initializer for constructor initializer lists
struct BaseInitializer {
	StringHandle base_class_name;
	std::vector<ASTNode> arguments;

	BaseInitializer(StringHandle name, std::vector<ASTNode> args)
		: base_class_name(name), arguments(std::move(args)) {}
	
	StringHandle getBaseClassName() const {
		return base_class_name;
	}
};

// Delegating constructor initializer (C++11 feature)
struct DelegatingInitializer {
	std::vector<ASTNode> arguments;

	explicit DelegatingInitializer(std::vector<ASTNode> args)
		: arguments(std::move(args)) {}
};

// Constructor declaration node
class ConstructorDeclarationNode {
public:
	ConstructorDeclarationNode() = delete;
	ConstructorDeclarationNode(StringHandle struct_name_handle, StringHandle name_handle)
		: struct_name_(struct_name_handle), name_(name_handle), is_implicit_(false) {}

	StringHandle struct_name() const { return struct_name_; }
	StringHandle name() const { return name_; }
	Token name_token() const { return Token(Token::Type::Identifier, StringTable::getStringView(name_), 0, 0, 0); }  // Create token on demand
	const std::vector<ASTNode>& parameter_nodes() const { return parameter_nodes_; }
	const std::vector<MemberInitializer>& member_initializers() const { return member_initializers_; }
	const std::vector<BaseInitializer>& base_initializers() const { return base_initializers_; }
	const std::optional<DelegatingInitializer>& delegating_initializer() const { return delegating_initializer_; }
	bool is_implicit() const { return is_implicit_; }

	void add_parameter_node(ASTNode parameter_node) {
		parameter_nodes_.push_back(parameter_node);
	}

	// Update parameter nodes from the definition (to use definition's parameter names)
	// C++ allows declaration and definition to have different parameter names
	void update_parameter_nodes_from_definition(const std::vector<ASTNode>& definition_params) {
		if (definition_params.size() != parameter_nodes_.size()) {
			return; // Signature mismatch - shouldn't happen after validation
		}
		parameter_nodes_ = definition_params;
	}

	void add_member_initializer(std::string_view member_name, ASTNode initializer_expr) {
		member_initializers_.emplace_back(member_name, initializer_expr);
	}

	void add_base_initializer(StringHandle base_name, std::vector<ASTNode> args) {
		base_initializers_.emplace_back(base_name, std::move(args));
	}

	void set_delegating_initializer(std::vector<ASTNode> args) {
		delegating_initializer_.emplace(std::move(args));
	}

	void set_is_implicit(bool implicit) {
		is_implicit_ = implicit;
	}

	const std::optional<ASTNode>& get_definition() const {
		return definition_block_;
	}

	bool set_definition(ASTNode block_node) {
		if (definition_block_.has_value())
			return false;
		definition_block_.emplace(block_node);
		return true;
	}

	// Pre-computed mangled name for consistent access across all compiler stages
	void set_mangled_name(std::string_view name) { mangled_name_ = name; }
	std::string_view mangled_name() const { return mangled_name_; }
	bool has_mangled_name() const { return !mangled_name_.empty(); }

	// noexcept specifier support
	void set_noexcept(bool is_noexcept) { is_noexcept_ = is_noexcept; }
	bool is_noexcept() const { return is_noexcept_; }

	// explicit specifier support (for future use)
	void set_explicit(bool is_explicit) { is_explicit_ = is_explicit; }
	bool is_explicit() const { return is_explicit_; }

	// constexpr specifier support (for future use)
	void set_constexpr(bool is_constexpr) { is_constexpr_ = is_constexpr; }
	bool is_constexpr() const { return is_constexpr_; }

	// requires clause support (C++20)
	void set_requires_clause(ASTNode requires_clause) { requires_clause_ = requires_clause; }
	const std::optional<ASTNode>& requires_clause() const { return requires_clause_; }
	bool has_requires_clause() const { return requires_clause_.has_value(); }

	// Template body position: for member function template constructors whose bodies
	// are deferred to instantiation time (two-phase lookup, C++ §13.9.2).
	void set_template_body_position(SaveHandle handle) {
		has_template_body_ = true;
		template_body_position_handle_ = handle;
	}
	bool has_template_body_position() const { return has_template_body_; }
	SaveHandle template_body_position() const { return template_body_position_handle_; }

private:
	StringHandle struct_name_;
	StringHandle name_;
	std::vector<ASTNode> parameter_nodes_;
	std::vector<MemberInitializer> member_initializers_;
	std::vector<BaseInitializer> base_initializers_;  // Base class initializers
	std::optional<DelegatingInitializer> delegating_initializer_;  // Delegating constructor call
	std::optional<ASTNode> definition_block_;  // Store ASTNode to keep BlockNode alive
	bool is_implicit_;  // True if this is an implicitly generated default constructor
	bool is_noexcept_ = false;  // noexcept specifier
	bool is_explicit_ = false;  // explicit specifier
	bool is_constexpr_ = false;  // constexpr specifier
	std::string_view mangled_name_;  // Pre-computed mangled name (points to ChunkedStringAllocator storage)
	std::optional<ASTNode> requires_clause_;  // C++20 trailing requires clause
	bool has_template_body_ = false;
	SaveHandle template_body_position_handle_;  // Handle to saved position for template body
};

// Destructor declaration node
class DestructorDeclarationNode {
public:
	DestructorDeclarationNode() = delete;
	DestructorDeclarationNode(StringHandle struct_name_handle, StringHandle name_handle)
		: struct_name_(struct_name_handle), name_(name_handle) {}

	StringHandle struct_name() const { return struct_name_; }
	StringHandle name() const { return name_; }
	Token name_token() const { return Token(Token::Type::Identifier, StringTable::getStringView(name_), 0, 0, 0); }  // Create token on demand

	const std::optional<ASTNode>& get_definition() const {
		return definition_block_;
	}

	bool set_definition(ASTNode block_node) {
		if (definition_block_.has_value())
			return false;
		definition_block_.emplace(block_node);
		return true;
	}

	// Pre-computed mangled name for consistent access across all compiler stages
	void set_mangled_name(StringHandle name) { mangled_name_ = name; }
	StringHandle mangled_name() const { return mangled_name_; }
	bool has_mangled_name() const { return mangled_name_.isValid(); }

	// noexcept specifier support
	void set_noexcept(bool is_noexcept) { is_noexcept_ = is_noexcept; }
	bool is_noexcept() const { return is_noexcept_; }

private:
	StringHandle struct_name_;  // Points directly into source text from lexer token
	StringHandle name_;         // Points directly into source text from lexer token
	std::optional<ASTNode> definition_block_;  // Store ASTNode to keep BlockNode alive
	StringHandle mangled_name_;  // Pre-computed mangled name (points to ChunkedStringAllocator storage)
	bool is_noexcept_ = false;  // noexcept specifier
};

// Anonymous union member information - stored during parsing, processed during layout
struct AnonymousUnionMemberInfo {
	StringHandle member_name;            // Name of the union member
	Type member_type;                    // Base type of the member
	TypeIndex type_index;                // Type index for struct types
	size_t member_size;                  // Size in bytes (including array size if applicable)
	size_t member_alignment;             // Alignment requirement in bytes
	std::optional<size_t> bitfield_width; // Width in bits for bitfield members
	size_t referenced_size_bits;         // Size in bits of referenced type (for references)
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;  // None, LValueReference, or RValueReference
	bool is_array;                       // True if member is an array
	std::vector<size_t> array_dimensions; // Dimension sizes for multidimensional arrays (e.g., {3, 3} for int[3][3])
	int pointer_depth;                   // Pointer indirection level

	// Convenience helpers
	bool is_reference() const { return reference_qualifier != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier == ReferenceQualifier::RValueReference; }
	
	AnonymousUnionMemberInfo(StringHandle name, Type type, TypeIndex tidx, size_t size, size_t align,
	                         std::optional<size_t> bitfield_w,
	                         size_t ref_size_bits, ReferenceQualifier ref_qual,
	                         bool is_arr,
	                         int ptr_depth,
	                         std::vector<size_t> arr_dims)
		: member_name(name), member_type(type), type_index(tidx), member_size(size),
		  member_alignment(align), bitfield_width(bitfield_w), referenced_size_bits(ref_size_bits), reference_qualifier(ref_qual),
		  is_array(is_arr), array_dimensions(std::move(arr_dims)),
		  pointer_depth(ptr_depth) {}
};

// Anonymous union information - groups all members that should share the same offset
struct AnonymousUnionInfo {
	size_t member_index_in_ast;  // Index in members_ vector where this union appears
	std::vector<AnonymousUnionMemberInfo> union_members;
	bool is_union;  // true for union (anonymous struct would be false, but not yet implemented)
	
	AnonymousUnionInfo(size_t index, bool is_u) : member_index_in_ast(index), is_union(is_u) {}
};

// Struct member with access specifier
struct StructMemberDecl {
	ASTNode declaration;
	AccessSpecifier access;
	std::optional<ASTNode> default_initializer;  // C++11 default member initializer
	std::optional<size_t> bitfield_width;
	std::optional<ASTNode> bitfield_width_expr;  // Deferred bitfield width for template non-type params

	StructMemberDecl(ASTNode decl, AccessSpecifier acc, std::optional<ASTNode> init = std::nullopt,
	                 std::optional<size_t> width = std::nullopt)
		: declaration(decl), access(acc), default_initializer(init), bitfield_width(width),
		  bitfield_width_expr(std::nullopt) {}
};

// Struct member function with access specifier
struct StructMemberFunctionDecl {
	ASTNode function_declaration;  // FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
	AccessSpecifier access;
	bool is_constructor;
	bool is_destructor;
	bool is_operator_overload;
	std::string_view operator_symbol;  // The operator symbol (e.g., "=", "+") if is_operator_overload is true

	// Virtual function support (Phase 2)
	bool is_virtual = false;        // True if declared with 'virtual' keyword
	bool is_pure_virtual = false;   // True if pure virtual (= 0)
	bool is_override = false;       // True if declared with 'override' keyword
	bool is_final = false;          // True if declared with 'final' keyword

	// CV qualifiers for member functions (Phase 4)
	bool is_const = false;          // True if const member function (e.g., void foo() const)
	bool is_volatile = false;       // True if volatile member function (e.g., void foo() volatile)

	StructMemberFunctionDecl(ASTNode func_decl, AccessSpecifier acc, bool is_ctor = false, bool is_dtor = false,
	                         bool is_op_overload = false, std::string_view op_symbol = "")
		: function_declaration(func_decl), access(acc), is_constructor(is_ctor), is_destructor(is_dtor),
		  is_operator_overload(is_op_overload), operator_symbol(op_symbol) {}
};

// Friend declaration node
class FriendDeclarationNode {
public:
	// Friend class declaration: friend class ClassName;
	explicit FriendDeclarationNode(FriendKind kind, StringHandle name)
		: kind_(kind), name_(name) {}

	// Friend member function declaration: friend void ClassName::functionName();
	FriendDeclarationNode(FriendKind kind, StringHandle name, StringHandle class_name)
		: kind_(kind), name_(name), class_name_(class_name) {}

	FriendKind kind() const { return kind_; }
	StringHandle name() const { return name_; }
	StringHandle class_name() const { return class_name_; }

	// For friend functions, store the function declaration
	void set_function_declaration(ASTNode decl) { function_decl_ = decl; }
	std::optional<ASTNode> function_declaration() const { return function_decl_; }

private:
	FriendKind kind_;
	StringHandle name_;           // Function or class name
	StringHandle class_name_;     // For member functions: the class name
	std::optional<ASTNode> function_decl_;  // For friend functions
};

// Type alias declaration (using alias = type;)
struct TypeAliasDecl {
	StringHandle alias_name;  // The alias name
	ASTNode type_node;            // TypeSpecifierNode representing the aliased type
	AccessSpecifier access;       // Access specifier (public/private/protected)

	TypeAliasDecl(StringHandle name, ASTNode type, AccessSpecifier acc)
		: alias_name(name), type_node(type), access(acc) {}
};

// Static member declaration (for AST storage in templates/partial specializations)
struct StaticMemberDecl {
	StringHandle name;            // The member name
	Type type;                    // The member type
	TypeIndex type_index;         // Type index for user-defined types
	size_t size;                  // Size in bytes
	size_t alignment;             // Alignment requirement
	AccessSpecifier access;       // Access specifier (public/private/protected)
	std::optional<ASTNode> initializer;  // AST node for initializer expression (e.g., sizeof(T)), used for template parameter substitution during instantiation
	bool is_const;                // Whether member is const
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;  // None, LValueReference (&), or RValueReference (&&)
	int pointer_depth = 0;        // Pointer indirection level (e.g., int* = 1, int** = 2)

	StaticMemberDecl(StringHandle name_, Type type_, TypeIndex type_index_, size_t size_, size_t alignment_,
	                 AccessSpecifier access_, std::optional<ASTNode> initializer_, bool is_const_,
	                 ReferenceQualifier ref_qual_ = ReferenceQualifier::None, int ptr_depth_ = 0)
		: name(name_), type(type_), type_index(type_index_), size(size_), alignment(alignment_),
		  access(access_), initializer(initializer_), is_const(is_const_),
		  reference_qualifier(ref_qual_), pointer_depth(ptr_depth_) {}
};

class StructDeclarationNode {
public:
	explicit StructDeclarationNode(StringHandle name, bool is_class = false, bool is_union = false)
		: name_(name), is_class_(is_class), is_union_(is_union) {}

	StringHandle name() const { return name_; }
	const std::vector<StructMemberDecl>& members() const { return members_; }
	const std::vector<StructMemberFunctionDecl>& member_functions() const { return member_functions_; }
	std::vector<StructMemberFunctionDecl>& member_functions() { return member_functions_; }
	const std::vector<BaseClassSpecifier>& base_classes() const { return base_classes_; }
	const std::vector<DeferredBaseClassSpecifier>& deferred_base_classes() const { return deferred_base_classes_; }
	std::vector<DeferredBaseClassSpecifier>& deferred_base_classes() { return deferred_base_classes_; }
	const std::vector<DeferredTemplateBaseClassSpecifier>& deferred_template_base_classes() const { return deferred_template_base_classes_; }
	bool is_class() const { return is_class_; }
	bool is_union() const { return is_union_; }
	bool is_final() const { return is_final_; }
	void set_is_final(bool final) { is_final_ = final; }
	bool is_forward_declaration() const { return is_forward_declaration_; }
	void set_is_forward_declaration(bool value) { is_forward_declaration_ = value; }
	AccessSpecifier default_access() const {
		return is_class_ ? AccessSpecifier::Private : AccessSpecifier::Public;
	}

	void add_member(const ASTNode& member, AccessSpecifier access, std::optional<ASTNode> default_initializer = std::nullopt,
	               std::optional<size_t> bitfield_width = std::nullopt,
	               std::optional<ASTNode> bitfield_width_expr = std::nullopt) {
		members_.emplace_back(member, access, std::move(default_initializer), bitfield_width);
		members_.back().bitfield_width_expr = std::move(bitfield_width_expr);
	}

	void add_base_class(std::string_view base_name, TypeIndex base_type_index, AccessSpecifier access, bool is_virtual = false, bool is_deferred = false) {
		base_classes_.emplace_back(base_name, base_type_index, access, is_virtual, 0, is_deferred);
	}

	void add_deferred_base_class(ASTNode decltype_expr, AccessSpecifier access, bool is_virtual = false) {
		deferred_base_classes_.emplace_back(decltype_expr, access, is_virtual);
	}

	void add_deferred_template_base_class(StringHandle base_template_name,
	                                      std::vector<TemplateArgumentNodeInfo> args,
	                                      std::optional<StringHandle> member_type,
	                                      AccessSpecifier access,
	                                      bool is_virtual = false) {
		deferred_template_base_classes_.emplace_back(base_template_name, std::move(args), member_type, access, is_virtual);
	}

	void add_member_function(ASTNode function_decl, AccessSpecifier access,
	                         bool is_virtual = false, bool is_pure_virtual = false,
	                         bool is_override = false, bool is_final = false,
	                         bool is_const = false, bool is_volatile = false) {
		auto& func_decl = member_functions_.emplace_back(function_decl, access, false, false);
		func_decl.is_virtual = is_virtual;
		func_decl.is_pure_virtual = is_pure_virtual;
		func_decl.is_override = is_override;
		func_decl.is_final = is_final;
		func_decl.is_const = is_const;
		func_decl.is_volatile = is_volatile;
	}

	void add_constructor(ASTNode constructor_decl, AccessSpecifier access) {
		member_functions_.emplace_back(constructor_decl, access, true, false);
	}

	void add_destructor(ASTNode destructor_decl, AccessSpecifier access, bool is_virtual = false) {
		auto& dtor_decl = member_functions_.emplace_back(destructor_decl, access, false, true, false, "");
		dtor_decl.is_virtual = is_virtual;
	}

	void add_operator_overload(std::string_view operator_symbol, ASTNode function_decl, AccessSpecifier access,
	                           bool is_virtual = false, bool is_pure_virtual = false,
	                           bool is_override = false, bool is_final = false,
	                           bool is_const = false, bool is_volatile = false) {
		auto& func_decl = member_functions_.emplace_back(function_decl, access, false, false, true, operator_symbol);
		func_decl.is_virtual = is_virtual;
		func_decl.is_pure_virtual = is_pure_virtual;
		func_decl.is_override = is_override;
		func_decl.is_final = is_final;
		func_decl.is_const = is_const;
		func_decl.is_volatile = is_volatile;
	}

	// Friend declaration support
	void add_friend(ASTNode friend_decl) {
		friend_declarations_.push_back(friend_decl);
	}

	const std::vector<ASTNode>& friend_declarations() const {
		return friend_declarations_;
	}

	// Nested class support
	void add_nested_class(ASTNode nested_class) {
		nested_classes_.push_back(nested_class);
	}

	const std::vector<ASTNode>& nested_classes() const {
		return nested_classes_;
	}

	// Type alias support
	void add_type_alias(StringHandle alias_name, ASTNode type_node, AccessSpecifier access) {
		type_aliases_.emplace_back(alias_name, type_node, access);
	}

	const std::vector<TypeAliasDecl>& type_aliases() const {
		return type_aliases_;
	}

	// Static member support (for template/partial specialization AST storage)
	void add_static_member(StringHandle name, Type type, TypeIndex type_index, size_t size, size_t alignment,
	                       AccessSpecifier access, std::optional<ASTNode> initializer, bool is_const,
	                       ReferenceQualifier ref_qual = ReferenceQualifier::None, int ptr_depth = 0) {
		static_members_.emplace_back(name, type, type_index, size, alignment, access, initializer, is_const, ref_qual, ptr_depth);
	}

	const std::vector<StaticMemberDecl>& static_members() const {
		return static_members_;
	}
	
	// Anonymous union support
	void add_anonymous_union_marker(size_t member_index, bool is_union) {
		anonymous_unions_.emplace_back(member_index, is_union);
	}
	
	// Add a member to the most recently created anonymous union
	// Must be called after add_anonymous_union_marker()
	void add_anonymous_union_member(StringHandle member_name, Type member_type, TypeIndex type_index,
	                                 size_t member_size, size_t member_alignment, std::optional<size_t> bitfield_width,
	                                 size_t referenced_size_bits, ReferenceQualifier reference_qualifier,
	                                 bool is_array,
	                                 int pointer_depth,
	                                 std::vector<size_t> array_dimensions) {
		// Add to the last anonymous union that was created
		if (!anonymous_unions_.empty()) {
			anonymous_unions_.back().union_members.emplace_back(
				member_name, member_type, type_index, member_size, member_alignment,
				bitfield_width, referenced_size_bits, reference_qualifier, is_array,
				pointer_depth,
				std::move(array_dimensions)
			);
		}
		// Note: If anonymous_unions_ is empty, this is a programming error in the parser
		// The parser should always call add_anonymous_union_marker() before adding members
	}
	
	const std::vector<AnonymousUnionInfo>& anonymous_unions() const {
		return anonymous_unions_;
	}

	void set_enclosing_class(StructDeclarationNode* enclosing) {
		enclosing_class_ = enclosing;
	}

	StructDeclarationNode* enclosing_class() const {
		return enclosing_class_;
	}

	// Deleted special member function tracking
	void mark_deleted_default_constructor() { has_deleted_default_constructor_ = true; }
	void mark_deleted_copy_constructor() { has_deleted_copy_constructor_ = true; }
	void mark_deleted_move_constructor() { has_deleted_move_constructor_ = true; }

	bool has_deleted_default_constructor() const { return has_deleted_default_constructor_; }
	bool has_deleted_copy_constructor() const { return has_deleted_copy_constructor_; }
	bool has_deleted_move_constructor() const { return has_deleted_move_constructor_; }

	bool is_nested() const {
		return enclosing_class_ != nullptr;
	}

	// Get fully qualified name (e.g., "Outer::Inner")
	StringHandle qualified_name() const {
		if (enclosing_class_) {
			return StringTable::getOrInternStringHandle(StringBuilder().append(enclosing_class_->qualified_name()).append("::"sv).append(name_).commit());
		}
		return name_;
	}

	// Deferred static_assert support (for templates)
	void add_deferred_static_assert(ASTNode condition_expr, StringHandle message) {
		deferred_static_asserts_.emplace_back(condition_expr, message);
	}
	
	const std::vector<DeferredStaticAssert>& deferred_static_asserts() const {
		return deferred_static_asserts_;
	}

private:
	StringHandle name_;  // Points directly into source text from lexer token
	std::vector<StructMemberDecl> members_;
	std::vector<StructMemberFunctionDecl> member_functions_;
	std::vector<BaseClassSpecifier> base_classes_;  // Base classes for inheritance
	std::vector<DeferredBaseClassSpecifier> deferred_base_classes_;  // Decltype base classes (for templates)
	std::vector<DeferredTemplateBaseClassSpecifier> deferred_template_base_classes_;  // Template-dependent base classes
	std::vector<ASTNode> friend_declarations_;  // Friend declarations
	std::vector<ASTNode> nested_classes_;  // Nested classes
	std::vector<TypeAliasDecl> type_aliases_;  // Type aliases (using X = Y;)
	std::vector<StaticMemberDecl> static_members_;  // Static members (for templates)
	std::vector<AnonymousUnionInfo> anonymous_unions_;  // Anonymous union tracking info
	StructDeclarationNode* enclosing_class_ = nullptr;  // Enclosing class (if nested)
	bool is_class_;  // true for class, false for struct
	bool is_union_;  // true for union, false for struct/class
	bool is_final_ = false;  // true if declared with 'final' keyword
	bool is_forward_declaration_ = false;  // true for forward declarations without body
	bool has_deleted_default_constructor_ = false;  // Track deleted default constructor
	bool has_deleted_copy_constructor_ = false;     // Track deleted copy constructor
	bool has_deleted_move_constructor_ = false;     // Track deleted move constructor
	std::vector<DeferredStaticAssert> deferred_static_asserts_;  // Static_asserts deferred during template definition
};

// Template class declaration node - represents a class template
class TemplateClassDeclarationNode {
public:
	TemplateClassDeclarationNode() = delete;
	TemplateClassDeclarationNode(std::vector<ASTNode> template_params, 
								std::vector<std::string_view> param_names,
								ASTNode class_decl)
		: template_parameters_(std::move(template_params))
		, template_param_names_(std::move(param_names))
		, class_declaration_(class_decl) {}

	const std::vector<ASTNode>& template_parameters() const { return template_parameters_; }
	std::vector<ASTNode>& template_parameters() { return template_parameters_; }
	const std::vector<std::string_view>& template_param_names() const { return template_param_names_; }
	ASTNode class_declaration() const { return class_declaration_; }

	// Get the underlying StructDeclarationNode
	StructDeclarationNode& class_decl_node() {
		return class_declaration_.as<StructDeclarationNode>();
	}
	const StructDeclarationNode& class_decl_node() const {
		return class_declaration_.as<StructDeclarationNode>();
	}

	// Deferred template body parsing support
	void set_deferred_bodies(std::vector<DeferredTemplateMemberBody> bodies) {
		deferred_bodies_ = std::move(bodies);
	}
	const std::vector<DeferredTemplateMemberBody>& deferred_bodies() const {
		return deferred_bodies_;
	}
	std::vector<DeferredTemplateMemberBody>& deferred_bodies() {
		return deferred_bodies_;
	}

private:
	std::vector<ASTNode> template_parameters_;  // TemplateParameterNode instances
	std::vector<std::string_view> template_param_names_;  // Parameter names for lookup
	ASTNode class_declaration_;  // StructDeclarationNode
	std::vector<DeferredTemplateMemberBody> deferred_bodies_;  // Member function bodies to parse at instantiation

};
