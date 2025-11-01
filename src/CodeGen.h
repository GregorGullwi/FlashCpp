#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include <type_traits>
#include <variant>
#include <vector>
#include <unordered_map>
#include <assert.h>
#include "IRConverter.h"

class Parser;

// Structure to hold lambda information for deferred generation
struct LambdaInfo {
	std::string closure_type_name;      // e.g., "__lambda_0"
	std::string operator_call_name;     // e.g., "__lambda_0_operator_call"
	std::string invoke_name;            // e.g., "__lambda_0_invoke"
	std::string conversion_op_name;     // e.g., "__lambda_0_conversion"
	Type return_type;
	int return_size;
	std::vector<std::tuple<Type, int, int, std::string>> parameters;  // type, size, pointer_depth, name
	std::vector<ASTNode> parameter_nodes;  // Actual parameter AST nodes for symbol table
	ASTNode lambda_body;                // Copy of the lambda body
	std::vector<LambdaCaptureNode> captures;  // Copy of captures
	std::vector<ASTNode> captured_var_decls;  // Declarations of captured variables (for symbol table)
	size_t lambda_id;
	Token lambda_token;
};

class AstToIr {
public:
	AstToIr() = delete;  // Require valid references
	AstToIr(SymbolTable& global_symbol_table, CompileContext& context)
		: global_symbol_table_(&global_symbol_table), context_(&context) {}

	void visit(const ASTNode& node) {
		// Skip empty nodes (e.g., from forward declarations)
		if (!node.has_value()) {
			return;
		}

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
		else if (node.is<SwitchStatementNode>()) {
			visitSwitchStatementNode(node.as<SwitchStatementNode>());
		}
		else if (node.is<BreakStatementNode>()) {
			visitBreakStatementNode(node.as<BreakStatementNode>());
		}
		else if (node.is<ContinueStatementNode>()) {
			visitContinueStatementNode(node.as<ContinueStatementNode>());
		}
		else if (node.is<GotoStatementNode>()) {
			visitGotoStatementNode(node.as<GotoStatementNode>());
		}
		else if (node.is<LabelStatementNode>()) {
			visitLabelStatementNode(node.as<LabelStatementNode>());
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
		else if (node.is<EnumDeclarationNode>()) {
			visitEnumDeclarationNode(node.as<EnumDeclarationNode>());
		}
		else if (node.is<NamespaceDeclarationNode>()) {
			visitNamespaceDeclarationNode(node.as<NamespaceDeclarationNode>());
		}
		else if (node.is<UsingDirectiveNode>()) {
			visitUsingDirectiveNode(node.as<UsingDirectiveNode>());
		}
		else if (node.is<UsingDeclarationNode>()) {
			visitUsingDeclarationNode(node.as<UsingDeclarationNode>());
		}
		else if (node.is<NamespaceAliasNode>()) {
			visitNamespaceAliasNode(node.as<NamespaceAliasNode>());
		}
		else if (node.is<ConstructorDeclarationNode>()) {
			visitConstructorDeclarationNode(node.as<ConstructorDeclarationNode>());
		}
		else if (node.is<DestructorDeclarationNode>()) {
			visitDestructorDeclarationNode(node.as<DestructorDeclarationNode>());
		}
		else if (node.is<DeclarationNode>()) {
			// Forward declarations or global variable declarations
			// These are already in the symbol table, no code generation needed
			return;
		}
		else if (node.is<TypeSpecifierNode>()) {
			// Type specifier nodes can appear in the AST for forward declarations
			// No code generation needed
			return;
		}
		else if (node.is<TypedefDeclarationNode>()) {
			// Typedef declarations don't generate code - they're handled during parsing
			return;
		}
		else if (node.is<ExpressionNode>()) {
			// Expression statement (e.g., function call, lambda expression, etc.)
			// Evaluate the expression but discard the result
			visitExpressionNode(node.as<ExpressionNode>());
		}
		else if (node.is<LambdaExpressionNode>()) {
			// Lambda expression as a statement
			// Evaluate the lambda (creates closure instance) but discard the result
			generateLambdaExpressionIr(node.as<LambdaExpressionNode>());
		}
		else {
			puts(node.type_name());
			assert(false && "Unhandled AST node type");
		}
	}

	const Ir& getIr() const { return ir_; }

	// Generate all collected lambdas (must be called after visiting all nodes)
	void generateCollectedLambdas() {
		for (const auto& lambda_info : collected_lambdas_) {
			generateLambdaFunctions(lambda_info);
		}
	}

	// Reserve space for IR instructions (optimization)
	void reserveInstructions(size_t capacity) {
		ir_.reserve(capacity);
	}

private:
	// Helper function to check if access to a member is allowed
	// Returns true if access is allowed, false otherwise
	bool checkMemberAccess(const StructMember* member,
	                       const StructTypeInfo* member_owner_struct,
	                       const StructTypeInfo* accessing_struct,
	                       const BaseClassSpecifier* inheritance_path = nullptr,
	                       const std::string& accessing_function = "") const {
		if (!member || !member_owner_struct) {
			return false;
		}

		// If access control is disabled, allow all access
		if (context_->isAccessControlDisabled()) {
			return true;
		}

		// Public members are always accessible
		if (member->access == AccessSpecifier::Public) {
			return true;
		}

		// Check if accessing function is a friend function of the member owner
		if (!accessing_function.empty() && member_owner_struct->isFriendFunction(accessing_function)) {
			return true;
		}

		// Check if accessing class is a friend class of the member owner
		if (accessing_struct && member_owner_struct->isFriendClass(accessing_struct->name)) {
			return true;
		}

		// If we're not in a member function context, only public members are accessible
		if (!accessing_struct) {
			return false;
		}

		// Private members are only accessible from:
		// 1. The same class
		// 2. Nested classes within the same class
		if (member->access == AccessSpecifier::Private) {
			if (accessing_struct == member_owner_struct) {
				return true;
			}
			// Check if accessing_struct is nested within member_owner_struct
			return isNestedWithin(accessing_struct, member_owner_struct);
		}

		// Protected members are accessible from:
		// 1. The same class
		// 2. Derived classes (if inherited as public or protected)
		if (member->access == AccessSpecifier::Protected) {
			// Same class
			if (accessing_struct == member_owner_struct) {
				return true;
			}

			// Check if accessing_struct is derived from member_owner_struct
			return isAccessibleThroughInheritance(accessing_struct, member_owner_struct);
		}

		return false;
	}

	// Helper to check if accessing_struct is nested within member_owner_struct
	bool isNestedWithin(const StructTypeInfo* accessing_struct,
	                     const StructTypeInfo* member_owner_struct) const {
		if (!accessing_struct || !member_owner_struct) {
			return false;
		}

		// Check if accessing_struct is nested within member_owner_struct
		StructTypeInfo* current = accessing_struct->getEnclosingClass();
		while (current) {
			if (current == member_owner_struct) {
				return true;
			}
			current = current->getEnclosingClass();
		}

		return false;
	}

	// Helper to check if derived_struct can access protected members of base_struct
	bool isAccessibleThroughInheritance(const StructTypeInfo* derived_struct,
	                                     const StructTypeInfo* base_struct) const {
		if (!derived_struct || !base_struct) {
			return false;
		}

		// Check direct base classes
		for (const auto& base : derived_struct->base_classes) {
			if (base.type_index >= gTypeInfo.size()) {
				continue;
			}

			const TypeInfo& base_type = gTypeInfo[base.type_index];
			const StructTypeInfo* base_info = base_type.getStructInfo();

			if (!base_info) {
				continue;
			}

			// Found the base class
			if (base_info == base_struct) {
				// Protected members are accessible if inherited as public or protected
				return base.access == AccessSpecifier::Public ||
				       base.access == AccessSpecifier::Protected;
			}

			// Recursively check base classes
			if (isAccessibleThroughInheritance(base_info, base_struct)) {
				return true;
			}
		}

		return false;
	}

	// Get the current struct context (which class we're currently in)
	const StructTypeInfo* getCurrentStructContext() const {
		// Check if we're in a member function by looking at the symbol table
		// The 'this' pointer is only present in member function contexts
		auto this_symbol = symbol_table.lookup("this");
		if (this_symbol.has_value() && this_symbol->is<DeclarationNode>()) {
			const DeclarationNode& this_decl = this_symbol->as<DeclarationNode>();
			const TypeSpecifierNode& this_type = this_decl.type_node().as<TypeSpecifierNode>();

			if (this_type.type() == Type::Struct && this_type.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[this_type.type_index()];
				return type_info.getStructInfo();
			}
		}

		return nullptr;
	}

	// Get the current function name
	std::string getCurrentFunctionName() const {
		return current_function_name_;
	}

	// Helper function to check if access to a member function is allowed
	bool checkMemberFunctionAccess(const StructMemberFunction* member_func,
	                                const StructTypeInfo* member_owner_struct,
	                                const StructTypeInfo* accessing_struct,
	                                const std::string& accessing_function = "") const {
		if (!member_func || !member_owner_struct) {
			return false;
		}

		// If access control is disabled, allow all access
		if (context_->isAccessControlDisabled()) {
			return true;
		}

		// Public member functions are always accessible
		if (member_func->access == AccessSpecifier::Public) {
			return true;
		}

		// Check if accessing function is a friend function of the member owner
		if (!accessing_function.empty() && member_owner_struct->isFriendFunction(accessing_function)) {
			return true;
		}

		// Check if accessing class is a friend class of the member owner
		if (accessing_struct && member_owner_struct->isFriendClass(accessing_struct->name)) {
			return true;
		}

		// If we're not in a member function context, only public functions are accessible
		if (!accessing_struct) {
			return false;
		}

		// Private member functions are only accessible from the same class
		if (member_func->access == AccessSpecifier::Private) {
			return accessing_struct == member_owner_struct;
		}

		// Protected member functions are accessible from:
		// 1. The same class
		// 2. Derived classes
		if (member_func->access == AccessSpecifier::Protected) {
			// Same class
			if (accessing_struct == member_owner_struct) {
				return true;
			}

			// Check if accessing_struct is derived from member_owner_struct
			return isAccessibleThroughInheritance(accessing_struct, member_owner_struct);
		}

		return false;
	}

	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		auto definition_block = node.get_definition();
		if (!definition_block.has_value())
			return;

		// Reset the temporary variable counter for each new function
		var_counter = TempVar();

		// Set current function name for static local variable mangling
		const DeclarationNode& func_decl = node.decl_node();
		current_function_name_ = std::string(func_decl.identifier_token().value());

		// Clear static local names map for new function
		static_local_names_.clear();

		const TypeSpecifierNode& ret_type = func_decl.type_node().as<TypeSpecifierNode>();

		// Create function declaration with return type and name
		std::vector<IrOperand> funcDeclOperands;
		funcDeclOperands.emplace_back(ret_type.type());
		funcDeclOperands.emplace_back(static_cast<int>(ret_type.size_in_bits()));
		funcDeclOperands.emplace_back(static_cast<int>(ret_type.pointer_depth()));  // Add pointer depth
		funcDeclOperands.emplace_back(func_decl.identifier_token().value());

		// Add struct/class name for member functions
		if (node.is_member_function()) {
			funcDeclOperands.emplace_back(std::string_view(node.parent_struct_name()));
		} else {
			funcDeclOperands.emplace_back(std::string_view(""));  // Empty string_view for non-member functions
		}

		// Add linkage information (C vs C++)
		funcDeclOperands.emplace_back(static_cast<int>(node.linkage()));

		// Add parameter types to function declaration
		//size_t paramCount = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			// Add parameter type, size, and pointer depth to function declaration
			funcDeclOperands.emplace_back(param_type.type());
			funcDeclOperands.emplace_back(static_cast<int>(param_type.size_in_bits()));

			// References are treated like pointers in the IR (they're both addresses)
			int pointer_depth = static_cast<int>(param_type.pointer_depth());
			if (param_type.is_reference()) {
				pointer_depth = 1;  // Treat reference as pointer depth 1
			}
			funcDeclOperands.emplace_back(pointer_depth);
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

		// Check if this is an implicit operator= that needs code generation
		if (node.is_implicit() && node.is_member_function()) {
			std::string_view func_name = func_decl.identifier_token().value();
			if (func_name == "operator=") {
				// This is an implicit copy or move assignment operator
				// Determine which one by checking the parameter type
				bool is_move_assignment = false;
				if (node.parameter_nodes().size() == 1) {
					const auto& param_decl = node.parameter_nodes()[0].as<DeclarationNode>();
					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					if (param_type.is_rvalue_reference()) {
						is_move_assignment = true;
					}
				}

				// Generate memberwise assignment from 'other' to 'this'
				// (same code for both copy and move assignment - memberwise copy/move)

				// Look up the struct type
				auto type_it = gTypesByName.find(node.parent_struct_name());
				if (type_it != gTypesByName.end()) {
					const TypeInfo* struct_type_info = type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

					if (struct_info) {
						// Generate memberwise assignment
						for (const auto& member : struct_info->members) {
							// First, load the member from 'other'
							// Format: [result_var, member_type, member_size, object_name, member_name, offset]
							TempVar member_value = var_counter.next();
							std::vector<IrOperand> load_operands;
							load_operands.emplace_back(member_value);
							load_operands.emplace_back(member.type);
							load_operands.emplace_back(static_cast<int>(member.size * 8));
							load_operands.emplace_back(std::string_view("other"));  // Load from 'other' parameter
							load_operands.emplace_back(std::string_view(member.name));
							load_operands.emplace_back(static_cast<int>(member.offset));

							ir_.addInstruction(IrOpcode::MemberAccess, std::move(load_operands), func_decl.identifier_token());

							// Then, store the member to 'this'
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							std::vector<IrOperand> store_operands;
							store_operands.emplace_back(member.type);
							store_operands.emplace_back(static_cast<int>(member.size * 8));
							store_operands.emplace_back(std::string_view("this"));  // Store to 'this'
							store_operands.emplace_back(std::string_view(member.name));
							store_operands.emplace_back(static_cast<int>(member.offset));
							store_operands.emplace_back(member_value);

							ir_.addInstruction(IrOpcode::MemberStore, std::move(store_operands), func_decl.identifier_token());
						}

						// Return *this (the return value is the 'this' pointer dereferenced)
						// Generate: %temp = dereference [Type][Size] %this
						//           return [Type][Size] %temp
						TempVar this_deref = var_counter.next();
						std::vector<IrOperand> deref_operands;
						deref_operands.emplace_back(this_deref);  // result variable
						deref_operands.emplace_back(Type::Struct);  // type
						deref_operands.emplace_back(static_cast<int>(struct_info->total_size * 8));  // size in bits
						deref_operands.emplace_back(std::string_view("this"));  // operand (this pointer)

						ir_.addInstruction(IrOpcode::Dereference, std::move(deref_operands), func_decl.identifier_token());

						// Return the dereferenced value
						std::vector<IrOperand> ret_operands;
						ret_operands.emplace_back(Type::Struct);
						ret_operands.emplace_back(static_cast<int>(struct_info->total_size * 8));
						ret_operands.emplace_back(this_deref);
						ir_.addInstruction(IrOpcode::Return, std::move(ret_operands), func_decl.identifier_token());
					}
				}
			}
		} else {
			// User-defined function body
			(*definition_block)->get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		}

		symbol_table.exit_scope();
	}

	void visitStructDeclarationNode(const StructDeclarationNode& node) {
		// Struct declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system

		// Only visit member functions if we're at the top level (not inside a function)
		// Local struct declarations inside functions should not generate member function IR
		// because the member functions are already generated when the struct type is registered
		if (current_function_name_.empty()) {
			// We're at the top level, visit member functions
			for (const auto& member_func : node.member_functions()) {
				// Each member function can be a FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
				visit(member_func.function_declaration);
			}
		}
		// If we're inside a function, just skip - the struct type is already registered
	}

	void visitEnumDeclarationNode(const EnumDeclarationNode& node) {
		// Enum declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system
		// Enumerators are treated as compile-time constants and don't need runtime code generation
		// For unscoped enums, the enumerators are already added to the symbol table during parsing
	}

	void visitConstructorDeclarationNode(const ConstructorDeclarationNode& node) {
		auto definition_block = node.get_definition();
		if (!definition_block.has_value())
			return;

		// Reset the temporary variable counter for each new constructor
		var_counter = TempVar();

		// Set current function name for static local variable mangling
		current_function_name_ = std::string(node.name());
		static_local_names_.clear();

		// Create constructor declaration with struct name
		std::vector<IrOperand> ctorDeclOperands;
		ctorDeclOperands.emplace_back(Type::Void);  // Constructors don't have a return type
		ctorDeclOperands.emplace_back(0);  // Size is 0 for void
		ctorDeclOperands.emplace_back(0);  // Pointer depth is 0 for void
		ctorDeclOperands.emplace_back(std::string(node.struct_name()) + "::" + std::string(node.struct_name()));
		ctorDeclOperands.emplace_back(std::string_view(node.struct_name()));  // Struct name for member function

		// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
		// We don't add it here to avoid duplication

		// Add parameter types to constructor declaration
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			ctorDeclOperands.emplace_back(param_type.type());
			ctorDeclOperands.emplace_back(static_cast<int>(param_type.size_in_bits()));
			ctorDeclOperands.emplace_back(static_cast<int>(param_type.pointer_depth()));  // Add pointer depth
			ctorDeclOperands.emplace_back(param_decl.identifier_token().value());
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(ctorDeclOperands), node.name_token()));

		symbol_table.enter_scope(ScopeType::Function);

		// Add 'this' pointer to symbol table for member access
		// Look up the struct type to get its type index and size
		auto type_it = gTypesByName.find(node.struct_name());
		if (type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
				Token this_token = node.name_token();  // Use constructor token for location
				auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
					Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
				auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

				// Add 'this' to symbol table (it's the implicit first parameter)
				symbol_table.insert("this", this_decl);
			}
		}

		// Add parameters to symbol table
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			symbol_table.insert(param_decl.identifier_token().value(), param);
		}

		// C++ construction order:
		// 1. Base class constructors (in declaration order)
		// 2. Member variables (in declaration order)
		// 3. Constructor body

		// Look up the struct type to get base class and member information
		auto struct_type_it = gTypesByName.find(node.struct_name());
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Step 1: Call base class constructors (in declaration order)
				for (const auto& base : struct_info->base_classes) {
					// Check if there's an explicit base initializer
					const BaseInitializer* base_init = nullptr;
					for (const auto& init : node.base_initializers()) {
						if (init.base_class_name == base.name) {
							base_init = &init;
							break;
						}
					}

					// Get base class type info
					if (base.type_index >= gTypeInfo.size()) {
						continue;  // Invalid base type index
					}
					const TypeInfo& base_type_info = gTypeInfo[base.type_index];

					// Build constructor call: Base::Base(this, args...)
					std::vector<IrOperand> ctor_operands;
					ctor_operands.emplace_back(base_type_info.name_);  // Base class name
					ctor_operands.emplace_back(std::string_view("this"));  // 'this' pointer (base subobject is at offset 0 for now)

					// Add constructor arguments from base initializer
					if (base_init) {
						for (const auto& arg : base_init->arguments) {
							auto arg_operands = visitExpressionNode(arg.as<ExpressionNode>());
							// Add the argument value (type, size, value)
							ctor_operands.insert(ctor_operands.end(), arg_operands.begin(), arg_operands.end());
						}
					}
					// If no explicit initializer, call default constructor (no args)

					ir_.addInstruction(IrOpcode::ConstructorCall, std::move(ctor_operands), node.name_token());
				}

				// Step 1.5: Initialize vptr if this class has virtual functions
				// This must happen after base constructor calls (which set up base vptr)
				// but before member initialization
				if (struct_info->has_vtable) {
					// TODO: Generate vptr initialization
					// For now, we skip this - vtable generation is complex and requires:
					// 1. Generating vtable data structure in .rdata section
					// 2. Creating vtable symbol
					// 3. Storing vtable address in vptr (first 8 bytes of object)
					// This will be implemented in a future phase
				}
			}
		}

		// Step 2: Generate IR for member initializers (executed before constructor body)
		// Look up the struct type to get member information
		struct_type_it = gTypesByName.find(node.struct_name());
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// If this is an implicit constructor, generate appropriate initialization
				if (node.is_implicit()) {
					// Check if this is a copy or move constructor (has one parameter that is a reference)
					bool is_copy_constructor = false;
					bool is_move_constructor = false;
					if (node.parameter_nodes().size() == 1) {
						const auto& param_decl = node.parameter_nodes()[0].as<DeclarationNode>();
						const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
						if (param_type.is_reference() && param_type.type() == Type::Struct) {
							if (param_type.is_rvalue_reference()) {
								is_move_constructor = true;
							} else {
								is_copy_constructor = true;
							}
						}
					}

					if (is_copy_constructor || is_move_constructor) {
						// Implicit copy/move constructor: memberwise copy/move from 'other' to 'this'
						for (const auto& member : struct_info->members) {
							// First, load the member from 'other'
							// Format: [result_var, member_type, member_size, object_name, member_name, offset]
							TempVar member_value = var_counter.next();
							std::vector<IrOperand> load_operands;
							load_operands.emplace_back(member_value);
							load_operands.emplace_back(member.type);
							load_operands.emplace_back(static_cast<int>(member.size * 8));
							load_operands.emplace_back(std::string_view("other"));  // Load from 'other' parameter
							load_operands.emplace_back(std::string_view(member.name));
							load_operands.emplace_back(static_cast<int>(member.offset));

							ir_.addInstruction(IrOpcode::MemberAccess, std::move(load_operands), node.name_token());

							// Then, store the member to 'this'
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							std::vector<IrOperand> store_operands;
							store_operands.emplace_back(member.type);
							store_operands.emplace_back(static_cast<int>(member.size * 8));
							store_operands.emplace_back(std::string_view("this"));
							store_operands.emplace_back(std::string_view(member.name));
							store_operands.emplace_back(static_cast<int>(member.offset));
							store_operands.emplace_back(member_value);  // Value from 'other'

							ir_.addInstruction(IrOpcode::MemberStore, std::move(store_operands), node.name_token());
						}
					} else {
						// Implicit default constructor: use default member initializers or zero-initialize
						for (const auto& member : struct_info->members) {
							// Generate MemberStore IR to initialize the member
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							std::vector<IrOperand> store_operands;
							store_operands.emplace_back(member.type);  // member type
							store_operands.emplace_back(static_cast<int>(member.size * 8));  // member size in bits
							store_operands.emplace_back(std::string_view("this"));  // object name (use 'this' in constructor)
							store_operands.emplace_back(std::string_view(member.name));  // member name
							store_operands.emplace_back(static_cast<int>(member.offset));  // member offset

							// Check if member has a default initializer (C++11 feature)
							if (member.default_initializer.has_value()) {
								const ASTNode& init_node = member.default_initializer.value();
								if (init_node.has_value() && init_node.is<ExpressionNode>()) {
									// Use the default member initializer
									auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
									// Add just the value (third element of init_operands)
									store_operands.emplace_back(init_operands[2]);
								} else {
									// Default initializer exists but isn't an expression, zero-initialize
									if (member.type == Type::Int || member.type == Type::Long ||
									    member.type == Type::Short || member.type == Type::Char) {
										store_operands.emplace_back(0);
									} else if (member.type == Type::Float || member.type == Type::Double) {
										store_operands.emplace_back(0.0);
									} else if (member.type == Type::Bool) {
										store_operands.emplace_back(false);
									} else {
										store_operands.emplace_back(0);
									}
								}
							} else {
								// Zero-initialize based on type
								if (member.type == Type::Int || member.type == Type::Long ||
								    member.type == Type::Short || member.type == Type::Char) {
									store_operands.emplace_back(0);  // Zero for integer types
								} else if (member.type == Type::Float || member.type == Type::Double) {
									store_operands.emplace_back(0.0);  // Zero for floating-point types
								} else if (member.type == Type::Bool) {
									store_operands.emplace_back(false);  // False for bool
								} else {
									store_operands.emplace_back(0);  // Default to zero
								}
							}

							ir_.addInstruction(IrOpcode::MemberStore, std::move(store_operands), node.name_token());
						}
					}
				} else {
					// User-defined constructor: initialize all members
					// Precedence: explicit initializer > default initializer > zero-initialize

					// Build a map of explicit member initializers for quick lookup
					std::unordered_map<std::string, const MemberInitializer*> explicit_inits;
					for (const auto& initializer : node.member_initializers()) {
						explicit_inits[std::string(initializer.member_name)] = &initializer;
					}

					// Initialize all members
					for (const auto& member : struct_info->members) {
						// Generate MemberStore IR to initialize the member
						// Format: [member_type, member_size, object_name, member_name, offset, value]
						std::vector<IrOperand> store_operands;
						store_operands.emplace_back(member.type);  // member type
						store_operands.emplace_back(static_cast<int>(member.size * 8));  // member size in bits
						store_operands.emplace_back(std::string_view("this"));  // object name (use 'this' in constructor)
						store_operands.emplace_back(std::string_view(member.name));  // member name
						store_operands.emplace_back(static_cast<int>(member.offset));  // member offset

						// Check for explicit initializer first (highest precedence)
						auto explicit_it = explicit_inits.find(member.name);
						if (explicit_it != explicit_inits.end()) {
							// Use explicit initializer from constructor initializer list
							auto init_operands = visitExpressionNode(explicit_it->second->initializer_expr.as<ExpressionNode>());
							store_operands.emplace_back(init_operands[2]);
						} else if (member.default_initializer.has_value()) {
							const ASTNode& init_node = member.default_initializer.value();
							if (init_node.has_value() && init_node.is<ExpressionNode>()) {
								// Use default member initializer (C++11 feature)
								auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
								store_operands.emplace_back(init_operands[2]);
							} else {
								// Default initializer exists but isn't an expression, zero-initialize
								if (member.type == Type::Int || member.type == Type::Long ||
								    member.type == Type::Short || member.type == Type::Char) {
									store_operands.emplace_back(0);
								} else if (member.type == Type::Float || member.type == Type::Double) {
									store_operands.emplace_back(0.0);
								} else if (member.type == Type::Bool) {
									store_operands.emplace_back(false);
								} else {
									store_operands.emplace_back(0);
								}
							}
						} else {
							// Zero-initialize based on type
							if (member.type == Type::Int || member.type == Type::Long ||
							    member.type == Type::Short || member.type == Type::Char) {
								store_operands.emplace_back(0);  // Zero for integer types
							} else if (member.type == Type::Float || member.type == Type::Double) {
								store_operands.emplace_back(0.0);  // Zero for floating-point types
							} else if (member.type == Type::Bool) {
								store_operands.emplace_back(false);  // False for bool
							} else {
								store_operands.emplace_back(0);  // Default to zero
							}
						}

						ir_.addInstruction(IrOpcode::MemberStore, std::move(store_operands), node.name_token());
					}
				}
			}
		}

		// Visit the constructor body
		definition_block.value()->get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		// Add implicit return for constructor (constructors don't have explicit return statements)
		// Format: [type, size, value] - for void return, value is 0
		ir_.addInstruction(IrOpcode::Return, {Type::Void, 0, 0ULL}, node.name_token());

		symbol_table.exit_scope();
	}

	void visitDestructorDeclarationNode(const DestructorDeclarationNode& node) {
		auto definition_block = node.get_definition();
		if (!definition_block.has_value())
			return;

		// Reset the temporary variable counter for each new destructor
		var_counter = TempVar();

		// Set current function name for static local variable mangling
		current_function_name_ = std::string(node.name());
		static_local_names_.clear();

		// Create destructor declaration with struct name
		std::vector<IrOperand> dtorDeclOperands;
		dtorDeclOperands.emplace_back(Type::Void);  // Destructors don't have a return type
		dtorDeclOperands.emplace_back(0);  // Size is 0 for void
		dtorDeclOperands.emplace_back(0);  // Pointer depth is 0 for void
		dtorDeclOperands.emplace_back(std::string(node.struct_name()) + "::~" + std::string(node.struct_name()));
		dtorDeclOperands.emplace_back(std::string_view(node.struct_name()));  // Struct name for member function

		// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
		// We don't add it here to avoid duplication

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(dtorDeclOperands), node.name_token()));

		symbol_table.enter_scope(ScopeType::Function);

		// Add 'this' pointer to symbol table for member access
		// Look up the struct type to get its type index and size
		auto type_it = gTypesByName.find(node.struct_name());
		if (type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
				Token this_token = node.name_token();  // Use destructor token for location
				auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
					Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
				auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

				// Add 'this' to symbol table (it's the implicit first parameter)
				symbol_table.insert("this", this_decl);
			}
		}

		// C++ destruction order:
		// 1. Destructor body
		// 2. Member variables destroyed (automatic for non-class types)
		// 3. Base class destructors (in REVERSE declaration order)

		// Step 1: Visit the destructor body
		definition_block.value()->get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		// Step 2: Member destruction is automatic for primitive types (no action needed)

		// Step 3: Call base class destructors in REVERSE order
		auto struct_type_it = gTypesByName.find(node.struct_name());
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info && !struct_info->base_classes.empty()) {
				// Iterate through base classes in reverse order
				for (auto it = struct_info->base_classes.rbegin(); it != struct_info->base_classes.rend(); ++it) {
					const auto& base = *it;

					// Get base class type info
					if (base.type_index >= gTypeInfo.size()) {
						continue;  // Invalid base type index
					}
					const TypeInfo& base_type_info = gTypeInfo[base.type_index];

					// Build destructor call: Base::~Base(this)
					std::vector<IrOperand> dtor_operands;
					dtor_operands.emplace_back(base_type_info.name_);  // Base class name
					dtor_operands.emplace_back(std::string_view("this"));  // 'this' pointer (base subobject is at offset 0 for now)

					ir_.addInstruction(IrOpcode::DestructorCall, std::move(dtor_operands), node.name_token());
				}
			}
		}

		// Add implicit return for destructor (destructors don't have explicit return statements)
		// Format: [type, size, value] - for void return, value is 0
		ir_.addInstruction(IrOpcode::Return, {Type::Void, 0, 0ULL}, node.name_token());

		symbol_table.exit_scope();
	}

	void visitNamespaceDeclarationNode(const NamespaceDeclarationNode& node) {
		// Namespace declarations themselves don't generate IR - they just provide scope
		// Visit all declarations within the namespace
		for (const auto& decl : node.declarations()) {
			visit(decl);
		}
	}

	void visitUsingDirectiveNode(const UsingDirectiveNode& node) {
		// Using directives don't generate IR - they affect name lookup in the symbol table
		// Add the namespace to the current scope's using directives
		gSymbolTable.add_using_directive(node.namespace_path());
	}

	void visitUsingDeclarationNode(const UsingDeclarationNode& node) {
		// Using declarations don't generate IR - they import a specific name into the current scope
		// Add the using declaration to the current scope
		gSymbolTable.add_using_declaration(
			node.identifier_name(),
			node.namespace_path(),
			node.identifier_name()
		);
	}

	void visitNamespaceAliasNode(const NamespaceAliasNode& node) {
		// Namespace aliases don't generate IR - they create an alias for a namespace
		// Add the alias to the current scope
		gSymbolTable.add_namespace_alias(node.alias_name(), node.target_namespace());
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
		// Enter a new scope
		enterScope();
		ir_.addInstruction(IrOpcode::ScopeBegin, {}, Token());

		// Visit all statements in the block
		node.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		// Exit scope and call destructors
		ir_.addInstruction(IrOpcode::ScopeEnd, {}, Token());
		exitScope();
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

	void visitSwitchStatementNode(const SwitchStatementNode& node) {
		// Generate unique labels for this switch statement
		static size_t switch_counter = 0;
		std::string switch_end_label = "switch_end_" + std::to_string(switch_counter);
		std::string default_label = "switch_default_" + std::to_string(switch_counter);
		switch_counter++;

		// Evaluate the switch condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Get the condition type and value
		Type condition_type = std::get<Type>(condition_operands[0]);
		int condition_size = std::get<int>(condition_operands[1]);

		// Mark switch begin for break support (switch acts like a loop for break)
		// Continue is not allowed in switch, but break is
		ir_.addInstruction(IrOpcode::LoopBegin, {switch_end_label, switch_end_label, switch_end_label}, Token());

		// Process the switch body to collect case labels
		auto body = node.get_body();
		if (!body.is<BlockNode>()) {
			assert(false && "Switch body must be a BlockNode");
			return;
		}

		const BlockNode& block = body.as<BlockNode>();
		std::vector<std::pair<std::string, ASTNode>> case_labels;  // label name, case value
		bool has_default = false;

		// First pass: generate labels and collect case values
		size_t case_index = 0;
		block.get_statements().visit([&](const ASTNode& stmt) {
			if (stmt.is<CaseLabelNode>()) {
				std::string case_label = "switch_case_" + std::to_string(switch_counter - 1) + "_" + std::to_string(case_index);
				case_labels.push_back({case_label, stmt.as<CaseLabelNode>().get_case_value()});
				case_index++;
			} else if (stmt.is<DefaultLabelNode>()) {
				has_default = true;
			}
		});

		// Generate comparison chain for each case
		size_t check_index = 0;
		for (const auto& [case_label, case_value_node] : case_labels) {
			// Evaluate case value (must be constant)
			auto case_value_operands = visitExpressionNode(case_value_node.as<ExpressionNode>());

			// Compare condition with case value using Equal opcode
			TempVar cmp_result = var_counter.next();
			std::vector<IrOperand> cmp_operands;
			cmp_operands.push_back(cmp_result);
			cmp_operands.push_back(condition_type);
			cmp_operands.push_back(condition_size);
			cmp_operands.push_back(condition_operands[2]);  // condition value
			cmp_operands.push_back(condition_type);
			cmp_operands.push_back(condition_size);
			cmp_operands.push_back(case_value_operands[2]);  // case value
			ir_.addInstruction(IrOpcode::Equal, std::move(cmp_operands), Token());

			// Branch to case label if equal, otherwise check next case
			std::string next_check_label = "switch_check_" + std::to_string(switch_counter - 1) + "_" +
			                               std::to_string(check_index + 1);

			std::vector<IrOperand> branch_operands;
			branch_operands.push_back(cmp_result);
			branch_operands.push_back(Type::Bool);
			branch_operands.push_back(1);
			branch_operands.push_back(case_label);
			branch_operands.push_back(next_check_label);
			ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());

			// Next check label
			ir_.addInstruction(IrOpcode::Label, {next_check_label}, Token());
			check_index++;
		}

		// If no case matched, jump to default or end
		if (has_default) {
			ir_.addInstruction(IrOpcode::Branch, {default_label}, Token());
		} else {
			ir_.addInstruction(IrOpcode::Branch, {switch_end_label}, Token());
		}

		// Second pass: generate code for each case/default
		case_index = 0;
		block.get_statements().visit([&](const ASTNode& stmt) {
			if (stmt.is<CaseLabelNode>()) {
				const CaseLabelNode& case_node = stmt.as<CaseLabelNode>();
				std::string case_label = "switch_case_" + std::to_string(switch_counter - 1) + "_" + std::to_string(case_index);

				// Case label
				ir_.addInstruction(IrOpcode::Label, {case_label}, Token());

				// Execute case statements
				if (case_node.has_statement()) {
					auto case_stmt = case_node.get_statement();
					if (case_stmt->is<BlockNode>()) {
						case_stmt->as<BlockNode>().get_statements().visit([&](ASTNode statement) {
							visit(statement);
						});
					} else {
						visit(*case_stmt);
					}
				}
				// Note: Fall-through is automatic - no break means execution continues to next case

				case_index++;
			} else if (stmt.is<DefaultLabelNode>()) {
				const DefaultLabelNode& default_node = stmt.as<DefaultLabelNode>();

				// Default label
				ir_.addInstruction(IrOpcode::Label, {default_label}, Token());

				// Execute default statements
				if (default_node.has_statement()) {
					auto default_stmt = default_node.get_statement();
					if (default_stmt->is<BlockNode>()) {
						default_stmt->as<BlockNode>().get_statements().visit([&](ASTNode statement) {
							visit(statement);
						});
					} else {
						visit(*default_stmt);
					}
				}
			}
		});

		// Switch end label
		ir_.addInstruction(IrOpcode::Label, {switch_end_label}, Token());

		// Mark switch end
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

	void visitGotoStatementNode(const GotoStatementNode& node) {
		// Generate Branch IR instruction (unconditional jump) with the target label name
		std::string label_name(node.label_name());
		ir_.addInstruction(IrOpcode::Branch, {label_name}, node.goto_token());
	}

	void visitLabelStatementNode(const LabelStatementNode& node) {
		// Generate Label IR instruction with the label name
		std::string label_name(node.label_name());
		ir_.addInstruction(IrOpcode::Label, {label_name}, node.label_token());
	}

	void visitVariableDeclarationNode(const ASTNode& ast_node) {
		const VariableDeclarationNode& node = ast_node.as<VariableDeclarationNode>();
		const auto& decl = node.declaration();
		const auto& type_node = decl.type_node().as<TypeSpecifierNode>();

		// Check if this is a global variable (declared at global scope)
		bool is_global = (symbol_table.get_current_scope_type() == ScopeType::Global);

		// Check if this is a static local variable
		bool is_static_local = (node.storage_class() == StorageClass::Static && !is_global);

		if (is_global || is_static_local) {
			// Handle global variable or static local variable
			// For static locals, mangle the name to include the function name
			std::string var_name;
			if (is_static_local) {
				// Mangle name as: function_name.variable_name
				var_name = std::string(current_function_name_) + "." + std::string(decl.identifier_token().value());
			} else {
				var_name = std::string(decl.identifier_token().value());
			}

			// Format: [type, size_in_bits, var_name, is_initialized, init_value?]
			std::vector<IrOperand> operands;
			operands.emplace_back(type_node.type());
			operands.emplace_back(static_cast<int>(type_node.size_in_bits()));
			operands.emplace_back(var_name);

			// Check if initialized
			if (node.initializer()) {
				const ASTNode& init_node = *node.initializer();
				if (init_node.is<ExpressionNode>()) {
					auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
					// init_operands = [type, size, value]
					if (init_operands.size() >= 3) {
						operands.emplace_back(true);  // is_initialized
						operands.emplace_back(init_operands[2]);  // init_value
					} else {
						operands.emplace_back(false);  // is_initialized
					}
				} else {
					operands.emplace_back(false);  // is_initialized
				}
			} else {
				operands.emplace_back(false);  // is_initialized
			}

			ir_.addInstruction(IrOpcode::GlobalVariableDecl, std::move(operands), decl.identifier_token());

			// For static locals, store the mapping from local name to mangled name and type info
			// (The parser already added it to the symbol table)
			if (is_static_local) {
				StaticLocalInfo info;
				info.mangled_name = var_name;
				info.type = type_node.type();
				info.size_in_bits = static_cast<int>(type_node.size_in_bits());
				static_local_names_[std::string(decl.identifier_token().value())] = info;
			}

			return;
		}

		// Handle local variable
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

				// Add to symbol table first
				if (!symbol_table.insert(decl.identifier_token().value(), node.declaration_node())) {
					assert(false && "Expected identifier to be unique");
				}

				// Add the variable declaration without initializer
				ir_.addInstruction(IrOpcode::VariableDecl, std::move(operands), node.declaration().identifier_token());

				// Check if this struct has a constructor
				if (type_node.type() == Type::Struct) {
					TypeIndex type_index = type_node.type_index();
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						if (type_info.struct_info_) {
							const StructTypeInfo& struct_info = *type_info.struct_info_;

							// Check if this is an abstract class (only for non-pointer types)
							if (struct_info.is_abstract && type_node.pointer_levels().empty()) {
								std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name_ << "'\n";
								assert(false && "Cannot instantiate abstract class");
							}

							const auto& initializers = init_list.initializers();

							if (struct_info.hasAnyConstructor()) {
								// Generate constructor call with parameters from initializer list
								std::vector<IrOperand> ctor_operands;
								ctor_operands.emplace_back(type_info.name_);  // Struct name
								ctor_operands.emplace_back(std::string(decl.identifier_token().value()));  // Object name

								// Add each initializer as a constructor parameter
								for (const ASTNode& init_expr : initializers) {
									if (init_expr.is<ExpressionNode>()) {
										auto init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
										// init_operands = [type, size, value]
										// Add all three to match function call parameter format
										if (init_operands.size() >= 3) {
											ctor_operands.insert(ctor_operands.end(), init_operands.begin(), init_operands.end());
										} else {
											assert(false && "Invalid initializer operands - expected [type, size, value]");
										}
									} else {
										assert(false && "Initializer must be an ExpressionNode");
									}
								}

								ir_.addInstruction(IrOpcode::ConstructorCall, std::move(ctor_operands), decl.identifier_token());
							} else {
								// No constructor - use direct member initialization
								// Build a map of member names to initializer expressions
								std::unordered_map<std::string, const ASTNode*> member_values;
								size_t positional_index = 0;

								for (size_t i = 0; i < initializers.size(); ++i) {
									if (init_list.is_designated(i)) {
										// Designated initializer - use member name
										const std::string& member_name = init_list.member_name(i);
										member_values[member_name] = &initializers[i];
									} else {
										// Positional initializer - map to member by index
										if (positional_index < struct_info.members.size()) {
											const std::string& member_name = struct_info.members[positional_index].name;
											member_values[member_name] = &initializers[i];
											positional_index++;
										}
									}
								}

								// Generate member stores for each struct member
								for (const StructMember& member : struct_info.members) {
									std::vector<IrOperand> member_store_operands;
									member_store_operands.emplace_back(member.type);
									member_store_operands.emplace_back(static_cast<int>(member.size * 8));
									member_store_operands.emplace_back(decl.identifier_token().value());
									member_store_operands.emplace_back(std::string_view(member.name));
									member_store_operands.emplace_back(static_cast<int>(member.offset));

									// Check if this member has an initializer
									if (member_values.count(member.name)) {
										const ASTNode& init_expr = *member_values[member.name];
										std::vector<IrOperand> init_operands;
										if (init_expr.is<ExpressionNode>()) {
											init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
										} else {
											assert(false && "Initializer must be an ExpressionNode");
										}

										if (init_operands.size() >= 3) {
											member_store_operands.emplace_back(init_operands[2]);
										} else {
											assert(false && "Invalid initializer operands");
										}
									} else {
										// Zero-initialize unspecified members
										member_store_operands.emplace_back(0);
									}

									ir_.addInstruction(IrOpcode::MemberStore, std::move(member_store_operands), decl.identifier_token());
								}
							}

							// Register for destructor if needed
							if (struct_info.hasDestructor()) {
								registerVariableWithDestructor(
									std::string(decl.identifier_token().value()),
									type_info.name_
								);
							}
						}
					}
				}
				return; // Early return - we've already added the variable declaration
			} else if (init_node.is<LambdaExpressionNode>()) {
				// Lambda expression initializer
				// Generate the lambda functions (operator() and __invoke)
				const auto& lambda = init_node.as<LambdaExpressionNode>();
				generateLambdaExpressionIr(lambda);
				// For now, lambda variables are just empty structs (1 byte)
				// TODO: Store function pointer or closure data in the variable
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

		// If this is a struct type with a constructor, generate a constructor call
		if (type_node.type() == Type::Struct) {
			TypeIndex type_index = type_node.type_index();
			if (type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_index];
				if (type_info.struct_info_) {
					// Check if this is an abstract class (only for non-pointer types)
					if (type_info.struct_info_->is_abstract && type_node.pointer_levels().empty()) {
						std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name_ << "'\n";
						assert(false && "Cannot instantiate abstract class");
					}

					if (type_info.struct_info_->hasConstructor()) {
						// Generate constructor call
						std::vector<IrOperand> ctor_operands;
						ctor_operands.emplace_back(type_info.name_);  // Struct name (std::string)
						ctor_operands.emplace_back(std::string(decl.identifier_token().value()));    // Object variable name

						// TODO: Add support for constructor parameters from initializer

						ir_.addInstruction(IrOpcode::ConstructorCall, std::move(ctor_operands), decl.identifier_token());
					}
				}

				// If this struct has a destructor, register it for automatic cleanup
				if (type_info.struct_info_ && type_info.struct_info_->hasDestructor()) {
					registerVariableWithDestructor(
						std::string(decl.identifier_token().value()),
						type_info.name_
					);
				}
			}
		}
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
		else if (std::holds_alternative<NewExpressionNode>(exprNode)) {
			const auto& expr = std::get<NewExpressionNode>(exprNode);
			return generateNewExpressionIr(expr);
		}
		else if (std::holds_alternative<DeleteExpressionNode>(exprNode)) {
			const auto& expr = std::get<DeleteExpressionNode>(exprNode);
			return generateDeleteExpressionIr(expr);
		}
		else if (std::holds_alternative<StaticCastNode>(exprNode)) {
			const auto& expr = std::get<StaticCastNode>(exprNode);
			return generateStaticCastIr(expr);
		}
		else if (std::holds_alternative<DynamicCastNode>(exprNode)) {
			const auto& expr = std::get<DynamicCastNode>(exprNode);
			return generateDynamicCastIr(expr);
		}
		else if (std::holds_alternative<TypeidNode>(exprNode)) {
			const auto& expr = std::get<TypeidNode>(exprNode);
			return generateTypeidIr(expr);
		}
		else if (std::holds_alternative<LambdaExpressionNode>(exprNode)) {
			const auto& expr = std::get<LambdaExpressionNode>(exprNode);
			return generateLambdaExpressionIr(expr);
		}
		else {
			assert(false && "Not implemented yet");
		}

		return {};
	}

	std::vector<IrOperand> generateIdentifierIr(const IdentifierNode& identifierNode) {
		// Check if this is a captured variable in a lambda
		std::string var_name_str(identifierNode.name());
		if (!current_lambda_closure_type_.empty() &&
		    current_lambda_captures_.find(var_name_str) != current_lambda_captures_.end()) {
			// This is a captured variable - generate member access (this->x)
			// Look up the closure struct type
			auto type_it = gTypesByName.find(current_lambda_closure_type_);
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					// Find the member
					const StructMember* member = struct_info->findMemberRecursive(var_name_str);
					if (member) {
						// Check if this is a by-reference capture
						auto kind_it = current_lambda_capture_kinds_.find(var_name_str);
						bool is_reference = (kind_it != current_lambda_capture_kinds_.end() &&
						                     kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);

						if (is_reference) {
							// By-reference capture: member is a pointer, need to dereference
							// First, load the pointer from the closure
							TempVar ptr_temp = var_counter.next();
							std::vector<IrOperand> ptr_operands;
							ptr_operands.emplace_back(ptr_temp);
							ptr_operands.emplace_back(member->type);  // Base type (e.g., Int)
							ptr_operands.emplace_back(64);  // pointer size in bits
							ptr_operands.emplace_back(std::string_view("this"));
							ptr_operands.emplace_back(std::string_view(member->name));
							ptr_operands.emplace_back(static_cast<int>(member->offset));
							ir_.addInstruction(IrOpcode::MemberAccess, std::move(ptr_operands), Token());

							// The ptr_temp now contains the address of the captured variable
							// We need to dereference it using PointerDereference
							auto type_it = current_lambda_capture_types_.find(var_name_str);
							if (type_it != current_lambda_capture_types_.end()) {
								const TypeSpecifierNode& orig_type = type_it->second;

								// Generate Dereference to load the value
								TempVar result_temp = var_counter.next();
								std::vector<IrOperand> deref_operands;
								deref_operands.emplace_back(result_temp);
								deref_operands.emplace_back(orig_type.type());
								deref_operands.emplace_back(static_cast<int>(orig_type.size_in_bits()));
								deref_operands.emplace_back(ptr_temp);  // Dereference this pointer
								ir_.addInstruction(IrOpcode::Dereference, std::move(deref_operands), Token());

								return { orig_type.type(), static_cast<int>(orig_type.size_in_bits()), result_temp };
							}

							// Fallback: return the pointer temp
							return { member->type, 64, ptr_temp };
						} else {
							// By-value capture: direct member access
							// Format: [result_var, member_type, member_size, object_name, member_name, offset]
							TempVar result_temp = var_counter.next();
							std::vector<IrOperand> operands;
							operands.emplace_back(result_temp);
							operands.emplace_back(member->type);
							operands.emplace_back(static_cast<int>(member->size * 8));  // size in bits
							operands.emplace_back(std::string_view("this"));  // implicit this pointer
							operands.emplace_back(std::string_view(member->name));  // member name
							operands.emplace_back(static_cast<int>(member->offset));
							ir_.addInstruction(IrOpcode::MemberAccess, std::move(operands), Token());
							return { member->type, static_cast<int>(member->size * 8), result_temp };
						}
					}
				}
			}
		}

		// Check if this is a static local variable FIRST (before symbol table lookup)
		auto static_local_it = static_local_names_.find(std::string(identifierNode.name()));
		if (static_local_it != static_local_names_.end()) {
			// This is a static local - generate GlobalLoad with mangled name
			const StaticLocalInfo& info = static_local_it->second;

			// Generate GlobalLoad with mangled name
			TempVar result_temp = var_counter.next();
			std::vector<IrOperand> operands;
			operands.emplace_back(result_temp);
			operands.emplace_back(info.mangled_name);  // Use mangled name
			ir_.addInstruction(IrOpcode::GlobalLoad, std::move(operands), Token());

			// Return the temp variable that will hold the loaded value
			return { info.type, info.size_in_bits, result_temp };
		}

		// First try local symbol table
		std::optional<ASTNode> symbol = symbol_table.lookup(identifierNode.name());
		bool is_global = false;

		// If not found locally, try global symbol table (for enum values, global variables, etc.)
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(identifierNode.name());
			is_global = symbol.has_value();  // If found in global table, it's a global
		}

		if (!symbol.has_value()) {
			assert(false && "Expected symbol to exist");
			return {};
		}

		if (symbol->is<DeclarationNode>()) {
			const auto& decl_node = symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Check if this is an enum value (constant)
			if (type_node.type() == Type::Enum) {
				// This is an enum value - look up its constant value
				size_t enum_type_index = type_node.type_index();
				if (enum_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[enum_type_index];
					const EnumTypeInfo* enum_info = type_info.getEnumInfo();
					if (enum_info) {
						long long enum_value = enum_info->getEnumeratorValue(std::string(identifierNode.name()));
						// Return the enum value as a constant (using the underlying type)
						return { enum_info->underlying_type, static_cast<int>(enum_info->underlying_size),
						         static_cast<unsigned long long>(enum_value) };
					}
				}
			}

			// Check if this is a global variable
			if (is_global) {
				// Generate GlobalLoad IR instruction
				TempVar result_temp = var_counter.next();
				std::vector<IrOperand> operands;
				operands.emplace_back(result_temp);
				operands.emplace_back(identifierNode.name());
				ir_.addInstruction(IrOpcode::GlobalLoad, std::move(operands), Token());

				// Return the temp variable that will hold the loaded value
				return { type_node.type(), static_cast<int>(type_node.size_in_bits()), result_temp };
			}

			// Regular local variable
			return { type_node.type(), static_cast<int>(type_node.size_in_bits()), identifierNode.name() };
		}

		// Check if it's a VariableDeclarationNode (global variable)
		if (symbol->is<VariableDeclarationNode>()) {
			const auto& var_decl_node = symbol->as<VariableDeclarationNode>();
			const auto& decl_node = var_decl_node.declaration();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// This is a global variable - generate GlobalLoad
			TempVar result_temp = var_counter.next();
			std::vector<IrOperand> operands;
			operands.emplace_back(result_temp);
			operands.emplace_back(identifierNode.name());
			ir_.addInstruction(IrOpcode::GlobalLoad, std::move(operands), Token());

			// Return the temp variable that will hold the loaded value
			return { type_node.type(), static_cast<int>(type_node.size_in_bits()), result_temp };
		}

		// Check if it's a FunctionDeclarationNode (function name used as value)
		if (symbol->is<FunctionDeclarationNode>()) {
			// This is a function name being used as a value (e.g., fp = add)
			// Generate FunctionAddress IR instruction
			TempVar func_addr_var = var_counter.next();
			std::vector<IrOperand> func_addr_operands;
			func_addr_operands.emplace_back(func_addr_var);
			func_addr_operands.emplace_back(identifierNode.name());
			ir_.addInstruction(IrOpcode::FunctionAddress, std::move(func_addr_operands), Token());

			// Return the function address as a pointer (64 bits)
			return { Type::FunctionPointer, 64, func_addr_var };
		}

		// If we get here, the symbol is not a DeclarationNode
		assert(false && "Identifier is not a DeclarationNode");
		return {};
	}

	std::vector<IrOperand> generateQualifiedIdentifierIr(const QualifiedIdentifierNode& qualifiedIdNode) {
		// Check if this is a scoped enum value (e.g., Direction::North)
		const auto& namespaces = qualifiedIdNode.namespaces();
		if (namespaces.size() == 1) {
			// Could be EnumName::EnumeratorName
			std::string enum_name(namespaces[0]);
			auto type_it = gTypesByName.find(enum_name);
			if (type_it != gTypesByName.end() && type_it->second->isEnum()) {
				const EnumTypeInfo* enum_info = type_it->second->getEnumInfo();
				if (enum_info && enum_info->is_scoped) {
					// This is a scoped enum - look up the enumerator value
					long long enum_value = enum_info->getEnumeratorValue(std::string(qualifiedIdNode.name()));
					// Return the enum value as a constant
					return { enum_info->underlying_type, static_cast<int>(enum_info->underlying_size),
					         static_cast<unsigned long long>(enum_value) };
				}
			}
		}

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

		// For non-literal values (variables, TempVars), check if conversion is needed
		// If sizes are equal and only signedness differs, no actual conversion instruction is needed
		// The value can be reinterpreted as the new type
		if (fromSize == toSize) {
			// Same size, different signedness - just change the type metadata
			// Return the same value with the new type
			std::vector<IrOperand> result;
			result.push_back(toType);
			result.push_back(toSize);
			// Copy the value (TempVar or identifier)
			result.insert(result.end(), operands.begin() + 2, operands.end());
			return result;
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
		// Generate IR for string literal
		// Create a temporary variable to hold the address of the string
		TempVar result_var = var_counter.next();

		// Add StringLiteral IR instruction
		// Format: [result_var, string_content]
		std::vector<IrOperand> operands;
		operands.emplace_back(result_var);
		operands.emplace_back(stringLiteralNode.value());

		ir_.addInstruction(IrInstruction(IrOpcode::StringLiteral, std::move(operands), Token()));

		// Return the result as a char pointer (const char*)
		// We use Type::Char with 64-bit size to indicate it's a pointer
		return { Type::Char, 64, result_var };
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

		// Special handling for assignment to array subscript
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<ArraySubscriptNode>(lhs_expr)) {
				// This is an array subscript assignment: arr[index] = value
				const ArraySubscriptNode& array_subscript = std::get<ArraySubscriptNode>(lhs_expr);

				// Generate IR for the RHS value
				auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

				// Generate IR for the array subscript to get the address
				auto arrayAccessIrOperands = generateArraySubscriptIr(array_subscript);

				// The arrayAccessIrOperands contains [type, size, temp_var]
				// where temp_var holds the address of the array element
				// We need to generate an ArrayStore instruction
				// Format: [element_type, element_size, array_name, index_type, index_size, index_value, value]

				// Get array information
				const ExpressionNode& array_expr = array_subscript.array_expr().as<ExpressionNode>();
				if (!std::holds_alternative<IdentifierNode>(array_expr)) {
					// Error: array must be an identifier for now
					return { Type::Int, 32, TempVar{0} };
				}

				const IdentifierNode& array_ident = std::get<IdentifierNode>(array_expr);
				std::string_view array_name = array_ident.name();

				// Get index information
				auto indexIrOperands = visitExpressionNode(array_subscript.index_expr().as<ExpressionNode>());

				// Build ArrayStore IR operands
				std::vector<IrOperand> arrayStoreOperands;
				arrayStoreOperands.emplace_back(std::get<Type>(arrayAccessIrOperands[0])); // element type
				arrayStoreOperands.emplace_back(std::get<int>(arrayAccessIrOperands[1]));   // element size
				arrayStoreOperands.emplace_back(array_name);                                 // array name
				arrayStoreOperands.emplace_back(std::get<Type>(indexIrOperands[0]));        // index type
				arrayStoreOperands.emplace_back(std::get<int>(indexIrOperands[1]));         // index size
				arrayStoreOperands.emplace_back(indexIrOperands[2]);                         // index value
				arrayStoreOperands.emplace_back(rhsIrOperands[2]);                           // value to store

				ir_.addInstruction(IrOpcode::ArrayStore, std::move(arrayStoreOperands), binaryOperatorNode.get_token());

				// Return the RHS value as the result
				return rhsIrOperands;
			}
			else if (std::holds_alternative<MemberAccessNode>(lhs_expr)) {
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
				else if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
					// Check if this is a struct assignment that needs operator=
					const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
					std::string_view lhs_name = lhs_ident.name();

					// Look up the LHS in the symbol table
					const std::optional<ASTNode> lhs_symbol = symbol_table.lookup(lhs_name);
					if (lhs_symbol.has_value() && lhs_symbol->is<DeclarationNode>()) {
						const auto& lhs_decl = lhs_symbol->as<DeclarationNode>();
						const auto& lhs_type = lhs_decl.type_node().as<TypeSpecifierNode>();

						// Check if this is a struct type
						if (lhs_type.type() == Type::Struct) {
							TypeIndex struct_type_index = lhs_type.type_index();
							if (struct_type_index < gTypeInfo.size()) {
								const TypeInfo& struct_type_info = gTypeInfo[struct_type_index];
								const StructTypeInfo* struct_info = struct_type_info.getStructInfo();

								// Check if the struct has an operator=
								if (struct_info && struct_info->hasCopyAssignmentOperator()) {
									const StructMemberFunction* copy_assign_op = struct_info->findCopyAssignmentOperator();
									if (copy_assign_op) {
										// Generate a member function call to operator=
										// Format: [ret_var, function_name, this_type, this_size, this_name, param_type, param_size, param_value]

										TempVar ret_var = var_counter.next();
										std::vector<IrOperand> call_operands;
										call_operands.emplace_back(ret_var);
										call_operands.emplace_back(copy_assign_op->name);  // "operator="

										// Add 'this' pointer (the LHS object)
										call_operands.emplace_back(lhs_type.type());
										call_operands.emplace_back(static_cast<int>(lhs_type.size_in_bits()));
										call_operands.emplace_back(lhs_name);

										// Generate IR for the RHS value
										auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

										// Add the RHS as the parameter
										call_operands.emplace_back(rhsIrOperands[0]);  // type
										call_operands.emplace_back(rhsIrOperands[1]);  // size
										call_operands.emplace_back(rhsIrOperands[2]);  // value

										// Add the function call instruction
										ir_.addInstruction(IrOpcode::FunctionCall, std::move(call_operands), binaryOperatorNode.get_token());

										// Return the LHS value as the result (operator= returns *this)
										return { lhs_type.type(), static_cast<int>(lhs_type.size_in_bits()), ret_var };
									}
								}
							}
						}
					}
				}
			}
		}

		// Special handling for function pointer assignment
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
				std::string_view lhs_name = lhs_ident.name();

				// Look up the LHS in the symbol table
				const std::optional<ASTNode> lhs_symbol = symbol_table.lookup(lhs_name);
				if (lhs_symbol.has_value() && lhs_symbol->is<DeclarationNode>()) {
					const auto& lhs_decl = lhs_symbol->as<DeclarationNode>();
					const auto& lhs_type = lhs_decl.type_node().as<TypeSpecifierNode>();

					// Check if LHS is a function pointer
					if (lhs_type.is_function_pointer()) {
						// This is a function pointer assignment
						// Generate IR for the RHS (which should be a function address)
						auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

						// Generate Assignment IR
						// Format: [result_var, lhs_type, lhs_size, lhs_value, rhs_type, rhs_size, rhs_value]
						TempVar result_var = var_counter.next();
						std::vector<IrOperand> assign_operands;
						assign_operands.emplace_back(result_var);  // result_var
						assign_operands.emplace_back(lhs_type.type());  // lhs_type
						assign_operands.emplace_back(static_cast<int>(lhs_type.size_in_bits()));  // lhs_size
						assign_operands.emplace_back(lhs_name);  // lhs_value
						assign_operands.emplace_back(rhsIrOperands[0]);  // rhs_type
						assign_operands.emplace_back(rhsIrOperands[1]);  // rhs_size
						assign_operands.emplace_back(rhsIrOperands[2]);  // rhs_value (TempVar with function address)
						ir_.addInstruction(IrOpcode::Assignment, std::move(assign_operands), binaryOperatorNode.get_token());

						// Return the result
						return { lhs_type.type(), static_cast<int>(lhs_type.size_in_bits()), result_var };
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

		// For comparison operations, return boolean type (1 bit)
		// For other operations, return the common type
		if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
			return { Type::Bool, 1, result_var };
		} else {
			// Return the result variable with its type and size
			return { commonType, get_type_size_bits(commonType), result_var };
		}
	}

	// Helper function to generate Microsoft Visual C++ mangled name for function calls
	// This matches the mangling scheme in ObjFileWriter::generateMangledName
	std::string generateMangledNameForCall(const std::string& name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& param_types) {
		// Special case: main function is never mangled
		if (name == "main") {
			return "main";
		}

		std::string mangled = "?";
		mangled += name;
		mangled += "@@";

		// Add calling convention and linkage (__cdecl)
		mangled += "YA";

		// Add return type code
		mangled += getTypeCodeForMangling(return_type);

		// Add parameter type codes
		for (const auto& param_type : param_types) {
			mangled += getTypeCodeForMangling(param_type);
		}

		// End marker
		mangled += "@Z";

		return mangled;
	}

	// Helper to get type code for mangling (matches ObjFileWriter::getTypeCode)
	std::string getTypeCodeForMangling(const TypeSpecifierNode& type_node) {
		std::string code;

		// Handle references - they're treated as pointers in MSVC mangling
		// References add one level of pointer indirection
		if (type_node.is_reference()) {
			code += "PE";  // Pointer prefix for reference
		}

		// Add pointer prefix for each level of indirection
		for (int i = 0; i < type_node.pointer_depth(); ++i) {
			code += "PE";  // Pointer prefix in MSVC mangling
		}

		// Add base type code
		switch (type_node.type()) {
			case Type::Void: code += "X"; break;
			case Type::Bool: code += "_N"; break;  // bool
			case Type::Char: code += "D"; break;   // char
			case Type::UnsignedChar: code += "E"; break;  // unsigned char
			case Type::Short: code += "F"; break;  // short
			case Type::UnsignedShort: code += "G"; break;  // unsigned short
			case Type::Int: code += "H"; break;    // int
			case Type::UnsignedInt: code += "I"; break;  // unsigned int
			case Type::Long: code += "J"; break;   // long
			case Type::UnsignedLong: code += "K"; break;  // unsigned long
			case Type::LongLong: code += "_J"; break;  // long long
			case Type::UnsignedLongLong: code += "_K"; break;  // unsigned long long
			case Type::Float: code += "M"; break;  // float
			case Type::Double: code += "N"; break;  // double
			case Type::LongDouble: code += "O"; break;  // long double
			default: code += "H"; break;  // Default to int
		}

		return code;
	}

	std::vector<IrOperand> generateFunctionCallIr(const FunctionCallNode& functionCallNode) {
		std::vector<IrOperand> irOperands;

		const auto& decl_node = functionCallNode.function_declaration();
		std::string_view func_name_view = decl_node.identifier_token().value();

		// Check if this is a function pointer call
		// Look up the identifier in the symbol table to see if it's a function pointer variable
		const std::optional<ASTNode> func_symbol = symbol_table.lookup(func_name_view);
		if (func_symbol.has_value() && func_symbol->is<DeclarationNode>()) {
			const auto& func_decl = func_symbol->as<DeclarationNode>();
			const auto& func_type = func_decl.type_node().as<TypeSpecifierNode>();

			// Check if this is a function pointer
			if (func_type.is_function_pointer()) {
				// This is an indirect call through a function pointer
				// Generate IndirectCall IR: [result_var, func_ptr_var, arg1, arg2, ...]
				TempVar ret_var = var_counter.next();
				irOperands.emplace_back(ret_var);
				irOperands.emplace_back(func_name_view);  // The function pointer variable

				// Generate IR for function arguments
				functionCallNode.arguments().visit([&](ASTNode argument) {
					auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
					irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
				});

				// Add the indirect call instruction
				ir_.addInstruction(IrOpcode::IndirectCall, std::move(irOperands), functionCallNode.called_from());

				// Return the result variable with the return type from the function signature
				if (func_type.has_function_signature()) {
					const auto& sig = func_type.function_signature();
					return { sig.return_type, 64, ret_var };  // 64 bits for return value
				} else {
					// Fallback to int if no signature
					return { Type::Int, 32, ret_var };
				}
			}
		}

		// Get the function declaration to extract parameter types for mangling
		std::string function_name = std::string(func_name_view);

		// Look up the function in the global symbol table to get all overloads
		// Use global_symbol_table_ if available, otherwise fall back to local symbol_table
		auto all_overloads = global_symbol_table_
			? global_symbol_table_->lookup_all(decl_node.identifier_token().value())
			: symbol_table.lookup_all(decl_node.identifier_token().value());

		// Find the matching overload by comparing the DeclarationNode address
		// This works because the FunctionCallNode holds a reference to the specific
		// DeclarationNode that was selected by overload resolution
		for (const auto& overload : all_overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode* overload_func_decl = &overload.as<FunctionDeclarationNode>();
				const DeclarationNode* overload_decl = &overload_func_decl->decl_node();
				if (overload_decl == &decl_node) {
					// Found the matching overload
					const auto& func_decl = *overload_func_decl;

					// Extract parameter types for mangling (including pointer information)
					std::vector<TypeSpecifierNode> param_types;
					for (const auto& param_node : func_decl.parameter_nodes()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
							param_types.push_back(param_type);
						}
					}

					// Get return type (including pointer information)
					const auto& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();

					// Generate the mangled name directly (unless C linkage)
					// This ensures we call the correct overload
					if (func_decl.linkage() != Linkage::C) {
						function_name = generateMangledNameForCall(function_name, return_type, param_types);
					}
					break;
				}
			}
		}

		// Always add the return variable and function name (mangled for overload resolution)
		TempVar ret_var = var_counter.next();
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(function_name);

		// Generate IR for function arguments
		functionCallNode.arguments().visit([&](ASTNode argument) {
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			// For variables, we need to add the type and size
			if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				const auto& decl_node = symbol->as<DeclarationNode>();
				const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

				// Check if this is an array - arrays decay to pointers when passed to functions
				if (decl_node.is_array()) {
					// For arrays, we need to pass the address of the first element
					// Create a temporary for the address
					TempVar addr_var = var_counter.next();

					// Generate AddressOf IR instruction to get the address of the array
					std::vector<IrOperand> addrOfOperands;
					addrOfOperands.emplace_back(addr_var);
					addrOfOperands.emplace_back(type_node.type());
					addrOfOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
					addrOfOperands.emplace_back(identifier.name());
					ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addrOfOperands), Token()));

					// Add the pointer (address) to the function call operands
					// For now, we use the element type with 64-bit size to indicate it's a pointer
					// TODO: Add proper pointer type support to the Type enum
					irOperands.emplace_back(type_node.type());  // Element type (e.g., Char for char[])
					irOperands.emplace_back(64);  // Pointer size is 64 bits on x64
					irOperands.emplace_back(addr_var);
				} else {
					// Regular variable - pass by value
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
					irOperands.emplace_back(identifier.name());
				}
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

		// Special case: Immediate lambda invocation [](){}()
		// Check if the object is a LambdaExpressionNode (either directly or wrapped in ExpressionNode)
		const LambdaExpressionNode* lambda_ptr = nullptr;

		if (object_node.is<LambdaExpressionNode>()) {
			// Lambda stored directly
			lambda_ptr = &object_node.as<LambdaExpressionNode>();
		} else if (object_node.is<ExpressionNode>()) {
			const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
			if (std::holds_alternative<LambdaExpressionNode>(object_expr)) {
				// Lambda wrapped in ExpressionNode
				lambda_ptr = &std::get<LambdaExpressionNode>(object_expr);
			}
		}

		if (lambda_ptr) {
			const LambdaExpressionNode& lambda = *lambda_ptr;

			// Get the lambda info
			std::string closure_type_name = lambda.generate_lambda_name();
			std::string invoke_name = closure_type_name + "_invoke";

			// Generate a direct function call to __invoke
			TempVar ret_var = var_counter.next();
			irOperands.emplace_back(ret_var);
			irOperands.emplace_back(invoke_name);

			// Add arguments
			memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
				const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
				auto argumentIrOperands = visitExpressionNode(arg_expr);
				if (std::holds_alternative<IdentifierNode>(arg_expr)) {
					const auto& identifier = std::get<IdentifierNode>(arg_expr);
					const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					const auto& decl_node = symbol->as<DeclarationNode>();
					const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
					irOperands.emplace_back(identifier.name());
				} else {
					irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
				}
			});

			// Add the function call instruction
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(irOperands), memberFunctionCallNode.called_from()));

			// Return the result
			// TODO: Get actual return type from lambda
			return { Type::Int, 32, ret_var };
		}

		// Regular member function call on an expression
		// Get the object's type
		std::string_view object_name;
		const DeclarationNode* object_decl = nullptr;
		TypeSpecifierNode object_type;

		// The object must be an ExpressionNode for regular member function calls
		if (!object_node.is<ExpressionNode>()) {
			assert(false && "Member function call object must be an ExpressionNode");
			return {};
		}

		const ExpressionNode& object_expr = object_node.as<ExpressionNode>();

		if (std::holds_alternative<IdentifierNode>(object_expr)) {
			const IdentifierNode& object_ident = std::get<IdentifierNode>(object_expr);
			object_name = object_ident.name();

			// Look up the object in the symbol table
			const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
			if (symbol.has_value() && symbol->is<DeclarationNode>()) {
				object_decl = &symbol->as<DeclarationNode>();
				object_type = object_decl->type_node().as<TypeSpecifierNode>();
			}
		} else if (std::holds_alternative<UnaryOperatorNode>(object_expr)) {
			// Handle dereference operator (from ptr->member transformation)
			const UnaryOperatorNode& unary_op = std::get<UnaryOperatorNode>(object_expr);
			if (unary_op.op() == "*") {
				// This is a dereference - get the pointer operand
				const ASTNode& operand_node = unary_op.get_operand();
				if (operand_node.is<ExpressionNode>()) {
					const ExpressionNode& operand_expr = operand_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(operand_expr)) {
						const IdentifierNode& ptr_ident = std::get<IdentifierNode>(operand_expr);
						object_name = ptr_ident.name();

						// Look up the pointer in the symbol table
						const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
						if (symbol.has_value() && symbol->is<DeclarationNode>()) {
							object_decl = &symbol->as<DeclarationNode>();
							// Get the pointer type and remove one level of indirection
							TypeSpecifierNode ptr_type = object_decl->type_node().as<TypeSpecifierNode>();
							if (ptr_type.pointer_levels().size() > 0) {
								object_type = ptr_type;
								object_type.remove_pointer_level();
							}
						}
					}
				}
			}
		}

		// For immediate lambda invocation, object_decl can be nullptr
		// In that case, we still need object_type to be set correctly

		// Verify this is a struct type
		if (object_type.type() != Type::Struct) {
			assert(false && "Member function call on non-struct type");
			return { Type::Int, 32, TempVar{0} };
		}

		// Get the function declaration directly from the node (no need to look it up)
		const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
		const DeclarationNode& func_decl_node = func_decl.decl_node();

		// Check if this is a virtual function call
		// Look up the struct type to check if the function is virtual
		bool is_virtual_call = false;
		int vtable_index = -1;

		size_t struct_type_index = object_type.type_index();
		const StructMemberFunction* called_member_func = nullptr;
		const StructTypeInfo* struct_info = nullptr;

		if (struct_type_index < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[struct_type_index];
			struct_info = type_info.getStructInfo();

			if (struct_info) {
				// Find the member function in the struct
				std::string_view func_name = func_decl_node.identifier_token().value();
				for (const auto& member_func : struct_info->member_functions) {
					if (member_func.name == func_name) {
						called_member_func = &member_func;
						if (member_func.is_virtual) {
							is_virtual_call = true;
							vtable_index = member_func.vtable_index;
						}
						break;
					}
				}
			}
		}

		// Check access control for member function calls
		if (called_member_func && struct_info) {
			const StructTypeInfo* current_context = getCurrentStructContext();
			std::string current_function = getCurrentFunctionName();
			if (!checkMemberFunctionAccess(called_member_func, struct_info, current_context, current_function)) {
				std::cerr << "Error: Cannot access ";
				if (called_member_func->access == AccessSpecifier::Private) {
					std::cerr << "private";
				} else if (called_member_func->access == AccessSpecifier::Protected) {
					std::cerr << "protected";
				}
				std::cerr << " member function '" << called_member_func->name << "' of '" << struct_info->name << "'";
				if (current_context) {
					std::cerr << " from '" << current_context->name << "'";
				}
				std::cerr << "\n";
				assert(false && "Access control violation");
				return { Type::Int, 32, TempVar{0} };
			}
		}

		TempVar ret_var = var_counter.next();

		if (is_virtual_call && vtable_index >= 0) {
			// Generate virtual function call
			// Format: [result_var, object_type, object_size, object_name, vtable_index, arg1_type, arg1_size, arg1_value, ...]
			irOperands.emplace_back(ret_var);
			irOperands.emplace_back(object_type.type());
			irOperands.emplace_back(static_cast<int>(object_type.size_in_bits()));
			irOperands.emplace_back(object_name);
			irOperands.emplace_back(vtable_index);

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

			// Add the virtual call instruction
			ir_.addInstruction(IrInstruction(IrOpcode::VirtualCall, std::move(irOperands), memberFunctionCallNode.called_from()));
		} else {
			// Generate regular (non-virtual) member function call
			irOperands.emplace_back(ret_var);

			// For member functions, include the struct name in the function name
			// This allows proper name mangling (e.g., "__lambda_0::operator()")
			if (func_decl.is_member_function() && !func_decl.parent_struct_name().empty()) {
				std::string_view struct_name = func_decl.parent_struct_name();
				std::string_view func_name = func_decl_node.identifier_token().value();

				// Reserve space to avoid reallocations
				std::string function_name;
				function_name.reserve(struct_name.size() + 2 + func_name.size());
				function_name += struct_name;
				function_name += "::";
				function_name += func_name;
				irOperands.emplace_back(std::move(function_name));
			} else {
				irOperands.emplace_back(std::string(func_decl_node.identifier_token().value()));
			}

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
		}

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

				// Verify this is a struct type (or a reference to a struct type)
				// References are automatically dereferenced for member access
				// Note: Type can be either Struct or UserDefined (for user-defined types like Point)
				if (object_type.type() != Type::Struct && object_type.type() != Type::UserDefined) {
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
					base_type_index = std::get<unsigned long long>(nested_result[3]);
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
			// Case 3: Pointer dereference (e.g., ptr->member, which is transformed to (*ptr).member)
			else if (std::holds_alternative<UnaryOperatorNode>(expr)) {
				const UnaryOperatorNode& unary_op = std::get<UnaryOperatorNode>(expr);

				// This should be a dereference operator (*)
				if (unary_op.op() != "*") {
					assert(false && "Member access on non-dereference unary operator");
					return {};
				}

				// Get the pointer operand
				const ASTNode& operand_node = unary_op.get_operand();
				if (!operand_node.is<ExpressionNode>()) {
					assert(false && "Dereference operand is not an expression");
					return {};
				}

				const ExpressionNode& operand_expr = operand_node.as<ExpressionNode>();
				if (!std::holds_alternative<IdentifierNode>(operand_expr)) {
					assert(false && "Dereference operand is not an identifier");
					return {};
				}

				const IdentifierNode& ptr_ident = std::get<IdentifierNode>(operand_expr);
				std::string_view ptr_name = ptr_ident.name();

				// Look up the pointer in the symbol table
				const std::optional<ASTNode> symbol = symbol_table.lookup(ptr_name);
				if (!symbol.has_value() || !symbol->is<DeclarationNode>()) {
					assert(false && "Pointer not found in symbol table");
					return {};
				}

				const DeclarationNode& ptr_decl = symbol->as<DeclarationNode>();
				const TypeSpecifierNode& ptr_type = ptr_decl.type_node().as<TypeSpecifierNode>();

				// Verify this is a pointer to a struct type
				if (ptr_type.pointer_depth() == 0) {
					assert(false && "Member access on non-pointer type");
					return {};
				}

				if (ptr_type.type() != Type::Struct && ptr_type.type() != Type::UserDefined) {
					assert(false && "Member access on pointer to non-struct type");
					return {};
				}

				base_object = ptr_name;
				base_type = ptr_type.type();
				base_type_index = ptr_type.type_index();
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

		// First try to find by type_index
		if (base_type_index < gTypeInfo.size()) {
			const TypeInfo& ti = gTypeInfo[base_type_index];
			if (ti.type_ == Type::Struct && ti.getStructInfo()) {
				type_info = &ti;
			}
		}

		// If not found by index, search through all type info entries
		// This handles cases where type_index might not be set correctly
		if (!type_info) {
			for (const auto& ti : gTypeInfo) {
				if (ti.type_index_ == base_type_index && ti.type_ == Type::Struct && ti.getStructInfo()) {
					type_info = &ti;
					break;
				}
			}
		}

		// If still not found, try looking up by type_index in gTypeInfo directly
		// This handles cases where the type_index is valid but the lookup above failed
		if (!type_info && base_type_index > 0 && base_type_index < gTypeInfo.size()) {
			const TypeInfo& ti = gTypeInfo[base_type_index];
			if (ti.type_ == Type::Struct && ti.getStructInfo()) {
				type_info = &ti;
			}
		}

		if (!type_info || !type_info->getStructInfo()) {
			std::cerr << "Error: Struct type info not found for type_index=" << base_type_index << "\n";
			if (std::holds_alternative<std::string_view>(base_object)) {
				std::cerr << "  Object name: " << std::get<std::string_view>(base_object) << "\n";
			}
			std::cerr << "  Available struct types in gTypeInfo:\n";
			for (const auto& ti : gTypeInfo) {
				if (ti.type_ == Type::Struct && ti.getStructInfo()) {
					std::cerr << "    - " << ti.name_ << " (type_index=" << ti.type_index_ << ")\n";
				}
			}
			std::cerr << "  Available types in gTypesByName:\n";
			for (const auto& [name, ti] : gTypesByName) {
				if (ti->type_ == Type::Struct) {
					std::cerr << "    - " << name << " (type_index=" << ti->type_index_ << ")\n";
				}
			}
			assert(false && "Struct type info not found");
			return {};
		}

		const StructTypeInfo* struct_info = type_info->getStructInfo();
		// Use recursive lookup to find members in base classes as well
		const StructMember* member = struct_info->findMemberRecursive(std::string(member_name));

		if (!member) {
			std::cerr << "Error: Member '" << member_name << "' not found in struct '" << type_info->name_ << "' (type_index=" << type_info->type_index_ << ")\n";
			std::cerr << "  Available members:\n";
			for (const auto& m : struct_info->members) {
				std::cerr << "    - " << m.name << " (type=" << static_cast<int>(m.type) << ", offset=" << m.offset << ")\n";
			}
			assert(false && "Member not found in struct or base classes");
			return {};
		}

		// Check access control
		const StructTypeInfo* current_context = getCurrentStructContext();
		std::string current_function = getCurrentFunctionName();
		if (!checkMemberAccess(member, struct_info, current_context, nullptr, current_function)) {
			std::cerr << "Error: Cannot access ";
			if (member->access == AccessSpecifier::Private) {
				std::cerr << "private";
			} else if (member->access == AccessSpecifier::Protected) {
				std::cerr << "protected";
			}
			std::cerr << " member '" << member_name << "' of '" << struct_info->name << "'";
			if (current_context) {
				std::cerr << " from '" << current_context->name << "'";
			}
			std::cerr << "\n";
			assert(false && "Access control violation");
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

		// Return the result variable with its type, size, and optionally type_index
		// For struct types, we need to include type_index for nested member access (e.g., obj.inner.member)
		// For primitive types, we only return 3 operands to maintain compatibility with binary operators
		if (member->type == Type::Struct) {
			// Format: [type, size_bits, temp_var, type_index]
			return { member->type, static_cast<int>(member->size * 8), result_var, static_cast<unsigned long long>(member->type_index) };
		} else {
			// Format: [type, size_bits, temp_var]
			return { member->type, static_cast<int>(member->size * 8), result_var };
		}
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

	std::vector<IrOperand> generateNewExpressionIr(const NewExpressionNode& newExpr) {
		const TypeSpecifierNode& type_spec = newExpr.type_node().as<TypeSpecifierNode>();
		Type type = type_spec.type();
		int size_in_bits = static_cast<int>(type_spec.size_in_bits());
		int pointer_depth = static_cast<int>(type_spec.pointer_depth());

		// Create a temporary variable for the result (pointer to allocated memory)
		TempVar result_var = var_counter.next();

		// Check if this is placement new
		if (newExpr.placement_address().has_value()) {
			// Placement new: new (address) Type or new (address) Type(args)
			// Evaluate the placement address expression
			auto address_operands = visitExpressionNode(newExpr.placement_address()->as<ExpressionNode>());

			// Format: [result_var, type, size_in_bytes, pointer_depth, address_operand]
			std::vector<IrOperand> operands;
			operands.emplace_back(result_var);
			operands.emplace_back(type);
			operands.emplace_back(size_in_bits / 8);  // Convert bits to bytes
			operands.emplace_back(pointer_depth);
			operands.emplace_back(address_operands[2]);  // placement address (TempVar, identifier, or constant)

			ir_.addInstruction(IrOpcode::PlacementNew, std::move(operands), Token());

			// If this is a struct type with a constructor, generate constructor call
			if (type == Type::Struct) {
				TypeIndex type_index = type_spec.type_index();
				if (type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_index];
					if (type_info.struct_info_) {
						// Check if this is an abstract class
						if (type_info.struct_info_->is_abstract) {
							std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name_ << "'\n";
							assert(false && "Cannot instantiate abstract class");
						}

						if (type_info.struct_info_->hasConstructor()) {
							// Generate constructor call on the placement address
							std::vector<IrOperand> ctor_operands;
							ctor_operands.emplace_back(type_info.name_);  // Struct name
							ctor_operands.emplace_back(result_var);       // Pointer to object (placement address)

							// Add constructor arguments
							const auto& ctor_args = newExpr.constructor_args();
							for (size_t i = 0; i < ctor_args.size(); ++i) {
								auto arg_operands = visitExpressionNode(ctor_args[i].as<ExpressionNode>());
								ctor_operands.insert(ctor_operands.end(), arg_operands.begin(), arg_operands.end());
							}

							ir_.addInstruction(IrOpcode::ConstructorCall, std::move(ctor_operands), Token());
						}
					}
				}
			}
		} else if (newExpr.is_array()) {
			// Array allocation: new Type[size]
			// Evaluate the size expression
			auto size_operands = visitExpressionNode(newExpr.size_expr()->as<ExpressionNode>());

			// Format: [result_var, type, size_in_bytes, pointer_depth, count_operand]
			// count_operand can be either TempVar or a constant value (int/unsigned long long)
			std::vector<IrOperand> operands;
			operands.emplace_back(result_var);
			operands.emplace_back(type);
			operands.emplace_back(size_in_bits / 8);  // Convert bits to bytes
			operands.emplace_back(pointer_depth);
			operands.emplace_back(size_operands[2]);  // size expression result (TempVar or constant)

			ir_.addInstruction(IrOpcode::HeapAllocArray, std::move(operands), Token());
		} else {
			// Single object allocation: new Type or new Type(args)
			// Format: [result_var, type, size_in_bytes, pointer_depth]
			std::vector<IrOperand> operands;
			operands.emplace_back(result_var);
			operands.emplace_back(type);
			operands.emplace_back(size_in_bits / 8);  // Convert bits to bytes
			operands.emplace_back(pointer_depth);

			ir_.addInstruction(IrOpcode::HeapAlloc, std::move(operands), Token());

			// If this is a struct type with a constructor, generate constructor call
			if (type == Type::Struct) {
				TypeIndex type_index = type_spec.type_index();
				if (type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_index];
					if (type_info.struct_info_) {
						// Check if this is an abstract class
						if (type_info.struct_info_->is_abstract) {
							std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name_ << "'\n";
							assert(false && "Cannot instantiate abstract class");
						}

						if (type_info.struct_info_->hasConstructor()) {
							// Generate constructor call on the newly allocated object
							std::vector<IrOperand> ctor_operands;
							ctor_operands.emplace_back(type_info.name_);  // Struct name
							ctor_operands.emplace_back(result_var);       // Pointer to object

							// Add constructor arguments
							const auto& ctor_args = newExpr.constructor_args();
							for (size_t i = 0; i < ctor_args.size(); ++i) {
								auto arg_operands = visitExpressionNode(ctor_args[i].as<ExpressionNode>());
								ctor_operands.insert(ctor_operands.end(), arg_operands.begin(), arg_operands.end());
							}

							ir_.addInstruction(IrOpcode::ConstructorCall, std::move(ctor_operands), Token());
						}
					}
				}
			}
		}

		// Return pointer to allocated memory
		// The result is a pointer, so we return it with pointer_depth + 1
		return { type, size_in_bits, result_var };
	}

	std::vector<IrOperand> generateDeleteExpressionIr(const DeleteExpressionNode& deleteExpr) {
		// Evaluate the expression to get the pointer to delete
		auto ptr_operands = visitExpressionNode(deleteExpr.expr().as<ExpressionNode>());

		// Get the pointer type
		Type ptr_type = std::get<Type>(ptr_operands[0]);

		// Check if we need to call destructor (for struct types)
		if (ptr_type == Type::Struct && !deleteExpr.is_array()) {
			// For single object deletion, call destructor before freeing
			// Note: For array deletion, we'd need to track the array size and call destructors for each element
			// This is a simplified implementation

			// We need the type index to get struct info
			// For now, we'll skip destructor calls for delete (can be enhanced later)
			// TODO: Track type information through pointer types to enable destructor calls
		}

		// Generate the appropriate free instruction
		// Pass the pointer operand directly (can be TempVar or std::string_view)
		std::vector<IrOperand> operands;
		operands.emplace_back(ptr_operands[2]);  // The pointer value (TempVar or identifier)

		if (deleteExpr.is_array()) {
			ir_.addInstruction(IrOpcode::HeapFreeArray, std::move(operands), Token());
		} else {
			ir_.addInstruction(IrOpcode::HeapFree, std::move(operands), Token());
		}

		// delete is a statement, not an expression, so return empty
		return {};
	}

	std::vector<IrOperand> generateStaticCastIr(const StaticCastNode& staticCastNode) {
		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(staticCastNode.expr().as<ExpressionNode>());

		// Get the target type from the type specifier
		const auto& target_type_node = staticCastNode.target_type().as<TypeSpecifierNode>();
		Type target_type = target_type_node.type();
		int target_size = static_cast<int>(target_type_node.size_in_bits());

		// Get the source type
		Type source_type = std::get<Type>(expr_operands[0]);
		int source_size = std::get<int>(expr_operands[1]);

		// For now, static_cast just changes the type metadata
		// The actual value remains the same (this works for enum to int, int to enum, etc.)
		// More complex casts (e.g., pointer casts, numeric conversions) would need additional logic

		// If the types are the same, just return the expression as-is
		if (source_type == target_type && source_size == target_size) {
			return expr_operands;
		}

		// For enum to int or int to enum, we can just change the type
		if ((source_type == Type::Enum && target_type == Type::Int) ||
		    (source_type == Type::Int && target_type == Type::Enum) ||
		    (source_type == Type::Enum && target_type == Type::UnsignedInt) ||
		    (source_type == Type::UnsignedInt && target_type == Type::Enum)) {
			// Return the value with the new type
			return { target_type, target_size, expr_operands[2] };
		}

		// For numeric conversions, we might need to generate a conversion instruction
		// For now, just change the type metadata (works for most cases)
		return { target_type, target_size, expr_operands[2] };
	}

	std::vector<IrOperand> generateTypeidIr(const TypeidNode& typeidNode) {
		// typeid returns a reference to const std::type_info
		// For polymorphic types, we need to get RTTI from the vtable
		// For non-polymorphic types, we return a compile-time constant

		TempVar result_temp = var_counter.next();

		if (typeidNode.is_type()) {
			// typeid(Type) - compile-time constant
			const auto& type_node = typeidNode.operand().as<TypeSpecifierNode>();

			// Get type information
			std::string type_name;
			if (type_node.type() == Type::Struct) {
				TypeIndex type_idx = type_node.type_index();
				if (type_idx < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_idx];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						type_name = struct_info->name;
					}
				}
			}

			// Generate IR to get compile-time type_info
			std::vector<IrOperand> operands;
			operands.emplace_back(result_temp);
			operands.emplace_back(type_name);  // Type name for RTTI lookup
			operands.emplace_back(true);       // is_type = true
			ir_.addInstruction(IrOpcode::Typeid, std::move(operands), typeidNode.typeid_token());
		}
		else {
			// typeid(expr) - may need runtime lookup for polymorphic types
			auto expr_operands = visitExpressionNode(typeidNode.operand().as<ExpressionNode>());

			std::vector<IrOperand> operands;
			operands.emplace_back(result_temp);
			operands.emplace_back(expr_operands[2]);  // Expression result
			operands.emplace_back(false);             // is_type = false
			ir_.addInstruction(IrOpcode::Typeid, std::move(operands), typeidNode.typeid_token());
		}

		// Return pointer to type_info (64-bit pointer)
		// Use void* type for now (Type::Void with pointer depth)
		return { Type::Void, 64, result_temp };
	}

	std::vector<IrOperand> generateDynamicCastIr(const DynamicCastNode& dynamicCastNode) {
		// dynamic_cast<Type>(expr) performs runtime type checking
		// Returns nullptr (for pointers) or throws bad_cast (for references) on failure

		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(dynamicCastNode.expr().as<ExpressionNode>());

		// Get the target type
		const auto& target_type_node = dynamicCastNode.target_type().as<TypeSpecifierNode>();

		// Get target struct type information
		std::string target_type_name;
		if (target_type_node.type() == Type::Struct) {
			TypeIndex type_idx = target_type_node.type_index();
			if (type_idx < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_idx];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					target_type_name = struct_info->name;
				}
			}
		}

		TempVar result_temp = var_counter.next();

		// Generate dynamic_cast IR
		std::vector<IrOperand> operands;
		operands.emplace_back(result_temp);           // Result temporary
		operands.emplace_back(expr_operands[2]);      // Source pointer
		operands.emplace_back(target_type_name);      // Target type name
		operands.emplace_back(target_type_node.is_reference());  // Is reference cast?
		ir_.addInstruction(IrOpcode::DynamicCast, std::move(operands), dynamicCastNode.cast_token());

		// Return the casted pointer/reference
		Type result_type = target_type_node.type();
		int result_size = static_cast<int>(target_type_node.size_in_bits());
		return { result_type, result_size, result_temp };
	}

	// Structure to track variables that need destructors called
	struct ScopeVariableInfo {
		std::string variable_name;
		std::string struct_name;
	};

	// Stack of scopes, each containing variables that need destructors
	std::vector<std::vector<ScopeVariableInfo>> scope_stack_;

	void enterScope() {
		scope_stack_.push_back({});
	}

	void exitScope() {
		if (!scope_stack_.empty()) {
			// Generate destructor calls for all variables in this scope (in reverse order)
			const auto& scope_vars = scope_stack_.back();
			for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
				// Generate destructor call
				std::vector<IrOperand> dtor_operands;
				dtor_operands.emplace_back(it->struct_name);  // Struct name
				dtor_operands.emplace_back(it->variable_name); // Object variable name
				ir_.addInstruction(IrOpcode::DestructorCall, std::move(dtor_operands), Token());
			}
			scope_stack_.pop_back();
		}
	}

	void registerVariableWithDestructor(const std::string& var_name, const std::string& struct_name) {
		if (!scope_stack_.empty()) {
			scope_stack_.back().push_back({var_name, struct_name});
		}
	}

	std::vector<IrOperand> generateLambdaExpressionIr(const LambdaExpressionNode& lambda) {
		// Collect lambda information for deferred generation
		// Following Clang's approach: generate closure class, operator(), __invoke, and conversion operator

		LambdaInfo info;
		info.lambda_id = lambda.lambda_id();
		info.closure_type_name = lambda.generate_lambda_name();

		// Build derived names with reserve to avoid reallocations
		size_t base_len = info.closure_type_name.size();

		// Build operator_call_name: closure_type_name + "_operator_call"
		info.operator_call_name.reserve(base_len + 14);
		info.operator_call_name = info.closure_type_name;
		info.operator_call_name += "_operator_call";

		// Build invoke_name: closure_type_name + "_invoke"
		info.invoke_name.reserve(base_len + 7);
		info.invoke_name = info.closure_type_name;
		info.invoke_name += "_invoke";

		// Build conversion_op_name: closure_type_name + "_conversion"
		info.conversion_op_name.reserve(base_len + 11);
		info.conversion_op_name = info.closure_type_name;
		info.conversion_op_name += "_conversion";

		info.lambda_token = lambda.lambda_token();

		// Copy lambda body and captures (we need them later)
		info.lambda_body = lambda.body();
		info.captures = lambda.captures();

		// Collect captured variable declarations from current scope
		for (const auto& capture : lambda.captures()) {
			if (capture.is_capture_all()) {
				// TODO: Handle capture-all [=] and [&]
				continue;
			}

			// Look up the captured variable in the current scope
			std::string_view var_name = capture.identifier_name();
			std::optional<ASTNode> var_symbol = symbol_table.lookup(var_name);

			if (var_symbol.has_value()) {
				// Store the variable declaration for later use
				info.captured_var_decls.push_back(*var_symbol);
			} else {
				std::cerr << "Warning: Captured variable '" << var_name << "' not found in scope during lambda generation\n";
			}
		}

		// Determine return type
		info.return_type = Type::Int;  // Default to int
		info.return_size = 32;
		if (lambda.return_type().has_value()) {
			const auto& ret_type_node = lambda.return_type()->as<TypeSpecifierNode>();
			info.return_type = ret_type_node.type();
			info.return_size = ret_type_node.size_in_bits();
		}

		// Collect parameters
		for (const auto& param : lambda.parameters()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				info.parameters.emplace_back(
					param_type.type(),
					param_type.size_in_bits(),
					static_cast<int>(param_type.pointer_levels().size()),
					std::string(param_decl.identifier_token().value())
				);
				// Also store the actual parameter node for symbol table
				info.parameter_nodes.push_back(param);
			}
		}

		// Look up the closure type (registered during parsing) BEFORE moving info
		auto type_it = gTypesByName.find(info.closure_type_name);
		if (type_it == gTypesByName.end()) {
			// Error: closure type not found
			TempVar dummy = var_counter.next();
			return {Type::Int, 32, dummy};
		}

		const TypeInfo* closure_type = type_it->second;

		// Store lambda info for later generation (after we've used closure_type_name)
		collected_lambdas_.push_back(std::move(info));
		const LambdaInfo& lambda_info = collected_lambdas_.back();

		// Create a closure variable name
		std::string closure_var_name = "__closure_" + std::to_string(lambda_info.lambda_id);

		// Declare the closure variable
		// Format: [type, size, name, custom_alignment]
		std::vector<IrOperand> decl_operands;
		decl_operands.emplace_back(Type::Struct);
		decl_operands.emplace_back(static_cast<int>(closure_type->getStructInfo()->total_size * 8));
		decl_operands.emplace_back(std::string(closure_var_name));  // Use std::string
		decl_operands.emplace_back(static_cast<unsigned long long>(0));  // custom_alignment
		ir_.addInstruction(IrOpcode::VariableDecl, std::move(decl_operands), lambda.lambda_token());

		// Now initialize captured members
		// The key insight: we need to generate the initialization code that will be
		// executed during IR conversion, after the variable has been added to scope
		if (!lambda_info.captures.empty()) {
			const StructTypeInfo* struct_info = closure_type->getStructInfo();
			if (struct_info) {
				size_t capture_index = 0;
				for (const auto& capture : lambda_info.captures) {
					if (capture.is_capture_all()) {
						continue;
					}

					std::string var_name(capture.identifier_name());
					const StructMember* member = struct_info->findMember(var_name);

					if (member && capture_index < lambda_info.captured_var_decls.size()) {
						if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
							// By-reference: store the address of the variable
							// Get the original variable type from captured_var_decls
							const ASTNode& var_decl = lambda_info.captured_var_decls[capture_index];
							if (!var_decl.is<DeclarationNode>()) {
								capture_index++;
								continue;
							}
							const auto& decl = var_decl.as<DeclarationNode>();
							const auto& orig_type = decl.type_node().as<TypeSpecifierNode>();

							// Generate AddressOf: [result_var, type, size, operand]
							TempVar addr_temp = var_counter.next();
							std::vector<IrOperand> addr_operands;
							addr_operands.emplace_back(addr_temp);
							addr_operands.emplace_back(orig_type.type());
							addr_operands.emplace_back(static_cast<int>(orig_type.size_in_bits()));
							addr_operands.emplace_back(std::string(var_name));  // Use std::string to avoid dangling reference
							ir_.addInstruction(IrOpcode::AddressOf, std::move(addr_operands), lambda.lambda_token());

							// Store the address in the closure member
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							std::vector<IrOperand> store_operands;
							store_operands.emplace_back(member->type);
							store_operands.emplace_back(static_cast<int>(member->size * 8));
							store_operands.emplace_back(std::string(closure_var_name));  // Use std::string
							store_operands.emplace_back(std::string(member->name));  // Use std::string
							store_operands.emplace_back(static_cast<int>(member->offset));
							store_operands.emplace_back(addr_temp);
							ir_.addInstruction(IrOpcode::MemberStore, std::move(store_operands), lambda.lambda_token());
						} else {
							// By-value: copy the value
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							std::vector<IrOperand> store_operands;
							store_operands.emplace_back(member->type);
							store_operands.emplace_back(static_cast<int>(member->size * 8));
							store_operands.emplace_back(std::string(closure_var_name));  // Use std::string
							store_operands.emplace_back(std::string(member->name));  // Use std::string
							store_operands.emplace_back(static_cast<int>(member->offset));
							store_operands.emplace_back(std::string(var_name));  // Use std::string
							ir_.addInstruction(IrOpcode::MemberStore, std::move(store_operands), lambda.lambda_token());
						}

						capture_index++;
					}
				}
			}
		}

		// Return a temp var representing the lambda closure
		TempVar lambda_var = var_counter.next();
		return {Type::Struct, 8, closure_type->type_index_, lambda_var};
	}



	// Generate all functions for a lambda (following Clang's approach)
	void generateLambdaFunctions(const LambdaInfo& lambda_info) {
		// Following Clang's approach, we generate:
		// 1. operator() - member function with lambda body
		// 2. __invoke - static function that can be used as function pointer (only for non-capturing lambdas)
		// 3. conversion operator - returns pointer to __invoke (only for non-capturing lambdas)

		// Generate operator() member function
		generateLambdaOperatorCallFunction(lambda_info);

		// Generate __invoke static function only for non-capturing lambdas
		// Capturing lambdas can't be converted to function pointers
		if (lambda_info.captures.empty()) {
			generateLambdaInvokeFunction(lambda_info);
		}
	}

	// Generate the operator() member function for a lambda
	void generateLambdaOperatorCallFunction(const LambdaInfo& lambda_info) {
		// Generate function declaration for operator()
		std::vector<IrOperand> funcDeclOperands;
		funcDeclOperands.emplace_back(lambda_info.return_type);
		funcDeclOperands.emplace_back(lambda_info.return_size);
		funcDeclOperands.emplace_back(0);  // pointer depth
		funcDeclOperands.emplace_back(std::string_view("operator()"));
		funcDeclOperands.emplace_back(std::string_view(lambda_info.closure_type_name));  // struct name (member function)
		funcDeclOperands.emplace_back(static_cast<int>(Linkage::None));  // C++ linkage

		// Add parameters
		for (const auto& [type, size, pointer_depth, name] : lambda_info.parameters) {
			funcDeclOperands.emplace_back(type);
			funcDeclOperands.emplace_back(size);
			funcDeclOperands.emplace_back(pointer_depth);
			funcDeclOperands.emplace_back(std::string_view(name));
		}

		ir_.addInstruction(IrOpcode::FunctionDecl, std::move(funcDeclOperands), lambda_info.lambda_token);

		symbol_table.enter_scope(ScopeType::Function);

		// Set lambda context for captured variable access
		current_lambda_closure_type_ = lambda_info.closure_type_name;
		current_lambda_captures_.clear();
		current_lambda_capture_kinds_.clear();
		current_lambda_capture_types_.clear();

		size_t capture_index = 0;
		for (const auto& capture : lambda_info.captures) {
			if (!capture.is_capture_all()) {
				std::string var_name(capture.identifier_name());
				current_lambda_captures_.insert(var_name);
				current_lambda_capture_kinds_[var_name] = capture.kind();

				// Store the original type of the captured variable
				if (capture_index < lambda_info.captured_var_decls.size()) {
					const ASTNode& var_decl = lambda_info.captured_var_decls[capture_index];
					if (var_decl.is<DeclarationNode>()) {
						const auto& decl = var_decl.as<DeclarationNode>();
						current_lambda_capture_types_[var_name] = decl.type_node().as<TypeSpecifierNode>();
					}
				}
				capture_index++;
			}
		}

		// Add lambda parameters to symbol table
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				symbol_table.insert(param_decl.identifier_token().value(), param_node);
			}
		}

		// Add captured variables to symbol table
		// These will be accessed through member access (this->x)
		addCapturedVariablesToSymbolTable(lambda_info.captures, lambda_info.captured_var_decls);

		// Generate the lambda body
		if (lambda_info.lambda_body.is<BlockNode>()) {
			const auto& body = lambda_info.lambda_body.as<BlockNode>();
			body.get_statements().visit([&](const ASTNode& stmt) {
				visit(stmt);
			});
		}

		// Clear lambda context
		current_lambda_closure_type_.clear();
		current_lambda_captures_.clear();
		current_lambda_capture_kinds_.clear();
		current_lambda_capture_types_.clear();

		symbol_table.exit_scope();
	}

	// Generate the __invoke static function for a lambda
	void generateLambdaInvokeFunction(const LambdaInfo& lambda_info) {
		// Generate function declaration for __invoke
		std::vector<IrOperand> funcDeclOperands;
		funcDeclOperands.emplace_back(lambda_info.return_type);
		funcDeclOperands.emplace_back(lambda_info.return_size);
		funcDeclOperands.emplace_back(0);  // pointer depth
		funcDeclOperands.emplace_back(std::string_view(lambda_info.invoke_name));
		funcDeclOperands.emplace_back(std::string_view(""));  // no struct name (static function)
		funcDeclOperands.emplace_back(static_cast<int>(Linkage::None));  // C++ linkage

		// Add parameters
		for (const auto& [type, size, pointer_depth, name] : lambda_info.parameters) {
			funcDeclOperands.emplace_back(type);
			funcDeclOperands.emplace_back(size);
			funcDeclOperands.emplace_back(pointer_depth);
			funcDeclOperands.emplace_back(std::string_view(name));
		}

		ir_.addInstruction(IrOpcode::FunctionDecl, std::move(funcDeclOperands), lambda_info.lambda_token);

		symbol_table.enter_scope(ScopeType::Function);

		// Add lambda parameters to symbol table
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				symbol_table.insert(param_decl.identifier_token().value(), param_node);
			}
		}

		// Add captured variables to symbol table
		addCapturedVariablesToSymbolTable(lambda_info.captures, lambda_info.captured_var_decls);

		// Generate the lambda body
		if (lambda_info.lambda_body.is<BlockNode>()) {
			const auto& body = lambda_info.lambda_body.as<BlockNode>();
			body.get_statements().visit([&](const ASTNode& stmt) {
				visit(stmt);
			});
		}

		symbol_table.exit_scope();
	}

	// Helper function to add captured variables to symbol table
	void addCapturedVariablesToSymbolTable(const std::vector<LambdaCaptureNode>& captures,
	                                        const std::vector<ASTNode>& captured_var_decls) {
		// Add captured variables to the symbol table
		// We use the stored declarations from when the lambda was created
		size_t capture_index = 0;
		for (const auto& capture : captures) {
			if (capture.is_capture_all()) {
				// TODO: Handle capture-all [=] and [&]
				continue;
			}

			if (capture_index >= captured_var_decls.size()) {
				std::cerr << "Error: Mismatch between captures and captured variable declarations\n";
				break;
			}

			// Get the stored variable declaration
			const ASTNode& var_decl = captured_var_decls[capture_index];
			std::string_view var_name = capture.identifier_name();

			// Add the captured variable to the current scope
			// For by-value captures, we create a copy
			// For by-reference captures, we use the original
			symbol_table.insert(var_name, var_decl);

			capture_index++;
		}
	}

	Ir ir_;
	TempVar var_counter{ 0 };
	SymbolTable symbol_table;
	SymbolTable* global_symbol_table_;  // Reference to the global symbol table for function overload lookup
	CompileContext* context_;  // Reference to compile context for flags

	// Current function name (for mangling static local variables)
	std::string current_function_name_;

	// Static local variable information
	struct StaticLocalInfo {
		std::string mangled_name;
		Type type;
		int size_in_bits;
	};

	// Map from local static variable name to info
	// Key: local variable name, Value: static local info
	std::unordered_map<std::string, StaticLocalInfo> static_local_names_;

	// Collected lambdas for deferred generation
	std::vector<LambdaInfo> collected_lambdas_;

	// Current lambda context (for tracking captured variables)
	// When generating lambda body, this contains the closure type name
	std::string current_lambda_closure_type_;
	std::unordered_set<std::string> current_lambda_captures_;
	std::unordered_map<std::string, LambdaCaptureNode::CaptureKind> current_lambda_capture_kinds_;
	std::unordered_map<std::string, TypeSpecifierNode> current_lambda_capture_types_;
};
