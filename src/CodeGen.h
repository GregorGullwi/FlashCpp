#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "SymbolTable.h"
#include <type_traits>
#include <variant>
#include <vector>
#include <unordered_map>
#include <assert.h>
#include "IRConverter.h"

class Parser;

class AstToIr {
public:
	AstToIr() {}

	void visit(const ASTNode& node) {
		if (node.is<FunctionDeclarationNode>()) {
			visitFunctionDeclarationNode(node.as<FunctionDeclarationNode>());
		}
		else if (node.is<ReturnStatementNode>()) {
			visitReturnStatementNode(node.as<ReturnStatementNode>());
		}
		else if (node.is<VariableDeclarationNode>()) {
			visitVariableDeclarationNode(node);
		}
		else if (node.is<IfStatementNode>()) {
			visitIfStatementNode(node.as<IfStatementNode>());
		}
		else if (node.is<ForStatementNode>()) {
			visitForStatementNode(node.as<ForStatementNode>());
		}
		else if (node.is<RangedForStatementNode>()) {
			visitRangedForStatementNode(node.as<RangedForStatementNode>());
		}
		else if (node.is<WhileStatementNode>()) {
			visitWhileStatementNode(node.as<WhileStatementNode>());
		}
		else if (node.is<DoWhileStatementNode>()) {
			visitDoWhileStatementNode(node.as<DoWhileStatementNode>());
		}
		else if (node.is<BreakStatementNode>()) {
			visitBreakStatementNode(node.as<BreakStatementNode>());
		}
		else if (node.is<ContinueStatementNode>()) {
			visitContinueStatementNode(node.as<ContinueStatementNode>());
		}
		else if (node.is<BlockNode>()) {
			visitBlockNode(node.as<BlockNode>());
		}
		else if (node.is<ExpressionNode>()) {
			visitExpressionNode(node.as<ExpressionNode>());
		}
		else if (node.is<StructDeclarationNode>()) {
			visitStructDeclarationNode(node.as<StructDeclarationNode>());
		}
		else if (node.is<NamespaceDeclarationNode>()) {
			visitNamespaceDeclarationNode(node.as<NamespaceDeclarationNode>());
		}
		else {
			assert(false && "Unhandled AST node type");
		}
	}

	const Ir& getIr() const { return ir_; }

private:
	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		auto definition_block = node.get_definition();
		if (!definition_block.has_value())
			return;

		// Reset the temporary variable counter for each new function
		var_counter = TempVar();

		const DeclarationNode& func_decl = node.decl_node();
		const TypeSpecifierNode& ret_type = func_decl.type_node().as<TypeSpecifierNode>();

		// Create function declaration with return type and name
		std::vector<IrOperand> funcDeclOperands;
		funcDeclOperands.emplace_back(ret_type.type());
		funcDeclOperands.emplace_back(static_cast<int>(ret_type.size_in_bits()));
		funcDeclOperands.emplace_back(func_decl.identifier_token().value());

		// Add struct/class name for member functions
		if (node.is_member_function()) {
			funcDeclOperands.emplace_back(std::string_view(node.parent_struct_name()));
		} else {
			funcDeclOperands.emplace_back(std::string_view(""));  // Empty string_view for non-member functions
		}

		// Add parameter types to function declaration
		//size_t paramCount = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			// Add parameter type and size to function declaration
			funcDeclOperands.emplace_back(param_type.type());
			funcDeclOperands.emplace_back(static_cast<int>(param_type.size_in_bits()));
			funcDeclOperands.emplace_back(param_decl.identifier_token().value());

			//paramCount++;
			var_counter.next();
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(funcDeclOperands), func_decl.identifier_token()));

		symbol_table.enter_scope(ScopeType::Function);

		// For member functions, add implicit 'this' pointer to symbol table
		if (node.is_member_function()) {
			// Look up the struct type to get its type index and size
			auto type_it = gTypesByName.find(node.parent_struct_name());
			if (type_it != gTypesByName.end()) {
				const TypeInfo* struct_type_info = type_it->second;
				const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

				if (struct_info) {
					// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
					Token this_token = func_decl.identifier_token();  // Use function token for location
					auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
						Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
					auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

					// Add 'this' to symbol table (it's the implicit first parameter)
					symbol_table.insert("this", this_decl);
				}
			}
		}

		// Allocate stack space for local variables and parameters
		// Parameters are already in their registers, we just need to allocate space for them
		//size_t paramIndex = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			//const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			symbol_table.insert(param_decl.identifier_token().value(), param);
			//paramIndex++;
		}

		(*definition_block)->get_statements().visit([&](ASTNode statement) {
			visit(statement);
		});

		symbol_table.exit_scope();
	}

	void visitStructDeclarationNode(const StructDeclarationNode& node) {
		// Struct declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system
		// However, we need to visit member functions to generate IR for them
		for (const auto& member_func : node.member_functions()) {
			// Each member function has a FunctionDeclarationNode
			visit(member_func.function_declaration);
		}
	}

	void visitNamespaceDeclarationNode(const NamespaceDeclarationNode& node) {
		// Namespace declarations themselves don't generate IR - they just provide scope
		// Visit all declarations within the namespace
		for (const auto& decl : node.declarations()) {
			visit(decl);
		}
	}

	void visitReturnStatementNode(const ReturnStatementNode& node) {
		if (node.expression()) {
			auto operands = visitExpressionNode(node.expression()->as<ExpressionNode>());
			// Add the return value's type and size information
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(operands), node.return_token()));
		}
		else {
			// For void returns, we don't need any operands
			ir_.addInstruction(IrOpcode::Return, {}, node.return_token());
		}
	}

	void visitBlockNode(const BlockNode& node) {
		// Visit all statements in the block
		node.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});
	}

	void visitIfStatementNode(const IfStatementNode& node) {
		// Generate unique labels for this if statement
		static size_t if_counter = 0;
		std::string then_label = "if_then_" + std::to_string(if_counter);
		std::string else_label = "if_else_" + std::to_string(if_counter);
		std::string end_label = "if_end_" + std::to_string(if_counter);
		if_counter++;

		// Handle C++20 if-with-initializer
		if (node.has_init()) {
			auto init_stmt = node.get_init_statement();
			if (init_stmt.has_value()) {
				visit(*init_stmt);
			}
		}

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch
		std::vector<IrOperand> branch_operands;
		branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
		branch_operands.emplace_back(then_label);
		branch_operands.emplace_back(node.has_else() ? else_label : end_label);
		ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());

		// Then block
		ir_.addInstruction(IrOpcode::Label, {then_label}, Token());

		// Visit then statement
		auto then_stmt = node.get_then_statement();
		if (then_stmt.is<BlockNode>()) {
			then_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(then_stmt);
		}

		// Branch to end after then block (skip else)
		if (node.has_else()) {
			ir_.addInstruction(IrOpcode::Branch, {end_label}, Token());
		}

		// Else block (if present)
		if (node.has_else()) {
			ir_.addInstruction(IrOpcode::Label, {else_label}, Token());

			auto else_stmt = node.get_else_statement();
			if (else_stmt.has_value()) {
				if (else_stmt->is<BlockNode>()) {
					else_stmt->as<BlockNode>().get_statements().visit([&](ASTNode statement) {
						visit(statement);
					});
				} else {
					visit(*else_stmt);
				}
			}
		}

		// End label
		ir_.addInstruction(IrOpcode::Label, {end_label}, Token());
	}

	void visitForStatementNode(const ForStatementNode& node) {
		// Generate unique labels for this for loop
		static size_t for_counter = 0;
		std::string loop_start_label = "for_start_" + std::to_string(for_counter);
		std::string loop_body_label = "for_body_" + std::to_string(for_counter);
		std::string loop_increment_label = "for_increment_" + std::to_string(for_counter);
		std::string loop_end_label = "for_end_" + std::to_string(for_counter);
		for_counter++;

		// Execute init statement (if present)
		if (node.has_init()) {
			auto init_stmt = node.get_init_statement();
			if (init_stmt.has_value()) {
				visit(*init_stmt);
			}
		}

		// Mark loop begin for break/continue support
		ir_.addInstruction(IrOpcode::LoopBegin, {loop_start_label, loop_end_label, loop_increment_label}, Token());

		// Loop start: evaluate condition
		ir_.addInstruction(IrOpcode::Label, {loop_start_label}, Token());

		// Evaluate condition (if present, otherwise infinite loop)
		if (node.has_condition()) {
			auto condition_operands = visitExpressionNode(node.get_condition()->as<ExpressionNode>());

			// Generate conditional branch: if true goto body, else goto end
			std::vector<IrOperand> branch_operands;
			branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
			branch_operands.emplace_back(loop_body_label);
			branch_operands.emplace_back(loop_end_label);
			ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());
		}

		// Loop body label
		ir_.addInstruction(IrOpcode::Label, {loop_body_label}, Token());

		// Visit loop body
		auto body_stmt = node.get_body_statement();
		if (body_stmt.is<BlockNode>()) {
			body_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(body_stmt);
		}

		// Loop increment label (for continue statements)
		ir_.addInstruction(IrOpcode::Label, {loop_increment_label}, Token());

		// Execute update/increment expression (if present)
		if (node.has_update()) {
			visitExpressionNode(node.get_update_expression()->as<ExpressionNode>());
		}

		// Branch back to loop start
		ir_.addInstruction(IrOpcode::Branch, {loop_start_label}, Token());

		// Loop end label
		ir_.addInstruction(IrOpcode::Label, {loop_end_label}, Token());

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitWhileStatementNode(const WhileStatementNode& node) {
		// Generate unique labels for this while loop
		static size_t while_counter = 0;
		std::string loop_start_label = "while_start_" + std::to_string(while_counter);
		std::string loop_body_label = "while_body_" + std::to_string(while_counter);
		std::string loop_end_label = "while_end_" + std::to_string(while_counter);
		while_counter++;

		// Mark loop begin for break/continue support
		// For while loops, continue jumps to loop_start (re-evaluate condition)
		ir_.addInstruction(IrOpcode::LoopBegin, {loop_start_label, loop_end_label, loop_start_label}, Token());

		// Loop start: evaluate condition
		ir_.addInstruction(IrOpcode::Label, {loop_start_label}, Token());

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch: if true goto body, else goto end
		std::vector<IrOperand> branch_operands;
		branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
		branch_operands.emplace_back(loop_body_label);
		branch_operands.emplace_back(loop_end_label);
		ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());

		// Loop body label
		ir_.addInstruction(IrOpcode::Label, {loop_body_label}, Token());

		// Visit loop body
		auto body_stmt = node.get_body_statement();
		if (body_stmt.is<BlockNode>()) {
			body_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(body_stmt);
		}

		// Branch back to loop start (re-evaluate condition)
		ir_.addInstruction(IrOpcode::Branch, {loop_start_label}, Token());

		// Loop end label
		ir_.addInstruction(IrOpcode::Label, {loop_end_label}, Token());

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitDoWhileStatementNode(const DoWhileStatementNode& node) {
		// Generate unique labels for this do-while loop
		static size_t do_while_counter = 0;
		std::string loop_start_label = "do_while_start_" + std::to_string(do_while_counter);
		std::string loop_condition_label = "do_while_condition_" + std::to_string(do_while_counter);
		std::string loop_end_label = "do_while_end_" + std::to_string(do_while_counter);
		do_while_counter++;

		// Mark loop begin for break/continue support
		// For do-while loops, continue jumps to condition check (not body start)
		ir_.addInstruction(IrOpcode::LoopBegin, {loop_start_label, loop_end_label, loop_condition_label}, Token());

		// Loop start: execute body first (do-while always executes at least once)
		ir_.addInstruction(IrOpcode::Label, {loop_start_label}, Token());

		// Visit loop body
		auto body_stmt = node.get_body_statement();
		if (body_stmt.is<BlockNode>()) {
			body_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(body_stmt);
		}

		// Condition check label (for continue statements)
		ir_.addInstruction(IrOpcode::Label, {loop_condition_label}, Token());

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch: if true goto start, else goto end
		std::vector<IrOperand> branch_operands;
		branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
		branch_operands.emplace_back(loop_start_label);
		branch_operands.emplace_back(loop_end_label);
		ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());

		// Loop end label
		ir_.addInstruction(IrOpcode::Label, {loop_end_label}, Token());

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitRangedForStatementNode(const RangedForStatementNode& node) {
		// Desugar ranged for loop into traditional for loop
		// for (int x : arr) { body } becomes:
		// for (int __i = 0; __i < array_size; ++__i) { int x = arr[__i]; body }

		// Generate unique labels and counter for this ranged for loop
		static size_t ranged_for_counter = 0;
		std::string loop_start_label = "ranged_for_start_" + std::to_string(ranged_for_counter);
		std::string loop_body_label = "ranged_for_body_" + std::to_string(ranged_for_counter);
		std::string loop_increment_label = "ranged_for_increment_" + std::to_string(ranged_for_counter);
		std::string loop_end_label = "ranged_for_end_" + std::to_string(ranged_for_counter);
		std::string index_var_name = "__range_index_" + std::to_string(ranged_for_counter);
		ranged_for_counter++;

		// Get the loop variable declaration and range expression
		auto loop_var_decl = node.get_loop_variable_decl();
		auto range_expr = node.get_range_expression();

		// For now, we only support arrays as range expressions
		// The range expression should be an identifier referring to an array
		if (!range_expr.is<ExpressionNode>()) {
			assert(false && "Range expression must be an expression");
			return;
		}

		auto& expr_variant = range_expr.as<ExpressionNode>();
		if (!std::holds_alternative<IdentifierNode>(expr_variant)) {
			assert(false && "Currently only array identifiers are supported in ranged for loops");
			return;
		}

		const IdentifierNode& array_ident = std::get<IdentifierNode>(expr_variant);
		std::string_view array_name = array_ident.name();

		// Look up the array in the symbol table to get its size
		const std::optional<ASTNode> array_symbol = symbol_table.lookup(array_name);
		if (!array_symbol.has_value() || !array_symbol->is<DeclarationNode>()) {
			assert(false && "Array not found in symbol table");
			return;
		}

		const DeclarationNode& array_decl = array_symbol->as<DeclarationNode>();
		if (!array_decl.is_array()) {
			assert(false && "Range expression must be an array");
			return;
		}

		// Get array size
		auto array_size_node = array_decl.array_size();
		if (!array_size_node.has_value()) {
			assert(false && "Array must have a size for ranged for loop");
			return;
		}

		// Create index variable: int __i = 0
		auto index_type_node = ASTNode::emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
		Token index_token(Token::Type::Identifier, index_var_name, 0, 0, 0);
		auto index_decl_node = ASTNode::emplace_node<DeclarationNode>(index_type_node, index_token);

		// Initialize index to 0
		auto zero_literal = ASTNode::emplace_node<ExpressionNode>(
			NumericLiteralNode(Token(Token::Type::Literal, "0", 0, 0, 0),
				static_cast<unsigned long long>(0), Type::Int, TypeQualifier::None, 32));
		auto index_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(index_decl_node, zero_literal);

		// Add index variable to symbol table
		symbol_table.insert(index_var_name, index_decl_node);

		// Generate IR for index variable declaration
		visit(index_var_decl_node);

		// Mark loop begin for break/continue support
		ir_.addInstruction(IrOpcode::LoopBegin, {loop_start_label, loop_end_label, loop_increment_label}, Token());

		// Loop start: evaluate condition (__i < array_size)
		ir_.addInstruction(IrOpcode::Label, {loop_start_label}, Token());

		// Create condition: __i < array_size
		auto index_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(index_token));
		auto condition_expr = ASTNode::emplace_node<ExpressionNode>(
			BinaryOperatorNode(Token(Token::Type::Operator, "<", 0, 0, 0), index_ident_expr, array_size_node.value())
		);
		auto condition_operands = visitExpressionNode(condition_expr.as<ExpressionNode>());

		// Generate conditional branch
		std::vector<IrOperand> branch_operands;
		branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
		branch_operands.emplace_back(loop_body_label);
		branch_operands.emplace_back(loop_end_label);
		ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());

		// Loop body label
		ir_.addInstruction(IrOpcode::Label, {loop_body_label}, Token());

		// Declare and initialize the loop variable: int x = arr[__i]
		// Create array subscript: arr[__i]
		auto array_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(Token(Token::Type::Identifier, array_name, 0, 0, 0)));
		auto array_subscript = ASTNode::emplace_node<ExpressionNode>(
			ArraySubscriptNode(array_expr, index_ident_expr, Token(Token::Type::Punctuator, "[", 0, 0, 0))
		);

		// Create the loop variable declaration with initialization
		auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_var_decl, array_subscript);

		// Add loop variable to symbol table
		if (loop_var_decl.is<DeclarationNode>()) {
			const DeclarationNode& decl = loop_var_decl.as<DeclarationNode>();
			symbol_table.insert(decl.identifier_token().value(), loop_var_decl);
		}

		// Generate IR for loop variable declaration
		visit(loop_var_with_init);

		// Visit loop body
		auto body_stmt = node.get_body_statement();
		if (body_stmt.is<BlockNode>()) {
			body_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(body_stmt);
		}

		// Loop increment label (for continue statements)
		ir_.addInstruction(IrOpcode::Label, {loop_increment_label}, Token());

		// Increment index: ++__i
		auto increment_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "++", 0, 0, 0), index_ident_expr, true)
		);
		visitExpressionNode(increment_expr.as<ExpressionNode>());

		// Branch back to loop start
		ir_.addInstruction(IrOpcode::Branch, {loop_start_label}, Token());

		// Loop end label
		ir_.addInstruction(IrOpcode::Label, {loop_end_label}, Token());

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitBreakStatementNode(const BreakStatementNode& node) {
		// Generate Break IR instruction (no operands - uses loop context stack in IRConverter)
		ir_.addInstruction(IrOpcode::Break, {}, node.break_token());
	}

	void visitContinueStatementNode(const ContinueStatementNode& node) {
		// Generate Continue IR instruction (no operands - uses loop context stack in IRConverter)
		ir_.addInstruction(IrOpcode::Continue, {}, node.continue_token());
	}

	void visitVariableDeclarationNode(const ASTNode& ast_node) {
		const VariableDeclarationNode& node = ast_node.as<VariableDeclarationNode>();
		const auto& decl = node.declaration();
		const auto& type_node = decl.type_node().as<TypeSpecifierNode>();

		// Create variable declaration operands
		// Format: [type, size_in_bits, var_name, custom_alignment]
		std::vector<IrOperand> operands;
		operands.emplace_back(type_node.type());
		operands.emplace_back(static_cast<int>(type_node.size_in_bits()));
		operands.emplace_back(decl.identifier_token().value());
		operands.emplace_back(static_cast<unsigned long long>(decl.custom_alignment()));

		// For arrays, add the array size
		if (decl.is_array()) {
			auto size_expr = decl.array_size();
			if (size_expr.has_value()) {
				auto size_operands = visitExpressionNode(size_expr->as<ExpressionNode>());
				// Add array size as an operand
				operands.insert(operands.end(), size_operands.begin(), size_operands.end());
			}
		}

		// Add initializer if present (for non-arrays)
		if (node.initializer() && !decl.is_array()) {
			const ASTNode& init_node = *node.initializer();

			// Check if this is a brace initializer (InitializerListNode)
			if (init_node.is<InitializerListNode>()) {
				// Handle brace initialization for structs
				const InitializerListNode& init_list = init_node.as<InitializerListNode>();

				// Add to symbol table first (needed for member store instructions)
				if (!symbol_table.insert(decl.identifier_token().value(), node.declaration_node())) {
					assert(false && "Expected identifier to be unique");
				}

				// First, add the variable declaration without initializer
				ir_.addInstruction(IrOpcode::VariableDecl, std::move(operands), node.declaration().identifier_token());

				// Then, generate member store instructions for each initializer
				if (type_node.type() == Type::Struct) {
					TypeIndex type_index = type_node.type_index();
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						if (type_info.struct_info_) {
							const StructTypeInfo& struct_info = *type_info.struct_info_;
							const auto& initializers = init_list.initializers();

							// Generate member store for each initializer
							for (size_t i = 0; i < initializers.size() && i < struct_info.members.size(); ++i) {
								const StructMember& member = struct_info.members[i];
								const ASTNode& init_expr = initializers[i];

								// Generate IR for the initializer expression
								std::vector<IrOperand> init_operands;
								if (init_expr.is<ExpressionNode>()) {
									init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
								} else {
									assert(false && "Initializer must be an ExpressionNode");
								}

								// Generate member store: struct.member = init_expr
								std::vector<IrOperand> member_store_operands;
								member_store_operands.emplace_back(member.type);
								member_store_operands.emplace_back(static_cast<int>(member.size * 8)); // size in bits
								member_store_operands.emplace_back(decl.identifier_token().value()); // object name
								member_store_operands.emplace_back(std::string_view(member.name)); // member name (convert to string_view)
								member_store_operands.emplace_back(static_cast<int>(member.offset)); // member offset

								// Add only the value from init_operands (init_operands = [type, size, value])
								// We only need the value (index 2)
								if (init_operands.size() >= 3) {
									member_store_operands.emplace_back(init_operands[2]);
								} else {
									// Error: invalid init operands
									assert(false && "Invalid initializer operands");
								}

								ir_.addInstruction(IrOpcode::MemberStore, std::move(member_store_operands), decl.identifier_token());
							}
						}
					}
				}
				return; // Early return - we've already added the variable declaration
			} else {
				// Regular expression initializer
				auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
				operands.insert(operands.end(), init_operands.begin(), init_operands.end());
			}
		}

		if (!symbol_table.insert(decl.identifier_token().value(), node.declaration_node())) {
			assert(false && "Expected identifier to be unique");
		}

		ir_.addInstruction(IrOpcode::VariableDecl, std::move(operands), node.declaration().identifier_token());
	}

	std::vector<IrOperand> visitExpressionNode(const ExpressionNode& exprNode) {
		if (std::holds_alternative<IdentifierNode>(exprNode)) {
			const auto& expr = std::get<IdentifierNode>(exprNode);
			return generateIdentifierIr(expr);
		}
		else if (std::holds_alternative<QualifiedIdentifierNode>(exprNode)) {
			const auto& expr = std::get<QualifiedIdentifierNode>(exprNode);
			return generateQualifiedIdentifierIr(expr);
		}
		else if (std::holds_alternative<NumericLiteralNode>(exprNode)) {
			const auto& expr = std::get<NumericLiteralNode>(exprNode);
			return generateNumericLiteralIr(expr);
		}
		else if (std::holds_alternative<StringLiteralNode>(exprNode)) {
			const auto& expr = std::get<StringLiteralNode>(exprNode);
			return generateStringLiteralIr(expr);
		}
		else if (std::holds_alternative<BinaryOperatorNode>(exprNode)) {
			const auto& expr = std::get<BinaryOperatorNode>(exprNode);
			return generateBinaryOperatorIr(expr);
		}
		else if (std::holds_alternative<UnaryOperatorNode>(exprNode)) {
			const auto& expr = std::get<UnaryOperatorNode>(exprNode);
			return generateUnaryOperatorIr(expr);
		}
		else if (std::holds_alternative<FunctionCallNode>(exprNode)) {
			const auto& expr = std::get<FunctionCallNode>(exprNode);
			return generateFunctionCallIr(expr);
		}
		else if (std::holds_alternative<MemberFunctionCallNode>(exprNode)) {
			const auto& expr = std::get<MemberFunctionCallNode>(exprNode);
			return generateMemberFunctionCallIr(expr);
		}
		else if (std::holds_alternative<ArraySubscriptNode>(exprNode)) {
			const auto& expr = std::get<ArraySubscriptNode>(exprNode);
			return generateArraySubscriptIr(expr);
		}
		else if (std::holds_alternative<MemberAccessNode>(exprNode)) {
			const auto& expr = std::get<MemberAccessNode>(exprNode);
			return generateMemberAccessIr(expr);
		}
		else if (std::holds_alternative<SizeofExprNode>(exprNode)) {
			const auto& expr = std::get<SizeofExprNode>(exprNode);
			return generateSizeofIr(expr);
		}
		else if (std::holds_alternative<OffsetofExprNode>(exprNode)) {
			const auto& expr = std::get<OffsetofExprNode>(exprNode);
			return generateOffsetofIr(expr);
		}
		else {
			assert(false && "Not implemented yet");
		}

		return {};
	}

	std::vector<IrOperand> generateIdentifierIr(const IdentifierNode& identifierNode) {
		const std::optional<ASTNode> symbol = symbol_table.lookup(identifierNode.name());
		if (!symbol.has_value()) {
			assert(false && "Expected symbol to exist");
			return {};
		}

		if (symbol->is<DeclarationNode>()) {
			const auto& decl_node = symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
			return { type_node.type(), static_cast<int>(type_node.size_in_bits()), identifierNode.name() };
		}

		// If we get here, the symbol is not a DeclarationNode
		assert(false && "Identifier is not a DeclarationNode");
		return {};
	}

	std::vector<IrOperand> generateQualifiedIdentifierIr(const QualifiedIdentifierNode& qualifiedIdNode) {
		// For now, treat qualified identifiers similarly to regular identifiers
		// In a full implementation, we would use the namespace information for name mangling
		// For external functions like std::print, we just use the identifier name
		const std::optional<ASTNode> symbol = symbol_table.lookup_qualified(qualifiedIdNode.namespaces(), qualifiedIdNode.name());
		if (!symbol.has_value()) {
			// For external functions (like std::print), we might not have them in our symbol table
			// Return a placeholder - the actual linking will happen later
			return { Type::Int, 32, qualifiedIdNode.name() };
		}

		if (symbol->is<DeclarationNode>()) {
			const auto& decl_node = symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
			return { type_node.type(), static_cast<int>(type_node.size_in_bits()), qualifiedIdNode.name() };
		}

		// If we get here, the symbol is not a DeclarationNode
		assert(false && "Qualified identifier is not a DeclarationNode");
		return {};
	}

	std::vector<IrOperand>
		generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode) {
		// Generate IR for numeric literal using the actual type from the literal
		// Check if it's a floating-point type
		if (is_floating_point_type(numericLiteralNode.type())) {
			// For floating-point literals, the value is stored as double
			return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<double>(numericLiteralNode.value()) };
		} else {
			// For integer literals, the value is stored as unsigned long long
			return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<unsigned long long>(numericLiteralNode.value()) };
		}
	}

	std::vector<IrOperand> generateTypeConversion(const std::vector<IrOperand>& operands, Type fromType, Type toType, const Token& source_token) {
		if (fromType == toType) {
			return operands; // No conversion needed
		}

		int fromSize = get_type_size_bits(fromType);
		int toSize = get_type_size_bits(toType);

		// Check if the value is a compile-time constant (literal)
		// operands format: [type, size, value]
		bool is_literal = (operands.size() == 3) &&
		                  (std::holds_alternative<unsigned long long>(operands[2]) ||
		                   std::holds_alternative<int>(operands[2]) ||
		                   std::holds_alternative<double>(operands[2]));

		if (is_literal) {
			// For literal values, just convert the value directly without creating a TempVar
			// This allows the literal to be used as an immediate value in instructions
			if (std::holds_alternative<unsigned long long>(operands[2])) {
				unsigned long long value = std::get<unsigned long long>(operands[2]);
				// For integer literals, the value remains the same (truncation/extension happens at runtime)
				return { toType, toSize, value };
			} else if (std::holds_alternative<int>(operands[2])) {
				int value = std::get<int>(operands[2]);
				// Convert to unsigned long long for consistency
				return { toType, toSize, static_cast<unsigned long long>(value) };
			} else if (std::holds_alternative<double>(operands[2])) {
				double value = std::get<double>(operands[2]);
				return { toType, toSize, value };
			}
		}

		// For non-literal values (variables, TempVars), create a conversion instruction
		TempVar resultVar = var_counter.next();
		std::vector<IrOperand> conversionOperands;
		conversionOperands.push_back(resultVar);
		conversionOperands.push_back(fromType);
		conversionOperands.push_back(fromSize);
		conversionOperands.insert(conversionOperands.end(), operands.begin() + 2, operands.end()); // Skip type and size, add value
		conversionOperands.push_back(toSize);

		if (fromSize < toSize) {
			// Extension needed
			if (is_signed_integer_type(fromType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::SignExtend, std::move(conversionOperands), source_token));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::ZeroExtend, std::move(conversionOperands), source_token));
			}
		} else if (fromSize > toSize) {
			// Truncation needed
			ir_.addInstruction(IrInstruction(IrOpcode::Truncate, std::move(conversionOperands), source_token));
		}

		// Return the converted operands
		return { toType, toSize, resultVar };
	}

	std::vector<IrOperand>
		generateStringLiteralIr(const StringLiteralNode& stringLiteralNode) {
		// Generate IR for string literal and return appropriate operand
		// ...
		return { stringLiteralNode.value() };
	}

	std::vector<IrOperand> generateUnaryOperatorIr(const UnaryOperatorNode& unaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		// Generate IR for the operand
		auto operandIrOperands = visitExpressionNode(unaryOperatorNode.get_operand().as<ExpressionNode>());

		// Get the type of the operand
		Type operandType = std::get<Type>(operandIrOperands[0]);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Add the result variable first
		irOperands.emplace_back(result_var);

		// Add the operand
		irOperands.insert(irOperands.end(), operandIrOperands.begin(), operandIrOperands.end());

		// Generate the IR for the operation based on the operator
		if (unaryOperatorNode.op() == "!") {
			// Logical NOT
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalNot, std::move(irOperands), Token()));
		}
		else if (unaryOperatorNode.op() == "~") {
			// Bitwise NOT
			ir_.addInstruction(IrInstruction(IrOpcode::BitwiseNot, std::move(irOperands), Token()));
		}
		else if (unaryOperatorNode.op() == "-") {
			// Unary minus (negation)
			ir_.addInstruction(IrInstruction(IrOpcode::Negate, std::move(irOperands), Token()));
		}
		else if (unaryOperatorNode.op() == "+") {
			// Unary plus (no-op, just return the operand)
			return operandIrOperands;
		}
		else if (unaryOperatorNode.op() == "++") {
			// Increment operator (prefix or postfix)
			if (unaryOperatorNode.is_prefix()) {
				// Prefix increment: ++x
				ir_.addInstruction(IrInstruction(IrOpcode::PreIncrement, std::move(irOperands), Token()));
			} else {
				// Postfix increment: x++
				ir_.addInstruction(IrInstruction(IrOpcode::PostIncrement, std::move(irOperands), Token()));
			}
		}
		else if (unaryOperatorNode.op() == "--") {
			// Decrement operator (prefix or postfix)
			if (unaryOperatorNode.is_prefix()) {
				// Prefix decrement: --x
				ir_.addInstruction(IrInstruction(IrOpcode::PreDecrement, std::move(irOperands), Token()));
			} else {
				// Postfix decrement: x--
				ir_.addInstruction(IrInstruction(IrOpcode::PostDecrement, std::move(irOperands), Token()));
			}
		}
		else if (unaryOperatorNode.op() == "&") {
			// Address-of operator: &x
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(irOperands), Token()));
		}
		else if (unaryOperatorNode.op() == "*") {
			// Dereference operator: *x
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(irOperands), Token()));
		}
		else {
			assert(false && "Unary operator not implemented yet");
		}

		// Return the result
		return { operandType, std::get<int>(operandIrOperands[1]), result_var };
	}

	std::vector<IrOperand> generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		const auto& op = binaryOperatorNode.op();

		// Special handling for assignment to member access
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<MemberAccessNode>(lhs_expr)) {
				// This is a member access assignment: obj.member = value
				const MemberAccessNode& member_access = std::get<MemberAccessNode>(lhs_expr);

				// Generate IR for the RHS value
				auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

				// Get the object and member information
				const ASTNode& object_node = member_access.object();
				std::string_view member_name = member_access.member_name();

				// Unwrap ExpressionNode if needed
				if (object_node.is<ExpressionNode>()) {
					const ExpressionNode& expr = object_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(expr)) {
						const IdentifierNode& object_ident = std::get<IdentifierNode>(expr);
						std::string_view object_name = object_ident.name();

						// Look up the object in the symbol table
						const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
						if (!symbol.has_value() || !symbol->is<DeclarationNode>()) {
							// Error: variable not found
							return { Type::Int, 32, TempVar{0} };
						}

						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

						if (type_node.type() != Type::Struct) {
							// Error: not a struct type
							return { Type::Int, 32, TempVar{0} };
						}

						// Get the struct type info
						TypeIndex struct_type_index = type_node.type_index();
						if (struct_type_index >= gTypeInfo.size()) {
							// Error: invalid type index
							return { Type::Int, 32, TempVar{0} };
						}

						const TypeInfo& struct_type_info = gTypeInfo[struct_type_index];
						const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
						if (!struct_info) {
							// Error: struct info not found
							return { Type::Int, 32, TempVar{0} };
						}

						const StructMember* member = struct_info->findMember(std::string(member_name));
						if (!member) {
							// Error: member not found
							return { Type::Int, 32, TempVar{0} };
						}

						// Build MemberStore IR operands: [member_type, member_size, object_name, member_name, offset, value]
						irOperands.emplace_back(member->type);
						irOperands.emplace_back(static_cast<int>(member->size * 8));
						irOperands.emplace_back(object_name);
						irOperands.emplace_back(member_name);
						irOperands.emplace_back(static_cast<int>(member->offset));

						// Add only the value from RHS (rhsIrOperands = [type, size, value])
						// We only need the value (index 2)
						if (rhsIrOperands.size() >= 3) {
							irOperands.emplace_back(rhsIrOperands[2]);
						} else {
							// Error: invalid RHS operands
							return { Type::Int, 32, TempVar{0} };
						}

						ir_.addInstruction(IrOpcode::MemberStore, std::move(irOperands), binaryOperatorNode.get_token());

						// Return the RHS value as the result
						return rhsIrOperands;
					}
				}
			}
		}

		// Generate IR for the left-hand side and right-hand side of the operation
		auto lhsIrOperands = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>());
		auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

		// Get the types of the operands
		Type lhsType = std::get<Type>(lhsIrOperands[0]);
		Type rhsType = std::get<Type>(rhsIrOperands[0]);

		// Apply integer promotions and find common type
		Type commonType = get_common_type(lhsType, rhsType);

		// Generate conversions if needed
		if (lhsType != commonType) {
			lhsIrOperands = generateTypeConversion(lhsIrOperands, lhsType, commonType, binaryOperatorNode.get_token());
		}
		if (rhsType != commonType) {
			rhsIrOperands = generateTypeConversion(rhsIrOperands, rhsType, commonType, binaryOperatorNode.get_token());
		}

		// Check if we're dealing with floating-point operations
		bool is_floating_point_op = is_floating_point_type(commonType);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Add the result variable first
		irOperands.emplace_back(result_var);

		// Add the left-hand side operands
		irOperands.insert(irOperands.end(), lhsIrOperands.begin(), lhsIrOperands.end());

		// Add the right-hand side operands
		irOperands.insert(irOperands.end(), rhsIrOperands.begin(), rhsIrOperands.end());

		// Generate the IR for the operation based on the operator and operand types
		// Use a lookup table approach for better performance and maintainability
		IrOpcode opcode;

		// Simple operators (no type variants)
		static const std::unordered_map<std::string_view, IrOpcode> simple_ops = {
			{"&", IrOpcode::BitwiseAnd}, {"|", IrOpcode::BitwiseOr}, {"^", IrOpcode::BitwiseXor},
			{"%", IrOpcode::Modulo}, {"<<", IrOpcode::ShiftLeft},
			{"&&", IrOpcode::LogicalAnd}, {"||", IrOpcode::LogicalOr},
			{"=", IrOpcode::Assignment}, {"+=", IrOpcode::AddAssign}, {"-=", IrOpcode::SubAssign},
			{"*=", IrOpcode::MulAssign}, {"/=", IrOpcode::DivAssign}, {"%=", IrOpcode::ModAssign},
			{"&=", IrOpcode::AndAssign}, {"|=", IrOpcode::OrAssign}, {"^=", IrOpcode::XorAssign},
			{"<<=", IrOpcode::ShlAssign}, {">>=", IrOpcode::ShrAssign}
		};

		auto simple_it = simple_ops.find(op);
		if (simple_it != simple_ops.end()) {
			opcode = simple_it->second;
		}
		// Arithmetic operators (float vs int)
		else if (op == "+") {
			opcode = is_floating_point_op ? IrOpcode::FloatAdd : IrOpcode::Add;
		}
		else if (op == "-") {
			opcode = is_floating_point_op ? IrOpcode::FloatSubtract : IrOpcode::Subtract;
		}
		else if (op == "*") {
			opcode = is_floating_point_op ? IrOpcode::FloatMultiply : IrOpcode::Multiply;
		}
		// Division (float vs unsigned vs signed)
		else if (op == "/") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatDivide;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedDivide;
			} else {
				opcode = IrOpcode::Divide;
			}
		}
		// Right shift (unsigned vs signed)
		else if (op == ">>") {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedShiftRight : IrOpcode::ShiftRight;
		}
		// Comparison operators (float vs unsigned vs signed)
		else if (op == "==") {
			opcode = is_floating_point_op ? IrOpcode::FloatEqual : IrOpcode::Equal;
		}
		else if (op == "!=") {
			opcode = is_floating_point_op ? IrOpcode::FloatNotEqual : IrOpcode::NotEqual;
		}
		else if (op == "<") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatLessThan;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedLessThan;
			} else {
				opcode = IrOpcode::LessThan;
			}
		}
		else if (op == "<=") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatLessEqual;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedLessEqual;
			} else {
				opcode = IrOpcode::LessEqual;
			}
		}
		else if (op == ">") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatGreaterThan;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedGreaterThan;
			} else {
				opcode = IrOpcode::GreaterThan;
			}
		}
		else if (op == ">=") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatGreaterEqual;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedGreaterEqual;
			} else {
				opcode = IrOpcode::GreaterEqual;
			}
		}
		else {
			assert(false && "Unsupported binary operator");
			return {};
		}

		ir_.addInstruction(IrInstruction(opcode, std::move(irOperands), binaryOperatorNode.get_token()));

		// Return the result variable with its type and size
		return { lhsIrOperands[0], lhsIrOperands[1], result_var };
	}

	std::vector<IrOperand> generateFunctionCallIr(const FunctionCallNode& functionCallNode) {
		std::vector<IrOperand> irOperands;

		const auto& decl_node = functionCallNode.function_declaration();
		auto type = gSymbolTable.lookup(decl_node.identifier_token().value());

		// Always add the return variable and function name
		TempVar ret_var = var_counter.next();
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(decl_node.identifier_token().value());

		// Generate IR for function arguments
		functionCallNode.arguments().visit([&](ASTNode argument) {
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			// For variables, we need to add the type and size
			if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				const auto& decl_node = symbol->as<DeclarationNode>();
				const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
				irOperands.emplace_back(type_node.type());
				irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
				irOperands.emplace_back(identifier.name());
			}
			else {
				irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
			}
		});

		// Add the function call instruction
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(irOperands), functionCallNode.called_from()));

		// Return the result variable with its type and size
		const auto& return_type = decl_node.type_node().as<TypeSpecifierNode>();
		return { return_type.type(), static_cast<int>(return_type.size_in_bits()), ret_var };
	}

	std::vector<IrOperand> generateMemberFunctionCallIr(const MemberFunctionCallNode& memberFunctionCallNode) {
		std::vector<IrOperand> irOperands;

		// Get the object expression
		ASTNode object_node = memberFunctionCallNode.object();
		const ExpressionNode& object_expr = object_node.as<ExpressionNode>();

		// Get the object's type
		// For now, we'll assume the object is an identifier
		std::string_view object_name;
		const DeclarationNode* object_decl = nullptr;

		if (std::holds_alternative<IdentifierNode>(object_expr)) {
			const IdentifierNode& object_ident = std::get<IdentifierNode>(object_expr);
			object_name = object_ident.name();

			// Look up the object in the symbol table
			const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
			if (symbol.has_value() && symbol->is<DeclarationNode>()) {
				object_decl = &symbol->as<DeclarationNode>();
			}
		}

		if (!object_decl) {
			// Error: object not found
			assert(false && "Object not found for member function call");
			return { Type::Int, 32, TempVar{0} };
		}

		const TypeSpecifierNode& object_type = object_decl->type_node().as<TypeSpecifierNode>();

		// Verify this is a struct type
		if (object_type.type() != Type::Struct) {
			assert(false && "Member function call on non-struct type");
			return { Type::Int, 32, TempVar{0} };
		}

		// Get the function declaration directly from the node (no need to look it up)
		const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
		const DeclarationNode& func_decl_node = func_decl.decl_node();

		// Always add the return variable and function name
		TempVar ret_var = var_counter.next();
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(func_decl_node.identifier_token().value());

		// Add the object as the first argument (this pointer)
		// We need to pass the address of the object
		irOperands.emplace_back(object_type.type());
		irOperands.emplace_back(static_cast<int>(object_type.size_in_bits()));
		irOperands.emplace_back(object_name);

		// Generate IR for function arguments
		memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			// For variables, we need to add the type and size
			if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				const auto& decl_node = symbol->as<DeclarationNode>();
				const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
				irOperands.emplace_back(type_node.type());
				irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
				irOperands.emplace_back(identifier.name());
			}
			else {
				irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
			}
		});

		// Add the function call instruction
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(irOperands), memberFunctionCallNode.called_from()));

		// Return the result variable with its type and size
		const auto& return_type = func_decl_node.type_node().as<TypeSpecifierNode>();
		return { return_type.type(), static_cast<int>(return_type.size_in_bits()), ret_var };
	}

	std::vector<IrOperand> generateArraySubscriptIr(const ArraySubscriptNode& arraySubscriptNode) {
		// Generate IR for array[index] expression
		// This computes the address: base_address + (index * element_size)

		// Get the array expression (should be an identifier for now)
		auto array_operands = visitExpressionNode(arraySubscriptNode.array_expr().as<ExpressionNode>());

		// Get the index expression
		auto index_operands = visitExpressionNode(arraySubscriptNode.index_expr().as<ExpressionNode>());

		// Get array type information
		Type array_type = std::get<Type>(array_operands[0]);
		int element_size_bits = std::get<int>(array_operands[1]);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Build operands for array access IR instruction
		// Format: [result_var, array_type, element_size, array_name/temp, index_type, index_size, index_value]
		std::vector<IrOperand> irOperands;
		irOperands.emplace_back(result_var);

		// Array operands (type, size, name/temp)
		irOperands.insert(irOperands.end(), array_operands.begin(), array_operands.end());

		// Index operands (type, size, value)
		irOperands.insert(irOperands.end(), index_operands.begin(), index_operands.end());

		// For now, we'll use a Load-like instruction to read from the computed address
		// The IRConverter will handle the address calculation
		// We'll add a new IR opcode for this: ArrayAccess
		ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(irOperands), arraySubscriptNode.bracket_token()));

		// Return the result with the element type
		return { array_type, element_size_bits, result_var };
	}

	std::vector<IrOperand> generateMemberAccessIr(const MemberAccessNode& memberAccessNode) {
		std::vector<IrOperand> irOperands;

		// Get the object being accessed
		ASTNode object_node = memberAccessNode.object();
		std::string_view member_name = memberAccessNode.member_name();

		// Variables to hold the base object info
		std::variant<std::string_view, TempVar> base_object;
		Type base_type = Type::Void;
		size_t base_type_index = 0;

		// Unwrap ExpressionNode if needed
		if (object_node.is<ExpressionNode>()) {
			const ExpressionNode& expr = object_node.as<ExpressionNode>();

			// Case 1: Simple identifier (e.g., obj.member)
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& object_ident = std::get<IdentifierNode>(expr);
				std::string_view object_name = object_ident.name();

				// Look up the object in the symbol table
				const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
				if (!symbol.has_value() || !symbol->is<DeclarationNode>()) {
					assert(false && "Object not found in symbol table");
					return {};
				}

				const DeclarationNode& object_decl = symbol->as<DeclarationNode>();
				const TypeSpecifierNode& object_type = object_decl.type_node().as<TypeSpecifierNode>();

				// Verify this is a struct type
				if (object_type.type() != Type::Struct) {
					assert(false && "Member access on non-struct type");
					return {};
				}

				base_object = object_name;
				base_type = object_type.type();
				base_type_index = object_type.type_index();
			}
			// Case 2: Nested member access (e.g., obj.inner.member)
			else if (std::holds_alternative<MemberAccessNode>(expr)) {
				const MemberAccessNode& nested_access = std::get<MemberAccessNode>(expr);

				// Recursively generate IR for the nested member access
				std::vector<IrOperand> nested_result = generateMemberAccessIr(nested_access);
				if (nested_result.empty()) {
					return {};
				}

				// The result is [type, size_bits, temp_var, type_index]
				// We need to add type_index to the return value
				base_type = std::get<Type>(nested_result[0]);
				// size_bits is in nested_result[1]
				base_object = std::get<TempVar>(nested_result[2]);

				// For nested member access, we need to get the type_index from the result
				// The base_type should be Type::Struct
				if (base_type != Type::Struct) {
					assert(false && "Nested member access on non-struct type");
					return {};
				}

				// Get the type_index from the nested result (if available)
				if (nested_result.size() >= 4) {
					base_type_index = std::get<size_t>(nested_result[3]);
				} else {
					// Fallback: search through gTypeInfo (less reliable)
					base_type_index = 0;
					for (const auto& ti : gTypeInfo) {
						if (ti.type_ == Type::Struct && ti.getStructInfo()) {
							base_type_index = ti.type_index_;
							break;
						}
					}
				}
			}
			else {
				assert(false && "Member access on unsupported expression type");
				return {};
			}
		}
		else if (object_node.is<IdentifierNode>()) {
			const IdentifierNode& object_ident = object_node.as<IdentifierNode>();
			std::string_view object_name = object_ident.name();

			// Look up the object in the symbol table
			const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
			if (!symbol.has_value() || !symbol->is<DeclarationNode>()) {
				assert(false && "Object not found in symbol table");
				return {};
			}

			const DeclarationNode& object_decl = symbol->as<DeclarationNode>();
			const TypeSpecifierNode& object_type = object_decl.type_node().as<TypeSpecifierNode>();

			// Verify this is a struct type
			if (object_type.type() != Type::Struct) {
				assert(false && "Member access on non-struct type");
				return {};
			}

			base_object = object_name;
			base_type = object_type.type();
			base_type_index = object_type.type_index();
		}
		else {
			assert(false && "Member access on unsupported object type");
			return {};
		}

		// Now we have the base object (either a name or a temp var) and its type
		// Get the struct type info
		const TypeInfo* type_info = nullptr;
		for (const auto& ti : gTypeInfo) {
			if (ti.type_index_ == base_type_index) {
				type_info = &ti;
				break;
			}
		}

		if (!type_info || !type_info->getStructInfo()) {
			assert(false && "Struct type info not found");
			return {};
		}

		const StructTypeInfo* struct_info = type_info->getStructInfo();
		const StructMember* member = struct_info->findMember(std::string(member_name));

		if (!member) {
			assert(false && "Member not found in struct");
			return {};
		}

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Build IR operands: [result_var, member_type, member_size, object_name/temp, member_name, offset]
		irOperands.emplace_back(result_var);
		irOperands.emplace_back(member->type);
		irOperands.emplace_back(static_cast<int>(member->size * 8));  // Convert bytes to bits

		// Add the base object (either string_view or TempVar)
		if (std::holds_alternative<std::string_view>(base_object)) {
			irOperands.emplace_back(std::get<std::string_view>(base_object));
		} else {
			irOperands.emplace_back(std::get<TempVar>(base_object));
		}

		irOperands.emplace_back(std::string_view(member_name));
		irOperands.emplace_back(static_cast<int>(member->offset));

		// Add the member access instruction
		ir_.addInstruction(IrOpcode::MemberAccess, std::move(irOperands), Token());

		// Return the result variable with its type and size (standard 3-operand format)
		// Format: [type, size_bits, temp_var]
		// Note: type_index is not returned because it's not needed for arithmetic operations
		// and would break binary operators that expect exactly 3 operands per side
		return { member->type, static_cast<int>(member->size * 8), result_var };
	}

	std::vector<IrOperand> generateSizeofIr(const SizeofExprNode& sizeofNode) {
		size_t size_in_bytes = 0;

		if (sizeofNode.is_type()) {
			// sizeof(type)
			const ASTNode& type_node = sizeofNode.type_or_expr();
			if (!type_node.is<TypeSpecifierNode>()) {
				assert(false && "sizeof type argument must be TypeSpecifierNode");
				return {};
			}

			const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
			Type type = type_spec.type();

			// Handle struct types
			if (type == Type::Struct) {
				size_t type_index = type_spec.type_index();
				if (type_index >= gTypeInfo.size()) {
					assert(false && "Invalid type index for struct");
					return {};
				}

				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (!struct_info) {
					assert(false && "Struct type info not found");
					return {};
				}

				size_in_bytes = struct_info->total_size;
			}
			else {
				// For primitive types, convert bits to bytes
				size_in_bytes = type_spec.size_in_bits() / 8;
			}
		}
		else {
			// sizeof(expression) - evaluate the type of the expression
			const ASTNode& expr_node = sizeofNode.type_or_expr();
			if (!expr_node.is<ExpressionNode>()) {
				assert(false && "sizeof expression argument must be ExpressionNode");
				return {};
			}

			// Generate IR for the expression to get its type
			auto expr_operands = visitExpressionNode(expr_node.as<ExpressionNode>());
			if (expr_operands.empty()) {
				return {};
			}

			// Extract type and size from the expression result
			Type expr_type = std::get<Type>(expr_operands[0]);
			int size_in_bits = std::get<int>(expr_operands[1]);

			// Handle struct types
			if (expr_type == Type::Struct) {
				// For struct expressions, we need to look up the type index
				// This is a simplification - in a full implementation we'd track type_index through expressions
				assert(false && "sizeof(struct_expression) not fully implemented yet");
				return {};
			}
			else {
				size_in_bytes = size_in_bits / 8;
			}
		}

		// Return sizeof result as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
	}

	std::vector<IrOperand> generateOffsetofIr(const OffsetofExprNode& offsetofNode) {
		// offsetof(struct_type, member)
		const ASTNode& type_node = offsetofNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			assert(false && "offsetof type argument must be TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		if (type_spec.type() != Type::Struct) {
			assert(false && "offsetof requires a struct type");
			return {};
		}

		// Get the struct type info
		size_t type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			assert(false && "Invalid type index for struct");
			return {};
		}

		const TypeInfo& type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (!struct_info) {
			assert(false && "Struct type info not found");
			return {};
		}

		// Find the member
		std::string_view member_name = offsetofNode.member_name();
		const StructMember* member = struct_info->findMember(std::string(member_name));
		if (!member) {
			assert(false && "Member not found in struct");
			return {};
		}

		// Return offset as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(member->offset) };
	}

	Ir ir_;
	TempVar var_counter{ 0 };
	SymbolTable symbol_table;
};
