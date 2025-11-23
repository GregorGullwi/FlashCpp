#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include "TemplateRegistry.h"
#include "ChunkedString.h"
#include "NameMangling.h"
#include "ConstExprEvaluator.h"
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
	AstToIr(SymbolTable& global_symbol_table, CompileContext& context, Parser& parser)
		: global_symbol_table_(&global_symbol_table), context_(&context), parser_(&parser) {
		// Generate static member declarations for template classes before processing AST
		generateStaticMemberDeclarations();
	}

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
		else if (node.is<TemplateFunctionDeclarationNode>()) {
			// Template declarations don't generate code yet - they're stored for later instantiation
			// TODO: Implement template instantiation in Phase 2
			return;
		}
		else if (node.is<TemplateClassDeclarationNode>()) {
			// Template class declarations don't generate code yet - they're stored for later instantiation
			// TODO: Implement class template instantiation in Phase 6
			return;
		}
		else if (node.is<TemplateAliasNode>()) {
			// Template alias declarations don't generate code - they're compile-time type substitutions
			// The type is resolved during parsing when the alias is used
			return;
		}
		else if (node.is<TemplateVariableDeclarationNode>()) {
			// Template variable declarations don't generate code yet - they're stored for later instantiation
			// Instantiations are generated when the template is used with explicit template arguments
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
	
	// Generate all collected template instantiations (must be called after visiting all nodes)
	void generateCollectedTemplateInstantiations() {
		for (const auto& inst_info : collected_template_instantiations_) {
			generateTemplateInstantiation(inst_info);
		}
	}

	// Reserve space for IR instructions (optimization)
	void reserveInstructions(size_t capacity) {
		ir_.reserve(capacity);
	}

	// Generate GlobalVariableDecl for all static members in all registered types
	// This is called at the beginning of IR generation to ensure all template
	// instantiation static members are emitted
	void generateStaticMemberDeclarations() {
		for (const auto& [type_name, type_info] : gTypesByName) {
			if (!type_info->isStruct()) {
				continue;
			}
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (!struct_info || struct_info->static_members.empty()) {
				continue;
			}
			
			for (const auto& static_member : struct_info->static_members) {
				GlobalVariableDeclOp op;
				op.type = static_member.type;
				op.size_in_bits = static_cast<int>(static_member.size * 8);
				op.var_name = StringBuilder().append(type_name).append("::").append(static_member.name).commit();

				// Check if static member has an initializer
				op.is_initialized = static_member.initializer.has_value();
				if (op.is_initialized) {
					// Evaluate the initializer expression
					auto init_operands = visitExpressionNode(static_member.initializer->as<ExpressionNode>());
					// Add only the initializer value (init_operands[2] is the value)
					if (init_operands.size() >= 3) {
						// Convert IrOperand to IrValue
						if (std::holds_alternative<unsigned long long>(init_operands[2])) {
							op.init_value = std::get<unsigned long long>(init_operands[2]);
						} else if (std::holds_alternative<double>(init_operands[2])) {
							op.init_value = std::get<double>(init_operands[2]);
						} else if (std::holds_alternative<TempVar>(init_operands[2])) {
							op.init_value = std::get<TempVar>(init_operands[2]);
						} else if (std::holds_alternative<std::string_view>(init_operands[2])) {
							op.init_value = std::get<std::string_view>(init_operands[2]);
						} else {
							// Fallback to zero if type not recognized
							op.init_value = 0ULL;
						}
					} else {
						// Fallback to zero if initializer evaluation failed
						op.init_value = 0ULL;
					}
				}
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), Token()));
			}
		}
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

	const DeclarationNode& requireDeclarationNode(const ASTNode& node, std::string_view context) const {
		try {
			return node.as<DeclarationNode>();
		} catch (...) {
			std::cerr << "BAD DeclarationNode cast in " << context
			          << ": type_name=" << node.type_name()
			          << " has_value=" << node.has_value() << "\n";
			throw;
		}
	}

	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		if (!node.get_definition().has_value()) {
			std::cerr << "DEBUG: Function has no definition, skipping\n";
			return;
		}

		// Reset the temporary variable counter for each new function
		// For member functions, reserve TempVar(1) for the implicit 'this' parameter
		var_counter = node.is_member_function() ? TempVar(2) : TempVar();

		// Set current function name for static local variable mangling
		const DeclarationNode& func_decl = node.decl_node();
		current_function_name_ = std::string(func_decl.identifier_token().value());
		
		// Set current function return type and size for type checking in return statements
		const TypeSpecifierNode& ret_type_spec = func_decl.type_node().as<TypeSpecifierNode>();
		current_function_return_type_ = ret_type_spec.type();
		current_function_return_size_ = static_cast<int>(ret_type_spec.size_in_bits());

		// Clear current_struct_name_ if this is not a member function
		// This prevents struct context from leaking into free functions
		if (!node.is_member_function()) {
			current_struct_name_.clear();
		}

		// DEBUG: Check what we're receiving
		const TypeSpecifierNode& debug_ret_type = func_decl.type_node().as<TypeSpecifierNode>();
		std::cerr << "\n===== CODEGEN visitFunctionDeclarationNode: " << func_decl.identifier_token().value() << " =====\n";
		std::cerr << "  return_type: " << (int)debug_ret_type.type() << " size: " << (int)debug_ret_type.size_in_bits() << " ptr_depth: " << debug_ret_type.pointer_depth() << "\n";
		std::cerr << "  is_member_function: " << node.is_member_function() << "\n";
		if (node.is_member_function()) {
			std::cerr << "  parent_struct_name: " << node.parent_struct_name() << "\n";
		}
		std::cerr << "  parameter_count: " << node.parameter_nodes().size() << "\n";
		for (size_t i = 0; i < node.parameter_nodes().size(); ++i) {
			const auto& param = node.parameter_nodes()[i];
			if (param.is<DeclarationNode>()) {
				const DeclarationNode& param_decl = param.as<DeclarationNode>();
				const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				std::cerr << "  param[" << i << "]: name='" << param_decl.identifier_token().value() 
						  << "' type=" << (int)param_type.type() 
						  << " size=" << (int)param_type.size_in_bits()
						  << " ptr_depth=" << param_type.pointer_depth()
						  << " base_cv=" << (int)param_type.cv_qualifier();
				for (size_t j = 0; j < param_type.pointer_levels().size(); ++j) {
					std::cerr << " ptr[" << j << "]_cv=" << (int)param_type.pointer_levels()[j].cv_qualifier;
				}
				std::cerr << "\n";
			}
		}
		std::cerr << "=====\n";

		// Clear static local names map for new function
		static_local_names_.clear();

		const TypeSpecifierNode& ret_type = func_decl.type_node().as<TypeSpecifierNode>();

		// Create function declaration with return type and name
		// Use FunctionDeclOp to store typed payload
		FunctionDeclOp func_decl_op;
		
		// Return type information
		func_decl_op.return_type = ret_type.type();
		func_decl_op.return_size_in_bits = static_cast<int>(ret_type.size_in_bits());
		func_decl_op.return_pointer_depth = ret_type.pointer_depth();
		
		// Function name
		func_decl_op.function_name = std::string(func_decl.identifier_token().value());

		// Add struct/class name for member functions
		// Use current_struct_name_ if set (for instantiated template specializations),
		// otherwise use the function node's parent_struct_name
		std::string_view struct_name_for_function;
		if (!current_struct_name_.empty()) {
			struct_name_for_function = current_struct_name_;
		} else if (node.is_member_function()) {
			struct_name_for_function = node.parent_struct_name();
		} else {
			struct_name_for_function = "";
		}
		func_decl_op.struct_name = std::string(struct_name_for_function);
		
		// Linkage and variadic flag
		func_decl_op.linkage = node.linkage();
		func_decl_op.is_variadic = node.is_variadic();

		// Generate mangled name now while we have full type information with CV-qualifiers
		// Extract parameter types for mangling
		std::vector<TypeSpecifierNode> param_types_for_mangling;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			param_types_for_mangling.push_back(param_decl.type_node().as<TypeSpecifierNode>());
		}
		
		std::string_view mangled_name = generateMangledNameForCall(
			std::string(func_decl.identifier_token().value()),
			ret_type,
			param_types_for_mangling,
			node.is_variadic(),
			node.is_member_function() ? std::string(struct_name_for_function) : ""
		);
		func_decl_op.mangled_name = std::string(mangled_name);

		// Add parameters to function declaration
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			FunctionParam param_info;
			param_info.type = param_type.type();
			param_info.size_in_bits = static_cast<int>(param_type.size_in_bits());
			
			// References are treated like pointers in the IR (they're both addresses)
			int pointer_depth = static_cast<int>(param_type.pointer_depth());
			if (param_type.is_reference()) {
				pointer_depth += 1;  // Add 1 for reference (ABI treats it as an additional pointer level)
			}
			param_info.pointer_depth = pointer_depth;
			param_info.name = std::string(param_decl.identifier_token().value());
			param_info.is_reference = param_type.is_reference();
			param_info.is_rvalue_reference = param_type.is_rvalue_reference();
			param_info.cv_qualifier = param_type.cv_qualifier();

			func_decl_op.parameters.push_back(std::move(param_info));
			var_counter.next();
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), func_decl.identifier_token()));

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
// 				bool is_move_assignment = false;
// 				if (node.parameter_nodes().size() == 1) {
// 					const auto& param_decl = node.parameter_nodes()[0].as<DeclarationNode>();
// 					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
// 					if (param_type.is_rvalue_reference()) {
// 						is_move_assignment = true;
// 					}
// 				}

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
							TempVar member_value = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = member_value;
							member_load.result.type = member.type;
							member_load.result.size_in_bits = static_cast<int>(member.size * 8);
							member_load.object = std::string_view("other");  // Load from 'other' parameter
							member_load.member_name = std::string_view(member.name);
							member_load.offset = static_cast<int>(member.offset);
							member_load.is_reference = member.is_reference;
							member_load.is_rvalue_reference = member.is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), func_decl.identifier_token()));

							// Then, store the member to 'this'
							// Format: [member_type, member_size, object_name, member_name, offset, is_ref, is_rvalue_ref, ref_size_bits, value]
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = std::string_view("this");
							member_store.member_name = std::string_view(member.name);
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference;
							member_store.is_rvalue_reference = member.is_rvalue_reference;
							member_store.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), func_decl.identifier_token()));
						}

						// Return *this (the return value is the 'this' pointer dereferenced)
						// Generate: %temp = dereference [Type][Size] %this
						//           return [Type][Size] %temp
						TempVar this_deref = var_counter.next();
						std::vector<IrOperand> deref_operands;
						deref_operands.emplace_back(this_deref);  // result variable
						DereferenceOp deref_op;
						deref_op.result = this_deref;
						deref_op.pointee_type = Type::Struct;
						deref_op.pointee_size_in_bits = static_cast<int>(struct_info->total_size * 8);
						deref_op.pointer = std::string_view("this");

						ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), func_decl.identifier_token()));

						// Return the dereferenced value
						ReturnOp ret_op;
						ret_op.return_value = this_deref;
						ret_op.return_type = Type::Struct;
						ret_op.return_size = static_cast<int>(struct_info->total_size * 8);
						ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
					}
				}
			}
		} else {
			// User-defined function body
			const BlockNode& block = node.get_definition().value().as<BlockNode>();
			std::cerr << "DEBUG: About to visit " << block.get_statements().size() 
			          << " statements in function " << func_decl.identifier_token().value() << "\n";
			block.get_statements().visit([&](ASTNode statement) {
				std::cerr << "DEBUG: Visiting statement in " << func_decl.identifier_token().value() << "\n";
				visit(statement);
			});
		}

		// Add implicit return for void functions if no explicit return was added
		if (ret_type.type() == Type::Void) {
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
		}

		symbol_table.exit_scope();
		current_function_name_.clear();
	}

	void visitStructDeclarationNode(const StructDeclarationNode& node) {
		// Struct declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system

		std::cerr << "DEBUG visitStructDeclarationNode: " << node.name() << " with " << node.member_functions().size() << " member functions\n";

		// Skip pattern structs - they're templates and shouldn't generate code
		// Pattern structs have "_pattern_" in their name
		std::string_view struct_name = node.name();
		if (struct_name.find("_pattern_") != std::string_view::npos) {
			return;
		}

		// Only visit member functions if we're at the top level (not inside a function)
		// Local struct declarations inside functions should not generate member function IR
		// because the member functions are already generated when the struct type is registered
		if (current_function_name_.empty()) {
			// We're at the top level, visit member functions
			// Set struct context so member functions know which struct they belong to
			// NOTE: We don't clear this until the next struct - the string must persist
			// because IrOperands store string_view references to it
			current_struct_name_ = std::string(struct_name);
			for (const auto& member_func : node.member_functions()) {
				std::cerr << "  Visiting member function\n";
				// Each member function can be a FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
				try {
					visit(member_func.function_declaration);
				} catch (const std::exception& ex) {
					std::cerr << "ERROR: Exception while visiting member function in struct "
					          << struct_name << ": " << ex.what() << "\n";
					throw;
				} catch (...) {
					std::cerr << "ERROR: Unknown exception while visiting member function in struct "
					          << struct_name << "\n";
					throw;
				}
			}

			// Generate global storage for static members
			auto type_it = gTypesByName.find(std::string(node.name()));
			if (type_it != gTypesByName.end()) {
				const TypeInfo* type_info = type_it->second;
				const StructTypeInfo* struct_info = type_info->getStructInfo();
				if (struct_info) {
					for (const auto& static_member : struct_info->static_members) {
						GlobalVariableDeclOp op;
						op.type = static_member.type;
						op.size_in_bits = static_cast<int>(static_member.size * 8);
						op.var_name = std::string(node.name()) + "::" + static_member.name;

						// Check if static member has an initializer
						op.is_initialized = static_member.initializer.has_value();
						if (op.is_initialized) {
							// Evaluate the initializer expression
							auto init_operands = visitExpressionNode(static_member.initializer->as<ExpressionNode>());
							// Add only the initializer value (init_operands[2] is the value)
							if (init_operands.size() >= 3) {
								// Convert IrOperand to IrValue
								if (std::holds_alternative<unsigned long long>(init_operands[2])) {
									op.init_value = std::get<unsigned long long>(init_operands[2]);
								} else if (std::holds_alternative<double>(init_operands[2])) {
									op.init_value = std::get<double>(init_operands[2]);
								} else if (std::holds_alternative<TempVar>(init_operands[2])) {
									op.init_value = std::get<TempVar>(init_operands[2]);
								} else if (std::holds_alternative<std::string_view>(init_operands[2])) {
									op.init_value = std::get<std::string_view>(init_operands[2]);
								} else {
									// Fallback to zero if type not recognized
									op.init_value = 0ULL;
								}
							} else {
								// Fallback to zero if initializer evaluation failed
								op.init_value = 0ULL;
							}
						}
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), Token()));
					}
				}
			}
			// Don't clear current_struct_name_ - it will be overwritten by the next struct
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
		std::cerr << "DEBUG visitConstructorDeclarationNode: struct=" << node.struct_name()
		          << " name=" << node.name()
		          << " has_definition=" << node.get_definition().has_value() << "\n";
		if (!node.get_definition().has_value())
			return;

		// Reset the temporary variable counter for each new constructor
		// Constructors are always member functions, so reserve TempVar(1) for 'this'
		var_counter = TempVar(2);

		// Set current function name for static local variable mangling
		current_function_name_ = std::string(node.name());
		static_local_names_.clear();

		// Create constructor declaration with typed payload
		FunctionDeclOp ctor_decl_op;
		ctor_decl_op.function_name = std::string(node.struct_name());  // Constructor name (same as struct)
		ctor_decl_op.struct_name = std::string(node.struct_name());  // Struct name for member function
		ctor_decl_op.return_type = Type::Void;  // Constructors don't have a return type
		ctor_decl_op.return_size_in_bits = 0;  // Size is 0 for void
		ctor_decl_op.return_pointer_depth = 0;  // Pointer depth is 0 for void
		ctor_decl_op.linkage = Linkage::CPlusPlus;  // C++ linkage for constructors
		ctor_decl_op.is_variadic = false;  // Constructors are never variadic

		// Generate mangled name for constructor
		std::vector<TypeSpecifierNode> param_types_for_mangling;
		for (const auto& param : node.parameter_nodes()) {
			std::cerr << "DEBUG ctor param node type (mangle): " << param.type_name() << " has_value=" << param.has_value() << "\n";
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor mangle params");
			param_types_for_mangling.push_back(param_decl.type_node().as<TypeSpecifierNode>());
		}
	
		// Generate mangled name for constructor (MSVC format: ?ConstructorName@ClassName@@...)
		std::string ctor_name = std::string(node.struct_name());
		TypeSpecifierNode void_type(Type::Void, TypeQualifier::None, 0);
		ctor_decl_op.mangled_name = generateMangledNameForCall(
			ctor_name,
			void_type,
			param_types_for_mangling,
			false,  // not variadic
			std::string(node.struct_name())  // struct name for member function mangling
		);

		// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
		// We don't add it here to avoid duplication

		// Add parameter types to constructor declaration
		for (const auto& param : node.parameter_nodes()) {
			std::cerr << "DEBUG ctor param node type (decl): " << param.type_name() << " has_value=" << param.has_value() << "\n";
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor decl operands");
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			FunctionParam func_param;
			func_param.type = param_type.type();
			func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
			func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
			func_param.name = std::string(param_decl.identifier_token().value());
			func_param.is_reference = param_type.is_reference();
			func_param.is_rvalue_reference = param_type.is_rvalue_reference();
			func_param.cv_qualifier = param_type.cv_qualifier();
			ctor_decl_op.parameters.push_back(func_param);
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(ctor_decl_op), node.name_token()));
		
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
			std::cerr << "DEBUG ctor param node type (symbol): " << param.type_name() << " has_value=" << param.has_value() << "\n";
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor symbol table");
			symbol_table.insert(param_decl.identifier_token().value(), param);
		}
		std::cerr << "DEBUG ctor stage: symbol table populated for " << node.parameter_nodes().size() << " params\n";

		// C++11 Delegating constructor: if present, ONLY call the target constructor
		// No base class or member initialization should happen
		if (node.delegating_initializer().has_value()) {
			const auto& delegating_init = node.delegating_initializer().value();
			std::cerr << "DEBUG ctor stage: delegating initializer detected\n";
			
			// Build constructor call: StructName::StructName(this, args...)
			ConstructorCallOp ctor_op;
			ctor_op.struct_name = std::string(node.struct_name());
			ctor_op.object = std::string_view("this");

			// Add constructor arguments from delegating initializer
			for (const auto& arg : delegating_init.arguments) {
				auto arg_operands = visitExpressionNode(arg.as<ExpressionNode>());
				// arg_operands = [type, size, value]
				if (arg_operands.size() >= 3) {
					TypedValue tv = toTypedValue(arg_operands);
					ctor_op.arguments.push_back(std::move(tv));
				}
			}

			ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
			
			// Delegating constructors don't execute the body or initialize members
			// Just return
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.name_token()));
			return;
		}

		// C++ construction order:
		// 1. Base class constructors (in declaration order)
		// 2. Member variables (in declaration order)
		// 3. Constructor body

		// Look up the struct type to get base class and member information
		std::cerr << "DEBUG ctor stage: entering base/member initialization\n";
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
					ConstructorCallOp ctor_op;
					ctor_op.struct_name = base_type_info.name_;
					ctor_op.object = std::string_view("this");

					// Add constructor arguments from base initializer
					if (base_init) {
						for (const auto& arg : base_init->arguments) {
							auto arg_operands = visitExpressionNode(arg.as<ExpressionNode>());
							// arg_operands = [type, size, value]
							if (arg_operands.size() >= 3) {
								TypedValue tv = toTypedValue(arg_operands);
								ctor_op.arguments.push_back(std::move(tv));
							}
						}
						// If there's an explicit initializer, generate the constructor call
						ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
					}
					// If no explicit initializer and this is NOT an implicit copy/move constructor,
					// call default constructor (no args)
					// For implicit copy/move constructors, the base constructor call is generated
					// in the implicit constructor generation code above (lines 1000-1023)
					else if (!node.is_implicit()) {
						// Call default constructor with no arguments
						ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
					}
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
						// Implicit copy/move constructor: call base class copy/move constructors first, then memberwise copy/move from 'other' to 'this'

						// Step 1: Call base class copy/move constructors (in declaration order)
						for (const auto& base : struct_info->base_classes) {
							// Get base class type info
							if (base.type_index >= gTypeInfo.size()) {
								continue;  // Invalid base type index
							}
							const TypeInfo& base_type_info = gTypeInfo[base.type_index];

							// Build constructor call: Base::Base(this, other)
							// For copy constructors, pass 'other' as the copy source
							// For move constructors, pass 'other' as the move source
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = std::string(base_type_info.name_);
							ctor_op.object = std::string_view("this");  // 'this' pointer (base subobject is at offset 0 for now)
							// Add 'other' parameter for copy/move constructor
							TypedValue other_arg;
							other_arg.type = Type::Struct;  // Parameter type (struct reference)
							other_arg.size_in_bits = static_cast<int>(struct_info->total_size * 8);  // Parameter size in bits
							other_arg.value = "other"sv;  // Parameter value ('other' object)
							ctor_op.arguments.push_back(std::move(other_arg));

							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
						}

						// Step 2: Memberwise copy/move from 'other' to 'this'
						for (const auto& member : struct_info->members) {
							// First, load the member from 'other'
							TempVar member_value = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = member_value;
							member_load.result.type = member.type;
							member_load.result.size_in_bits = static_cast<int>(member.size * 8);
							member_load.object = "other"sv;  // Load from 'other' parameter
							member_load.member_name = std::string_view(member.name);
							member_load.offset = static_cast<int>(member.offset);
							member_load.is_reference = member.is_reference;
							member_load.is_rvalue_reference = member.is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), node.name_token()));

							// Then, store the member to 'this'
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = "this"sv;
							member_store.member_name = std::string_view(member.name);
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference;
							member_store.is_rvalue_reference = member.is_rvalue_reference;
							member_store.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
						}
					} else {
						// Implicit default constructor: use default member initializers or zero-initialize
						for (const auto& member : struct_info->members) {
							// Generate MemberStore IR to initialize the member
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							
							// Determine the initial value
							IrValue member_value;
							// Check if member has a default initializer (C++11 feature)
							if (member.default_initializer.has_value()) {
								const ASTNode& init_node = member.default_initializer.value();
								if (init_node.has_value() && init_node.is<ExpressionNode>()) {
									// Use the default member initializer
									auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
									// Extract just the value (third element of init_operands)
									if (std::holds_alternative<TempVar>(init_operands[2])) {
										member_value = std::get<TempVar>(init_operands[2]);
									} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
										member_value = std::get<unsigned long long>(init_operands[2]);
									} else if (std::holds_alternative<double>(init_operands[2])) {
										member_value = std::get<double>(init_operands[2]);
									} else if (std::holds_alternative<std::string_view>(init_operands[2])) {
										member_value = std::get<std::string_view>(init_operands[2]);
									} else {
										member_value = 0ULL;  // fallback
									}
								} else {
									// Default initializer exists but isn't an expression, zero-initialize
									if (member.type == Type::Int || member.type == Type::Long ||
									    member.type == Type::Short || member.type == Type::Char) {
										member_value = 0ULL;  // Zero for integer types
									} else if (member.type == Type::Float || member.type == Type::Double) {
										member_value = 0.0;  // Zero for floating-point types
									} else if (member.type == Type::Bool) {
										member_value = 0ULL;  // False for bool (0)
									} else {
										member_value = 0ULL;  // Default to zero
									}
								}
							} else {
								// Zero-initialize based on type
								if (member.type == Type::Int || member.type == Type::Long ||
								    member.type == Type::Short || member.type == Type::Char) {
									member_value = 0ULL;  // Zero for integer types
								} else if (member.type == Type::Float || member.type == Type::Double) {
									member_value = 0.0;  // Zero for floating-point types
								} else if (member.type == Type::Bool) {
									member_value = 0ULL;  // False for bool (0)
								} else {
									member_value = 0ULL;  // Default to zero
								}
							}

							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = std::string_view("this");
							member_store.member_name = std::string_view(member.name);
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference;
							member_store.is_rvalue_reference = member.is_rvalue_reference;
							member_store.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
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
						
						// Determine the initial value
						IrValue member_value;
						// Check for explicit initializer first (highest precedence)
						auto explicit_it = explicit_inits.find(member.name);
						if (explicit_it != explicit_inits.end()) {
							// Use explicit initializer from constructor initializer list
							auto init_operands = visitExpressionNode(explicit_it->second->initializer_expr.as<ExpressionNode>());
							// Extract just the value (third element of init_operands)
							if (std::holds_alternative<TempVar>(init_operands[2])) {
								member_value = std::get<TempVar>(init_operands[2]);
							} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
								member_value = std::get<unsigned long long>(init_operands[2]);
							} else if (std::holds_alternative<double>(init_operands[2])) {
								member_value = std::get<double>(init_operands[2]);
							} else if (std::holds_alternative<std::string_view>(init_operands[2])) {
								member_value = std::get<std::string_view>(init_operands[2]);
							} else {
								member_value = 0ULL;  // fallback
							}
						} else if (member.default_initializer.has_value()) {
							const ASTNode& init_node = member.default_initializer.value();
							if (init_node.has_value() && init_node.is<ExpressionNode>()) {
								// Use default member initializer (C++11 feature)
								auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
								// Extract just the value (third element of init_operands)
								if (std::holds_alternative<TempVar>(init_operands[2])) {
									member_value = std::get<TempVar>(init_operands[2]);
								} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
									member_value = std::get<unsigned long long>(init_operands[2]);
								} else if (std::holds_alternative<double>(init_operands[2])) {
									member_value = std::get<double>(init_operands[2]);
								} else if (std::holds_alternative<std::string_view>(init_operands[2])) {
									member_value = std::get<std::string_view>(init_operands[2]);
								} else {
									member_value = 0ULL;  // fallback
								}
							} else {
								// Default initializer exists but isn't an expression, zero-initialize
								if (member.type == Type::Int || member.type == Type::Long ||
								    member.type == Type::Short || member.type == Type::Char) {
									member_value = 0ULL;
								} else if (member.type == Type::Float || member.type == Type::Double) {
									member_value = 0.0;
								} else if (member.type == Type::Bool) {
									member_value = 0ULL;  // False for bool (0)
								} else {
									member_value = 0ULL;
								}
							}
						} else {
							// Zero-initialize based on type
							if (member.type == Type::Int || member.type == Type::Long ||
							    member.type == Type::Short || member.type == Type::Char) {
								member_value = 0ULL;  // Zero for integer types
							} else if (member.type == Type::Float || member.type == Type::Double) {
								member_value = 0.0;  // Zero for floating-point types
							} else if (member.type == Type::Bool) {
								member_value = 0ULL;  // False for bool (0)
							} else {
								member_value = 0ULL;  // Default to zero
							}
						}

						MemberStoreOp member_store;
						member_store.value.type = member.type;
						member_store.value.size_in_bits = static_cast<int>(member.size * 8);
						member_store.value.value = member_value;
						member_store.object = std::string_view("this");
						member_store.member_name = std::string_view(member.name);
						member_store.offset = static_cast<int>(member.offset);
						member_store.is_reference = member.is_reference;
						member_store.is_rvalue_reference = member.is_rvalue_reference;
						member_store.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
					}
				}
			}
		}

		// Visit the constructor body
		std::cerr << "DEBUG ctor stage: visiting body\n";
		const BlockNode& block = node.get_definition().value().as<BlockNode>();
		size_t ctor_stmt_index = 0;
		block.get_statements().visit([&](const ASTNode& statement) {
			std::cerr << "DEBUG ctor body stmt " << ctor_stmt_index++
			          << " type=" << statement.type_name() << "\n";
			visit(statement);
		});
		std::cerr << "DEBUG ctor stage: body visit complete\n";

		// Add implicit return for constructor (constructors don't have explicit return statements)
		ReturnOp ret_op;  // No return value for void
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.name_token()));

		symbol_table.exit_scope();
		current_function_name_.clear();
	}

	void visitDestructorDeclarationNode(const DestructorDeclarationNode& node) {
		std::cerr << "DEBUG visitDestructorDeclarationNode: struct=" << node.struct_name()
		          << " name=" << node.name()
		          << " has_definition=" << node.get_definition().has_value() << "\n";
		if (!node.get_definition().has_value())
			return;

		// Reset the temporary variable counter for each new destructor
		// Destructors are always member functions, so reserve TempVar(1) for 'this'
		var_counter = TempVar(2);

	// Set current function name for static local variable mangling
	current_function_name_ = std::string(node.name());
	static_local_names_.clear();

	// Create destructor declaration with typed payload
	FunctionDeclOp dtor_decl_op;
	dtor_decl_op.function_name = std::string("~") + std::string(node.struct_name());  // Destructor name
	dtor_decl_op.struct_name = std::string(node.struct_name());  // Struct name for member function
	dtor_decl_op.return_type = Type::Void;  // Destructors don't have a return type
	dtor_decl_op.return_size_in_bits = 0;  // Size is 0 for void
	dtor_decl_op.return_pointer_depth = 0;  // Pointer depth is 0 for void
	dtor_decl_op.linkage = Linkage::CPlusPlus;  // C++ linkage for destructors
	dtor_decl_op.is_variadic = false;  // Destructors are never variadic

	// Generate mangled name for destructor (MSVC format: ?~ClassName@ClassName@@...)
	// Destructors have no parameters (except implicit 'this')
	std::vector<TypeSpecifierNode> empty_params;
	std::string dtor_name = std::string("~") + std::string(node.struct_name());
	TypeSpecifierNode void_type(Type::Void, TypeQualifier::None, 0);
	dtor_decl_op.mangled_name = generateMangledNameForCall(
		dtor_name,
		void_type,
		empty_params,
		false,  // not variadic
		std::string(node.struct_name())  // struct name for member function mangling
	);

	// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
	// We don't add it here to avoid duplication

	ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(dtor_decl_op), node.name_token()));		symbol_table.enter_scope(ScopeType::Function);

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
		const BlockNode& block = node.get_definition().value().as<BlockNode>();
		block.get_statements().visit([&](const ASTNode& statement) {
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
					DestructorCallOp dtor_op;
					dtor_op.struct_name = base_type_info.name_;
					dtor_op.object = std::string_view("this");

					ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), node.name_token()));
				}
			}
		}

		// Add implicit return for destructor (destructors don't have explicit return statements)
		ReturnOp ret_op;  // No return value for void
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.name_token()));

		symbol_table.exit_scope();
		current_function_name_.clear();
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
			const auto& expr_opt = node.expression();
			assert(expr_opt->is<ExpressionNode>());
			auto operands = visitExpressionNode(expr_opt->as<ExpressionNode>());
			
		// Convert to the function's return type if necessary
		if (!operands.empty() && operands.size() >= 2) {
			Type expr_type = std::get<Type>(operands[0]);
			int expr_size = std::get<int>(operands[1]);
			
			// Get the current function's return type
			Type return_type = current_function_return_type_;
			int return_size = current_function_return_size_;
			
			// Convert if types don't match
			if (expr_type != return_type || expr_size != return_size) {
				// Update operands with the converted value
				operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
			}
		}
		
		// Create ReturnOp with the return value
		ReturnOp ret_op;
		// Extract IrValue from operand[2] - it could be various types
		if (std::holds_alternative<unsigned long long>(operands[2])) {
			ret_op.return_value = std::get<unsigned long long>(operands[2]);
		} else if (std::holds_alternative<TempVar>(operands[2])) {
			ret_op.return_value = std::get<TempVar>(operands[2]);
		} else if (std::holds_alternative<std::string_view>(operands[2])) {
			ret_op.return_value = std::get<std::string_view>(operands[2]);
		} else if (std::holds_alternative<double>(operands[2])) {
			ret_op.return_value = std::get<double>(operands[2]);
		}
		ret_op.return_type = std::get<Type>(operands[0]);
		ret_op.return_size = std::get<int>(operands[1]);
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));		}
		else {
			// For void returns, we don't need any operands
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
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
		// Handle C++17 if constexpr - evaluate condition at compile time
		if (node.is_constexpr()) {
			// Evaluate the condition at compile time
			ConstExpr::EvaluationContext ctx(gSymbolTable);
			auto result = ConstExpr::Evaluator::evaluate(node.get_condition(), ctx);
			
			if (!result.success) {
				std::cerr << "Error: if constexpr condition is not a constant expression: " 
				          << result.error_message << std::endl;
				return;
			}

			// Only compile the taken branch
			if (result.as_bool()) {
				// Compile then branch
				auto then_stmt = node.get_then_statement();
				if (then_stmt.is<BlockNode>()) {
					then_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
						visit(statement);
					});
				} else {
					visit(then_stmt);
				}
			} else if (node.has_else()) {
				// Compile else branch
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
			// Note: Non-taken branch is completely discarded (not compiled)
			return;
		}

	// Regular if statement (runtime conditional)
	// Generate unique labels for this if statement
	static size_t if_counter = 0;
	StringBuilder then_sb, else_sb, end_sb;
	then_sb.append("if_then_").append(static_cast<int>(if_counter));
	else_sb.append("if_else_").append(static_cast<int>(if_counter));
	end_sb.append("if_end_").append(static_cast<int>(if_counter));
	std::string_view then_label = then_sb.commit();
	std::string_view else_label = else_sb.commit();
	std::string_view end_label = end_sb.commit();
	if_counter++;		// Handle C++20 if-with-initializer
		if (node.has_init()) {
			auto init_stmt = node.get_init_statement();
			if (init_stmt.has_value()) {
				visit(*init_stmt);
			}
		}

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch
		CondBranchOp cond_branch;
		cond_branch.label_true = then_label;
		cond_branch.label_false = node.has_else() ? else_label : end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Then block
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = then_label}, Token()));

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
			BranchOp branch_to_end;
			branch_to_end.target_label = end_label;
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_end), Token()));
		}

		// Else block (if present)
		if (node.has_else()) {
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = else_label}, Token()));

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
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = end_label}, Token()));
	}

	void visitForStatementNode(const ForStatementNode& node) {
		// Generate unique labels for this for loop
		static size_t for_counter = 0;
		StringBuilder start_sb, body_sb, inc_sb, end_sb;
		start_sb.append("for_start_").append(static_cast<int>(for_counter));
		body_sb.append("for_body_").append(static_cast<int>(for_counter));
		inc_sb.append("for_increment_").append(static_cast<int>(for_counter));
		end_sb.append("for_end_").append(static_cast<int>(for_counter));
		std::string_view loop_start_label = start_sb.commit();
		std::string_view loop_body_label = body_sb.commit();
		std::string_view loop_increment_label = inc_sb.commit();
		std::string_view loop_end_label = end_sb.commit();
		for_counter++;		// Execute init statement (if present)
		if (node.has_init()) {
			auto init_stmt = node.get_init_statement();
			if (init_stmt.has_value()) {
				visit(*init_stmt);
			}
		}

		// Mark loop begin for break/continue support
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_increment_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: evaluate condition
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

		// Evaluate condition (if present, otherwise infinite loop)
		if (node.has_condition()) {
			auto condition_operands = visitExpressionNode(node.get_condition()->as<ExpressionNode>());

			// Generate conditional branch: if true goto body, else goto end
			CondBranchOp cond_branch;
			cond_branch.label_true = loop_body_label;
			cond_branch.label_false = loop_end_label;
			cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
			ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));
		}

		// Loop body label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

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
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_increment_label}, Token()));

		// Execute update/increment expression (if present)
		if (node.has_update()) {
			visitExpressionNode(node.get_update_expression()->as<ExpressionNode>());
		}

		// Branch back to loop start
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitWhileStatementNode(const WhileStatementNode& node) {
		// Generate unique labels for this while loop
		static size_t while_counter = 0;
		StringBuilder start_sb, body_sb, end_sb;
		start_sb.append("while_start_").append(static_cast<int>(while_counter));
		body_sb.append("while_body_").append(static_cast<int>(while_counter));
		end_sb.append("while_end_").append(static_cast<int>(while_counter));
		std::string_view loop_start_label = start_sb.commit();
		std::string_view loop_body_label = body_sb.commit();
		std::string_view loop_end_label = end_sb.commit();
		while_counter++;
		
		// Mark loop begin for break/continue support
		// For while loops, continue jumps to loop_start (re-evaluate condition)
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_start_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: evaluate condition
		LabelOp start_lbl;
		start_lbl.label_name = loop_start_label;
		ir_.addInstruction(IrInstruction(IrOpcode::Label, std::move(start_lbl), Token()));

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch: if true goto body, else goto end
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_body_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Loop body label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

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
		BranchOp branch_to_start;
		branch_to_start.target_label = loop_start_label;
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_start), Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitDoWhileStatementNode(const DoWhileStatementNode& node) {
		// Generate unique labels for this do-while loop
		static size_t do_while_counter = 0;
		StringBuilder start_sb, condition_sb, end_sb;
		start_sb.append("do_while_start_").append(static_cast<int>(do_while_counter));
		condition_sb.append("do_while_condition_").append(static_cast<int>(do_while_counter));
		end_sb.append("do_while_end_").append(static_cast<int>(do_while_counter));
		std::string_view loop_start_label = start_sb.commit();
		std::string_view loop_condition_label = condition_sb.commit();
		std::string_view loop_end_label = end_sb.commit();
		do_while_counter++;
		
		// Mark loop begin for break/continue support
		// For do-while loops, continue jumps to condition check (not body start)
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_condition_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: execute body first (do-while always executes at least once)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

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
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_condition_label}, Token()));

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch: if true goto start, else goto end
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_start_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitSwitchStatementNode(const SwitchStatementNode& node) {
		// Generate unique labels for this switch statement
		static size_t switch_counter = 0;
		StringBuilder end_sb, default_sb;
		end_sb.append("switch_end_").append(static_cast<int>(switch_counter));
		default_sb.append("switch_default_").append(static_cast<int>(switch_counter));
		std::string_view switch_end_label = end_sb.commit();
		std::string_view default_label = default_sb.commit();
		switch_counter++;
		
		// Evaluate the switch condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Get the condition type and value
		Type condition_type = std::get<Type>(condition_operands[0]);
		int condition_size = std::get<int>(condition_operands[1]);

		// Mark switch begin for break support (switch acts like a loop for break)
		// Continue is not allowed in switch, but break is
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = switch_end_label;
		loop_begin.loop_end_label = switch_end_label;
		loop_begin.loop_increment_label = switch_end_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Process the switch body to collect case labels
		auto body = node.get_body();
		if (!body.is<BlockNode>()) {
			assert(false && "Switch body must be a BlockNode");
			return;
		}

		const BlockNode& block = body.as<BlockNode>();
		std::vector<std::pair<std::string_view, ASTNode>> case_labels;  // label name, case value
		bool has_default = false;		// First pass: generate labels and collect case values
		size_t case_index = 0;
		block.get_statements().visit([&](const ASTNode& stmt) {
			if (stmt.is<CaseLabelNode>()) {
				StringBuilder case_sb;
				case_sb.append("switch_case_").append(static_cast<int>(switch_counter - 1)).append("_").append(static_cast<int>(case_index));
				std::string_view case_label = case_sb.commit();
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
			StringBuilder next_check_sb;
			next_check_sb.append("switch_check_").append(static_cast<int>(switch_counter - 1)).append("_").append(static_cast<int>(check_index + 1));
			std::string_view next_check_label = next_check_sb.commit();

			CondBranchOp cond_branch;
			cond_branch.label_true = case_label;
			cond_branch.label_false = next_check_label;
			cond_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 1, .value = cmp_result};
			ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

			// Next check label
			LabelOp next_lbl;
			next_lbl.label_name = next_check_label;
			ir_.addInstruction(IrInstruction(IrOpcode::Label, std::move(next_lbl), Token()));
			check_index++;
		}

		// If no case matched, jump to default or end
		if (has_default) {
			BranchOp branch_to_default;
			branch_to_default.target_label = default_label;
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_default), Token()));
		} else {
			BranchOp branch_to_end;
			branch_to_end.target_label = switch_end_label;
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_end), Token()));
		}

		// Second pass: generate code for each case/default
		case_index = 0;
		block.get_statements().visit([&](const ASTNode& stmt) {
			if (stmt.is<CaseLabelNode>()) {
			const CaseLabelNode& case_node = stmt.as<CaseLabelNode>();
			StringBuilder case_sb;
			case_sb.append("switch_case_").append(static_cast<int>(switch_counter - 1)).append("_").append(static_cast<int>(case_index));
			std::string_view case_label = case_sb.commit();

			// Case label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = case_label}, Token()));				// Execute case statements
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
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = default_label}, Token()));				// Execute default statements
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
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = switch_end_label}, Token()));

		// Mark switch end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitRangedForStatementNode(const RangedForStatementNode& node) {
		// Desugar ranged for loop into traditional for loop
		// For arrays: for (int x : arr) { body } becomes:
		//   for (int __i = 0; __i < array_size; ++__i) { int x = arr[__i]; body }
		// For types with begin()/end(): for (int x : vec) { body } becomes:
		//   for (auto __begin = vec.begin(), __end = vec.end(); __begin != __end; ++__begin) { int x = *__begin; body }

		// Generate unique labels and counter for this ranged for loop
		static size_t ranged_for_counter = 0;
		StringBuilder start_sb, body_sb, increment_sb, end_sb;
		start_sb.append("ranged_for_start_").append(static_cast<int>(ranged_for_counter));
		body_sb.append("ranged_for_body_").append(static_cast<int>(ranged_for_counter));
		increment_sb.append("ranged_for_increment_").append(static_cast<int>(ranged_for_counter));
		end_sb.append("ranged_for_end_").append(static_cast<int>(ranged_for_counter));
		std::string_view loop_start_label = start_sb.commit();
		std::string_view loop_body_label = body_sb.commit();
		std::string_view loop_increment_label = increment_sb.commit();
		std::string_view loop_end_label = end_sb.commit();
		ranged_for_counter++;		// Get the loop variable declaration and range expression
		auto loop_var_decl = node.get_loop_variable_decl();
		auto range_expr = node.get_range_expression();

		// C++11+ standard: The range expression is bound to a reference for lifetime extension
		// This ensures temporary objects live for the entire loop duration
		// For now, we only support simple identifiers (not temporaries), so lifetime is already correct
		
		// Check what kind of range expression we have
		if (!range_expr.is<ExpressionNode>()) {
			std::cerr << "error: Range expression must be an expression\n";
			return;
		}

		auto& expr_variant = range_expr.as<ExpressionNode>();
		if (!std::holds_alternative<IdentifierNode>(expr_variant)) {
			std::cerr << "error: Currently only identifiers are supported as range expressions\n";
			return;
		}

		const IdentifierNode& range_ident = std::get<IdentifierNode>(expr_variant);
		std::string_view range_name = range_ident.name();

		// Look up the range object in the symbol table
		const std::optional<ASTNode> range_symbol = symbol_table.lookup(range_name);
		if (!range_symbol.has_value() || !range_symbol->is<DeclarationNode>()) {
			std::cerr << "error: Range object '" << range_name << "' not found in symbol table\n";
			return;
		}

		const DeclarationNode& range_decl = range_symbol->as<DeclarationNode>();
		const TypeSpecifierNode& range_type = range_decl.type_node().as<TypeSpecifierNode>();

		// C++ standard: pointers are NOT valid range expressions (no size information)
		// Only arrays and types with begin()/end() are allowed
		if (range_type.pointer_depth() > 0 && !range_decl.is_array()) {
			std::cerr << "error: Cannot use pointer in range-based for loop; use array or type with begin()/end()\n";
			return;
		}

		// Check if it's an array
		if (range_decl.is_array()) {
			// Array-based range-for loop
			visitRangedForArray(node, range_name, range_decl, loop_start_label, loop_body_label, 
			                    loop_increment_label, loop_end_label, ranged_for_counter - 1);
		}
		// Check if it's a struct with begin()/end() methods
		else if (range_type.type() == Type::Struct) {
			visitRangedForBeginEnd(node, range_name, range_type, loop_start_label, loop_body_label,
			                       loop_increment_label, loop_end_label, ranged_for_counter - 1);
		}
		else {
			std::cerr << "error: Range expression must be an array or a type with begin()/end() methods\n";
			return;
		}
	}

	void visitRangedForArray(const RangedForStatementNode& node, std::string_view array_name,
	                         const DeclarationNode& array_decl, std::string_view loop_start_label,
	                         std::string_view loop_body_label, std::string_view loop_increment_label,
	                         std::string_view loop_end_label, size_t counter) {
		auto loop_var_decl = node.get_loop_variable_decl();

		// Unified pointer-based approach: use begin/end pointers for arrays too
		// This is more efficient (no indexing multiplication) and matches what optimizing compilers do
		// For array: auto __begin = &array[0]; auto __end = &array[size]; for (; __begin != __end; ++__begin)

		// Get array size
		auto array_size_node = array_decl.array_size();
		if (!array_size_node.has_value()) {
			std::cerr << "error: Array must have a known size for range-based for loop\n";
			return;
		}

		// Create begin/end pointer variable names
		StringBuilder sb_begin;
		sb_begin.append("__range_begin_");
		sb_begin.append(static_cast<int>(counter));
		std::string_view begin_var_name = sb_begin.commit();

		StringBuilder sb_end;
		sb_end.append("__range_end_");
		sb_end.append(static_cast<int>(counter));
		std::string_view end_var_name = sb_end.commit();

		Token begin_token(Token::Type::Identifier, begin_var_name, 0, 0, 0);
		Token end_token(Token::Type::Identifier, end_var_name, 0, 0, 0);

		// Get the array element type to create pointer type
		const TypeSpecifierNode& array_type = array_decl.type_node().as<TypeSpecifierNode>();
		
		// Create pointer type for begin/end (element_type*)
		auto begin_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			array_type.type(), array_type.type_index(), array_type.size_in_bits(), Token()
		);
		begin_type_node.as<TypeSpecifierNode>().add_pointer_level();
		auto begin_decl_node = ASTNode::emplace_node<DeclarationNode>(begin_type_node, begin_token);

		auto end_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			array_type.type(), array_type.type_index(), array_type.size_in_bits(), Token()
		);
		end_type_node.as<TypeSpecifierNode>().add_pointer_level();
		auto end_decl_node = ASTNode::emplace_node<DeclarationNode>(end_type_node, end_token);

		// Create begin = &array[0]
		auto array_expr_begin = ASTNode::emplace_node<ExpressionNode>(
			IdentifierNode(Token(Token::Type::Identifier, array_name, 0, 0, 0))
		);
		auto zero_literal = ASTNode::emplace_node<ExpressionNode>(
			NumericLiteralNode(Token(Token::Type::Literal, "0", 0, 0, 0),
				static_cast<unsigned long long>(0), Type::Int, TypeQualifier::None, 32)
		);
		auto first_element = ASTNode::emplace_node<ExpressionNode>(
			ArraySubscriptNode(array_expr_begin, zero_literal, Token(Token::Type::Punctuator, "[", 0, 0, 0))
		);
		auto begin_init = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "&", 0, 0, 0), first_element, true)
		);
		auto begin_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(begin_decl_node, begin_init);
		visit(begin_var_decl_node);

		// Create end = &array[size] (one past the last element)
		auto array_expr_end = ASTNode::emplace_node<ExpressionNode>(
			IdentifierNode(Token(Token::Type::Identifier, array_name, 0, 0, 0))
		);
		auto past_end_element = ASTNode::emplace_node<ExpressionNode>(
			ArraySubscriptNode(array_expr_end, array_size_node.value(), Token(Token::Type::Punctuator, "[", 0, 0, 0))
		);
		auto end_init = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "&", 0, 0, 0), past_end_element, true)
		);
		auto end_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(end_decl_node, end_init);
		visit(end_var_decl_node);

		// Mark loop begin for break/continue support
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_increment_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: evaluate condition (__begin != __end)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

		// Create condition: __begin != __end
		auto begin_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto end_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(end_token));
		auto condition_expr = ASTNode::emplace_node<ExpressionNode>(
			BinaryOperatorNode(Token(Token::Type::Operator, "!=", 0, 0, 0), begin_ident_expr, end_ident_expr)
		);
		auto condition_operands = visitExpressionNode(condition_expr.as<ExpressionNode>());

		// Generate conditional branch
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_body_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Loop body label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

		// Declare and initialize the loop variable: T x = *__begin
		auto begin_deref_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto deref_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "*", 0, 0, 0), begin_deref_expr, true)
		);

		// Create the loop variable declaration with initialization
		// loop_var_decl is a VariableDeclarationNode from the parser
		// We need to extract its DeclarationNode and create a new VariableDeclarationNode with the initializer
		if (!loop_var_decl.is<VariableDeclarationNode>()) {
			std::cerr << "ERROR: loop_var_decl is not a VariableDeclarationNode!\n";
			return;
		}
		const VariableDeclarationNode& original_var_decl = loop_var_decl.as<VariableDeclarationNode>();
		ASTNode loop_decl_node = original_var_decl.declaration_node();
		
		auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_decl_node, deref_expr);

		// Generate IR for loop variable declaration
		// Note: visitVariableDeclarationNode will add it to the symbol table
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
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_increment_label}, Token()));

		// Increment pointer: ++__begin
		auto increment_begin = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto increment_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "++", 0, 0, 0), increment_begin, true)
		);
		visitExpressionNode(increment_expr.as<ExpressionNode>());

		// Branch back to loop start
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitRangedForBeginEnd(const RangedForStatementNode& node, std::string_view range_name,
	                            const TypeSpecifierNode& range_type, std::string_view loop_start_label,
	                            std::string_view loop_body_label, std::string_view loop_increment_label,
	                            std::string_view loop_end_label, size_t counter) {
		auto loop_var_decl = node.get_loop_variable_decl();

		// Get the struct type info
		if (range_type.type_index() >= gTypeInfo.size()) {
			std::cerr << "error: Invalid type index for range expression\n";
			return;
		}

		const TypeInfo& type_info = gTypeInfo[range_type.type_index()];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (!struct_info) {
			std::cerr << "error: Range expression is not a struct type\n";
			return;
		}

		// Check for begin() and end() methods
		const StructMemberFunction* begin_func = struct_info->findMemberFunction("begin");
		const StructMemberFunction* end_func = struct_info->findMemberFunction("end");

		if (!begin_func || !end_func) {
			std::cerr << "error: Range-based for loop requires type to have both begin() and end() methods\n";
			return;
		}

		// Create iterator variables: auto __begin = range.begin(), __end = range.end()
		StringBuilder sb_begin;
		sb_begin.append("__range_begin_");
		sb_begin.append(static_cast<int>(counter));
		std::string_view begin_var_name = sb_begin.commit();

		StringBuilder sb_end;
		sb_end.append("__range_end_");
		sb_end.append(static_cast<int>(counter));
		std::string_view end_var_name = sb_end.commit();

		// Get return type from begin() - should be a pointer type
		const FunctionDeclarationNode& begin_func_decl = begin_func->function_decl.as<FunctionDeclarationNode>();
		const TypeSpecifierNode& begin_return_type = begin_func_decl.decl_node().type_node().as<TypeSpecifierNode>();

		// Standard C++20 range-for with begin()/end() desugars to:
		//   auto __begin = range.begin();
		//   auto __end = range.end();
		//   for (; __begin != __end; ++__begin) { decl = *__begin; body; }
		
		Token begin_token(Token::Type::Identifier, begin_var_name, 0, 0, 0);
		Token end_token(Token::Type::Identifier, end_var_name, 0, 0, 0);

		// Create type nodes for the iterator variables (they're pointers typically)
		auto begin_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			begin_return_type.type(), begin_return_type.type_index(), begin_return_type.size_in_bits(), Token()
		);
		begin_type_node.as<TypeSpecifierNode>().copy_indirection_from(begin_return_type);
		auto begin_decl_node = ASTNode::emplace_node<DeclarationNode>(begin_type_node, begin_token);

		auto end_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			begin_return_type.type(), begin_return_type.type_index(), begin_return_type.size_in_bits(), Token()
		);
		end_type_node.as<TypeSpecifierNode>().copy_indirection_from(begin_return_type);
		auto end_decl_node = ASTNode::emplace_node<DeclarationNode>(end_type_node, end_token);

		// Create member function calls: range.begin() and range.end()
		auto range_expr_for_begin = ASTNode::emplace_node<ExpressionNode>(
			IdentifierNode(Token(Token::Type::Identifier, range_name, 0, 0, 0))
		);
		
		ChunkedVector<ASTNode> empty_args;
		auto begin_call_expr = ASTNode::emplace_node<ExpressionNode>(
			MemberFunctionCallNode(range_expr_for_begin, 
			                       const_cast<FunctionDeclarationNode&>(begin_func_decl),
			                       std::move(empty_args), Token())
		);
		
		auto begin_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(begin_decl_node, begin_call_expr);
		visit(begin_var_decl_node);

		// Similarly for end()
		const FunctionDeclarationNode& end_func_decl = end_func->function_decl.as<FunctionDeclarationNode>();
		auto range_expr_for_end = ASTNode::emplace_node<ExpressionNode>(
			IdentifierNode(Token(Token::Type::Identifier, range_name, 0, 0, 0))
		);
		
		ChunkedVector<ASTNode> empty_args2;
		auto end_call_expr = ASTNode::emplace_node<ExpressionNode>(
			MemberFunctionCallNode(range_expr_for_end,
			                       const_cast<FunctionDeclarationNode&>(end_func_decl),
			                       std::move(empty_args2), Token())
		);
		
		auto end_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(end_decl_node, end_call_expr);
		visit(end_var_decl_node);

		// Mark loop begin for break/continue support
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_increment_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: evaluate condition (__begin != __end)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

		// Create condition: __begin != __end
		auto begin_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto end_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(end_token));
		auto condition_expr = ASTNode::emplace_node<ExpressionNode>(
			BinaryOperatorNode(Token(Token::Type::Operator, "!=", 0, 0, 0), begin_ident_expr, end_ident_expr)
		);
		auto condition_operands = visitExpressionNode(condition_expr.as<ExpressionNode>());

		// Generate conditional branch
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_body_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Loop body label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

		// Declare and initialize the loop variable: T x = *__begin
		// Create dereference: *__begin
		auto begin_ident_deref = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto deref_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "*", 0, 0, 0), begin_ident_deref, true)
		);

		// Create the loop variable declaration with initialization
		if (!loop_var_decl.is<VariableDeclarationNode>()) {
			assert(false && "loop_var_decl must be a VariableDeclarationNode");
			return;
		}
		const VariableDeclarationNode& original_var_decl = loop_var_decl.as<VariableDeclarationNode>();
		ASTNode loop_decl_node = original_var_decl.declaration_node();
		
		auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_decl_node, deref_expr);

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
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_increment_label}, Token()));

		// Increment iterator: ++__begin
		auto increment_begin = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto increment_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "++", 0, 0, 0), increment_begin, true)
		);
		visitExpressionNode(increment_expr.as<ExpressionNode>());

		// Branch back to loop start
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

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
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = label_name}, node.goto_token()));
	}

	void visitLabelStatementNode(const LabelStatementNode& node) {
		// Generate Label IR instruction with the label name
		std::string label_name(node.label_name());
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = label_name}, node.label_token()));
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

			// Create GlobalVariableDeclOp
			GlobalVariableDeclOp op;
			op.type = type_node.type();
			op.size_in_bits = static_cast<int>(type_node.size_in_bits());
			op.var_name = var_name;

			// Check if initialized
			// Try to evaluate initializer as a constant expression using ConstExprEvaluator
			if (node.initializer()) {
				const ASTNode& init_node = *node.initializer();
				
				if (init_node.is<ExpressionNode>()) {
					// Try to evaluate as constant expression
					ConstExpr::EvaluationContext ctx(gSymbolTable);
					auto eval_result = ConstExpr::Evaluator::evaluate(init_node, ctx);
					
					if (eval_result.success) {
						op.is_initialized = true;
						
						// Convert the evaluated result to the appropriate storage format
						// based on the target type
						unsigned long long init_value = 0;
						
						// Check the target type to determine how to store the value
						Type target_type = type_node.type();
						
						if (target_type == Type::Float) {
							// For float, convert to float then reinterpret as uint32
							float f = static_cast<float>(eval_result.as_double());
							uint32_t f_bits;
							std::memcpy(&f_bits, &f, sizeof(float));
							init_value = f_bits;
						} else if (target_type == Type::Double || target_type == Type::LongDouble) {
							// For double/long double, reinterpret double bits as uint64
							double d = eval_result.as_double();
							std::memcpy(&init_value, &d, sizeof(double));
						} else if (std::holds_alternative<double>(eval_result.value)) {
							// Floating-point result but target is integer type - convert
							init_value = static_cast<unsigned long long>(eval_result.as_int());
						} else if (std::holds_alternative<unsigned long long>(eval_result.value)) {
							init_value = std::get<unsigned long long>(eval_result.value);
							std::cerr << "DEBUG:   Unsigned integer initializer value: " << init_value << "\n";
						} else if (std::holds_alternative<long long>(eval_result.value)) {
							init_value = static_cast<unsigned long long>(std::get<long long>(eval_result.value));
						} else if (std::holds_alternative<bool>(eval_result.value)) {
							init_value = std::get<bool>(eval_result.value) ? 1 : 0;
						}
						
						op.init_value = init_value;
					} else {
						// Constexpr evaluation failed - mark as uninitialized
						op.is_initialized = false;
					}
				} else {
					op.is_initialized = false;
				}
			} else {
				op.is_initialized = false;
			}

			ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), decl.identifier_token()));
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
		// Format: [type, size_in_bits, var_name, custom_alignment, is_ref, is_rvalue_ref, is_array, ...]
		std::vector<IrOperand> operands;
		operands.emplace_back(type_node.type());
		// For pointers, allocate 64 bits (pointer size on x64), not the pointed-to type size
		int size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
		operands.emplace_back(size_in_bits);
		operands.emplace_back(decl.identifier_token().value());
		operands.emplace_back(static_cast<unsigned long long>(decl.custom_alignment()));
		operands.emplace_back(type_node.is_reference());
		operands.emplace_back(type_node.is_rvalue_reference());
		operands.emplace_back(decl.is_array());  // Add is_array flag

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
				VariableDeclOp decl_op;
				decl_op.type = type_node.type();
				decl_op.size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
				decl_op.var_name = decl.identifier_token().value();
				decl_op.custom_alignment = static_cast<unsigned long long>(decl.custom_alignment());
				decl_op.is_reference = type_node.is_reference();
				decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
				decl_op.is_array = decl.is_array();
				ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), node.declaration().identifier_token()));

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

							// Check if this is a designated initializer list or aggregate initialization
							// Designated initializers always use direct member initialization
							bool use_direct_member_init = init_list.has_any_designated();

							if (struct_info.hasAnyConstructor() && !use_direct_member_init) {
								// Generate constructor call with parameters from initializer list
								ConstructorCallOp ctor_op;
								ctor_op.struct_name = type_info.name_;
								ctor_op.object = decl.identifier_token().value();

								// Add each initializer as a constructor parameter
								for (const ASTNode& init_expr : initializers) {
									if (init_expr.is<ExpressionNode>()) {
										auto init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
										// init_operands = [type, size, value]
										if (init_operands.size() >= 3) {
											TypedValue tv = toTypedValue(init_operands);
											ctor_op.arguments.push_back(std::move(tv));
										} else {
											assert(false && "Invalid initializer operands - expected [type, size, value]");
										}
									} else {
										assert(false && "Initializer must be an ExpressionNode");
									}
								}
								ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
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
									// Determine the initial value
									IrValue member_value;
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
											// Extract value from init_operands[2]
											if (std::holds_alternative<TempVar>(init_operands[2])) {
												member_value = std::get<TempVar>(init_operands[2]);
											} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
												member_value = std::get<unsigned long long>(init_operands[2]);
											} else if (std::holds_alternative<double>(init_operands[2])) {
												member_value = std::get<double>(init_operands[2]);
											} else if (std::holds_alternative<std::string_view>(init_operands[2])) {
												member_value = std::get<std::string_view>(init_operands[2]);
											} else {
												member_value = 0ULL;  // fallback
											}
										} else {
											assert(false && "Invalid initializer operands");
										}
									} else {
										// Zero-initialize unspecified members
										member_value = 0ULL;
									}

									MemberStoreOp member_store;
									member_store.value.type = member.type;
									member_store.value.size_in_bits = static_cast<int>(member.size * 8);
									member_store.value.value = member_value;
									member_store.object = decl.identifier_token().value();
									member_store.member_name = std::string_view(member.name);
									member_store.offset = static_cast<int>(member.offset);
									member_store.is_reference = member.is_reference;
									member_store.is_rvalue_reference = member.is_rvalue_reference;
									member_store.struct_type_info = nullptr;

									ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), decl.identifier_token()));
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
				// For struct types with copy constructors, don't add initializer to VariableDecl
				// It will be handled by ConstructorCall below
				bool is_copy_init_for_struct = (type_node.type() == Type::Struct && 
				                                 node.initializer() && 
				                                 init_node.is<ExpressionNode>() && 
				                                 !init_node.is<InitializerListNode>());
				
				if (!is_copy_init_for_struct) {
					auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
					operands.insert(operands.end(), init_operands.begin(), init_operands.end());
				}
			}
		}

		if (!symbol_table.insert(decl.identifier_token().value(), node.declaration_node())) {
			assert(false && "Expected identifier to be unique");
		}

		VariableDeclOp decl_op;
		decl_op.type = type_node.type();
		decl_op.size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
		decl_op.var_name = decl.identifier_token().value();
		decl_op.custom_alignment = static_cast<unsigned long long>(decl.custom_alignment());
		decl_op.is_reference = type_node.is_reference();
		decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
		decl_op.is_array = decl.is_array();
		if (decl.is_array() && operands.size() >= 10) {
			decl_op.array_element_type = std::get<Type>(operands[7]);
			decl_op.array_element_size = std::get<int>(operands[8]);
			if (std::holds_alternative<unsigned long long>(operands[9])) {
				decl_op.array_count = std::get<unsigned long long>(operands[9]);
			}
		}
		if (node.initializer() && !decl.is_array() && operands.size() >= 10) {
			TypedValue tv = toTypedValue(std::span<const IrOperand>(&operands[7], 3));
			decl_op.initializer = std::move(tv);
		}
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), node.declaration().identifier_token()));

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
						// Check if we have an initializer
						bool has_copy_init = false;
						if (node.initializer()) {
							const ASTNode& init_node = *node.initializer();
							if (init_node.is<ExpressionNode>() && !init_node.is<InitializerListNode>()) {
								// For copy initialization like "AllSizes b = a;", the initializer
								// operands were already appended to the VariableDecl instruction.
								// We need to generate a copy constructor call instead of default constructor.
								// The initializer operands are in format: [type, size, value]
								has_copy_init = true;
							}
						}

						// Generate constructor call
						ConstructorCallOp ctor_op;
						ctor_op.struct_name = std::string(type_info.name_);
						ctor_op.object = decl.identifier_token().value();    // Object variable name (string_view)

						if (has_copy_init && node.initializer()) {
							// Add initializer as copy constructor parameter
							const ASTNode& init_node = *node.initializer();
							auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
							// init_operands = [type, size, value]
							if (init_operands.size() >= 3) {
								TypedValue init_arg = toTypedValue(init_operands);
								ctor_op.arguments.push_back(std::move(init_arg));
							}
						}

						ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
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
		else if (std::holds_alternative<TernaryOperatorNode>(exprNode)) {
			const auto& expr = std::get<TernaryOperatorNode>(exprNode);
			return generateTernaryOperatorIr(expr);
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
		else if (std::holds_alternative<SizeofPackNode>(exprNode)) {
			const auto& expr = std::get<SizeofPackNode>(exprNode);
			// sizeof... should have been replaced with a constant during template instantiation
			// If we reach here, it means sizeof... wasn't properly substituted
			// This is an error - sizeof... can only appear in template contexts
			std::cerr << "ERROR: sizeof... operator found during code generation - should have been substituted during template instantiation\n";
			return {};
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
		else if (std::holds_alternative<ConstructorCallNode>(exprNode)) {
			const auto& expr = std::get<ConstructorCallNode>(exprNode);
			return generateConstructorCallIr(expr);
		}
		else if (std::holds_alternative<TemplateParameterReferenceNode>(exprNode)) {
			const auto& expr = std::get<TemplateParameterReferenceNode>(exprNode);
			return generateTemplateParameterReferenceIr(expr);
		}
		else if (std::holds_alternative<FoldExpressionNode>(exprNode)) {
			// Fold expressions should have been expanded during template instantiation
			// If we reach here, it means the fold wasn't properly substituted
			std::cerr << "ERROR: Fold expression found during code generation - should have been expanded during template instantiation\n";
			return {};
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
							MemberLoadOp member_load;
							member_load.result.value = ptr_temp;
							member_load.result.type = member->type;  // Base type (e.g., Int)
							member_load.result.size_in_bits = 64;  // pointer size in bits
							member_load.object = std::string_view("this");
							member_load.member_name = std::string_view(member->name);
							member_load.offset = static_cast<int>(member->offset);
							member_load.is_reference = member->is_reference;
							member_load.is_rvalue_reference = member->is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

							// The ptr_temp now contains the address of the captured variable
							// We need to dereference it using PointerDereference
							auto type_it = current_lambda_capture_types_.find(var_name_str);
							if (type_it != current_lambda_capture_types_.end()) {
								const TypeSpecifierNode& orig_type = type_it->second;

								// Generate Dereference to load the value
								TempVar result_temp = var_counter.next();
								std::vector<IrOperand> deref_operands;
								DereferenceOp deref_op;
								deref_op.result = result_temp;
								deref_op.pointee_type = orig_type.type();
								deref_op.pointee_size_in_bits = static_cast<int>(orig_type.size_in_bits());
								deref_op.pointer = ptr_temp;
								ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));

								return { orig_type.type(), static_cast<int>(orig_type.size_in_bits()), result_temp };
							}

							// Fallback: return the pointer temp
							return { member->type, 64, ptr_temp };
						} else {
							// By-value capture: direct member access
							TempVar result_temp = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = result_temp;
							member_load.result.type = member->type;
							member_load.result.size_in_bits = static_cast<int>(member->size * 8);
							member_load.object = std::string_view("this");  // implicit this pointer
							member_load.member_name = std::string_view(member->name);
							member_load.offset = static_cast<int>(member->offset);
							member_load.is_reference = member->is_reference;
							member_load.is_rvalue_reference = member->is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
							return { member->type, static_cast<int>(member->size * 8), result_temp };
						}
					}
				}
			}
		}

		// Check if we're in a member function and this identifier is a member variable
		if (!current_struct_name_.empty()) {
			// Look up the struct type
			auto type_it = gTypesByName.find(std::string(current_struct_name_));
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					// Check if this identifier is a member of the struct
					const StructMember* member = struct_info->findMemberRecursive(var_name_str);
					if (member) {
						// This is a member variable access - generate MemberAccess IR with implicit 'this'
						TempVar result_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = result_temp;
						member_load.result.type = member->type;
						member_load.result.size_in_bits = static_cast<int>(member->size * 8);
						member_load.object = std::string_view("this");  // implicit this pointer
						member_load.member_name = std::string_view(member->name);
						member_load.offset = static_cast<int>(member->offset);
						member_load.is_reference = member->is_reference;
						member_load.is_rvalue_reference = member->is_rvalue_reference;
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
						return { member->type, static_cast<int>(member->size * 8), result_temp };
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
			GlobalLoadOp op;
			op.result.type = info.type;
			op.result.size_in_bits = info.size_in_bits;
			op.result.value = result_temp;
			op.global_name = info.mangled_name;  // Use mangled name
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

			// Return the temp variable that will hold the loaded value
			return { info.type, info.size_in_bits, result_temp };
		}

		// First try local symbol table (for local variables, parameters, etc.)
		std::optional<ASTNode> symbol = symbol_table.lookup(identifierNode.name());
		bool is_global = false;

		// If not found locally, try global symbol table (for enum values, global variables, namespace-scoped variables, etc.)
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(identifierNode.name());
			is_global = symbol.has_value();  // If found in global table, it's a global
		}

		if (!symbol.has_value()) {
			std::cerr << "ERROR: Symbol '" << identifierNode.name() << "' not found in symbol table during code generation\n";
			std::cerr << "  Current function: " << current_function_name_ << "\n";
			std::cerr << "  Current struct: " << current_struct_name_ << "\n";
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
				int size_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
				GlobalLoadOp op;
				op.result.type = type_node.type();
				op.result.size_in_bits = size_bits;
				op.result.value = result_temp;
				op.global_name = identifierNode.name();
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Return the temp variable that will hold the loaded value
				// For pointers, return 64 bits (pointer size)
				return { type_node.type(), size_bits, result_temp };
			}

			// Regular local variable
			// For pointers, return 64 bits (pointer size)
			int size_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
			return { type_node.type(), size_bits, identifierNode.name() };
		}

		// Check if it's a VariableDeclarationNode (global variable)
		if (symbol->is<VariableDeclarationNode>()) {
			const auto& var_decl_node = symbol->as<VariableDeclarationNode>();
			const auto& decl_node = var_decl_node.declaration();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// This is a global variable - generate GlobalLoad
			TempVar result_temp = var_counter.next();
			GlobalLoadOp op;
			op.result.type = type_node.type();
			op.result.size_in_bits = static_cast<int>(type_node.size_in_bits());
			op.result.value = result_temp;
			op.global_name = identifierNode.name();
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

			// Return the temp variable that will hold the loaded value
			return { type_node.type(), static_cast<int>(type_node.size_in_bits()), result_temp };
		}
		
		// Check if it's a FunctionDeclarationNode (function name used as value)
		if (symbol->is<FunctionDeclarationNode>()) {
			// This is a function name being used as a value (e.g., fp = add)
			// Generate FunctionAddress IR instruction
			TempVar func_addr_var = var_counter.next();
			FunctionAddressOp op;
			op.result.type = Type::FunctionPointer;
			op.result.size_in_bits = 64;
			op.result.value = func_addr_var;
			op.function_name = identifierNode.name();
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionAddress, std::move(op), Token()));

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

			// Check if this is a static member access (e.g., StructName::static_member)
			auto struct_type_it = gTypesByName.find(namespaces[0]);
			if (struct_type_it != gTypesByName.end() && struct_type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				if (struct_info) {
					// Look for static member
					const StructStaticMember* static_member = struct_info->findStaticMember(std::string(qualifiedIdNode.name()));
					if (static_member) {
						// This is a static member access - generate GlobalLoad
						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.type = static_member->type;
						op.result.size_in_bits = static_cast<int>(static_member->size * 8);
						op.result.value = result_temp;
						// Use qualified name as the global symbol name: StructName::static_member
						// Use the actual type name (which may be an instantiated template name like Container_int)
						StringBuilder qualified_name_sb;
						qualified_name_sb.append(struct_type_it->second->name_);
						qualified_name_sb.append("::");
						qualified_name_sb.append(qualifiedIdNode.name());
						op.global_name = qualified_name_sb.commit();
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

						// Return the temp variable that will hold the loaded value
						return { static_member->type, static_cast<int>(static_member->size * 8), result_temp };
					}
				}
			}
		}

		// Look up the qualified identifier in the symbol table
		const std::optional<ASTNode> symbol = symbol_table.lookup_qualified(qualifiedIdNode.namespaces(), qualifiedIdNode.name());

		// Also try global symbol table for namespace-qualified globals
		std::optional<ASTNode> global_symbol;
		if (!symbol.has_value() && global_symbol_table_) {
			global_symbol = global_symbol_table_->lookup_qualified(qualifiedIdNode.namespaces(), qualifiedIdNode.name());
		}

		const std::optional<ASTNode>& found_symbol = symbol.has_value() ? symbol : global_symbol;

		if (!found_symbol.has_value()) {
			// For external functions (like std::print), we might not have them in our symbol table
			// Return a placeholder - the actual linking will happen later
			return { Type::Int, 32, qualifiedIdNode.name() };
		}

		if (found_symbol->is<DeclarationNode>()) {
			const auto& decl_node = found_symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Check if this is a global variable (namespace-scoped)
			// If found in global symbol table, it's a global variable
			bool is_global = global_symbol.has_value();

			if (is_global) {
				// Generate GlobalLoad for namespace-qualified global variable
				TempVar result_temp = var_counter.next();
				GlobalLoadOp op;
				op.result.type = type_node.type();
				op.result.size_in_bits = static_cast<int>(type_node.size_in_bits());
				op.result.value = result_temp;
				op.global_name = qualifiedIdNode.name();  // Use the identifier name
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Return the temp variable that will hold the loaded value
				return { type_node.type(), static_cast<int>(type_node.size_in_bits()), result_temp };
			} else {
				// Local variable - just return the name
				return { type_node.type(), static_cast<int>(type_node.size_in_bits()), qualifiedIdNode.name() };
			}
		}

		if (found_symbol->is<VariableDeclarationNode>()) {
			const auto& var_decl_node = found_symbol->as<VariableDeclarationNode>();
			const auto& decl_node = var_decl_node.declaration_node().as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Namespace-scoped variables are always global
			// Generate GlobalLoad for namespace-qualified global variable
			TempVar result_temp = var_counter.next();
			int size_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
			GlobalLoadOp op;
			op.result.type = type_node.type();
			op.result.size_in_bits = size_bits;
			op.result.value = result_temp;
			op.global_name = qualifiedIdNode.name();  // Use the identifier name
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

			// Return the temp variable that will hold the loaded value
			// For pointers, return 64 bits (pointer size)
			return { type_node.type(), size_bits, result_temp };
		}

		if (found_symbol->is<FunctionDeclarationNode>()) {
			// This is a function - just return the name for function calls
			// The actual function call handling is done elsewhere
			return { Type::Function, 64, qualifiedIdNode.name() };
		}

		// If we get here, the symbol is not a supported type
		assert(false && "Qualified identifier is not a supported type");
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
		// Get the actual size from the operands (they already contain the correct size)
		// operands format: [type, size, value]
		int fromSize = (operands.size() >= 2) ? std::get<int>(operands[1]) : get_type_size_bits(fromType);
		int toSize = get_type_size_bits(toType);
		
		if (fromType == toType && fromSize == toSize) {
			return operands; // No conversion needed
		}

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

		if (fromSize < toSize) {
			// Extension needed
			ConversionOp conv_op{
				.from = toTypedValue(std::span<const IrOperand>(operands.data(), operands.size())),
				.to_type = toType,
				.to_size = toSize,
				.result = resultVar
			};
		
			if (is_signed_integer_type(fromType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::SignExtend, std::move(conv_op), source_token));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::ZeroExtend, std::move(conv_op), source_token));
			}
		} else if (fromSize > toSize) {
			// Truncation needed
			ConversionOp conv_op{
				.from = toTypedValue(std::span<const IrOperand>(operands.data(), operands.size())),
				.to_type = toType,
				.to_size = toSize,
				.result = resultVar
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Truncate, std::move(conv_op), source_token));
		}
		// Return the converted operands
		return { toType, toSize, resultVar };
	}

	std::vector<IrOperand>
		generateStringLiteralIr(const StringLiteralNode& stringLiteralNode) {
		// Generate IR for string literal
		// Create a temporary variable to hold the address of the string
		TempVar result_var = var_counter.next();

		// Add StringLiteral IR instruction using StringLiteralOp
		StringLiteralOp op;
		op.result = result_var;
		op.content = stringLiteralNode.value();

		ir_.addInstruction(IrInstruction(IrOpcode::StringLiteral, std::move(op), Token()));

		// Return the result as a char pointer (const char*)
		// We use Type::Char with 64-bit size to indicate it's a pointer
		return { Type::Char, 64, result_var };
	}

	std::vector<IrOperand> generateUnaryOperatorIr(const UnaryOperatorNode& unaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		auto tryBuildIdentifierOperand = [&](const IdentifierNode& identifier, std::vector<IrOperand>& out) -> bool {
			std::string identifier_name(identifier.name());

			// Static local variables are stored as globals with mangled names
			auto static_local_it = static_local_names_.find(identifier_name);
			if (static_local_it != static_local_names_.end()) {
				out.clear();
				out.emplace_back(static_local_it->second.type);
				out.emplace_back(static_cast<int>(static_local_it->second.size_in_bits));
				out.emplace_back(static_local_it->second.mangled_name);
				return true;
			}

			std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(identifier.name());
			}
			if (!symbol.has_value()) {
				return false;
			}

			const TypeSpecifierNode* type_node = nullptr;
			if (symbol->is<DeclarationNode>()) {
				type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			} else if (symbol->is<VariableDeclarationNode>()) {
				type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
			} else {
				return false;
			}

			out.clear();
			out.emplace_back(type_node->type());
			out.emplace_back(static_cast<int>(type_node->size_in_bits()));
			out.emplace_back(identifier.name());
			return true;
		};

		// Special handling for &arr[index] - generate address directly without loading value
		if (unaryOperatorNode.op() == "&" && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<ArraySubscriptNode>(operandExpr)) {
				const ArraySubscriptNode& arraySubscript = std::get<ArraySubscriptNode>(operandExpr);
				
				// Get the array and index operands
				auto array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
				auto index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());
				
				Type element_type = std::get<Type>(array_operands[0]);
				int element_size_bits = std::get<int>(array_operands[1]);
				
				// Create temporary for the address
				TempVar addr_var = var_counter.next();
				
				// Create typed payload for ArrayElementAddress
				ArrayElementAddressOp payload;
				payload.result = addr_var;
				payload.element_type = element_type;
				payload.element_size_in_bits = element_size_bits;
				
				// Set array (either variable name or temp)
				if (std::holds_alternative<std::string_view>(array_operands[2])) {
					payload.array = std::get<std::string_view>(array_operands[2]);
				} else if (std::holds_alternative<TempVar>(array_operands[2])) {
					payload.array = std::get<TempVar>(array_operands[2]);
				}
				
				// Set index as TypedValue
				payload.index = toTypedValue(std::span<const IrOperand>(&index_operands[0], 3)); 
				
				ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(payload), arraySubscript.bracket_token()));
				
				// Return pointer to element (64-bit pointer)
				return { element_type, 64, addr_var };
			}
		}

		std::vector<IrOperand> operandIrOperands;
		bool operandHandledAsIdentifier = false;
		if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--") && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
				operandHandledAsIdentifier = tryBuildIdentifierOperand(identifier, operandIrOperands);
			}
		}

		if (!operandHandledAsIdentifier) {
			operandIrOperands = visitExpressionNode(unaryOperatorNode.get_operand().as<ExpressionNode>());
		}

		// Get the type of the operand
		Type operandType = std::get<Type>(operandIrOperands[0]);
		int operandSize = std::get<int>(operandIrOperands[1]);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Generate the IR for the operation based on the operator
		if (unaryOperatorNode.op() == "!") {
			// Logical NOT - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalNot, unary_op, Token()));
		}
		else if (unaryOperatorNode.op() == "~") {
			// Bitwise NOT - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			ir_.addInstruction(IrInstruction(IrOpcode::BitwiseNot, unary_op, Token()));
		}
		else if (unaryOperatorNode.op() == "-") {
			// Unary minus (negation) - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Negate, unary_op, Token()));
		}
		else if (unaryOperatorNode.op() == "+") {
			// Unary plus (no-op, just return the operand)
			return operandIrOperands;
		}
		else if (unaryOperatorNode.op() == "++") {
			// Increment operator (prefix or postfix) - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};

			if (unaryOperatorNode.is_prefix()) {
				// Prefix increment: ++x
				ir_.addInstruction(IrInstruction(IrOpcode::PreIncrement, unary_op, Token()));
			} else {
				// Postfix increment: x++
				ir_.addInstruction(IrInstruction(IrOpcode::PostIncrement, unary_op, Token()));
			}
		}
		else if (unaryOperatorNode.op() == "--") {
			// Decrement operator (prefix or postfix) - use UnaryOp struct
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};

			if (unaryOperatorNode.is_prefix()) {
				// Prefix decrement: --x
				ir_.addInstruction(IrInstruction(IrOpcode::PreDecrement, unary_op, Token()));
			} else {
				// Postfix decrement: x--
				ir_.addInstruction(IrInstruction(IrOpcode::PostDecrement, unary_op, Token()));
			}
		}
		else if (unaryOperatorNode.op() == "&") {
			// Address-of operator: &x
			// Create typed payload
			AddressOfOp op;
			op.result = result_var;
			op.pointee_type = operandType;
			op.pointee_size_in_bits = std::get<int>(operandIrOperands[1]);
			
			// Get the operand - it's at index 2 in operandIrOperands
			if (std::holds_alternative<std::string_view>(operandIrOperands[2])) {
				op.operand = std::get<std::string_view>(operandIrOperands[2]);
			} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
				op.operand = std::get<TempVar>(operandIrOperands[2]);
			} else if (std::holds_alternative<std::string>(operandIrOperands[2])) {
				op.operand = std::get<std::string>(operandIrOperands[2]);
			} else {
				assert(false && "AddressOf operand must be string_view, string, or TempVar");
			}
			
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, op, Token()));
			// Return 64-bit pointer
			return { operandType, 64, result_var };
		}
		else if (unaryOperatorNode.op() == "*") {
			// Dereference operator: *x
			// When dereferencing a pointer, the result size is the pointed-to type size,
			// not the pointer size (64). Use the Type enum to get the actual element size.
			int element_size;
			switch (operandType) {
				case Type::Bool: element_size = 8; break;
				case Type::Char: element_size = 8; break;
				case Type::Short: element_size = 16; break;
				case Type::Int: element_size = 32; break;
				case Type::Long: element_size = 64; break;
				case Type::Float: element_size = 32; break;
				case Type::Double: element_size = 64; break;
				default: element_size = 64; break;  // Fallback for unknown types
			}
		
			// Create typed payload
			DereferenceOp op;
			op.result = result_var;
			op.pointee_type = operandType;
			op.pointee_size_in_bits = element_size;
		
			// Get the pointer operand - it's at index 2 in operandIrOperands
			if (std::holds_alternative<std::string_view>(operandIrOperands[2])) {
				op.pointer = std::get<std::string_view>(operandIrOperands[2]);
			} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
				op.pointer = std::get<TempVar>(operandIrOperands[2]);
			} else {
				assert(false && "Dereference pointer must be string_view or TempVar");
			}
		
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, op, Token()));
		
			// Return the dereferenced value with the element type size
			return { operandType, element_size, result_var };
		}
		else {
			assert(false && "Unary operator not implemented yet");
		}

		// Return the result
		return { operandType, std::get<int>(operandIrOperands[1]), result_var };
	}

	std::vector<IrOperand> generateTernaryOperatorIr(const TernaryOperatorNode& ternaryNode) {
		// Ternary operator: condition ? true_expr : false_expr
		// Generate IR:
		// 1. Evaluate condition
		// 2. Conditional branch to true or false label
		// 3. Label for true branch, evaluate true_expr, assign to result, jump to end
		// 4. Label for false branch, evaluate false_expr, assign to result
		// 5. Label for end (both branches merge here)

		// Generate unique labels for this ternary
		static size_t ternary_counter = 0;
		StringBuilder true_sb, false_sb, end_sb;
		true_sb.append("ternary_true_").append(static_cast<int>(ternary_counter));
		false_sb.append("ternary_false_").append(static_cast<int>(ternary_counter));
		end_sb.append("ternary_end_").append(static_cast<int>(ternary_counter));
		std::string_view true_label = true_sb.commit();
		std::string_view false_label = false_sb.commit();
		std::string_view end_label = end_sb.commit();
		ternary_counter++;		// Evaluate the condition

		auto condition_operands = visitExpressionNode(ternaryNode.condition().as<ExpressionNode>());
	
		// Generate conditional branch: if condition true goto true_label, else goto false_label
		CondBranchOp cond_branch;
		cond_branch.label_true = true_label;
		cond_branch.label_false = false_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), ternaryNode.get_token()));

		// True branch label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = true_label}, ternaryNode.get_token()));
		
		// Evaluate true expression
		auto true_operands = visitExpressionNode(ternaryNode.true_expr().as<ExpressionNode>());
		
		// Create result variable to hold the final value
		TempVar result_var = var_counter.next();
		Type result_type = std::get<Type>(true_operands[0]);
		int result_size = std::get<int>(true_operands[1]);
		
		// Assign true_expr result to result variable
		AssignmentOp assign_true_op;
		assign_true_op.result = result_var;
		assign_true_op.lhs.type = result_type;
		assign_true_op.lhs.size_in_bits = result_size;
		assign_true_op.lhs.value = result_var;
		assign_true_op.rhs = toTypedValue(true_operands);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_true_op), ternaryNode.get_token()));
		
		// Unconditional branch to end
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = end_label}, ternaryNode.get_token()));

		// False branch label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = false_label}, ternaryNode.get_token()));
	
		// Evaluate false expression
		auto false_operands = visitExpressionNode(ternaryNode.false_expr().as<ExpressionNode>());

		// Assign false_expr result to result variable
		AssignmentOp assign_false_op;
		assign_false_op.result = result_var;
		assign_false_op.lhs.type = result_type;
		assign_false_op.lhs.size_in_bits = result_size;
		assign_false_op.lhs.value = result_var;
		assign_false_op.rhs = toTypedValue(false_operands);
		ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_false_op), ternaryNode.get_token()));
		
		// End label (merge point)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = end_label}, ternaryNode.get_token()));
		
		// Return the result variable
		return { result_type, result_size, result_var };
	}

	std::vector<IrOperand> generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		const auto& op = binaryOperatorNode.op();

		// Special handling for comma operator
		// The comma operator evaluates both operands left-to-right and returns the right operand
		if (op == ",") {
			// Generate IR for the left-hand side (evaluate for side effects, discard result)
			auto lhsIrOperands = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>());

			// Generate IR for the right-hand side (this is the result)
			auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

			// Return the right-hand side result
			return rhsIrOperands;
		}

		// Special handling for assignment to array subscript
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<ArraySubscriptNode>(lhs_expr)) {
				// This is an array subscript assignment: arr[index] = value
				const ArraySubscriptNode& array_subscript = std::get<ArraySubscriptNode>(lhs_expr);

				// Generate IR for the RHS value
				auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

				// Get array information FIRST to check if it's a member array
				const ExpressionNode& array_expr = array_subscript.array_expr().as<ExpressionNode>();
				
				// Check if this is a member array access: obj.array[index] = value
				if (std::holds_alternative<MemberAccessNode>(array_expr)) {
					const MemberAccessNode& member_access = std::get<MemberAccessNode>(array_expr);
					const ASTNode& object_node = member_access.object();
					std::string_view member_name = member_access.member_name();

					// Handle this->member[index] or obj.member[index]
					if (object_node.is<ExpressionNode>()) {
						const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(obj_expr)) {
							const IdentifierNode& object_ident = std::get<IdentifierNode>(obj_expr);
							std::string_view object_name = object_ident.name();

							// Look up the object in the symbol table
							const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
							if (symbol.has_value() && symbol->is<DeclarationNode>()) {
								const auto& decl_node = symbol->as<DeclarationNode>();
								const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

								if (type_node.type() == Type::Struct || type_node.type() == Type::UserDefined) {
									// Get the struct type info
									TypeIndex struct_type_index = type_node.type_index();
									if (struct_type_index < gTypeInfo.size()) {
										const TypeInfo& struct_type_info = gTypeInfo[struct_type_index];
										const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
										
										if (struct_info) {
											const StructMember* member = struct_info->findMember(std::string(member_name));
											if (member) {
												// Get index information
												auto indexIrOperands = visitExpressionNode(array_subscript.index_expr().as<ExpressionNode>());

												// Determine element type and size
												// For arrays, member->type is the array element type
												// member->size is the total array size in bytes
												// Calculate element size from member type
												Type element_type = member->type;
												int element_size_bytes = 0;
												
												// Get element size based on type
												if (element_type == Type::Int || element_type == Type::UnsignedInt) {
													element_size_bytes = 4;
												} else if (element_type == Type::Long || element_type == Type::UnsignedLong) {
													element_size_bytes = 8;
												} else if (element_type == Type::Short || element_type == Type::UnsignedShort) {
													element_size_bytes = 2;
												} else if (element_type == Type::Char || element_type == Type::UnsignedChar) {
													element_size_bytes = 1;
												} else if (element_type == Type::Bool) {
													element_size_bytes = 1;
												} else if (element_type == Type::Float) {
													element_size_bytes = 4;
												} else if (element_type == Type::Double) {
													element_size_bytes = 8;
												} else if (element_type == Type::Struct || element_type == Type::UserDefined) {
													// For struct types, get size from type info
													if (member->type_index < gTypeInfo.size()) {
														const TypeInfo& elem_type_info = gTypeInfo[member->type_index];
														if (elem_type_info.getStructInfo()) {
															element_size_bytes = static_cast<int>(elem_type_info.getStructInfo()->total_size);
														}
													}
												}
												
												int element_size = element_size_bytes * 8;  // Convert to bits

												// Create typed payload for member ArrayStore
												ArrayStoreOp payload;
												payload.element_type = element_type;
												payload.element_size_in_bits = element_size;
												payload.array = std::string(object_name) + "." + std::string(member_name);
												payload.member_offset = static_cast<int64_t>(member->offset);
												
												// Set index as TypedValue
												payload.index.type = std::get<Type>(indexIrOperands[0]);
												payload.index.size_in_bits = std::get<int>(indexIrOperands[1]);
												if (std::holds_alternative<unsigned long long>(indexIrOperands[2])) {
													payload.index.value = std::get<unsigned long long>(indexIrOperands[2]);
												} else if (std::holds_alternative<TempVar>(indexIrOperands[2])) {
													payload.index.value = std::get<TempVar>(indexIrOperands[2]);
												} else if (std::holds_alternative<std::string_view>(indexIrOperands[2])) {
													payload.index.value = std::get<std::string_view>(indexIrOperands[2]);
												}
												
												// Set value as TypedValue
												payload.value.type = std::get<Type>(rhsIrOperands[0]);
												payload.value.size_in_bits = std::get<int>(rhsIrOperands[1]);
												if (std::holds_alternative<unsigned long long>(rhsIrOperands[2])) {
													payload.value.value = std::get<unsigned long long>(rhsIrOperands[2]);
												} else if (std::holds_alternative<TempVar>(rhsIrOperands[2])) {
													payload.value.value = std::get<TempVar>(rhsIrOperands[2]);
												} else if (std::holds_alternative<std::string_view>(rhsIrOperands[2])) {
													payload.value.value = std::get<std::string_view>(rhsIrOperands[2]);
												}

												ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(payload), binaryOperatorNode.get_token()));

												// Return the RHS value as the result
												return rhsIrOperands;
											}
										}
									}
								}
							}
						}
					}
					// If we couldn't handle the member array access, fall through to error
					return { Type::Int, 32, TempVar{0} };
				}
				
				// NOT a member array - handle as regular array subscript
				// Generate IR for the array subscript to get the address
				auto arrayAccessIrOperands = generateArraySubscriptIr(array_subscript);

				// The arrayAccessIrOperands contains [type, size, temp_var]
				// where temp_var holds the address of the array element
				// We need to generate an ArrayStore instruction
				// Format: [element_type, element_size, array_name, index_type, index_size, index_value, value]

				if (!std::holds_alternative<IdentifierNode>(array_expr)) {
					// Error: array must be an identifier for now
					return { Type::Int, 32, TempVar{0} };
				}

				const IdentifierNode& array_ident = std::get<IdentifierNode>(array_expr);
				std::string_view array_name = array_ident.name();

				// Get index information
				auto indexIrOperands = visitExpressionNode(array_subscript.index_expr().as<ExpressionNode>());

				// Create typed payload for ArrayStore
				ArrayStoreOp payload;
				payload.element_type = std::get<Type>(arrayAccessIrOperands[0]);
				payload.element_size_in_bits = std::get<int>(arrayAccessIrOperands[1]);
				payload.array = array_name;
				payload.member_offset = 0;  // Not a member array
				
				// Set index as TypedValue
				payload.index.type = std::get<Type>(indexIrOperands[0]);
				payload.index.size_in_bits = std::get<int>(indexIrOperands[1]);
				if (std::holds_alternative<unsigned long long>(indexIrOperands[2])) {
					payload.index.value = std::get<unsigned long long>(indexIrOperands[2]);
				} else if (std::holds_alternative<TempVar>(indexIrOperands[2])) {
					payload.index.value = std::get<TempVar>(indexIrOperands[2]);
				} else if (std::holds_alternative<std::string_view>(indexIrOperands[2])) {
					payload.index.value = std::get<std::string_view>(indexIrOperands[2]);
				}
				
				// Set value as TypedValue
				payload.value.type = std::get<Type>(rhsIrOperands[0]);
				payload.value.size_in_bits = std::get<int>(rhsIrOperands[1]);
				if (std::holds_alternative<unsigned long long>(rhsIrOperands[2])) {
					payload.value.value = std::get<unsigned long long>(rhsIrOperands[2]);
				} else if (std::holds_alternative<TempVar>(rhsIrOperands[2])) {
					payload.value.value = std::get<TempVar>(rhsIrOperands[2]);
				} else if (std::holds_alternative<std::string_view>(rhsIrOperands[2])) {
					payload.value.value = std::get<std::string_view>(rhsIrOperands[2]);
				}

				ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(payload), binaryOperatorNode.get_token()));

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

						if (type_node.type() != Type::Struct && type_node.type() != Type::UserDefined) {
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

						// Build MemberStore operation
						IrValue member_value;
						// Add only the value from RHS (rhsIrOperands = [type, size, value])
						// We only need the value (index 2)
						if (rhsIrOperands.size() >= 3) {
							// Extract value from rhsIrOperands[2]
							if (std::holds_alternative<TempVar>(rhsIrOperands[2])) {
								member_value = std::get<TempVar>(rhsIrOperands[2]);
							} else if (std::holds_alternative<unsigned long long>(rhsIrOperands[2])) {
								member_value = std::get<unsigned long long>(rhsIrOperands[2]);
							} else if (std::holds_alternative<double>(rhsIrOperands[2])) {
								member_value = std::get<double>(rhsIrOperands[2]);
							} else if (std::holds_alternative<std::string_view>(rhsIrOperands[2])) {
								member_value = std::get<std::string_view>(rhsIrOperands[2]);
							} else {
								member_value = 0ULL;  // fallback
							}
						} else {
							// Error: invalid RHS operands
							return { Type::Int, 32, TempVar{0} };
						}

						MemberStoreOp member_store;
						member_store.value.type = member->type;
						member_store.value.size_in_bits = static_cast<int>(member->size * 8);
						member_store.value.value = member_value;
						member_store.object = object_name;
						member_store.member_name = member_name;
						member_store.offset = static_cast<int>(member->offset);
						member_store.is_reference = member->is_reference;
						member_store.is_rvalue_reference = member->is_rvalue_reference;
						member_store.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), binaryOperatorNode.get_token()));

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

		// Special handling for assignment to member variables in member functions
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>() && !current_struct_name_.empty()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
				std::string_view lhs_name = lhs_ident.name();

				// Check if this is a member variable of the current struct
				auto type_it = gTypesByName.find(std::string(current_struct_name_));
				if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
					const StructTypeInfo* struct_info = type_it->second->getStructInfo();
					if (struct_info) {
						const StructMember* member = struct_info->findMemberRecursive(std::string(lhs_name));
						if (member) {
						// This is an assignment to a member variable: member = value
						// Generate MemberStore instead of regular Assignment

						// Generate IR for the RHS value
						auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

						// Build MemberStore operation
						IrValue member_value;
						// Add only the value from RHS (rhsIrOperands = [type, size, value])
						if (rhsIrOperands.size() >= 3) {
							// Extract value from rhsIrOperands[2]
							if (std::holds_alternative<TempVar>(rhsIrOperands[2])) {
								member_value = std::get<TempVar>(rhsIrOperands[2]);
							} else if (std::holds_alternative<unsigned long long>(rhsIrOperands[2])) {
								member_value = std::get<unsigned long long>(rhsIrOperands[2]);
							} else if (std::holds_alternative<double>(rhsIrOperands[2])) {
								member_value = std::get<double>(rhsIrOperands[2]);
							} else if (std::holds_alternative<std::string_view>(rhsIrOperands[2])) {
								member_value = std::get<std::string_view>(rhsIrOperands[2]);
							} else {
								member_value = 0ULL;  // fallback
							}
						} else {
							// Error: invalid RHS operands
							return { Type::Int, 32, TempVar{0} };
						}

						MemberStoreOp member_store;
						member_store.value.type = member->type;
						member_store.value.size_in_bits = static_cast<int>(member->size * 8);
						member_store.value.value = member_value;
						member_store.object = std::string_view("this");  // implicit this pointer
						member_store.member_name = lhs_name;
						member_store.offset = static_cast<int>(member->offset);
						member_store.is_reference = member->is_reference;
						member_store.is_rvalue_reference = member->is_rvalue_reference;
						member_store.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), binaryOperatorNode.get_token()));							// Return the RHS value as the result
							return rhsIrOperands;
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

						// Generate Assignment IR using typed payload
						TempVar result_var = var_counter.next();
						AssignmentOp assign_op;
						assign_op.result = result_var;
						assign_op.lhs.type = lhs_type.type();
						assign_op.lhs.size_in_bits = static_cast<int>(lhs_type.size_in_bits());
						assign_op.lhs.value = lhs_name;
						assign_op.rhs = toTypedValue(rhsIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
						
						// Return the result
						return { lhs_type.type(), static_cast<int>(lhs_type.size_in_bits()), result_var };
					}
				}
			}
		}

		// Generate IR for the left-hand side and right-hand side of the operation
		auto lhsIrOperands = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>());
		auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

		// Get the types and sizes of the operands
		Type lhsType = std::get<Type>(lhsIrOperands[0]);
		Type rhsType = std::get<Type>(rhsIrOperands[0]);
		int lhsSize = std::get<int>(lhsIrOperands[1]);
		int rhsSize = std::get<int>(rhsIrOperands[1]);

		// Try to get pointer depth for pointer arithmetic
		int lhs_pointer_depth = 0;
		if (binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const auto& lhs_id = std::get<IdentifierNode>(lhs_expr);
				auto symbol = symbol_table.lookup(lhs_id.name());
				if (symbol && symbol->is<VariableDeclarationNode>()) {
					const auto& var_decl = symbol->as<VariableDeclarationNode>();
					const auto& decl = var_decl.declaration();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					lhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
				} else if (symbol && symbol->is<DeclarationNode>()) {
					const auto& decl = symbol->as<DeclarationNode>();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					lhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
				}
			}
		}

		// Special handling for pointer arithmetic (ptr + int or ptr - int)
		if ((op == "+" || op == "-") && lhsSize == 64 && is_integer_type(rhsType)) {
			// Left side is a pointer (64-bit), right side is integer
			// Result should be a pointer (64-bit)
			// Need to scale the offset by sizeof(pointed-to-type)
		
			// Determine element size based on pointer depth:
			// - int*   (pointer_depth=1): element_size = sizeof(int) = 4
			// - int**  (pointer_depth=2): element_size = sizeof(int*) = 8
			// - int*** (pointer_depth=3): element_size = sizeof(int**) = 8
			int element_size;
			if (lhs_pointer_depth > 1) {
				// Multi-level pointer: element is a pointer, so 8 bytes
				element_size = 8;
			} else {
				// Single-level pointer: element size is the base type size
				switch (lhsType) {
					case Type::Bool: element_size = 1; break;
					case Type::Char: element_size = 1; break;
					case Type::Short: element_size = 2; break;
					case Type::Int: element_size = 4; break;
					case Type::Long: element_size = 8; break;
					case Type::Float: element_size = 4; break;
					case Type::Double: element_size = 8; break;
					case Type::Struct: element_size = 8; break;  // Struct pointers
					default: element_size = 8; break;  // Fallback for unknown types
				}
			}
		
			// Scale the offset: offset_scaled = offset * element_size
			TempVar scaled_offset = var_counter.next();
			
		// Use typed BinaryOp for the multiply operation
		BinaryOp scale_op{
			.lhs = toTypedValue(rhsIrOperands),
			.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
			.result = scaled_offset,
		};
		ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(scale_op), binaryOperatorNode.get_token()));
	
		// Now add the scaled offset to the pointer
		TempVar result_var = var_counter.next();
		
		// Use typed BinaryOp for pointer addition/subtraction
		BinaryOp ptr_arith_op{
			.lhs = { lhsType, lhsSize, toIrValue(lhsIrOperands[2]) },
			.rhs = { Type::Int, 32, scaled_offset },
			.result = result_var,
		};
		
		IrOpcode ptr_opcode = (op == "+") ? IrOpcode::Add : IrOpcode::Subtract;
		ir_.addInstruction(IrInstruction(ptr_opcode, std::move(ptr_arith_op), binaryOperatorNode.get_token()));

			// Return pointer type with 64-bit size
			return { lhsType, 64, result_var };
		}
	
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

		// Generate the IR for the operation based on the operator and operand types
		// Use a lookup table approach for better performance and maintainability
		IrOpcode opcode;

		// New typed operand goes in here. Goal is that all operands live here
		static const std::unordered_map<std::string_view, IrOpcode> bin_ops = {
			{"+", IrOpcode::Add}, {"-", IrOpcode::Subtract}, {"*", IrOpcode::Multiply},
			{"<<", IrOpcode::ShiftLeft}, {"%", IrOpcode::Modulo},
			{"&", IrOpcode::BitwiseAnd}, {"|", IrOpcode::BitwiseOr}, {"^", IrOpcode::BitwiseXor}
		};

		auto bin_ops_it = !is_floating_point_op ? bin_ops.find(op) : bin_ops.end();
		if (bin_ops_it != bin_ops.end()) {
			opcode = bin_ops_it->second;

			// Use fully typed instruction (zero vector allocation!)
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Division operations (typed)
		else if (op == "/" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedDivide : IrOpcode::Divide;
			
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Right shift operations (typed)
		else if (op == ">>") {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedShiftRight : IrOpcode::ShiftRight;
			
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Logical operations (typed, always i1/bool)
		else if (op == "&&") {
			BinaryOp bin_op{
				.lhs = { Type::Bool, 1, toIrValue(lhsIrOperands[2]) },
				.rhs = { Type::Bool, 1, toIrValue(rhsIrOperands[2]) },
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalAnd, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "||") {
			BinaryOp bin_op{
				.lhs = { Type::Bool, 1, toIrValue(lhsIrOperands[2]) },
				.rhs = { Type::Bool, 1, toIrValue(rhsIrOperands[2]) },
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalOr, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Comparison operations (typed)
		else if (op == "==" && !is_floating_point_op) {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Equal, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "!=" && !is_floating_point_op) {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "<" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedLessThan : IrOpcode::LessThan;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "<=" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedLessEqual : IrOpcode::LessEqual;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == ">" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedGreaterThan : IrOpcode::GreaterThan;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == ">=" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedGreaterEqual : IrOpcode::GreaterEqual;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Compound assignment operations (typed)
		// For compound assignments, result is stored back in LHS variable
		else if (op == "+=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),  // Store result in LHS variable
			};
			ir_.addInstruction(IrInstruction(IrOpcode::AddAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "-=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::SubAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "*=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::MulAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "/=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::DivAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "%=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ModAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "&=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::AndAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "|=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::OrAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "^=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::XorAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "<<=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ShlAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == ">>=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ShrAssign, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else { // Build operands differently based on whether we're using typed struct
			// Float operations use typed BinaryOp
			if (is_floating_point_op && (op == "+" || op == "-" || op == "*" || op == "/")) {
				// Determine float opcode
				IrOpcode float_opcode;
				if (op == "+") float_opcode = IrOpcode::FloatAdd;
				else if (op == "-") float_opcode = IrOpcode::FloatSubtract;
				else if (op == "*") float_opcode = IrOpcode::FloatMultiply;
				else if (op == "/") float_opcode = IrOpcode::FloatDivide;
				else {
					assert(false && "Unsupported float operator");
					return {};
				}

			// Create typed BinaryOp for float arithmetic
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};

			ir_.addInstruction(IrInstruction(float_opcode, std::move(bin_op), binaryOperatorNode.get_token()));			// Return the result variable with float type and size
				return { commonType, get_type_size_bits(commonType), result_var };
			}

			// Float comparison operations use typed BinaryOp
			if (is_floating_point_op && (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=")) {
				// Determine float comparison opcode
				IrOpcode float_cmp_opcode;
				if (op == "==") float_cmp_opcode = IrOpcode::FloatEqual;
				else if (op == "!=") float_cmp_opcode = IrOpcode::FloatNotEqual;
				else if (op == "<") float_cmp_opcode = IrOpcode::FloatLessThan;
				else if (op == "<=") float_cmp_opcode = IrOpcode::FloatLessEqual;
				else if (op == ">") float_cmp_opcode = IrOpcode::FloatGreaterThan;
				else if (op == ">=") float_cmp_opcode = IrOpcode::FloatGreaterEqual;
				else {
					assert(false && "Unsupported float comparison operator");
					return {};
				}

				// Create typed BinaryOp for float comparison
				BinaryOp bin_op{
					.lhs = toTypedValue(lhsIrOperands),
					.rhs = toTypedValue(rhsIrOperands),
					.result = result_var,
				};

				ir_.addInstruction(IrInstruction(float_cmp_opcode, std::move(bin_op), binaryOperatorNode.get_token()));

				// Float comparisons return boolean (i1)
				return { Type::Bool, 1, result_var };
			}

			// Assignment using typed payload
			if (op == "=") {
				AssignmentOp assign_op;
				assign_op.result = result_var;
				assign_op.lhs = toTypedValue(lhsIrOperands);
				assign_op.rhs = toTypedValue(rhsIrOperands);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			}
			else {
				assert(false && "Unsupported binary operator in this code path");
				return {};
			}
		}
	
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
	std::string_view generateMangledNameForCall(const std::string& name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& param_types, bool is_variadic = false, std::string_view struct_name = "") {
		// Special case: main function is never mangled
		if (name == "main") {
			return "main";
		}

		StringBuilder builder;
		builder.append('?');
		
		// Handle member functions vs free functions
		if (!struct_name.empty()) {
			// Member function: ?name@ClassName@@QA...
			//  Extract just the function name (after ::)
			std::string func_only_name = name;
			size_t pos = name.rfind("::");
			if (pos != std::string::npos) {
				func_only_name = name.substr(pos + 2);
			}
			builder.append(func_only_name);
			builder.append('@');
			builder.append(struct_name);
			builder.append("@@QA");  // @@ + calling convention for member functions (Q = __thiscall-like)
		} else {
			// Free function: ?name@@YA...
			builder.append(name);
			builder.append("@@YA");  // @@ + calling convention (__cdecl)
		}

		// Add return type code
		appendTypeCodeForMangling(builder, return_type);

		// Add parameter type codes
		for (const auto& param_type : param_types) {
			appendTypeCodeForMangling(builder, param_type);
		}

		// End marker - different for variadic vs non-variadic
		if (is_variadic) {
			builder.append("ZZ");  // Variadic functions end with 'ZZ' in MSVC mangling
		} else {
			builder.append("@Z");  // Non-variadic functions end with '@Z'
		}

		return builder.commit();
	}

	// Helper to append type code for mangling to StringBuilder
	void appendTypeCodeForMangling(StringBuilder& builder, const TypeSpecifierNode& type_node) {
		NameMangling::appendTypeCode(builder, type_node);
	}	std::vector<IrOperand> generateFunctionCallIr(const FunctionCallNode& functionCallNode) {
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

		// Also try looking up in gSymbolTable directly for comparison
		extern SymbolTable gSymbolTable;
		auto gSymbolTable_overloads = gSymbolTable.lookup_all(decl_node.identifier_token().value());

		// Find the matching overload by comparing the DeclarationNode address
		// This works because the FunctionCallNode holds a reference to the specific
		// DeclarationNode that was selected by overload resolution
		std::vector<TypeSpecifierNode> param_types;  // Declare param_types here
		bool found_overload = false;
		for (const auto& overload : all_overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode* overload_func_decl = &overload.as<FunctionDeclarationNode>();
				const DeclarationNode* overload_decl = &overload_func_decl->decl_node();
				if (overload_decl == &decl_node) {
					// Found the matching overload
					const auto& func_decl = *overload_func_decl;

					// Extract parameter types for mangling (including pointer information)
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
						std::string_view struct_name_for_call = func_decl.is_member_function() ? func_decl.parent_struct_name() : "";
						function_name = generateMangledNameForCall(function_name, return_type, param_types, func_decl.is_variadic(), struct_name_for_call);
					}
					found_overload = true;
					break;
				}
			}
		}
	
		// Fallback: if pointer comparison failed (e.g., for template instantiations),
		// try to find the function by checking if there's only one overload with this name
		if (!found_overload && all_overloads.size() == 1 && all_overloads[0].is<FunctionDeclarationNode>()) {
			const auto& func_decl = all_overloads[0].as<FunctionDeclarationNode>();
		
			// Extract parameter types
			for (const auto& param_node : func_decl.parameter_nodes()) {
				if (param_node.is<DeclarationNode>()) {
					const auto& param_decl = param_node.as<DeclarationNode>();
					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					param_types.push_back(param_type);
				}
			}
		
			// Get return type
			const auto& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
		
			// Generate mangled name
			if (func_decl.linkage() != Linkage::C) {
				function_name = generateMangledNameForCall(function_name, return_type, param_types, func_decl.is_variadic());
			}
			found_overload = true;
		}
	
		// Always add the return variable and function name (mangled for overload resolution)
		TempVar ret_var = var_counter.next();
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(function_name);

		// Process arguments - match them with parameter types
		size_t arg_index = 0;
		functionCallNode.arguments().visit([&](ASTNode argument) {
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			
			// Get the parameter type for this argument (if it exists)
			const TypeSpecifierNode* param_type = (arg_index < param_types.size()) ? &param_types[arg_index] : nullptr;
			arg_index++;
			
			// For variables, we need to check if the parameter expects a reference
			if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier.name());
				}
				if (!symbol.has_value()) {
					std::cerr << "ERROR: Symbol '" << identifier.name() << "' not found for function argument\n";
					std::cerr << "  Current function: " << current_function_name_ << "\n";
					throw std::runtime_error("Missing symbol for function argument");
				}

				const DeclarationNode* decl_ptr = nullptr;
				if (symbol->is<DeclarationNode>()) {
					decl_ptr = &symbol->as<DeclarationNode>();
				} else if (symbol->is<VariableDeclarationNode>()) {
					decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
				}

				if (!decl_ptr) {
					std::cerr << "ERROR: Function argument '" << identifier.name() << "' is not a DeclarationNode\n";
					throw std::runtime_error("Unexpected symbol type for function argument");
				}

				const auto& decl_node = *decl_ptr;
				const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

				// Check if this is an array - arrays decay to pointers when passed to functions
				if (decl_node.is_array()) {
					// For arrays, we need to pass the address of the first element
					// Create a temporary for the address
					TempVar addr_var = var_counter.next();

					// Generate AddressOf IR instruction to get the address of the array
					AddressOfOp addr_op;
					addr_op.result = addr_var;
					addr_op.pointee_type = type_node.type();
					addr_op.pointee_size_in_bits = static_cast<int>(type_node.size_in_bits());
					addr_op.operand = identifier.name();
					ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

					// Add the pointer (address) to the function call operands
					// For now, we use the element type with 64-bit size to indicate it's a pointer
					// TODO: Add proper pointer type support to the Type enum
					irOperands.emplace_back(type_node.type());  // Element type (e.g., Char for char[])
					irOperands.emplace_back(64);  // Pointer size is 64 bits on x64
					irOperands.emplace_back(addr_var);
				} else if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
					// Parameter expects a reference - pass the address of the argument
					if (type_node.is_reference() || type_node.is_rvalue_reference()) {
						// Argument is already a reference - just pass it through
						irOperands.emplace_back(type_node.type());
						irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
						irOperands.emplace_back(identifier.name());
					} else {
						// Argument is a value - take its address
						TempVar addr_var = var_counter.next();

						AddressOfOp addr_op;
						addr_op.result = addr_var;
						addr_op.pointee_type = type_node.type();
						addr_op.pointee_size_in_bits = static_cast<int>(type_node.size_in_bits());
						addr_op.operand = identifier.name();
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

						// Pass the address
						irOperands.emplace_back(type_node.type());
						irOperands.emplace_back(64);  // Pointer size
						irOperands.emplace_back(addr_var);
					}
				} else if (type_node.is_reference() || type_node.is_rvalue_reference()) {
					// Argument is a reference but parameter expects a value - dereference
				TempVar deref_var = var_counter.next();

				DereferenceOp deref_op;
				deref_op.result = deref_var;
				deref_op.pointee_type = type_node.type();
				deref_op.pointee_size_in_bits = static_cast<int>(type_node.size_in_bits());
				deref_op.pointer = identifier.name();
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));					// Pass the dereferenced value
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
					irOperands.emplace_back(deref_var);
				} else {
					// Regular variable - pass by value
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
					irOperands.emplace_back(identifier.name());
				}
			}
			else {
				// Not an identifier - could be a literal, expression result, etc.
				// Check if parameter expects a reference and argument is a literal
				if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
					// Parameter expects a reference, but argument is not an identifier
					// We need to materialize the value into a temporary and pass its address
					
					// Check if this is a literal value (has unsigned long long or double in operand[2])
					bool is_literal = (argumentIrOperands.size() >= 3 && 
					                  (std::holds_alternative<unsigned long long>(argumentIrOperands[2]) ||
					                   std::holds_alternative<double>(argumentIrOperands[2])));
					
					if (is_literal) {
						// Materialize the literal into a temporary variable
						Type literal_type = std::get<Type>(argumentIrOperands[0]);
						int literal_size = std::get<int>(argumentIrOperands[1]);
						
						// Create a temporary variable to hold the literal value
						TempVar temp_var = var_counter.next();
						
						// Generate an assignment IR to store the literal using typed payload
						AssignmentOp assign_op;
						assign_op.result = temp_var;  // unused but required
						
						// Convert IrOperand to IrValue for the literal
						IrValue rhs_value;
						if (std::holds_alternative<unsigned long long>(argumentIrOperands[2])) {
							rhs_value = std::get<unsigned long long>(argumentIrOperands[2]);
						} else if (std::holds_alternative<double>(argumentIrOperands[2])) {
							rhs_value = std::get<double>(argumentIrOperands[2]);
						}
						
						// Create TypedValue for lhs and rhs
						assign_op.lhs = TypedValue{literal_type, literal_size, temp_var};
						assign_op.rhs = TypedValue{literal_type, literal_size, rhs_value};
						
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
						
						// Now take the address of the temporary
						TempVar addr_var = var_counter.next();
						AddressOfOp addr_op;
						addr_op.result = addr_var;
						addr_op.pointee_type = literal_type;
						addr_op.pointee_size_in_bits = literal_size;
						addr_op.operand = temp_var;
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
						
						// Pass the address
						irOperands.emplace_back(literal_type);
						irOperands.emplace_back(64);  // Pointer size
						irOperands.emplace_back(addr_var);
					} else {
						// Not a literal - just pass through
						irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
					}
				} else {
					// Parameter doesn't expect a reference - pass through as-is
					irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
				}
			}
		});

		// Create CallOp structure
		CallOp call_op;
		call_op.result = ret_var;
		call_op.function_name = function_name;
		
		// Get return type information
		const auto& return_type = decl_node.type_node().as<TypeSpecifierNode>();
		call_op.return_type = return_type.type();
		call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
		call_op.is_member_function = false;
		
		// Convert operands to TypedValue arguments (skip first 2: result and function_name)
		for (size_t i = 2, e = irOperands.size(); i < irOperands.size(); i += 3) {
			assert(i + 2 < irOperands.size());
			call_op.args.push_back(toTypedValue(std::span<const IrOperand>(&irOperands[i], 3)));
		}

		// Add the function call instruction with typed payload
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), functionCallNode.called_from()));

		// Return the result variable with its type and size
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
				
				// If not found as member function, check if it's a function pointer data member
				if (!called_member_func) {
					for (const auto& member : struct_info->members) {
						if (member.name == func_name && member.type == Type::FunctionPointer) {
							// This is a call through a function pointer member!
							// Generate an indirect call instead of a member function call
							// TODO: Get actual return type from function signature stored in member's TypeSpecifierNode
							// For now, we assume int return type which works for most common cases
							
							TempVar ret_var = var_counter.next();
							std::vector<IrOperand> irOperands;
							irOperands.emplace_back(ret_var);
							
							// Get the function pointer member
							// We need to generate member access to get the pointer value
							TempVar func_ptr_temp = var_counter.next();
							
							// Generate member access IR to load the function pointer
							MemberLoadOp member_load;
							member_load.result.value = func_ptr_temp;
							member_load.result.type = member.type;
							member_load.result.size_in_bits = static_cast<int>(member.size * 8);  // Convert bytes to bits
							
							// Add object operand
							if (object_name.empty()) {
								// Use temp var
								// TODO: Need to handle object expression properly
								assert(false && "Function pointer member call on expression not yet supported");
							} else {
								member_load.object = object_name;
							}
							
							member_load.member_name = func_name;  // Member name
							member_load.offset = static_cast<int>(member.offset);  // Member offset
							member_load.is_reference = member.is_reference;
							member_load.is_rvalue_reference = member.is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
							
							// Now add the indirect call with the function pointer temp var
							irOperands.emplace_back(func_ptr_temp);
							
							// Add arguments
							memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
								auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
								irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
							});
							
							ir_.addInstruction(IrInstruction(IrOpcode::IndirectCall, std::move(irOperands), memberFunctionCallNode.called_from()));
							
							// Return with function pointer's return type
							// TODO: Need to get the actual return type from the function signature stored in the member's TypeSpecifierNode
							// For now, assume int return type (common case)
							return { Type::Int, 32, ret_var };
						}
					}
				}
			}
		}

		// Check if this is a member function template that needs instantiation
		if (struct_info) {
			std::string_view func_name = func_decl_node.identifier_token().value();
			std::string qualified_template_name = std::string(struct_info->name) + "::" + std::string(func_name);
			
			// DEBUG removed
			
			// Look up if this is a template
			auto template_opt = gTemplateRegistry.lookupTemplate(qualified_template_name);
			if (template_opt.has_value()) {
				// DEBUG removed
				if (template_opt->is<TemplateFunctionDeclarationNode>()) {
					// DEBUG removed
				// This is a member function template - we need to instantiate it
				
				// Deduce template argument types from call arguments
				std::vector<Type> arg_types;
				// DEBUG removed
				memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
					// DEBUG removed
					if (!argument.is<ExpressionNode>()) {
						std::cerr << "NO\n";
						return;
					}
					std::cerr << "YES\n";
					
					const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
					
					// DEBUG removed
					
					// Get type of argument - for literals, use the literal type
					if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
						const NumericLiteralNode& lit = std::get<NumericLiteralNode>(arg_expr);
						// DEBUG removed
						arg_types.push_back(lit.type());
					} else if (std::holds_alternative<IdentifierNode>(arg_expr)) {
						// Look up variable type
						const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);
						// DEBUG removed
						auto symbol_opt = symbol_table.lookup(ident.name());
						if (symbol_opt.has_value() && symbol_opt->is<DeclarationNode>()) {
							const DeclarationNode& decl = symbol_opt->as<DeclarationNode>();
							const TypeSpecifierNode& type = decl.type_node().as<TypeSpecifierNode>();
							// DEBUG removed
							arg_types.push_back(type.type());
						}
					} else {
						// DEBUG removed
					}
				});
				
				// DEBUG removed

				// Try to instantiate the template with deduced argument types
				if (!arg_types.empty()) {
					// Build instantiation key
					const TemplateFunctionDeclarationNode& template_func = template_opt->as<TemplateFunctionDeclarationNode>();
					
					std::vector<TemplateArgument> template_args;
					for (const auto& arg_type : arg_types) {
						template_args.push_back(TemplateArgument::makeType(arg_type));
					}
					
					// Check if we already have this instantiation
					TemplateInstantiationKey inst_key;
					inst_key.template_name = qualified_template_name;
					for (const auto& arg : template_args) {
						if (arg.kind == TemplateArgument::Kind::Type) {
							inst_key.type_arguments.push_back(arg.type_value);
						}
					}
					
					auto existing_inst = gTemplateRegistry.getInstantiation(inst_key);
					if (!existing_inst.has_value()) {
						// Create new instantiation - collect it for deferred generation
						gTemplateRegistry.registerInstantiation(inst_key, template_func.function_declaration());
						
						// Get template parameter names
						std::vector<std::string_view> param_names;
						for (const auto& tparam_node : template_func.template_parameters()) {
							if (tparam_node.is<TemplateParameterNode>()) {
								param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
							}
						}
						
						// Generate the mangled name
						std::string_view mangled_func_name = TemplateRegistry::mangleTemplateName(func_name, template_args);
						
						// Template instantiation now happens during parsing
						// The instantiated function should already be in the AST
						// We just use the mangled name for the call
						
						/*
						// OLD: Collect this instantiation for deferred generation
						const FunctionDeclarationNode& template_func_decl = template_func.function_decl_node();
						if (template_func_decl.has_template_body_position()) {
							TemplateInstantiationInfo inst_info;
							inst_info.qualified_template_name = qualified_template_name;
							inst_info.mangled_name = std::string(mangled_func_name);
							inst_info.struct_name = std::string(struct_info->name);
							for (const auto& arg_type : arg_types) {
								inst_info.template_args.push_back(arg_type);
							}
							inst_info.body_position = template_func_decl.template_body_position();
							inst_info.template_param_names = param_names;
							inst_info.template_node_ptr = &template_func;
							
							// Collect the instantiation - it will be generated later at the top level
							// This ensures the FunctionDecl IR appears before any calls to it
							collected_template_instantiations_.push_back(std::move(inst_info));
						}
						*/
					}
				}
				} else {
					// DEBUG removed
				}
			} else {
				// DEBUG removed
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
			// Generate virtual function call using VirtualCallOp
			VirtualCallOp vcall_op;
			vcall_op.result = ret_var;
			vcall_op.object_type = object_type.type();
			vcall_op.object_size = static_cast<int>(object_type.size_in_bits());
			vcall_op.object = object_name;
			vcall_op.vtable_index = vtable_index;

			// Generate IR for function arguments
			memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
				auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
				
				// For variables, we need to add the type and size
				if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
					const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
					const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					const auto& decl_node = symbol->as<DeclarationNode>();
					const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
					
					TypedValue tv;
					tv.type = type_node.type();
					tv.size_in_bits = static_cast<int>(type_node.size_in_bits());
					tv.value = identifier.name();
					vcall_op.arguments.push_back(tv);
				}
				else {
					// Convert from IrOperand to TypedValue
					// Format: [type, size, value]
					if (argumentIrOperands.size() >= 3) {
						TypedValue tv = toTypedValue(argumentIrOperands);
						vcall_op.arguments.push_back(tv);
					}
				}
			});

			// Add the virtual call instruction
			ir_.addInstruction(IrInstruction(IrOpcode::VirtualCall, std::move(vcall_op), memberFunctionCallNode.called_from()));
		} else {
			// Generate regular (non-virtual) member function call using CallOp typed payload
			
			// Check if this is an instantiated template function
			std::string function_name;
			std::string_view func_name = func_decl_node.identifier_token().value();
			
			// Check if this is a member function - use struct_info to determine
			if (struct_info) {
				std::string_view struct_name = struct_info->name;
				std::string qualified_template_name = std::string(struct_name) + "::" + std::string(func_name);
				
				// Check if this is a template that has been instantiated
				auto template_opt = gTemplateRegistry.lookupTemplate(qualified_template_name);
				if (template_opt.has_value() && template_opt->is<TemplateFunctionDeclarationNode>()) {
					// This is a member function template - use the mangled name
					
					// Deduce template arguments from call arguments
					std::vector<TemplateArgument> template_args;
					memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
						if (!argument.is<ExpressionNode>()) return;
						const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
						
						// Get type of argument
						if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
							const NumericLiteralNode& lit = std::get<NumericLiteralNode>(arg_expr);
							template_args.push_back(TemplateArgument::makeType(lit.type()));
						} else if (std::holds_alternative<IdentifierNode>(arg_expr)) {
							const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);
							auto symbol_opt = symbol_table.lookup(ident.name());
							if (symbol_opt.has_value() && symbol_opt->is<DeclarationNode>()) {
								const DeclarationNode& decl = symbol_opt->as<DeclarationNode>();
								const TypeSpecifierNode& type = decl.type_node().as<TypeSpecifierNode>();
								template_args.push_back(TemplateArgument::makeType(type.type()));
							}
						}
					});
					
					// Generate the mangled name
					std::string_view mangled_func_name = TemplateRegistry::mangleTemplateName(func_name, template_args);
					
					// Build qualified function name with mangled template name
					function_name.reserve(struct_name.size() + 2 + mangled_func_name.size());
					function_name += struct_name;
					function_name += "::";
					function_name += mangled_func_name;
				} else {
					// Regular member function (not a template)
					function_name.reserve(struct_name.size() + 2 + func_name.size());
					function_name += struct_name;
					function_name += "::";
					function_name += func_name;
				}
			} else {
				// Non-member function or fallback
				function_name = std::string(func_name);
			}
			
			// Create CallOp structure
			CallOp call_op;
			call_op.result = ret_var;
			call_op.function_name = std::move(function_name);
			
			// Get return type information
			const auto& return_type = func_decl_node.type_node().as<TypeSpecifierNode>();
			call_op.return_type = return_type.type();
			call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
			call_op.is_member_function = true;
			
			// Add the object as the first argument (this pointer)
			call_op.args.push_back(TypedValue{
				.type = object_type.type(),
				.size_in_bits = static_cast<int>(object_type.size_in_bits()),
				.value = IrValue(object_name)
			});

			// Generate IR for function arguments and add to CallOp
			memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
				auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
				// For variables, we need to add the type and size
				if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
					const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
					const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					const auto& decl_node = symbol->as<DeclarationNode>();
					const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
					call_op.args.push_back(TypedValue{
						.type = type_node.type(),
						.size_in_bits = static_cast<int>(type_node.size_in_bits()),
						.value = IrValue(identifier.name())
					});
				}
				else {
					// Use toTypedValue helper to convert from operand vector
					call_op.args.push_back(toTypedValue(std::span<const IrOperand>(argumentIrOperands.data(), argumentIrOperands.size())));
				}
			});

			// Add the function call instruction with typed payload
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberFunctionCallNode.called_from()));
		}

		// Return the result variable with its type and size
		const auto& return_type = func_decl_node.type_node().as<TypeSpecifierNode>();
		return { return_type.type(), static_cast<int>(return_type.size_in_bits()), ret_var };
	}

	std::vector<IrOperand> generateArraySubscriptIr(const ArraySubscriptNode& arraySubscriptNode) {
		// Generate IR for array[index] expression
		// This computes the address: base_address + (index * element_size)

		// Check if the array expression is a member access (e.g., obj.array[index])
		const ExpressionNode& array_expr = arraySubscriptNode.array_expr().as<ExpressionNode>();
		if (std::holds_alternative<MemberAccessNode>(array_expr)) {
			const MemberAccessNode& member_access = std::get<MemberAccessNode>(array_expr);
			const ASTNode& object_node = member_access.object();
			std::string_view member_name = member_access.member_name();

			// Handle simple case: obj.array[index]
			if (object_node.is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const IdentifierNode& object_ident = std::get<IdentifierNode>(obj_expr);
					std::string_view object_name = object_ident.name();

					// Look up the object to get struct type
					const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

						if (type_node.type() == Type::Struct || type_node.type() == Type::UserDefined) {
							TypeIndex struct_type_index = type_node.type_index();
							if (struct_type_index < gTypeInfo.size()) {
								const TypeInfo& struct_type_info = gTypeInfo[struct_type_index];
								const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
								
								if (struct_info) {
									const StructMember* member = struct_info->findMember(std::string(member_name));
									if (member) {
										// Get index expression
										auto index_operands = visitExpressionNode(arraySubscriptNode.index_expr().as<ExpressionNode>());

										// Get element type and size from the member
										Type element_type = member->type;
										int element_size_bits = static_cast<int>(member->size * 8);
										
										// For array members, member->size is the total size, we need element size
										// This is a simplified assumption - we need better array type info
										// For now, assume arrays of primitives and compute element size
										int array_length = 1;  // Default if not an array
										// TODO: Get actual array length from type info
										// For now, use a heuristic: if size is larger than element type, it's an array
										int base_element_size = 0;
										if (element_type == Type::Int || element_type == Type::UnsignedInt) {
											base_element_size = 32;
										} else if (element_type == Type::Long || element_type == Type::UnsignedLong) {
											base_element_size = 64;
										} else if (element_type == Type::Short || element_type == Type::UnsignedShort) {
											base_element_size = 16;
										} else if (element_type == Type::Char || element_type == Type::UnsignedChar || element_type == Type::Bool) {
											base_element_size = 8;
										} else if (element_type == Type::Float) {
											base_element_size = 32;
										} else if (element_type == Type::Double) {
											base_element_size = 64;
										}
										
										if (base_element_size > 0 && element_size_bits > base_element_size) {
											// It's an array
											element_size_bits = base_element_size;
										}

										// Create a temporary variable for the result
										TempVar result_var = var_counter.next();

										// Create typed payload for ArrayAccess with qualified member name
										ArrayAccessOp payload;
										payload.result = result_var;
										payload.element_type = element_type;
										payload.element_size_in_bits = element_size_bits;
										payload.array = std::string(object_name) + "." + std::string(member_name);
										payload.member_offset = static_cast<int64_t>(member->offset);
										
										// Set index as TypedValue
										payload.index.type = std::get<Type>(index_operands[0]);
										payload.index.size_in_bits = std::get<int>(index_operands[1]);
										if (std::holds_alternative<unsigned long long>(index_operands[2])) {
											payload.index.value = std::get<unsigned long long>(index_operands[2]);
										} else if (std::holds_alternative<TempVar>(index_operands[2])) {
											payload.index.value = std::get<TempVar>(index_operands[2]);
										} else if (std::holds_alternative<std::string_view>(index_operands[2])) {
											payload.index.value = std::get<std::string_view>(index_operands[2]);
										}

										// Create instruction with typed payload
										ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));

										// Return the result with the element type
										return { element_type, element_size_bits, result_var };
									}
								}
							}
						}
					}
				}
			}
		}

		// Fall back to default handling for regular arrays
		// Get the array expression (should be an identifier for now)
		auto array_operands = visitExpressionNode(arraySubscriptNode.array_expr().as<ExpressionNode>());

		// Get the index expression
		auto index_operands = visitExpressionNode(arraySubscriptNode.index_expr().as<ExpressionNode>());

		// Get array type information
		Type element_type = std::get<Type>(array_operands[0]);
		int element_size_bits = std::get<int>(array_operands[1]);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Create typed payload for ArrayAccess
		ArrayAccessOp payload;
		payload.result = result_var;
		payload.element_type = element_type;
		payload.element_size_in_bits = element_size_bits;
		payload.member_offset = 0;  // Not a member array
		
		// Set array (either variable name or temp)
		if (std::holds_alternative<std::string_view>(array_operands[2])) {
			payload.array = std::get<std::string_view>(array_operands[2]);
		} else if (std::holds_alternative<TempVar>(array_operands[2])) {
			payload.array = std::get<TempVar>(array_operands[2]);
		}
		
		// Set index as TypedValue
		Type index_type = std::get<Type>(index_operands[0]);
		int index_size = std::get<int>(index_operands[1]);
		payload.index.type = index_type;
		payload.index.size_in_bits = index_size;
		
		if (std::holds_alternative<unsigned long long>(index_operands[2])) {
			payload.index.value = std::get<unsigned long long>(index_operands[2]);
		} else if (std::holds_alternative<TempVar>(index_operands[2])) {
			payload.index.value = std::get<TempVar>(index_operands[2]);
		} else if (std::holds_alternative<std::string_view>(index_operands[2])) {
			payload.index.value = std::get<std::string_view>(index_operands[2]);
		}

		// Create instruction with typed payload
		ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));

		// Return the result with the element type
		return { element_type, element_size_bits, result_var };
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

		// Build MemberLoadOp
		MemberLoadOp member_load;
		member_load.result.value = result_var;
		member_load.result.type = member->type;
		member_load.result.size_in_bits = static_cast<int>(member->size * 8);  // Convert bytes to bits

		// Add the base object (either string_view or TempVar)
		if (std::holds_alternative<std::string_view>(base_object)) {
			member_load.object = std::get<std::string_view>(base_object);
		} else {
			member_load.object = std::get<TempVar>(base_object);
		}

		member_load.member_name = std::string_view(member_name);
		member_load.offset = static_cast<int>(member->offset);
	
		// Add reference metadata (required for proper handling of reference members)
		member_load.is_reference = member->is_reference;
		member_load.is_rvalue_reference = member->is_rvalue_reference;
		member_load.struct_type_info = nullptr;

		// Add the member access instruction
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));		// Return the result variable with its type, size, and optionally type_index
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

			// Create PlacementNewOp
			PlacementNewOp op;
			op.result = result_var;
			op.type = type;
			op.size_in_bytes = size_in_bits / 8;
			op.pointer_depth = pointer_depth;
			// Convert IrOperand to IrValue
			if (std::holds_alternative<unsigned long long>(address_operands[2])) {
				op.address = std::get<unsigned long long>(address_operands[2]);
			} else if (std::holds_alternative<TempVar>(address_operands[2])) {
				op.address = std::get<TempVar>(address_operands[2]);
			} else if (std::holds_alternative<std::string_view>(address_operands[2])) {
				op.address = std::get<std::string_view>(address_operands[2]);
			} else if (std::holds_alternative<double>(address_operands[2])) {
				op.address = std::get<double>(address_operands[2]);
			}

			ir_.addInstruction(IrInstruction(IrOpcode::PlacementNew, std::move(op), Token()));

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
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name_;
							ctor_op.object = result_var;

							// Add constructor arguments
							const auto& ctor_args = newExpr.constructor_args();
							for (size_t i = 0; i < ctor_args.size(); ++i) {
								auto arg_operands = visitExpressionNode(ctor_args[i].as<ExpressionNode>());
								// arg_operands = [type, size, value]
								if (arg_operands.size() >= 3) {
									TypedValue tv = toTypedValue(arg_operands);
									ctor_op.arguments.push_back(std::move(tv));
								}
							}
							
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));
						}
					}
				}
			}
		} else if (newExpr.is_array()) {
			// Array allocation: new Type[size]
			// Evaluate the size expression
			auto size_operands = visitExpressionNode(newExpr.size_expr()->as<ExpressionNode>());

			// Create HeapAllocArrayOp
			HeapAllocArrayOp op;
			op.result = result_var;
			op.type = type;
			op.size_in_bytes = size_in_bits / 8;
			op.pointer_depth = pointer_depth;
			// Convert IrOperand to IrValue for count
			if (std::holds_alternative<unsigned long long>(size_operands[2])) {
				op.count = std::get<unsigned long long>(size_operands[2]);
			} else if (std::holds_alternative<TempVar>(size_operands[2])) {
				op.count = std::get<TempVar>(size_operands[2]);
			} else if (std::holds_alternative<std::string_view>(size_operands[2])) {
				op.count = std::get<std::string_view>(size_operands[2]);
			} else if (std::holds_alternative<double>(size_operands[2])) {
				op.count = std::get<double>(size_operands[2]);
			}

			ir_.addInstruction(IrInstruction(IrOpcode::HeapAllocArray, std::move(op), Token()));
		} else {
			// Single object allocation: new Type or new Type(args)
			HeapAllocOp op;
			op.result = result_var;
			op.type = type;
			op.size_in_bytes = size_in_bits / 8;
			op.pointer_depth = pointer_depth;

			ir_.addInstruction(IrInstruction(IrOpcode::HeapAlloc, std::move(op), Token()));

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
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name_;
							ctor_op.object = result_var;

							// Add constructor arguments
							const auto& ctor_args = newExpr.constructor_args();
							for (size_t i = 0; i < ctor_args.size(); ++i) {
								auto arg_operands = visitExpressionNode(ctor_args[i].as<ExpressionNode>());
								// arg_operands = [type, size, value]
								if (arg_operands.size() >= 3) {
									TypedValue tv = toTypedValue(arg_operands);
									ctor_op.arguments.push_back(std::move(tv));
								}
							}
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));
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
		// Convert IrOperand to IrValue
		IrValue ptr_value;
		if (std::holds_alternative<unsigned long long>(ptr_operands[2])) {
			ptr_value = std::get<unsigned long long>(ptr_operands[2]);
		} else if (std::holds_alternative<TempVar>(ptr_operands[2])) {
			ptr_value = std::get<TempVar>(ptr_operands[2]);
		} else if (std::holds_alternative<std::string_view>(ptr_operands[2])) {
			ptr_value = std::get<std::string_view>(ptr_operands[2]);
		} else if (std::holds_alternative<double>(ptr_operands[2])) {
			ptr_value = std::get<double>(ptr_operands[2]);
		}

		if (deleteExpr.is_array()) {
			HeapFreeArrayOp op;
			op.pointer = ptr_value;
			ir_.addInstruction(IrInstruction(IrOpcode::HeapFreeArray, std::move(op), Token()));
		} else {
			HeapFreeOp op;
			op.pointer = ptr_value;
			ir_.addInstruction(IrInstruction(IrOpcode::HeapFree, std::move(op), Token()));
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

		// For float-to-int conversions, generate FloatToInt IR
		if (is_floating_point_type(source_type) && is_integer_type(target_type)) {
			TempVar result_temp = var_counter.next();
			std::vector<IrOperand> operands;
			operands.emplace_back(result_temp);     // Result
			operands.emplace_back(source_type);     // From type
			operands.emplace_back(source_size);     // From size
			operands.emplace_back(expr_operands[2]); // Value
			operands.emplace_back(target_type);     // To type
			operands.emplace_back(target_size);     // To size
			ir_.addInstruction(IrOpcode::FloatToInt, std::move(operands), staticCastNode.cast_token());
			return { target_type, target_size, result_temp };
		}

		// For int-to-float conversions, generate IntToFloat IR
		if (is_integer_type(source_type) && is_floating_point_type(target_type)) {
			TempVar result_temp = var_counter.next();
			std::vector<IrOperand> operands;
			operands.emplace_back(result_temp);     // Result
			operands.emplace_back(source_type);     // From type
			operands.emplace_back(source_size);     // From size
			operands.emplace_back(expr_operands[2]); // Value
			operands.emplace_back(target_type);     // To type
			operands.emplace_back(target_size);     // To size
			ir_.addInstruction(IrOpcode::IntToFloat, std::move(operands), staticCastNode.cast_token());
			return { target_type, target_size, result_temp };
		}

		// For float-to-float conversions (float <-> double), generate FloatToFloat IR
		if (is_floating_point_type(source_type) && is_floating_point_type(target_type) && source_type != target_type) {
			TempVar result_temp = var_counter.next();
			std::vector<IrOperand> operands;
			operands.emplace_back(result_temp);     // Result
			operands.emplace_back(source_type);     // From type
			operands.emplace_back(source_size);     // From size
			operands.emplace_back(expr_operands[2]); // Value
			operands.emplace_back(target_type);     // To type
			operands.emplace_back(target_size);     // To size
			ir_.addInstruction(IrOpcode::FloatToFloat, std::move(operands), staticCastNode.cast_token());
			return { target_type, target_size, result_temp };
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
				DestructorCallOp dtor_op;
				dtor_op.struct_name = it->struct_name;
				dtor_op.object = std::string_view(it->variable_name);
				ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), Token()));
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
		VariableDeclOp lambda_decl_op;
		lambda_decl_op.type = Type::Struct;
		lambda_decl_op.size_in_bits = static_cast<int>(closure_type->getStructInfo()->total_size * 8);
		lambda_decl_op.var_name = std::string(closure_var_name);
		lambda_decl_op.custom_alignment = 0;
		lambda_decl_op.is_reference = false;
		lambda_decl_op.is_rvalue_reference = false;
		lambda_decl_op.is_array = false;
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(lambda_decl_op), lambda.lambda_token()));

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
							AddressOfOp addr_op;
							addr_op.result = addr_temp;
							addr_op.pointee_type = orig_type.type();
							addr_op.pointee_size_in_bits = static_cast<int>(orig_type.size_in_bits());
							addr_op.operand = std::string(var_name);  // Use std::string to avoid dangling reference
							ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), lambda.lambda_token()));

							// Store the address in the closure member
							MemberStoreOp member_store;
							member_store.value.type = member->type;
							member_store.value.size_in_bits = static_cast<int>(member->size * 8);
							member_store.value.value = addr_temp;
							member_store.object = std::string_view(closure_var_name);
							member_store.member_name = std::string_view(member->name);
							member_store.offset = static_cast<int>(member->offset);
							member_store.is_reference = member->is_reference;
							member_store.is_rvalue_reference = member->is_rvalue_reference;
							member_store.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), lambda.lambda_token()));
					} else {
						// By-value: copy the value
						MemberStoreOp member_store;
						member_store.value.type = member->type;
						member_store.value.size_in_bits = static_cast<int>(member->size * 8);
						member_store.value.value = std::string_view(var_name);
						member_store.object = std::string_view(closure_var_name);
						member_store.member_name = std::string_view(member->name);
						member_store.offset = static_cast<int>(member->offset);
						member_store.is_reference = member->is_reference;
						member_store.is_rvalue_reference = member->is_rvalue_reference;
						member_store.struct_type_info = nullptr;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), lambda.lambda_token()));
					}						capture_index++;
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
		FunctionDeclOp func_decl_op;
		func_decl_op.function_name = "operator()";
		func_decl_op.struct_name = lambda_info.closure_type_name;  // struct name (member function)
		func_decl_op.return_type = lambda_info.return_type;
		func_decl_op.return_size_in_bits = lambda_info.return_size;
		func_decl_op.return_pointer_depth = 0;  // pointer depth
		func_decl_op.linkage = Linkage::None;  // C++ linkage
		func_decl_op.is_variadic = false;
		func_decl_op.mangled_name = "";  // Lambda operator() doesn't need mangled name

		// Add parameters
		for (const auto& [type, size, pointer_depth, name] : lambda_info.parameters) {
			FunctionParam func_param;
			func_param.type = type;
			func_param.size_in_bits = size;
			func_param.pointer_depth = pointer_depth;
			func_param.name = std::string(name);
			func_param.is_reference = false;
			func_param.is_rvalue_reference = false;
			func_param.cv_qualifier = CVQualifier::None;
			func_decl_op.parameters.push_back(func_param);
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), lambda_info.lambda_token));
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
		FunctionDeclOp func_decl_op;
		func_decl_op.function_name = lambda_info.invoke_name;
		func_decl_op.struct_name = "";  // no struct name (static function)
		func_decl_op.return_type = lambda_info.return_type;
		func_decl_op.return_size_in_bits = lambda_info.return_size;
		func_decl_op.return_pointer_depth = 0;  // pointer depth
		func_decl_op.linkage = Linkage::None;  // C++ linkage
		func_decl_op.is_variadic = false;
		func_decl_op.mangled_name = "";  // Lambda __invoke doesn't need mangled name

		// Add parameters
		for (const auto& [type, size, pointer_depth, name] : lambda_info.parameters) {
			FunctionParam func_param;
			func_param.type = type;
			func_param.size_in_bits = size;
			func_param.pointer_depth = pointer_depth;
			func_param.name = std::string(name);
			func_param.is_reference = false;
			func_param.is_rvalue_reference = false;
			func_param.cv_qualifier = CVQualifier::None;
			func_decl_op.parameters.push_back(func_param);
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), lambda_info.lambda_token));
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
	Parser* parser_;  // Reference to parser for template instantiation

	// Current function name (for mangling static local variables)
	std::string current_function_name_;
	std::string current_struct_name_;  // For tracking which struct we're currently visiting member functions for
	Type current_function_return_type_;  // Current function's return type
	int current_function_return_size_;   // Current function's return size in bits

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
	
	// Structure to hold template instantiation info for deferred generation
	struct TemplateInstantiationInfo {
		std::string qualified_template_name;  // e.g., "Container::insert"
		std::string mangled_name;  // e.g., "insert_int"
		std::string struct_name;   // e.g., "Container"
		std::vector<Type> template_args;  // Concrete types
		TokenPosition body_position;  // Where the template body starts
		std::vector<std::string_view> template_param_names;  // e.g., ["U"]
		const TemplateFunctionDeclarationNode* template_node_ptr;  // Pointer to the template
	};
	
	// Collected template instantiations for deferred generation
	std::vector<TemplateInstantiationInfo> collected_template_instantiations_;

	// Current lambda context (for tracking captured variables)
	// When generating lambda body, this contains the closure type name
	std::string current_lambda_closure_type_;
	std::unordered_set<std::string> current_lambda_captures_;
	std::unordered_map<std::string, LambdaCaptureNode::CaptureKind> current_lambda_capture_kinds_;
	std::unordered_map<std::string, TypeSpecifierNode> current_lambda_capture_types_;

	// Generate just the function declaration for a template instantiation (without body)
	// This is called immediately when a template call is detected, so the IR converter
	// knows the full function signature before the call is converted to object code
	void generateTemplateFunctionDecl(const TemplateInstantiationInfo& inst_info) {
		const FunctionDeclarationNode& template_func_decl = inst_info.template_node_ptr->function_decl_node();
		const DeclarationNode& template_decl = template_func_decl.decl_node();

		// Create mangled name token
		Token mangled_token(
			Token::Type::Identifier,
			inst_info.mangled_name,
			template_decl.identifier_token().line(),
			template_decl.identifier_token().column(),
			template_decl.identifier_token().file_index()
		);

		// Build function name - use StringBuilder to create persistent string_view
		StringBuilder func_name_builder;
		func_name_builder.append(inst_info.mangled_name);
		std::string_view full_func_name = func_name_builder.commit();
		
		// Also create persistent string_view for struct name
		StringBuilder struct_name_builder;
		struct_name_builder.append(inst_info.struct_name);
		std::string_view struct_name_view = struct_name_builder.commit();

		// Generate function declaration IR using typed payload
		FunctionDeclOp func_decl_op;
		
		// Add return type
		const TypeSpecifierNode& return_type = template_decl.type_node().as<TypeSpecifierNode>();
		func_decl_op.return_type = return_type.type();
		func_decl_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
		func_decl_op.return_pointer_depth = static_cast<int>(return_type.pointer_depth());
		
		// Add function name and struct name
		func_decl_op.function_name = std::string(full_func_name);
		func_decl_op.struct_name = std::string(struct_name_view);
		
		// Add linkage (C++)
		func_decl_op.linkage = Linkage::None;

		// Add variadic flag (template functions are typically not variadic, but check anyway)
		func_decl_op.is_variadic = template_func_decl.is_variadic();

		// Mangled name is the full function name
		func_decl_op.mangled_name = std::string(full_func_name);

		// Add function parameters with concrete types
		for (size_t i = 0; i < template_func_decl.parameter_nodes().size(); ++i) {
			const auto& param_node = template_func_decl.parameter_nodes()[i];
			if (param_node.is<DeclarationNode>()) {
				const DeclarationNode& param_decl = param_node.as<DeclarationNode>();

				FunctionParam func_param;
				// Use concrete type if this parameter uses a template parameter
				if (i < inst_info.template_args.size()) {
					Type concrete_type = inst_info.template_args[i];
					func_param.type = concrete_type;
					func_param.size_in_bits = static_cast<int>(get_type_size_bits(concrete_type));
					func_param.pointer_depth = 0;  // pointer depth
					func_param.name = std::string(param_decl.identifier_token().value());
				} else {
					// Use original parameter type
					const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
					func_param.name = std::string(param_decl.identifier_token().value());
				}
				func_param.is_reference = false;
				func_param.is_rvalue_reference = false;
				func_param.cv_qualifier = CVQualifier::None;
				func_decl_op.parameters.push_back(func_param);
			}
		}

		// Emit function declaration IR (declaration only, no body)
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), mangled_token));
	}

	// Generate an instantiated member function template
	void generateTemplateInstantiation(const TemplateInstantiationInfo& inst_info) {
		// First, generate the FunctionDecl IR for the template instantiation
		// This must be done at the top level, BEFORE any function bodies that might call it
		generateTemplateFunctionDecl(inst_info);
		
		// Get the template function declaration
		const FunctionDeclarationNode& template_func_decl = inst_info.template_node_ptr->function_decl_node();
		const DeclarationNode& template_decl = template_func_decl.decl_node();

		// Create mangled name token
		Token mangled_token(
			Token::Type::Identifier,
			inst_info.mangled_name,
			template_decl.identifier_token().line(),
			template_decl.identifier_token().column(),
			template_decl.identifier_token().file_index()
		);

		// Enter function scope
		symbol_table.enter_scope(ScopeType::Function);

		// Get struct type info for member functions
		const TypeInfo* struct_type_info = nullptr;
		if (!inst_info.struct_name.empty()) {
			auto struct_type_it = gTypesByName.find(inst_info.struct_name);
			if (struct_type_it != gTypesByName.end()) {
				struct_type_info = struct_type_it->second;
			}
		}

		// For member functions, add implicit 'this' pointer to symbol table
		// This is needed so member variable access works during template body parsing
		if (struct_type_info) {
			// Create a 'this' pointer type (pointer to the struct)
			auto this_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
				Type::UserDefined,
				struct_type_info->type_index_,
				64,  // Pointer size in bits
				template_decl.identifier_token()
			);
			
			// Set pointer depth to 1 (this is a pointer)
			this_type_node.as<TypeSpecifierNode>().add_pointer_level(CVQualifier::None);
			
			// Create 'this' declaration
			Token this_token(Token::Type::Identifier, "this", 
				template_decl.identifier_token().line(),
				template_decl.identifier_token().column(),
				template_decl.identifier_token().file_index());
			auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type_node, this_token);
			
			// Add 'this' to symbol table
			symbol_table.insert("this", this_decl);
		}

		// Add function parameters to symbol table for name resolution during body parsing
		for (size_t i = 0; i < template_func_decl.parameter_nodes().size(); ++i) {
			const auto& param_node = template_func_decl.parameter_nodes()[i];
			if (param_node.is<DeclarationNode>()) {
				const DeclarationNode& param_decl = param_node.as<DeclarationNode>();

				// Create declaration with concrete type
				if (i < inst_info.template_args.size()) {
					Type concrete_type = inst_info.template_args[i];
					auto concrete_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
						concrete_type, 
						TypeQualifier::None, 
						get_type_size_bits(concrete_type),
						param_decl.identifier_token()
					);
					auto concrete_param_decl = ASTNode::emplace_node<DeclarationNode>(concrete_type_node, param_decl.identifier_token());
					symbol_table.insert(param_decl.identifier_token().value(), concrete_param_decl);
				} else {
					symbol_table.insert(param_decl.identifier_token().value(), param_node);
				}
			}
		}

		// Parse the template body with concrete types
		// Pass the struct name and type index so the parser can set up member function context
		auto body_node_opt = parser_->parseTemplateBody(
			inst_info.body_position,
			inst_info.template_param_names,
			inst_info.template_args,
			inst_info.struct_name,  // Pass struct name
			struct_type_info ? struct_type_info->type_index_ : 0  // Pass type index
		);

		if (body_node_opt.has_value()) {
			std::cerr << "DEBUG: Template body has value, is BlockNode=" << body_node_opt->is<BlockNode>() << "\n";
			if (body_node_opt->is<BlockNode>()) {
				const BlockNode& block = body_node_opt->as<BlockNode>();
				const auto& stmts = block.get_statements();
				std::cerr << "DEBUG: Template body has " << stmts.size() << " statements\n";
				
				// Visit each statement in the block to generate IR
				for (size_t i = 0; i < stmts.size(); ++i) {
					std::cerr << "DEBUG: Visiting template body statement " << i << "\n";
					visit(stmts[i]);
				}
			}
		} else {
			std::cerr << "DEBUG: Template body does NOT have value!\n";
		}

		// Add implicit return for void functions
		const TypeSpecifierNode& return_type = template_decl.type_node().as<TypeSpecifierNode>();
		if (return_type.type() == Type::Void) {
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), mangled_token));
		}

		// Exit function scope
		symbol_table.exit_scope();
	}

	std::vector<IrOperand> generateTemplateParameterReferenceIr(const TemplateParameterReferenceNode& templateParamRefNode) {
		// This should not happen during normal code generation - template parameters should be substituted
		// during template instantiation. If we get here, it means template instantiation failed.
		std::string param_name = std::string(templateParamRefNode.param_name());
		std::cerr << "Error: Template parameter '" << param_name << "' was not substituted during template instantiation\n";
		std::cerr << "This indicates a bug in template instantiation - template parameters should be replaced with concrete types/values\n";
		assert(false && "Template parameter reference found during code generation - should have been substituted");
		return {};
	}

	std::vector<IrOperand> generateConstructorCallIr(const ConstructorCallNode& constructorCallNode) {
		std::vector<IrOperand> irOperands;

		// Get the type being constructed
		const ASTNode& type_node = constructorCallNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			assert(false && "Constructor call type node must be a TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();

		// For constructor calls, we need to generate a function call to the constructor
		// In C++, constructors are named after the class
		std::string constructor_name;
		if (type_spec.type() == Type::Struct || type_spec.type() == Type::UserDefined) {
			// Use the type name as the constructor name
			constructor_name = gTypeInfo[type_spec.type_index()].name_;
		} else {
			// For basic types, constructors might not exist, but we can handle them as value construction
			// For now, treat as a regular function call
			constructor_name = gTypeInfo[type_spec.type_index()].name_;
		}

		// Create a temporary variable for the result (the constructed object)
		TempVar ret_var = var_counter.next();
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(constructor_name);

		// Generate IR for constructor arguments
		constructorCallNode.arguments().visit([&](ASTNode argument) {
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
		});

		// Add the constructor call instruction
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(irOperands), constructorCallNode.called_from()));

		// Return the result variable with the constructed type
		return { type_spec.type(), static_cast<int>(type_spec.size_in_bits()), ret_var };
	}

};




