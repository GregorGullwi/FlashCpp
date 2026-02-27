#pragma once
#include "AstNodeTypes_Expr.h"


class LoopStatementNode {
public:
	size_t start_pos;
	size_t end_pos;
};

class WhileLoopNode : public LoopStatementNode {
public:
	explicit WhileLoopNode(size_t start_pos, size_t end_pos, size_t condition,
		size_t body)
		: condition_(condition), body_(body) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	size_t condition() const { return condition_; }
	size_t body() const { return body_; }

private:
	size_t condition_;
	size_t body_;
};

class DoWhileLoopNode : public LoopStatementNode {
public:
	explicit DoWhileLoopNode(size_t start_pos, size_t end_pos, size_t body,
		size_t condition)
		: condition_(condition), body_(body) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	size_t condition() const { return condition_; }
	size_t body() const { return body_; }

private:
	size_t condition_;
	size_t body_;
};

// ForLoopNode removed - use ForStatementNode instead for C++20 compatibility

class ReturnStatementNode {
public:
	explicit ReturnStatementNode(
		std::optional<ASTNode> expression = std::nullopt, Token return_token = Token())
		: expression_(expression), return_token_(return_token) {}

	const std::optional<ASTNode>& expression() const { return expression_; }
	const Token& return_token() const { return return_token_; }

private:
	std::optional<ASTNode>
		expression_; // Optional, as a return statement may not have an expression
	Token return_token_;
};

class InitializerListNode {
public:
	explicit InitializerListNode() {}

	void add_initializer(ASTNode init_expr) {
		initializers_.push_back(init_expr);
		is_designated_.push_back(false);
		member_names_.push_back(StringHandle());  // Invalid StringHandle for positional initializers
	}

	void add_designated_initializer(StringHandle member_name, ASTNode init_expr) {
		initializers_.push_back(init_expr);
		is_designated_.push_back(true);
		member_names_.push_back(member_name);
	}

	const std::vector<ASTNode>& initializers() const {
		return initializers_;
	}

	size_t size() const {
		return initializers_.size();
	}

	bool is_designated(size_t index) const {
		return index < is_designated_.size() && is_designated_[index];
	}

	StringHandle member_name(size_t index) const {
		if (index < member_names_.size()) {
			return member_names_[index];
		}
		return StringHandle();  // Return invalid handle for out of bounds
	}

	bool has_any_designated() const {
		for (bool is_des : is_designated_) {
			if (is_des) return true;
		}
		return false;
	}

private:
	std::vector<ASTNode> initializers_;
	std::vector<bool> is_designated_;
	std::vector<StringHandle> member_names_;
};

class IfStatementNode {
public:
	explicit IfStatementNode(ASTNode condition, ASTNode then_statement,
		std::optional<ASTNode> else_statement = std::nullopt,
		std::optional<ASTNode> init_statement = std::nullopt,
		bool is_constexpr = false)
		: condition_(condition), then_statement_(then_statement),
		  else_statement_(else_statement), init_statement_(init_statement),
		  is_constexpr_(is_constexpr) {}

	auto get_condition() const { return condition_; }
	auto get_then_statement() const { return then_statement_; }
	auto get_else_statement() const { return else_statement_; }
	auto get_init_statement() const { return init_statement_; }
	bool has_else() const { return else_statement_.has_value(); }
	bool has_init() const { return init_statement_.has_value(); }
	bool is_constexpr() const { return is_constexpr_; }

private:
	ASTNode condition_;
	ASTNode then_statement_;
	std::optional<ASTNode> else_statement_;
	std::optional<ASTNode> init_statement_; // C++20 if (init; condition)
	bool is_constexpr_; // C++17 if constexpr
};

class ForStatementNode {
public:
	explicit ForStatementNode(std::optional<ASTNode> init_statement,
		std::optional<ASTNode> condition,
		std::optional<ASTNode> update_expression,
		ASTNode body_statement)
		: init_statement_(init_statement), condition_(condition),
		  update_expression_(update_expression), body_statement_(body_statement) {}

	auto get_init_statement() const { return init_statement_; }
	auto get_condition() const { return condition_; }
	auto get_update_expression() const { return update_expression_; }
	auto get_body_statement() const { return body_statement_; }
	bool has_init() const { return init_statement_.has_value(); }
	bool has_condition() const { return condition_.has_value(); }
	bool has_update() const { return update_expression_.has_value(); }

private:
	std::optional<ASTNode> init_statement_;    // for (init; condition; update)
	std::optional<ASTNode> condition_;
	std::optional<ASTNode> update_expression_;
	ASTNode body_statement_;
};

class WhileStatementNode {
public:
	explicit WhileStatementNode(ASTNode condition, ASTNode body_statement)
		: condition_(condition), body_statement_(body_statement) {}

	auto get_condition() const { return condition_; }
	auto get_body_statement() const { return body_statement_; }

private:
	ASTNode condition_;
	ASTNode body_statement_;
};

class DoWhileStatementNode {
public:
	explicit DoWhileStatementNode(ASTNode body_statement, ASTNode condition)
		: body_statement_(body_statement), condition_(condition) {}

	auto get_body_statement() const { return body_statement_; }
	auto get_condition() const { return condition_; }

private:
	ASTNode body_statement_;
	ASTNode condition_;
};

class RangedForStatementNode {
public:
	explicit RangedForStatementNode(ASTNode loop_variable_decl,
		ASTNode range_expression,
		ASTNode body_statement,
		std::optional<ASTNode> init_statement = std::nullopt)
		: loop_variable_decl_(loop_variable_decl),
		  range_expression_(range_expression),
		  body_statement_(body_statement),
		  init_statement_(init_statement) {}

	auto get_loop_variable_decl() const { return loop_variable_decl_; }
	auto get_range_expression() const { return range_expression_; }
	auto get_body_statement() const { return body_statement_; }
	auto get_init_statement() const { return init_statement_; }
	bool has_init_statement() const { return init_statement_.has_value(); }

private:
	ASTNode loop_variable_decl_;  // for (int x : range)
	ASTNode range_expression_;     // the array or container to iterate over
	ASTNode body_statement_;
	std::optional<ASTNode> init_statement_;  // C++20: for (init; decl : range)
};

class BreakStatementNode {
public:
	explicit BreakStatementNode(Token break_token = Token())
		: break_token_(break_token) {}

	const Token& break_token() const { return break_token_; }

private:
	Token break_token_;
};

class ContinueStatementNode {
public:
	explicit ContinueStatementNode(Token continue_token = Token())
		: continue_token_(continue_token) {}

	const Token& continue_token() const { return continue_token_; }

private:
	Token continue_token_;
};

// Case label node for switch statements
class CaseLabelNode {
public:
	explicit CaseLabelNode(ASTNode case_value, std::optional<ASTNode> statement = std::nullopt)
		: case_value_(case_value), statement_(statement) {}

	auto get_case_value() const { return case_value_; }
	auto get_statement() const { return statement_; }
	bool has_statement() const { return statement_.has_value(); }

private:
	ASTNode case_value_;  // Constant expression for case value
	std::optional<ASTNode> statement_;  // Optional statement (for fall-through cases)
};

// Default label node for switch statements
class DefaultLabelNode {
public:
	explicit DefaultLabelNode(std::optional<ASTNode> statement = std::nullopt)
		: statement_(statement) {}

	auto get_statement() const { return statement_; }
	bool has_statement() const { return statement_.has_value(); }

private:
	std::optional<ASTNode> statement_;  // Optional statement
};

// Switch statement node
class SwitchStatementNode {
public:
	explicit SwitchStatementNode(ASTNode condition, ASTNode body)
		: condition_(condition), body_(body) {}

	auto get_condition() const { return condition_; }
	auto get_body() const { return body_; }

private:
	ASTNode condition_;  // Expression to switch on
	ASTNode body_;       // Body (typically a BlockNode containing case/default labels)
};

// Label statement node (for goto targets)
class LabelStatementNode {
public:
	explicit LabelStatementNode(Token label_token)
		: label_token_(label_token) {}

	std::string_view label_name() const { return label_token_.value(); }
	const Token& label_token() const { return label_token_; }

private:
	Token label_token_;  // The label identifier
};

// Goto statement node
class GotoStatementNode {
public:
	explicit GotoStatementNode(Token label_token, Token goto_token = Token())
		: label_token_(label_token), goto_token_(goto_token) {}

	std::string_view label_name() const { return label_token_.value(); }
	const Token& label_token() const { return label_token_; }
	const Token& goto_token() const { return goto_token_; }

private:
	Token label_token_;  // The target label identifier
	Token goto_token_;   // The goto keyword token (for error reporting)
};

// Typedef declaration node: typedef existing_type new_name;
class TypedefDeclarationNode {
public:
	explicit TypedefDeclarationNode(ASTNode type_node, Token alias_name)
		: type_node_(type_node), alias_name_(alias_name) {}

	const ASTNode& type_node() const { return type_node_; }
	std::string_view alias_name() const { return alias_name_.value(); }
	const Token& alias_token() const { return alias_name_; }

private:
	ASTNode type_node_;  // The underlying type (TypeSpecifierNode)
	Token alias_name_;   // The new type alias name
};

// ============================================================================
// Exception Handling Support
// ============================================================================

// Throw statement node: throw expression; or throw;
class ThrowStatementNode {
public:
	// throw expression;
	explicit ThrowStatementNode(ASTNode expression, Token throw_token)
		: expression_(expression), throw_token_(throw_token), is_rethrow_(false) {}

	// throw; (rethrow)
	explicit ThrowStatementNode(Token throw_token)
		: expression_(), throw_token_(throw_token), is_rethrow_(true) {}

	const std::optional<ASTNode>& expression() const { return expression_; }
	bool is_rethrow() const { return is_rethrow_; }
	const Token& throw_token() const { return throw_token_; }

private:
	std::optional<ASTNode> expression_;  // The expression to throw (nullopt for rethrow)
	Token throw_token_;                   // For error reporting
	bool is_rethrow_;                     // True if this is a rethrow (throw;)
};

// Catch clause node: catch (type identifier) { block }
class CatchClauseNode {
public:
	// catch (type identifier) { block } or catch (type) { block }
	explicit CatchClauseNode(
		std::optional<ASTNode> exception_declaration,  // nullopt for catch(...)
		ASTNode body,
		Token catch_token = Token())
		: exception_declaration_(exception_declaration),
		  body_(body),
		  catch_token_(catch_token),
		  is_catch_all_(false) {}

	// catch(...) { block }
	explicit CatchClauseNode(
		ASTNode body,
		Token catch_token,
		bool catch_all)
		: exception_declaration_(std::nullopt),
		  body_(body),
		  catch_token_(catch_token),
		  is_catch_all_(catch_all) {}

	const std::optional<ASTNode>& exception_declaration() const { return exception_declaration_; }
	const ASTNode& body() const { return body_; }
	const Token& catch_token() const { return catch_token_; }
	bool is_catch_all() const { return is_catch_all_; }

private:
	std::optional<ASTNode> exception_declaration_;  // DeclarationNode for the caught exception, nullopt for catch(...)
	ASTNode body_;                                  // BlockNode for the catch block body
	Token catch_token_;                             // For error reporting
	bool is_catch_all_;                             // True for catch(...)
};

// Try statement node: try { block } catch (...) { block }
class TryStatementNode {
public:
	explicit TryStatementNode(
		ASTNode try_block,
		std::vector<ASTNode> catch_clauses,
		Token try_token = Token())
		: try_block_(try_block),
		  catch_clauses_(std::move(catch_clauses)),
		  try_token_(try_token) {}

	const ASTNode& try_block() const { return try_block_; }
	const std::vector<ASTNode>& catch_clauses() const { return catch_clauses_; }
	const Token& try_token() const { return try_token_; }

private:
	ASTNode try_block_;                   // BlockNode for the try block
	std::vector<ASTNode> catch_clauses_;  // Vector of CatchClauseNode
	Token try_token_;                     // For error reporting
};

// ============================================================================
// Windows SEH (Structured Exception Handling) Support
// ============================================================================

// SEH filter expression node: the expression in __except(filter_expression)
// Returns EXCEPTION_EXECUTE_HANDLER (1), EXCEPTION_CONTINUE_SEARCH (0), or EXCEPTION_CONTINUE_EXECUTION (-1)
class SehFilterExpressionNode {
public:
	explicit SehFilterExpressionNode(ASTNode expression, Token except_token)
		: expression_(expression), except_token_(except_token) {}

	const ASTNode& expression() const { return expression_; }
	const Token& except_token() const { return except_token_; }

private:
	ASTNode expression_;     // The filter expression
	Token except_token_;     // For error reporting
};

// SEH __except clause node: __except(filter) { block }
class SehExceptClauseNode {
public:
	explicit SehExceptClauseNode(
		ASTNode filter_expression,  // SehFilterExpressionNode
		ASTNode body,               // BlockNode
		Token except_token = Token())
		: filter_expression_(filter_expression),
		  body_(body),
		  except_token_(except_token) {}

	const ASTNode& filter_expression() const { return filter_expression_; }
	const ASTNode& body() const { return body_; }
	const Token& except_token() const { return except_token_; }

private:
	ASTNode filter_expression_;  // SehFilterExpressionNode for the filter
	ASTNode body_;               // BlockNode for the __except block body
	Token except_token_;         // For error reporting
};

// SEH __finally clause node: __finally { block }
class SehFinallyClauseNode {
public:
	explicit SehFinallyClauseNode(
		ASTNode body,
		Token finally_token = Token())
		: body_(body),
		  finally_token_(finally_token) {}

	const ASTNode& body() const { return body_; }
	const Token& finally_token() const { return finally_token_; }

private:
	ASTNode body_;           // BlockNode for the __finally block body
	Token finally_token_;    // For error reporting
};

// SEH try-except statement node: __try { block } __except(filter) { block }
class SehTryExceptStatementNode {
public:
	explicit SehTryExceptStatementNode(
		ASTNode try_block,
		ASTNode except_clause,  // SehExceptClauseNode
		Token try_token = Token())
		: try_block_(try_block),
		  except_clause_(except_clause),
		  try_token_(try_token) {}

	const ASTNode& try_block() const { return try_block_; }
	const ASTNode& except_clause() const { return except_clause_; }
	const Token& try_token() const { return try_token_; }

private:
	ASTNode try_block_;      // BlockNode for the __try block
	ASTNode except_clause_;  // SehExceptClauseNode
	Token try_token_;        // For error reporting
};

// SEH try-finally statement node: __try { block } __finally { block }
class SehTryFinallyStatementNode {
public:
	explicit SehTryFinallyStatementNode(
		ASTNode try_block,
		ASTNode finally_clause,  // SehFinallyClauseNode
		Token try_token = Token())
		: try_block_(try_block),
		  finally_clause_(finally_clause),
		  try_token_(try_token) {}

	const ASTNode& try_block() const { return try_block_; }
	const ASTNode& finally_clause() const { return finally_clause_; }
	const Token& try_token() const { return try_token_; }

private:
	ASTNode try_block_;        // BlockNode for the __try block
	ASTNode finally_clause_;   // SehFinallyClauseNode
	Token try_token_;          // For error reporting
};

// SEH __leave statement node: __leave;
// Exits the current __try block and jumps to the __finally or after __except
class SehLeaveStatementNode {
public:
	explicit SehLeaveStatementNode(Token leave_token)
		: leave_token_(leave_token) {}

	const Token& leave_token() const { return leave_token_; }

private:
	Token leave_token_;  // For error reporting
};

// ============================================================================
// C++20 Concepts Support
// ============================================================================

// Compound requirement node: { expression } -> ConceptName
// Used inside requires expressions with return-type-requirements
class CompoundRequirementNode {
public:
	explicit CompoundRequirementNode(
		ASTNode expression,
		std::optional<ASTNode> return_type_constraint = std::nullopt,
		bool is_noexcept = false,
		Token lbrace_token = Token())
		: expression_(expression),
		  return_type_constraint_(return_type_constraint),
		  is_noexcept_(is_noexcept),
		  lbrace_token_(lbrace_token) {}

	const ASTNode& expression() const { return expression_; }
	const std::optional<ASTNode>& return_type_constraint() const { return return_type_constraint_; }
	bool has_return_type_constraint() const { return return_type_constraint_.has_value(); }
	bool is_noexcept() const { return is_noexcept_; }
	const Token& lbrace_token() const { return lbrace_token_; }

private:
	ASTNode expression_;                          // The expression inside { }
	std::optional<ASTNode> return_type_constraint_;  // Optional -> ConceptName or -> Type
	bool is_noexcept_;                            // Whether noexcept specifier was present
	Token lbrace_token_;                          // For error reporting
};

// Requires expression node: requires { expression; }
// Used inside concept definitions and requires clauses
class RequiresExpressionNode {
public:
	explicit RequiresExpressionNode(
		std::vector<ASTNode> requirements,
		Token requires_token = Token())
		: requirements_(std::move(requirements)),
		  requires_token_(requires_token) {}

	const std::vector<ASTNode>& requirements() const { return requirements_; }
	const Token& requires_token() const { return requires_token_; }

private:
	std::vector<ASTNode> requirements_;  // List of requirement expressions
	Token requires_token_;               // For error reporting
};

// Requires clause node: requires constraint
// Used in template declarations to constrain template parameters
class RequiresClauseNode {
public:
	explicit RequiresClauseNode(
		ASTNode constraint_expr,
		Token requires_token = Token())
		: constraint_expr_(constraint_expr),
		  requires_token_(requires_token) {}

	const ASTNode& constraint_expr() const { return constraint_expr_; }
	const Token& requires_token() const { return requires_token_; }

private:
	ASTNode constraint_expr_;  // The constraint expression (can be a concept name or requires expression)
	Token requires_token_;     // For error reporting
};

// Concept declaration node: concept Name = constraint;
// Defines a named concept that can be used to constrain templates
class ConceptDeclarationNode {
public:
	explicit ConceptDeclarationNode(
		Token name,
		std::vector<TemplateParameterNode> template_params,
		ASTNode constraint_expr,
		Token concept_token = Token())
		: name_(name),
		  template_params_(std::move(template_params)),
		  constraint_expr_(constraint_expr),
		  concept_token_(concept_token) {}

	std::string_view name() const { return name_.value(); }
	const Token& name_token() const { return name_; }
	const std::vector<TemplateParameterNode>& template_params() const { return template_params_; }
	const ASTNode& constraint_expr() const { return constraint_expr_; }
	const Token& concept_token() const { return concept_token_; }

private:
	Token name_;                                     // Concept name
	std::vector<TemplateParameterNode> template_params_;  // Template parameters for the concept
	ASTNode constraint_expr_;                        // The constraint expression
	Token concept_token_;                            // For error reporting
};

// Helper to get DeclarationNode from a symbol that could be either DeclarationNode or VariableDeclarationNode
// Returns nullptr if the symbol is neither type
inline const DeclarationNode* get_decl_from_symbol(const ASTNode& symbol) {
	if (symbol.is<DeclarationNode>()) {
		return &symbol.as<DeclarationNode>();
	} else if (symbol.is<VariableDeclarationNode>()) {
		return &symbol.as<VariableDeclarationNode>().declaration();
	}
	return nullptr;
}
