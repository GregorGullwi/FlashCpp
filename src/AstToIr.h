#pragma once
#include "CodeGen.h"

class AstToIr {
public:
	AstToIr() = delete;  // Require valid references
	AstToIr(SymbolTable& global_symbol_table, CompileContext& context, Parser& parser);

	void visit(const ASTNode& node);

	const Ir& getIr() const { return ir_; }

	void generateCollectedLambdas();

	void generateCollectedLocalStructMembers();

	std::string get_deferred_func_name(const ASTNode& node) const;

	size_t generateDeferredMemberFunctions();

	void generateCollectedTemplateInstantiations();

	void reserveInstructions(size_t capacity) {
		ir_.reserve(capacity);
	}

	void generateStaticMemberDeclarations();

	void generateTrivialDefaultConstructors();

private:
	struct MultiDimArrayAccess {
		std::string_view base_array_name;
		std::vector<ASTNode> indices;  // Indices from outermost to innermost
		const DeclarationNode* base_decl = nullptr;
		bool is_valid = false;
	};

	struct MultiDimMemberArrayAccess {
		std::string_view object_name;
		std::string_view member_name;
		std::vector<ASTNode> indices;  // Indices from outermost to innermost
		const StructMember* member_info = nullptr;
		bool is_valid = false;
	};

	struct AddressComponents {
		std::variant<StringHandle, TempVar> base;           // Base variable or temp
		std::vector<ComputeAddressOp::ArrayIndex> array_indices;  // Array indices
		int total_member_offset = 0;                        // Accumulated member offsets
		Type final_type = Type::Void;                       // Type of final result
		int final_size_bits = 0;                            // Size in bits
		int pointer_depth = 0;                              // Pointer depth of final result
	};

	struct ScopeVariableInfo {
		std::string variable_name;
		std::string struct_name;
	};

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
				dtor_op.struct_name = StringTable::getOrInternStringHandle(it->struct_name);
				dtor_op.object = StringTable::getOrInternStringHandle(it->variable_name);
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

	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node);
	void visitStructDeclarationNode(const StructDeclarationNode& node);
	void visitEnumDeclarationNode([[maybe_unused]] const EnumDeclarationNode& node);
	void visitConstructorDeclarationNode(const ConstructorDeclarationNode& node);
	void visitDestructorDeclarationNode(const DestructorDeclarationNode& node);
	void visitNamespaceDeclarationNode(const NamespaceDeclarationNode& node);
	void visitUsingDirectiveNode(const UsingDirectiveNode& node);
	void visitUsingDeclarationNode(const UsingDeclarationNode& node);
	void visitUsingEnumNode(const UsingEnumNode& node);
	void visitNamespaceAliasNode(const NamespaceAliasNode& node);
	void visitReturnStatementNode(const ReturnStatementNode& node);
	void visitBlockNode(const BlockNode& node);
	void visitIfStatementNode(const IfStatementNode& node);
	void visitForStatementNode(const ForStatementNode& node);
	void visitWhileStatementNode(const WhileStatementNode& node);
	void visitDoWhileStatementNode(const DoWhileStatementNode& node);
	void visitSwitchStatementNode(const SwitchStatementNode& node);
	void visitRangedForStatementNode(const RangedForStatementNode& node);
	void visitRangedForArray(const RangedForStatementNode& node, std::string_view array_name,
	                         const DeclarationNode& array_decl, StringHandle loop_start_label,
	                         StringHandle loop_body_label, StringHandle loop_increment_label,
	                         StringHandle loop_end_label, size_t counter);
	void visitRangedForBeginEnd(const RangedForStatementNode& node, std::string_view range_name,
	                            const TypeSpecifierNode& range_type, StringHandle loop_start_label,
	                            StringHandle loop_body_label, StringHandle loop_increment_label,
	                            StringHandle loop_end_label, size_t counter);
	void visitBreakStatementNode(const BreakStatementNode& node);
	void visitContinueStatementNode(const ContinueStatementNode& node);
	void visitGotoStatementNode(const GotoStatementNode& node);
	void visitLabelStatementNode(const LabelStatementNode& node);
	void visitTryStatementNode(const TryStatementNode& node);
	void visitThrowStatementNode(const ThrowStatementNode& node);
	void visitSehTryExceptStatementNode(const SehTryExceptStatementNode& node);
	void visitSehTryFinallyStatementNode(const SehTryFinallyStatementNode& node);
	void visitSehLeaveStatementNode(const SehLeaveStatementNode& node);
	void visitVariableDeclarationNode(const ASTNode& ast_node);
	void visitStructuredBindingNode(const ASTNode& ast_node);
	std::vector<IrOperand> visitExpressionNode(const ExpressionNode& exprNode, 
	                                            ExpressionContext context = ExpressionContext::Load);
	std::vector<IrOperand> generateNoexceptExprIr(const NoexceptExprNode& noexcept_node);
	std::vector<IrOperand> generatePseudoDestructorCallIr(const PseudoDestructorCallNode& dtor);
	std::vector<IrOperand> generatePointerToMemberAccessIr(const PointerToMemberAccessNode& ptmNode);
	int calculateIdentifierSizeBits(const TypeSpecifierNode& type_node, bool is_array, std::string_view identifier_name);
	std::vector<IrOperand> generateIdentifierIr(const IdentifierNode& identifierNode, 
	                                             ExpressionContext context = ExpressionContext::Load);
	std::vector<IrOperand> generateQualifiedIdentifierIr(const QualifiedIdentifierNode& qualifiedIdNode);
	std::vector<IrOperand>
		generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode);
	std::vector<IrOperand> generateTypeConversion(const std::vector<IrOperand>& operands, Type fromType, Type toType, const Token& source_token);
	std::vector<IrOperand>
		generateStringLiteralIr(const StringLiteralNode& stringLiteralNode);
	std::optional<AddressComponents> analyzeAddressExpression(
		const ExpressionNode& expr, 
		int accumulated_offset = 0);
	std::vector<IrOperand> generateUnaryOperatorIr(const UnaryOperatorNode& unaryOperatorNode, 
	                                                 ExpressionContext context = ExpressionContext::Load);
	std::vector<IrOperand> generateTernaryOperatorIr(const TernaryOperatorNode& ternaryNode);
	std::vector<IrOperand> generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode);
	std::string_view generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& param_types, bool is_variadic = false, std::string_view struct_name = "", const std::vector<std::string>& namespace_path = {});
	std::string_view generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<ASTNode>& param_nodes, bool is_variadic = false, std::string_view struct_name = "", const std::vector<std::string>& namespace_path = {});
	std::string_view generateMangledNameForCall(const FunctionDeclarationNode& func_node, std::string_view struct_name_override = "", const std::vector<std::string>& namespace_path = {});
	std::optional<std::vector<IrOperand>> tryGenerateIntrinsicIr(std::string_view func_name, const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateBuiltinAbsIntIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateBuiltinAbsFloatIntrinsic(const FunctionCallNode& functionCallNode, std::string_view func_name);
	bool isVaListPointerType(const ASTNode& arg, const std::vector<IrOperand>& ir_result) const;
	std::vector<IrOperand> generateVaArgIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateVaStartIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateBuiltinUnreachableIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateBuiltinAssumeIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateBuiltinExpectIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateBuiltinLaunderIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateGetExceptionCodeIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateAbnormalTerminationIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateGetExceptionInformationIntrinsic(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateFunctionCallIr(const FunctionCallNode& functionCallNode);
	std::vector<IrOperand> generateMemberFunctionCallIr(const MemberFunctionCallNode& memberFunctionCallNode);
	MultiDimMemberArrayAccess collectMultiDimMemberArrayIndices(const ArraySubscriptNode& subscript);
	MultiDimArrayAccess collectMultiDimArrayIndices(const ArraySubscriptNode& subscript);
	std::vector<IrOperand> generateArraySubscriptIr(const ArraySubscriptNode& arraySubscriptNode,
	                                                 ExpressionContext context = ExpressionContext::Load);
	bool validateAndSetupIdentifierMemberAccess(
		std::string_view object_name,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		bool& is_pointer_dereference);
	bool extractBaseFromOperands(
		const std::vector<IrOperand>& operands,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		std::string_view error_context);
	static std::vector<IrOperand> makeMemberResult(Type type, int size_bits, TempVar result_var, size_t type_index = 0);
	bool setupBaseFromIdentifier(
		std::string_view object_name,
		const Token& member_token,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		bool& is_pointer_dereference);
	std::vector<IrOperand> generateMemberAccessIr(const MemberAccessNode& memberAccessNode,
	                                               ExpressionContext context = ExpressionContext::Load);
	std::optional<size_t> calculateArraySize(const DeclarationNode& decl);
	std::vector<IrOperand> generateSizeofIr(const SizeofExprNode& sizeofNode);
	std::vector<IrOperand> generateAlignofIr(const AlignofExprNode& alignofNode);
	std::vector<IrOperand> generateOffsetofIr(const OffsetofExprNode& offsetofNode);
	bool isScalarType(Type type, bool is_reference, size_t pointer_depth) const;
	bool isArithmeticType(Type type) const;
	bool isFundamentalType(Type type) const;
	std::vector<IrOperand> generateTypeTraitIr(const TypeTraitExprNode& traitNode);
	std::vector<IrOperand> generateNewExpressionIr(const NewExpressionNode& newExpr);
	std::vector<IrOperand> generateDeleteExpressionIr(const DeleteExpressionNode& deleteExpr);
	std::variant<StringHandle, TempVar> extractBaseOperand(
		const std::vector<IrOperand>& expr_operands,
		TempVar fallback_var,
		const char* cast_name = "cast");
	void markReferenceMetadata(
		const std::vector<IrOperand>& expr_operands,
		TempVar result_var,
		Type target_type,
		int target_size,
		bool is_rvalue_ref,
		const char* cast_name = "cast");
	void generateAddressOfForReference(
		const std::variant<StringHandle, TempVar>& base,
		TempVar result_var,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast");
	std::vector<IrOperand> handleRValueReferenceCast(
		const std::vector<IrOperand>& expr_operands,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast");
	std::vector<IrOperand> handleLValueReferenceCast(
		const std::vector<IrOperand>& expr_operands,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast");
	std::vector<IrOperand> generateStaticCastIr(const StaticCastNode& staticCastNode);
	std::vector<IrOperand> generateTypeidIr(const TypeidNode& typeidNode);
	std::vector<IrOperand> generateDynamicCastIr(const DynamicCastNode& dynamicCastNode);
	std::vector<IrOperand> generateConstCastIr(const ConstCastNode& constCastNode);
	std::vector<IrOperand> generateReinterpretCastIr(const ReinterpretCastNode& reinterpretCastNode);
	std::vector<IrOperand> generateLambdaExpressionIr(const LambdaExpressionNode& lambda, std::string_view target_var_name = "");
	void generateLambdaFunctions(const LambdaInfo& lambda_info);
	void generateLambdaOperatorCallFunction(const LambdaInfo& lambda_info);
	void generateLambdaInvokeFunction(const LambdaInfo& lambda_info);
	void addCapturedVariablesToSymbolTable(const std::vector<LambdaCaptureNode>& captures,
	                                        const std::vector<ASTNode>& captured_var_decls);

	// ── inline private helpers (CodeGen_Visitors_TypeInit.cpp) ──
	// Helper: resolve self-referential struct types in template instantiations.
	// When a template member function references its own class (e.g., const W& in W<T>::operator+=),
	// the type_index may point to the unfinalized template base. This resolves it to the
	// enclosing instantiated struct's type_index by mutating `type` in-place.
	// Important: only resolves when the unfinalized type's name matches the base name of the
	// enclosing struct — avoids incorrectly resolving outer class references in nested classes.
	static void resolveSelfReferentialType(TypeSpecifierNode& type, TypeIndex enclosing_type_index) {
		if (type.type() == Type::Struct && type.type_index() > 0 && type.type_index() < gTypeInfo.size()) {
			auto& ti = gTypeInfo[type.type_index()];
			if (!ti.struct_info_ || ti.struct_info_->total_size == 0) {
				if (enclosing_type_index < gTypeInfo.size()) {
					// Verify this is actually a self-reference by checking that the unfinalized
					// type's name matches the base name of the enclosing struct.
					// For template instantiations: W (unfinalized) matches W$hash (enclosing)
					// For nested classes: Outer (unfinalized) does NOT match Outer::Inner (enclosing)
					auto unfinalized_name = StringTable::getStringView(ti.name());
					auto enclosing_name = StringTable::getStringView(gTypeInfo[enclosing_type_index].name());
					
					// Extract the base name of the enclosing struct (strip template hash and nested class prefix)
					// Template hash: "Name$hash" -> "Name"
					// Nested class: "Outer::Inner" -> "Inner"
					auto base_name = enclosing_name;
					auto last_scope = base_name.rfind("::");
					if (last_scope != std::string_view::npos) {
						base_name = base_name.substr(last_scope + 2);
					}
					auto dollar_pos = base_name.find('$');
					if (dollar_pos != std::string_view::npos) {
						base_name = base_name.substr(0, dollar_pos);
					}
					
					if (unfinalized_name == base_name) {
						type.set_type_index(enclosing_type_index);
					}
				}
			}
		}
	}

	// Helper: generate a member function call for user-defined operator++/-- overloads on structs.
	// Returns the IR operands {result_type, result_size, ret_var, result_type_index} on success,
	// or std::nullopt if no overload was found.
	std::optional<std::vector<IrOperand>> generateUnaryIncDecOverloadCall(
		std::string_view op_name,  // "++" or "--"
		Type operandType,
		const std::vector<IrOperand>& operandIrOperands,
		bool is_prefix
	) {
		if (operandType != Type::Struct || operandIrOperands.size() < 4)
			return std::nullopt;

		TypeIndex operand_type_index = 0;
		if (std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
			operand_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operandIrOperands[3]));
		}
		if (operand_type_index == 0)
			return std::nullopt;

		// For ++/--, we need to distinguish prefix (0 params) from postfix (1 param: dummy int).
		// findUnaryOperatorOverload returns the first match; scan all member functions to pick
		// the overload whose parameter count matches the call form.
		size_t expected_param_count = is_prefix ? 0 : 1;
		const StructMemberFunction* matched_func = nullptr;
		const StructMemberFunction* fallback_func = nullptr;
		if (operand_type_index < gTypeInfo.size()) {
			const StructTypeInfo* struct_info = gTypeInfo[operand_type_index].getStructInfo();
			if (struct_info) {
				for (const auto& mf : struct_info->member_functions) {
					if (mf.is_operator_overload && mf.operator_symbol == op_name) {
						const auto& fd = mf.function_decl.as<FunctionDeclarationNode>();
						if (fd.parameter_nodes().size() == expected_param_count) {
							matched_func = &mf;
							break;
						}
						if (!fallback_func) fallback_func = &mf;
					}
				}
			}
		}
		// Fallback: if no exact arity match, use any operator++ / operator-- overload.
		// This handles the common case where only one form (prefix or postfix) is defined.
		if (!matched_func) matched_func = fallback_func;
		if (!matched_func)
			return std::nullopt;

		const StructMemberFunction& member_func = *matched_func;
		const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
		std::string_view struct_name = StringTable::getStringView(gTypeInfo[operand_type_index].name());
		TypeSpecifierNode return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
		resolveSelfReferentialType(return_type, operand_type_index);

		std::vector<TypeSpecifierNode> param_types;
		// Use the matched function's actual parameter count for mangling, not the call form.
		// When the fallback path is taken (e.g., only prefix defined but postfix called),
		// we must mangle to match the definition, not the call site.
		const auto& actual_params = func_decl.parameter_nodes();
		if (actual_params.size() == 1 && actual_params[0].is<DeclarationNode>()) {
			// Postfix overload has a dummy int parameter
			TypeSpecifierNode int_type(Type::Int, TypeQualifier::None, 32, Token());
			param_types.push_back(int_type);
		}
		std::vector<std::string_view> empty_namespace;
		auto op_func_name = StringBuilder().append("operator").append(op_name).commit();
		auto mangled_name = NameMangling::generateMangledName(
			op_func_name, return_type, param_types, false,
			struct_name, empty_namespace, Linkage::CPlusPlus
		);

		TempVar ret_var = var_counter.next();
		CallOp call_op;
		call_op.result = ret_var;
		call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
		call_op.return_type = return_type.type();
		call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
		if (call_op.return_size_in_bits == 0 && return_type.type_index() > 0 && return_type.type_index() < gTypeInfo.size() && gTypeInfo[return_type.type_index()].struct_info_) {
			call_op.return_size_in_bits = static_cast<int>(gTypeInfo[return_type.type_index()].struct_info_->total_size * 8);
		}
		call_op.return_type_index = return_type.type_index();
		call_op.is_member_function = true;

		// Detect if returning struct by value (needs hidden return parameter for RVO).
		// Small structs (≤ ABI threshold) return in registers and need no return_slot.
		if (needsHiddenReturnParam(return_type.type(), return_type.pointer_depth(), return_type.is_reference(), call_op.return_size_in_bits, context_->isLLP64())) {
			call_op.return_slot = ret_var;
		}

		// Take address of operand for 'this' pointer
		TempVar this_addr = var_counter.next();
		AddressOfOp addr_op;
		addr_op.result = this_addr;
		addr_op.operand = toTypedValue(operandIrOperands);
		addr_op.operand.pointer_depth = 0;
		ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

		TypedValue this_arg;
		this_arg.type = operandType;
		this_arg.size_in_bits = 64;
		this_arg.value = this_addr;
		call_op.args.push_back(this_arg);

		// For postfix operators, pass dummy int argument (value 0)
		// For postfix operators, pass dummy int argument (value 0)
		// Use the matched function's actual parameter count (not the call form) to decide,
		// since the fallback path may match a prefix function for a postfix call or vice versa.
		if (actual_params.size() == 1) {
			TypedValue dummy_arg;
			dummy_arg.type = Type::Int;
			dummy_arg.size_in_bits = 32;
			dummy_arg.value = 0ULL;
			call_op.args.push_back(dummy_arg);
		}

		int result_size = call_op.return_size_in_bits;
		TypeIndex result_type_index = call_op.return_type_index;
		Type result_type = call_op.return_type;
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), Token()));
		return std::vector<IrOperand>{ result_type, result_size, ret_var, static_cast<unsigned long long>(result_type_index) };
	}

	// Helper: generate built-in pointer or integer increment/decrement IR.
	// Handles pointer arithmetic (add/subtract element_size) and integer pre/post inc/dec.
	// is_increment: true for ++, false for --
	std::vector<IrOperand> generateBuiltinIncDec(
		bool is_increment,
		bool is_prefix,
		bool operandHandledAsIdentifier,
		const UnaryOperatorNode& unaryOperatorNode,
		const std::vector<IrOperand>& operandIrOperands,
		Type operandType,
		TempVar result_var
	) {
		// Check if this is a pointer increment/decrement (requires pointer arithmetic)
		bool is_pointer = false;
		int element_size = 1;
		if (operandHandledAsIdentifier && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
				auto symbol = symbol_table.lookup(identifier.name());
				if (symbol.has_value()) {
					const TypeSpecifierNode* type_node = nullptr;
					if (symbol->is<DeclarationNode>()) {
						type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
					} else if (symbol->is<VariableDeclarationNode>()) {
						type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
					}
					
					if (type_node && type_node->pointer_depth() > 0) {
						is_pointer = true;
						if (type_node->pointer_depth() > 1) {
							element_size = 8;  // Multi-level pointer: element is a pointer
						} else {
							element_size = getSizeInBytes(type_node->type(), type_node->type_index(), type_node->size_in_bits());
						}
					}
				}
			}
		}
		
		UnaryOp unary_op{
			.value = toTypedValue(operandIrOperands),
			.result = result_var
		};
		
		IrOpcode arith_opcode = is_increment ? IrOpcode::Add : IrOpcode::Subtract;
		
		if (is_pointer) {
			// For pointers, use a BinaryOp to add/subtract element_size
			// Extract the pointer operand value once (used in multiple BinaryOp/AssignmentOp below)
			IrValue ptr_operand = std::holds_alternative<StringHandle>(operandIrOperands[2])
				? IrValue(std::get<StringHandle>(operandIrOperands[2])) : IrValue{};
			
			if (is_prefix) {
				BinaryOp bin_op{
					.lhs = { Type::UnsignedLongLong, 64, ptr_operand },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(arith_opcode, std::move(bin_op), Token()));
				// Store back to the pointer variable
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					AssignmentOp assign_op;
					assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
					assign_op.lhs = { Type::UnsignedLongLong, 64, ptr_operand };
					assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
				}
				return { operandType, 64, result_var, 0ULL };
			} else {
				// Postfix: save old value, modify, return old value
				TempVar old_value = var_counter.next();
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					AssignmentOp save_op;
					save_op.result = old_value;
					save_op.lhs = { Type::UnsignedLongLong, 64, old_value };
					save_op.rhs = toTypedValue(operandIrOperands);
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(save_op), Token()));
				}
				BinaryOp bin_op{
					.lhs = { Type::UnsignedLongLong, 64, ptr_operand },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(arith_opcode, std::move(bin_op), Token()));
				// Store back to the pointer variable
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					AssignmentOp assign_op;
					assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
					assign_op.lhs = { Type::UnsignedLongLong, 64, ptr_operand };
					assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
				}
				return { operandType, 64, old_value, 0ULL };
			}
		} else {
			// Regular integer increment/decrement
			IrOpcode pre_opcode = is_increment ? IrOpcode::PreIncrement : IrOpcode::PreDecrement;
			IrOpcode post_opcode = is_increment ? IrOpcode::PostIncrement : IrOpcode::PostDecrement;
			ir_.addInstruction(IrInstruction(is_prefix ? pre_opcode : post_opcode, unary_op, Token()));
		}
		
		return { operandType, std::get<int>(operandIrOperands[1]), result_var, 0ULL };
	}

	// Helper function to resolve template parameter size from struct name
	// This is used by both ConstExpr evaluator and IR generation for sizeof(T)
	// where T is a template parameter in a template class member function
	static size_t resolveTemplateSizeFromStructName(std::string_view struct_name) {
		// Parse the struct name to extract template arguments
		// e.g., "Container_int" -> T = int (4 bytes), "Processor_char" -> T = char (1 byte)
		// Pointer types have "P" suffix: "Container_intP" -> T = int* (8 bytes)
		// Reference types have "R" or "RR" suffix: "Container_intR" -> T = int& (sizeof returns size of int)
		size_t underscore_pos = struct_name.rfind('_');
		if (underscore_pos == std::string_view::npos || underscore_pos + 1 >= struct_name.size()) {
			return 0;
		}
		
		std::string_view type_suffix = struct_name.substr(underscore_pos + 1);
		
		// Strip CV qualifier prefixes ('C' for const, 'V' for volatile)
		// TemplateTypeArg::toString() adds CV qualifiers as prefixes (e.g., "Cint" for const int)
		// sizeof(const T) and sizeof(volatile T) return the same size as sizeof(T)
		while (!type_suffix.empty() && (type_suffix.front() == 'C' || type_suffix.front() == 'V')) {
			type_suffix = type_suffix.substr(1);
		}
		
		// Check for reference types (suffix ends with 'R' or 'RR')
		// TemplateTypeArg::toString() appends "R" for lvalue reference, "RR" for rvalue reference
		// sizeof(T&) and sizeof(T&&) return the size of T, not the size of the reference itself
		if (type_suffix.size() >= 2 && type_suffix.ends_with("RR")) {
			// Rvalue reference - strip "RR" and get base type size
			type_suffix = type_suffix.substr(0, type_suffix.size() - 2);
		} else if (!type_suffix.empty() && type_suffix.back() == 'R') {
			// Lvalue reference - strip "R" and get base type size
			type_suffix = type_suffix.substr(0, type_suffix.size() - 1);
		}
		
		// Check for pointer types (suffix ends with 'P')
		// TemplateTypeArg::toString() appends 'P' for each pointer level
		// e.g., "intP" for int*, "intPP" for int**, etc.
		if (!type_suffix.empty() && type_suffix.back() == 'P') {
			// All pointers are 8 bytes on x64
			return 8;
		}
		
		// Check for array types (suffix contains 'A')
		// Arrays are like "intA[10]" - sizeof(array) = element_size * element_count
		size_t array_pos = type_suffix.find('A');
		if (array_pos != std::string_view::npos) {
			// Extract base type and array dimensions
			std::string_view base_type = type_suffix.substr(0, array_pos);
			std::string_view array_part = type_suffix.substr(array_pos + 1); // Skip 'A'
			
			// Strip CV qualifiers from base_type (already stripped from type_suffix earlier, but double-check)
			while (!base_type.empty() && (base_type.front() == 'C' || base_type.front() == 'V')) {
				base_type = base_type.substr(1);
			}
			
			// Parse array dimensions like "[10]" or "[]"
			if (array_part.starts_with('[') && array_part.ends_with(']')) {
				std::string_view dimensions = array_part.substr(1, array_part.size() - 2);
				if (!dimensions.empty()) {
					// Parse the dimension as a number
					size_t array_count = 0;
					auto result = std::from_chars(dimensions.data(), dimensions.data() + dimensions.size(), array_count);
					if (result.ec == std::errc{} && array_count > 0) {
						// Get base type size
						size_t base_size = 0;
						
						// Check if base_type is a pointer (ends with 'P')
						// e.g., "intP" for int*, "charPP" for char**, etc.
						if (!base_type.empty() && base_type.back() == 'P') {
							// All pointers are 8 bytes on x64
							base_size = 8;
						} else {
							// Look up non-pointer base type size
							if (base_type == "int") base_size = 4;
							else if (base_type == "char") base_size = 1;
							else if (base_type == "short") base_size = 2;
							else if (base_type == "long") base_size = get_long_size_bits() / 8;
							else if (base_type == "float") base_size = 4;
							else if (base_type == "double") base_size = 8;
							else if (base_type == "bool") base_size = 1;
							else if (base_type == "uint") base_size = 4;
							else if (base_type == "uchar") base_size = 1;
							else if (base_type == "ushort") base_size = 2;
							else if (base_type == "ulong") base_size = get_long_size_bits() / 8;
							else if (base_type == "ulonglong") base_size = 8;
							else if (base_type == "longlong") base_size = 8;
						}
						
						if (base_size > 0) {
							return base_size * array_count;
						}
					}
				}
			}
			return 0;  // Failed to parse array dimensions
		}
		
		// Map common type suffixes to their sizes
		// Note: Must match the output of TemplateTypeArg::toString() in TemplateRegistry.h
		if (type_suffix == "int") return 4;
		else if (type_suffix == "char") return 1;
		else if (type_suffix == "short") return 2;
		else if (type_suffix == "long") return get_long_size_bits() / 8;
		else if (type_suffix == "float") return 4;
		else if (type_suffix == "double") return 8;
		else if (type_suffix == "bool") return 1;
		else if (type_suffix == "uint") return 4;
		else if (type_suffix == "uchar") return 1;
		else if (type_suffix == "ushort") return 2;
		else if (type_suffix == "ulong") return get_long_size_bits() / 8;
		else if (type_suffix == "ulonglong") return 8;
		else if (type_suffix == "longlong") return 8;
		
		return 0;  // Unknown type
	}

	// Helper function to try evaluating sizeof/alignof using ConstExprEvaluator
	// Returns the evaluated operands if successful, empty vector otherwise
	template<typename NodeType>
	std::vector<IrOperand> tryEvaluateAsConstExpr(const NodeType& node) {
		// Try to evaluate as a constant expression first
		ConstExpr::EvaluationContext ctx(symbol_table);
		
		// Pass global symbol table for resolving global variables in sizeof etc.
		if (global_symbol_table_) {
			ctx.global_symbols = global_symbol_table_;
		}
		
		// If we're in a member function, set the struct_info in the context
		// This allows sizeof(T) to resolve template parameters from the struct
		if (current_struct_name_.isValid()) {
			auto struct_type_it = gTypesByName.find(current_struct_name_);
			if (struct_type_it != gTypesByName.end()) {
				const TypeInfo* struct_type_info = struct_type_it->second;
				ctx.struct_info = struct_type_info->getStructInfo();
			}
		}
		
		auto expr_node = ASTNode::emplace_node<ExpressionNode>(node);
		auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
		
		if (eval_result.success()) {
			// Return the constant value
			unsigned long long value = 0;
			if (std::holds_alternative<long long>(eval_result.value)) {
				value = static_cast<unsigned long long>(std::get<long long>(eval_result.value));
			} else if (std::holds_alternative<unsigned long long>(eval_result.value)) {
				value = std::get<unsigned long long>(eval_result.value);
			}
			return { Type::UnsignedLongLong, 64, value };
		}
		
		// Return empty vector if evaluation failed
		return {};
	}

	// Helper function to evaluate whether an expression is noexcept
	// Returns true if the expression is guaranteed not to throw, false otherwise
	bool isExpressionNoexcept(const ExpressionNode& expr) const {
		// Literals are always noexcept
		if (std::holds_alternative<BoolLiteralNode>(expr) ||
		    std::holds_alternative<NumericLiteralNode>(expr) ||
		    std::holds_alternative<StringLiteralNode>(expr)) {
			return true;
		}
		
		// Identifiers (variable references) are noexcept
		if (std::holds_alternative<IdentifierNode>(expr) ||
		    std::holds_alternative<QualifiedIdentifierNode>(expr)) {
			return true;
		}
		
		// Template parameter references are noexcept
		if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
			return true;
		}
		
		// Built-in operators on primitives are noexcept
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& binop = std::get<BinaryOperatorNode>(expr);
			// Recursively check operands
			if (binop.get_lhs().is<ExpressionNode>() && binop.get_rhs().is<ExpressionNode>()) {
				return isExpressionNoexcept(binop.get_lhs().as<ExpressionNode>()) &&
				       isExpressionNoexcept(binop.get_rhs().as<ExpressionNode>());
			}
			// If operands are not expressions, assume noexcept for built-ins
			return true;
		}
		
		if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const auto& unop = std::get<UnaryOperatorNode>(expr);
			if (unop.get_operand().is<ExpressionNode>()) {
				return isExpressionNoexcept(unop.get_operand().as<ExpressionNode>());
			}
			return true;
		}
		
		// Ternary operator: check all three sub-expressions
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			const auto& ternary = std::get<TernaryOperatorNode>(expr);
			bool cond_noexcept = true, then_noexcept = true, else_noexcept = true;
			if (ternary.condition().is<ExpressionNode>()) {
				cond_noexcept = isExpressionNoexcept(ternary.condition().as<ExpressionNode>());
			}
			if (ternary.true_expr().is<ExpressionNode>()) {
				then_noexcept = isExpressionNoexcept(ternary.true_expr().as<ExpressionNode>());
			}
			if (ternary.false_expr().is<ExpressionNode>()) {
				else_noexcept = isExpressionNoexcept(ternary.false_expr().as<ExpressionNode>());
			}
			return cond_noexcept && then_noexcept && else_noexcept;
		}
		
		// Function calls: check if function is declared noexcept
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			const auto& func_call = std::get<FunctionCallNode>(expr);
			// Check if function_declaration is available and noexcept
			// The FunctionCallNode contains a reference to the function's DeclarationNode
			// We need to look up the FunctionDeclarationNode to check noexcept
			const DeclarationNode& decl = func_call.function_declaration();
			std::string_view func_name = decl.identifier_token().value();
			
			// Look up the function in the symbol table
			extern SymbolTable gSymbolTable;
			auto symbol = gSymbolTable.lookup(StringTable::getOrInternStringHandle(func_name));
			if (symbol.has_value() && symbol->is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& func_decl = symbol->as<FunctionDeclarationNode>();
				return func_decl.is_noexcept();
			}
			// If we can't determine, conservatively assume it may throw
			return false;
		}
		
		// Member function calls: check if method is declared noexcept
		if (std::holds_alternative<MemberFunctionCallNode>(expr)) {
			const auto& member_call = std::get<MemberFunctionCallNode>(expr);
			const FunctionDeclarationNode& func_decl = member_call.function_declaration();
			return func_decl.is_noexcept();
		}
		
		// Constructor calls: check if constructor is noexcept
		if (std::holds_alternative<ConstructorCallNode>(expr)) {
			// For now, conservatively assume constructors may throw
			// A complete implementation would check the constructor declaration
			return false;
		}
		
		// Array subscript: noexcept if index expression is noexcept
		if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			const auto& subscript = std::get<ArraySubscriptNode>(expr);
			if (subscript.index_expr().is<ExpressionNode>()) {
				return isExpressionNoexcept(subscript.index_expr().as<ExpressionNode>());
			}
			return true;
		}
		
		// Member access is noexcept
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			return true;
		}
		
		// sizeof, alignof, offsetof are always noexcept
		if (std::holds_alternative<SizeofExprNode>(expr) ||
		    std::holds_alternative<SizeofPackNode>(expr) ||
		    std::holds_alternative<AlignofExprNode>(expr) ||
		    std::holds_alternative<OffsetofExprNode>(expr)) {
			return true;
		}
		
		// Type traits are noexcept
		if (std::holds_alternative<TypeTraitExprNode>(expr)) {
			return true;
		}
		
		// new/delete can throw (unless using nothrow variant)
		if (std::holds_alternative<NewExpressionNode>(expr) ||
		    std::holds_alternative<DeleteExpressionNode>(expr)) {
			return false;
		}
		
		// Cast expressions: check the operand
		if (std::holds_alternative<StaticCastNode>(expr)) {
			const auto& cast = std::get<StaticCastNode>(expr);
			if (cast.expr().is<ExpressionNode>()) {
				return isExpressionNoexcept(cast.expr().as<ExpressionNode>());
			}
			return true;
		}
		if (std::holds_alternative<DynamicCastNode>(expr)) {
			// dynamic_cast can throw std::bad_cast
			return false;
		}
		if (std::holds_alternative<ConstCastNode>(expr)) {
			const auto& cast = std::get<ConstCastNode>(expr);
			if (cast.expr().is<ExpressionNode>()) {
				return isExpressionNoexcept(cast.expr().as<ExpressionNode>());
			}
			return true;
		}
		if (std::holds_alternative<ReinterpretCastNode>(expr)) {
			const auto& cast = std::get<ReinterpretCastNode>(expr);
			if (cast.expr().is<ExpressionNode>()) {
				return isExpressionNoexcept(cast.expr().as<ExpressionNode>());
			}
			return true;
		}
		
		// typeid can throw for dereferencing null polymorphic pointers
		if (std::holds_alternative<TypeidNode>(expr)) {
			return false;
		}
		
		// Lambda expressions themselves are noexcept (creating the closure)
		if (std::holds_alternative<LambdaExpressionNode>(expr)) {
			return true;
		}
		
		// Fold expressions: would need to check all sub-expressions
		if (std::holds_alternative<FoldExpressionNode>(expr)) {
			// Conservatively assume may throw
			return false;
		}
		
		// Pseudo-destructor calls are noexcept
		if (std::holds_alternative<PseudoDestructorCallNode>(expr)) {
			return true;
		}
		
		// Nested noexcept expression
		if (std::holds_alternative<NoexceptExprNode>(expr)) {
			// noexcept(noexcept(x)) - the outer noexcept doesn't evaluate its operand
			return true;
		}
		
		// Default: conservatively assume may throw
		return false;
	}

	// Implementation of recursive nested member store generation
	void generateNestedMemberStores(
	    const StructTypeInfo& struct_info,
	    const InitializerListNode& init_list,
	    StringHandle base_object,
	    int base_offset,
	    const Token& token)
	{
		// Build map of member names to initializer expressions
		std::unordered_map<StringHandle, const ASTNode*> member_values;
		size_t positional_index = 0;
		const auto& initializers = init_list.initializers();

		for (size_t i = 0; i < initializers.size(); ++i) {
			if (init_list.is_designated(i)) {
				member_values[init_list.member_name(i)] = &initializers[i];
			} else if (positional_index < struct_info.members.size()) {
				StringHandle member_name = struct_info.members[positional_index].getName();
				member_values[member_name] = &initializers[i];
				positional_index++;
			}
		}

		// Process each struct member
		for (const StructMember& member : struct_info.members) {
			StringHandle member_name = member.getName();

			if (!member_values.count(member_name)) {
				// Zero-initialize unspecified members
				MemberStoreOp member_store;
				member_store.value.type = member.type;
				member_store.value.size_in_bits = static_cast<int>(member.size * 8);
				member_store.value.value = 0ULL;
				member_store.object = base_object;
				member_store.member_name = member_name;
				member_store.offset = base_offset + static_cast<int>(member.offset);
				member_store.is_reference = member.is_reference();
				member_store.is_rvalue_reference = member.is_rvalue_reference();
				member_store.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
				continue;
			}

			const ASTNode& init_expr = *member_values[member_name];

			if (init_expr.is<InitializerListNode>()) {
				// Nested brace initializer - check if member is a struct
				const InitializerListNode& nested_init_list = init_expr.as<InitializerListNode>();

				if (member.type_index < gTypeInfo.size()) {
					const TypeInfo& member_type_info = gTypeInfo[member.type_index];

					if (member_type_info.struct_info_ && !member_type_info.struct_info_->members.empty()) {
						// RECURSIVE CALL for nested struct
						generateNestedMemberStores(
						    *member_type_info.struct_info_,
						    nested_init_list,
						    base_object,
						    base_offset + static_cast<int>(member.offset),
						    token
						);
						continue;
					}
				}

				// Not a struct type - try to extract single value from single-element list
				const auto& nested_initializers = nested_init_list.initializers();
				if (nested_initializers.size() == 1 && nested_initializers[0].is<ExpressionNode>()) {
					auto init_operands = visitExpressionNode(nested_initializers[0].as<ExpressionNode>());
					IrValue member_value = 0ULL;
					if (init_operands.size() >= 3) {
						if (std::holds_alternative<TempVar>(init_operands[2])) {
							member_value = std::get<TempVar>(init_operands[2]);
						} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
							member_value = std::get<unsigned long long>(init_operands[2]);
						} else if (std::holds_alternative<double>(init_operands[2])) {
							member_value = std::get<double>(init_operands[2]);
						} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
							member_value = std::get<StringHandle>(init_operands[2]);
						}
					}

					MemberStoreOp member_store;
					member_store.value.type = member.type;
					member_store.value.size_in_bits = static_cast<int>(member.size * 8);
					member_store.value.value = member_value;
					member_store.object = base_object;
					member_store.member_name = member_name;
					member_store.offset = base_offset + static_cast<int>(member.offset);
					member_store.is_reference = member.is_reference();
					member_store.is_rvalue_reference = member.is_rvalue_reference();
					member_store.struct_type_info = nullptr;
					ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
				} else {
					// Zero-initialize if we can't extract a value
					MemberStoreOp member_store;
					member_store.value.type = member.type;
					member_store.value.size_in_bits = static_cast<int>(member.size * 8);
					member_store.value.value = 0ULL;
					member_store.object = base_object;
					member_store.member_name = member_name;
					member_store.offset = base_offset + static_cast<int>(member.offset);
					member_store.is_reference = member.is_reference();
					member_store.is_rvalue_reference = member.is_rvalue_reference();
					member_store.struct_type_info = nullptr;
					ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
				}
			} else if (init_expr.is<ExpressionNode>()) {
				// Direct expression initializer
				auto init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
				IrValue member_value = 0ULL;
				if (init_operands.size() >= 3) {
					if (std::holds_alternative<TempVar>(init_operands[2])) {
						member_value = std::get<TempVar>(init_operands[2]);
					} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
						member_value = std::get<unsigned long long>(init_operands[2]);
					} else if (std::holds_alternative<double>(init_operands[2])) {
						member_value = std::get<double>(init_operands[2]);
					} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
						member_value = std::get<StringHandle>(init_operands[2]);
					}
				}

				MemberStoreOp member_store;
				member_store.value.type = member.type;
				member_store.value.size_in_bits = static_cast<int>(member.size * 8);
				member_store.value.value = member_value;
				member_store.object = base_object;
				member_store.member_name = member_name;
				member_store.offset = base_offset + static_cast<int>(member.offset);
				member_store.is_reference = member.is_reference();
				member_store.is_rvalue_reference = member.is_rvalue_reference();
				member_store.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
			}
		}
	}

	// Helper function to convert a MemberFunctionCallNode to a regular FunctionCallNode
	// Used when a member function call syntax is used but the object is not a struct
	std::vector<IrOperand> convertMemberCallToFunctionCall(const MemberFunctionCallNode& memberFunctionCallNode) {
		const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
		const DeclarationNode& decl_node = func_decl.decl_node();
		
		// Copy the arguments using the visit method
		ChunkedVector<ASTNode> args_copy;
		memberFunctionCallNode.arguments().visit([&](ASTNode arg) {
			args_copy.push_back(arg);
		});
		
		FunctionCallNode function_call(decl_node, std::move(args_copy), memberFunctionCallNode.called_from());
		return generateFunctionCallIr(function_call);
	}

	// Helper function to check if access to a member is allowed
	// Returns true if access is allowed, false otherwise
	bool checkMemberAccess(const StructMember* member,
	                       const StructTypeInfo* member_owner_struct,
	                       const StructTypeInfo* accessing_struct,
	                       [[maybe_unused]] const BaseClassSpecifier* inheritance_path = nullptr,
	                       const std::string_view& accessing_function = "") const {
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
		if (accessing_struct && member_owner_struct->isFriendClass(accessing_struct->getName())) {
			return true;
		}

		// If we're not in a member function context, only public members are accessible
		if (!accessing_struct) {
			return false;
		}

		// Helper: check if two structs are the same class, including template instantiations.
		// Template instantiations use a '$hash' suffix (e.g., basic_string_view$291eceb35e7234a9)
		// that must be stripped for comparison with the base template.
		// Template instantiation names may lack namespace prefix (e.g., "basic_string_view$hash"
		// vs "std::basic_string_view"), so we compare the unqualified class name only when
		// one name is a namespace-qualified version of the other.
		auto isSameClassOrInstantiation = [](const StructTypeInfo* a, const StructTypeInfo* b) -> bool {
			if (a == b) return true;
			if (!a || !b) return false;
			std::string_view name_a = StringTable::getStringView(a->getName());
			std::string_view name_b = StringTable::getStringView(b->getName());
			if (name_a == name_b) return true;
			// Strip '$hash' suffix only
			auto stripHash = [](std::string_view name) -> std::string_view {
				std::string_view base = extractBaseTemplateName(name);
				if (!base.empty()) {
					// Preserve namespace qualification: find the base template name
					// in the original and return everything up to where it starts
					auto pos = name.find(base);
					if (pos != std::string_view::npos) {
						return name.substr(0, pos + base.size());
					}
					return base;
				}
				return name;
			};
			std::string_view base_a = stripHash(name_a);
			std::string_view base_b = stripHash(name_b);
			if (base_a.empty() || base_b.empty()) return false;
			if (base_a == base_b) return true;
			// Handle asymmetric namespace qualification:
			// "basic_string_view" should match "std::basic_string_view" but
			// "ns1::Foo" should NOT match "ns2::Foo"
			// Check if the shorter name matches the unqualified part of the longer name
			auto getUnqualified = [](std::string_view name) -> std::string_view {
				auto ns_pos = name.rfind("::");
				if (ns_pos != std::string_view::npos) {
					return name.substr(ns_pos + 2);
				}
				return name;
			};
			// Only allow match when one has no namespace and the other does
			bool a_has_ns = base_a.find("::") != std::string_view::npos;
			bool b_has_ns = base_b.find("::") != std::string_view::npos;
			if (a_has_ns == b_has_ns) return false; // both qualified or both unqualified - already compared
			std::string_view unqual_a = getUnqualified(base_a);
			std::string_view unqual_b = getUnqualified(base_b);
			return unqual_a == unqual_b;
		};

		// Private members are only accessible from:
		// 1. The same class (or a template instantiation of the same class)
		// 2. Nested classes within the same class
		if (member->access == AccessSpecifier::Private) {
			if (isSameClassOrInstantiation(accessing_struct, member_owner_struct)) {
				return true;
			}
			// Check if accessing_struct is nested within member_owner_struct
			return isNestedWithin(accessing_struct, member_owner_struct);
		}

		// Protected members are accessible from:
		// 1. The same class (or a template instantiation of the same class)
		// 2. Derived classes (if inherited as public or protected)
		// 3. Nested classes within the same class (C++ allows nested classes to access protected)
		if (member->access == AccessSpecifier::Protected) {
			// Same class
			if (isSameClassOrInstantiation(accessing_struct, member_owner_struct)) {
				return true;
			}

			// Check if accessing_struct is nested within member_owner_struct
			if (isNestedWithin(accessing_struct, member_owner_struct)) {
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
	std::string_view getCurrentFunctionName() const {
		return current_function_name_.isValid() ? StringTable::getStringView(current_function_name_) : std::string_view();
	}

	// Helper function to check if access to a member function is allowed
	bool checkMemberFunctionAccess(const StructMemberFunction* member_func,
	                                const StructTypeInfo* member_owner_struct,
	                                const StructTypeInfo* accessing_struct,
	                                std::string_view accessing_function = "") const {
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
		if (accessing_struct && member_owner_struct->isFriendClass(accessing_struct->getName())) {
			return true;
		}

		// If we're not in a member function context, only public functions are accessible
		if (!accessing_struct) {
			return false;
		}

		// Private member functions are only accessible from:
		// 1. The same class
		// 2. Nested classes within the same class
		if (member_func->access == AccessSpecifier::Private) {
			if (accessing_struct == member_owner_struct) {
				return true;
			}
			// Check if accessing_struct is nested within member_owner_struct
			return isNestedWithin(accessing_struct, member_owner_struct);
		}

		// Protected member functions are accessible from:
		// 1. The same class
		// 2. Derived classes
		// 3. Nested classes within the same class (C++ allows nested classes to access protected)
		if (member_func->access == AccessSpecifier::Protected) {
			// Same class
			if (accessing_struct == member_owner_struct) {
				return true;
			}

			// Check if accessing_struct is nested within member_owner_struct
			if (isNestedWithin(accessing_struct, member_owner_struct)) {
				return true;
			}

			// Check if accessing_struct is derived from member_owner_struct
			return isAccessibleThroughInheritance(accessing_struct, member_owner_struct);
		}

		return false;
	}

	// Helper function to check if a variable is a reference by looking it up in the symbol table
	// Returns true if the variable is declared as a reference (&  or &&)
	bool isVariableReference(std::string_view var_name) const {
		const std::optional<ASTNode> symbol = symbol_table.lookup(var_name);
		
		if (symbol.has_value() && symbol->is<DeclarationNode>()) {
			const auto& decl = symbol->as<DeclarationNode>();
			const auto& type_spec = decl.type_node().as<TypeSpecifierNode>();
			return type_spec.is_lvalue_reference() || type_spec.is_rvalue_reference();
		}
		
		return false;
	}

	// Helper function to resolve the struct type and member info for a member access chain
	// Handles nested member access like o.inner.callback by recursively resolving types
	// Returns true if successfully resolved, with the struct_info and member populated
	bool resolveMemberAccessType(const MemberAccessNode& member_access,
	                            const StructTypeInfo*& out_struct_info,
	                            const StructMember*& out_member) const {
		// Get the base object expression
		const ASTNode& base_node = member_access.object();
		if (!base_node.is<ExpressionNode>()) {
			return false;
		}
		
		const ExpressionNode& base_expr = base_node.as<ExpressionNode>();
		TypeSpecifierNode base_type;
		
		if (std::holds_alternative<IdentifierNode>(base_expr)) {
			// Simple identifier - look it up in the symbol table
			const IdentifierNode& base_ident = std::get<IdentifierNode>(base_expr);
			std::optional<ASTNode> symbol = lookupSymbol(base_ident.name());
			if (!symbol.has_value()) {
				return false;
			}
			const DeclarationNode* base_decl = get_decl_from_symbol(*symbol);
			if (!base_decl) {
				return false;
			}
			base_type = base_decl->type_node().as<TypeSpecifierNode>();
		} else if (std::holds_alternative<MemberAccessNode>(base_expr)) {
			// Nested member access - recursively resolve
			const MemberAccessNode& nested_access = std::get<MemberAccessNode>(base_expr);
			const StructTypeInfo* nested_struct_info = nullptr;
			const StructMember* nested_member = nullptr;
			if (!resolveMemberAccessType(nested_access, nested_struct_info, nested_member)) {
				return false;
			}
			if (!nested_member || nested_member->type != Type::Struct) {
				return false;
			}
			// Get the type info for the nested member's struct type
			if (nested_member->type_index >= gTypeInfo.size()) {
				return false;
			}
			const TypeInfo& nested_type_info = gTypeInfo[nested_member->type_index];
			if (!nested_type_info.isStruct()) {
				return false;
			}
			// Convert size from bytes to bits for TypeSpecifierNode
			base_type = TypeSpecifierNode(Type::Struct, nested_member->type_index, 
			                               nested_member->size * 8, Token());  // size in bits
		} else {
			// Unsupported base expression type
			return false;
		}
		
		// If the base type is a pointer, dereference it
		if (base_type.pointer_levels().size() > 0) {
			base_type.remove_pointer_level();
		}
		
		// The base type should now be a struct type
		if (base_type.type() != Type::Struct) {
			return false;
		}
		
		// Look up the struct info
		size_t struct_type_index = base_type.type_index();
		if (struct_type_index >= gTypeInfo.size()) {
			return false;
		}
		const TypeInfo& struct_type_info = gTypeInfo[struct_type_index];
		const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
		if (!struct_info) {
			return false;
		}
		
		// Find the member in the struct
		std::string_view member_name = member_access.member_name();
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		for (const auto& member : struct_info->members) {
			if (member.getName() == member_name_handle) {
				out_struct_info = struct_info;
				out_member = &member;
				return true;
			}
		}
		
		return false;
	}

	// Helper function to handle assignment using lvalue metadata
	// Queries LValueInfo::Kind and routes to appropriate store instruction
	// Returns true if assignment was handled via lvalue metadata, false otherwise
	//
	// USAGE: Call this after evaluating both LHS and RHS expressions.
	//        If it returns true, the assignment was handled and caller should skip normal assignment logic.
	//        If it returns false, fall back to normal assignment or special-case handling.
	//
	// CURRENT LIMITATIONS:
	// - ArrayElement and Member cases need additional metadata (index, member_name) not currently in LValueInfo
	// - Only Indirect (dereference) case is fully implemented
	// - Future work: Extend LValueInfo or pass additional context to handle all cases
	bool handleLValueAssignment(const std::vector<IrOperand>& lhs_operands,
	                            const std::vector<IrOperand>& rhs_operands,
	                            const Token& token) {
		// Check if LHS has a TempVar with lvalue metadata
		if (lhs_operands.size() < 3 || !std::holds_alternative<TempVar>(lhs_operands[2])) {
			FLASH_LOG(Codegen, Info, "handleLValueAssignment: FAIL - size=", lhs_operands.size(), " has_tempvar=", (lhs_operands.size() >= 3 ? std::holds_alternative<TempVar>(lhs_operands[2]) : false));
			return false;
		}

		TempVar lhs_temp = std::get<TempVar>(lhs_operands[2]);
		auto lvalue_info_opt = getTempVarLValueInfo(lhs_temp);
		
		if (!lvalue_info_opt.has_value()) {
			FLASH_LOG(Codegen, Info, "handleLValueAssignment: FAIL - no lvalue metadata for temp=", lhs_temp.var_number);
			return false;
		}

		const LValueInfo& lv_info = lvalue_info_opt.value();
		
		FLASH_LOG(Codegen, Debug, "handleLValueAssignment: kind=", static_cast<int>(lv_info.kind));

		// Route to appropriate store instruction based on LValueInfo::Kind
		switch (lv_info.kind) {
			case LValueInfo::Kind::ArrayElement: {
				// Array element assignment: arr[i] = value
				FLASH_LOG(Codegen, Debug, "  -> ArrayStore (handled via metadata)");
				
				// Check if we have the index stored in metadata
				if (!lv_info.array_index.has_value()) {
					FLASH_LOG(Codegen, Info, "     ArrayElement: No index in metadata, falling back");
					return false;
				}
				
				FLASH_LOG(Codegen, Info, "     ArrayElement: Has index in metadata, proceeding with unified handler");
				
				// Build TypedValue for index from metadata
				IrValue index_value = lv_info.array_index.value();
				TypedValue index_tv;
				index_tv.value = index_value;
				index_tv.type = Type::Int;  // Index type (typically int)
				index_tv.size_in_bits = 32;  // Standard index size
				
				// Build TypedValue for value with LHS type/size but RHS value
				// This is important: the size must match the array element type
				TypedValue value_tv;
				value_tv.type = std::get<Type>(lhs_operands[0]);
				value_tv.size_in_bits = std::get<int>(lhs_operands[1]);
				value_tv.value = toIrValue(rhs_operands[2]);
				
				// Emit the store using helper
				emitArrayStore(
					std::get<Type>(lhs_operands[0]),  // element_type
					std::get<int>(lhs_operands[1]),   // element_size_bits
					lv_info.base,                      // array
					index_tv,                          // index
					value_tv,                          // value (with LHS type/size, RHS value)
					lv_info.offset,                    // member_offset
					lv_info.is_pointer_to_array,       // is_pointer_to_array
					token
				);
				return true;
			}

			case LValueInfo::Kind::Member: {
				// Member assignment: obj.member = value
				FLASH_LOG(Codegen, Debug, "  -> MemberStore (handled via metadata)");
				
				// Check if we have member_name stored in metadata
				if (!lv_info.member_name.has_value()) {
					FLASH_LOG(Codegen, Debug, "     No member_name in metadata, falling back");
					return false;
				}
				
				// Safety check: validate size is reasonable (not 0 or negative)
				int lhs_size = std::get<int>(lhs_operands[1]);
				if (lhs_size <= 0 || lhs_size > 1024) {
					FLASH_LOG(Codegen, Debug, "     Invalid size in metadata (", lhs_size, "), falling back");
					return false;
				}
				
				// Build TypedValue with LHS type/size but RHS value
				// This is important: the size must match the member being stored to, not the RHS
				TypedValue value_tv;
				value_tv.type = std::get<Type>(lhs_operands[0]);
				value_tv.size_in_bits = lhs_size;
				value_tv.value = toIrValue(rhs_operands[2]);
				
				// Emit the store using helper
				emitMemberStore(
					value_tv,                           // value (with LHS type/size, RHS value)
					lv_info.base,                       // object
					lv_info.member_name.value(),        // member_name
					lv_info.offset,                     // offset
					false,                              // is_reference
					false,                              // is_rvalue_reference
					lv_info.is_pointer_to_member,       // is_pointer_to_member
					token,
					lv_info.bitfield_width,             // bitfield_width
					lv_info.bitfield_bit_offset         // bitfield_bit_offset
				);
				return true;
			}

			case LValueInfo::Kind::Indirect: {
				// Dereference assignment: *ptr = value
				// This case works because we have all needed info in LValueInfo
				FLASH_LOG(Codegen, Debug, "  -> DereferenceStore (handled via metadata)");
				
				// Emit the store using helper
				emitDereferenceStore(
					toTypedValue(rhs_operands),     // value
					std::get<Type>(lhs_operands[0]), // pointee_type
					std::get<int>(lhs_operands[1]),  // pointee_size_in_bits
					lv_info.base,                    // pointer
					token
				);
				return true;
			}

			case LValueInfo::Kind::Direct:
			case LValueInfo::Kind::Temporary:
				// Direct variable assignment - handled by regular assignment logic
				FLASH_LOG(Codegen, Debug, "  -> Regular assignment (Direct/Temporary)");
				return false;

			default:
				return false;
		}

		return false;
	}

	// Handle compound assignment to lvalues (e.g., v.x += 5, arr[i] += 5)
	// Supports Member kind (struct member access), Indirect kind (dereferenced pointers - already supported), and ArrayElement kind (array subscripts - added in this function)
	// This is similar to handleLValueAssignment but also performs the arithmetic operation
	bool handleLValueCompoundAssignment(const std::vector<IrOperand>& lhs_operands,
	                                     const std::vector<IrOperand>& rhs_operands,
	                                     const Token& token,
	                                     std::string_view op) {
		// Check if LHS has a TempVar with lvalue metadata
		if (lhs_operands.size() < 3 || !std::holds_alternative<TempVar>(lhs_operands[2])) {
			FLASH_LOG(Codegen, Info, "handleLValueCompoundAssignment: FAIL - size=", lhs_operands.size(), 
				", has_tempvar=", (lhs_operands.size() >= 3 && std::holds_alternative<TempVar>(lhs_operands[2])));
			return false;
		}

		TempVar lhs_temp = std::get<TempVar>(lhs_operands[2]);
		FLASH_LOG_FORMAT(Codegen, Debug, "handleLValueCompoundAssignment: Checking TempVar {} for metadata", lhs_temp.var_number);
		auto lvalue_info_opt = getTempVarLValueInfo(lhs_temp);
		
		if (!lvalue_info_opt.has_value()) {
			FLASH_LOG_FORMAT(Codegen, Debug, "handleLValueCompoundAssignment: FAIL - no lvalue metadata for TempVar {}", lhs_temp.var_number);
			return false;
		}

		const LValueInfo& lv_info = lvalue_info_opt.value();
		
		FLASH_LOG(Codegen, Debug, "handleLValueCompoundAssignment: kind=", static_cast<int>(lv_info.kind), " op=", op);

		// For compound assignments, we need to:
		// 1. The lhs_temp already contains the ADDRESS (from LValueAddress context)
		// 2. We need to LOAD the current value from that address
		// 3. Perform the operation with RHS
		// 4. Store the result back to the address
		
		// First, load the current value from the lvalue
		// The lhs_temp should contain the address, but we need to generate a Load instruction
		// to get the current value into a temp var
		TempVar current_value_temp = var_counter.next();
		
		// Generate a Load instruction based on the lvalue kind
		// Support both Member kind and Indirect kind (for dereferenced pointers like &y in lambda captures)
		if (lv_info.kind == LValueInfo::Kind::Indirect) {
			// For Indirect kind (dereferenced pointer), the base can be a TempVar or StringHandle
			// Generate a Dereference instruction to load the current value
			DereferenceOp deref_op;
			deref_op.result = current_value_temp;
			deref_op.pointer.type = std::get<Type>(lhs_operands[0]);
			deref_op.pointer.size_in_bits = 64;  // pointer size
			deref_op.pointer.pointer_depth = 1;
			
			// Extract the base (TempVar or StringHandle)
			std::variant<TempVar, StringHandle> base_value;
			if (std::holds_alternative<TempVar>(lv_info.base)) {
				deref_op.pointer.value = std::get<TempVar>(lv_info.base);
				base_value = std::get<TempVar>(lv_info.base);
			} else if (std::holds_alternative<StringHandle>(lv_info.base)) {
				deref_op.pointer.value = std::get<StringHandle>(lv_info.base);
				base_value = std::get<StringHandle>(lv_info.base);
			} else {
				FLASH_LOG(Codegen, Debug, "     Indirect kind requires TempVar or StringHandle base");
				return false;
			}
			
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), token));
			
			// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
			TempVar result_temp = var_counter.next();
			
			static const std::unordered_map<std::string_view, IrOpcode> compound_op_map = {
				{"+=", IrOpcode::Add},
				{"-=", IrOpcode::Subtract},
				{"*=", IrOpcode::Multiply},
				{"/=", IrOpcode::Divide},
				{"%=", IrOpcode::Modulo},
				{"&=", IrOpcode::BitwiseAnd},
				{"|=", IrOpcode::BitwiseOr},
				{"^=", IrOpcode::BitwiseXor},
				{"<<=", IrOpcode::ShiftLeft},
				{">>=", IrOpcode::ShiftRight}
			};
			
			auto op_it = compound_op_map.find(op);
			if (op_it == compound_op_map.end()) {
				FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
				return false;
			}
			IrOpcode operation_opcode = op_it->second;
			
			// Create the binary operation
			BinaryOp bin_op;
			bin_op.lhs.type = std::get<Type>(lhs_operands[0]);
			bin_op.lhs.size_in_bits = std::get<int>(lhs_operands[1]);
			bin_op.lhs.value = current_value_temp;
			bin_op.rhs = toTypedValue(rhs_operands);
			bin_op.result = result_temp;
			
			ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));
			
			// Store result back through the pointer using DereferenceStore
			TypedValue result_tv;
			result_tv.type = std::get<Type>(lhs_operands[0]);
			result_tv.size_in_bits = std::get<int>(lhs_operands[1]);
			result_tv.value = result_temp;
			
			// Handle both TempVar and StringHandle bases for DereferenceStore
			if (std::holds_alternative<TempVar>(base_value)) {
				emitDereferenceStore(result_tv, std::get<Type>(lhs_operands[0]), std::get<int>(lhs_operands[1]),
				                     std::get<TempVar>(base_value), token);
			} else {
				// StringHandle base: emitDereferenceStore expects a TempVar, so we pass the StringHandle as the pointer
				// Generate DereferenceStore with StringHandle directly
				DereferenceStoreOp store_op;
				store_op.pointer.type = std::get<Type>(lhs_operands[0]);
				store_op.pointer.size_in_bits = 64;
				store_op.pointer.pointer_depth = 1;
				store_op.pointer.value = std::get<StringHandle>(base_value);
				store_op.value = result_tv;
				ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));
			}
			
			return true;
		}
		
		// Handle ArrayElement kind for compound assignments (e.g., arr[i] += 5)
		if (lv_info.kind == LValueInfo::Kind::ArrayElement) {
			// Check if we have the index stored in metadata
			if (!lv_info.array_index.has_value()) {
				FLASH_LOG(Codegen, Debug, "     ArrayElement: No index in metadata for compound assignment");
				return false;
			}
			
			FLASH_LOG(Codegen, Debug, "     ArrayElement compound assignment: proceeding with unified handler");
			
			// Build TypedValue for index from metadata
			IrValue index_value = lv_info.array_index.value();
			TypedValue index_tv;
			index_tv.value = index_value;
			index_tv.type = Type::Int;  // Index type (typically int)
			index_tv.size_in_bits = 32;  // Standard index size
			
			// Create ArrayAccessOp to load current value
			ArrayAccessOp load_op;
			load_op.result = current_value_temp;
			load_op.element_type = std::get<Type>(lhs_operands[0]);
			load_op.element_size_in_bits = std::get<int>(lhs_operands[1]);
			load_op.array = lv_info.base;
			load_op.index = index_tv;
			load_op.member_offset = lv_info.offset;
			load_op.is_pointer_to_array = lv_info.is_pointer_to_array;
			
			ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(load_op), token));
			
			// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
			TempVar result_temp = var_counter.next();
			
			// Map compound assignment operator to the corresponding operation
			static const std::unordered_map<std::string_view, IrOpcode> compound_op_map = {
				{"+=", IrOpcode::Add},
				{"-=", IrOpcode::Subtract},
				{"*=", IrOpcode::Multiply},
				{"/=", IrOpcode::Divide},
				{"%=", IrOpcode::Modulo},
				{"&=", IrOpcode::BitwiseAnd},
				{"|=", IrOpcode::BitwiseOr},
				{"^=", IrOpcode::BitwiseXor},
				{"<<=", IrOpcode::ShiftLeft},
				{">>=", IrOpcode::ShiftRight}
			};
			
			auto op_it = compound_op_map.find(op);
			if (op_it == compound_op_map.end()) {
				FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
				return false;
			}
			IrOpcode operation_opcode = op_it->second;
			
			// Create the binary operation
			BinaryOp bin_op;
			bin_op.lhs.type = std::get<Type>(lhs_operands[0]);
			bin_op.lhs.size_in_bits = std::get<int>(lhs_operands[1]);
			bin_op.lhs.value = current_value_temp;
			bin_op.rhs = toTypedValue(rhs_operands);
			bin_op.result = result_temp;
			
			ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));
			
			// Finally, store the result back to the array element
			TypedValue result_tv;
			result_tv.type = std::get<Type>(lhs_operands[0]);
			result_tv.size_in_bits = std::get<int>(lhs_operands[1]);
			result_tv.value = result_temp;
			
			// Emit the store using helper
			emitArrayStore(
				std::get<Type>(lhs_operands[0]),  // element_type
				std::get<int>(lhs_operands[1]),   // element_size_bits
				lv_info.base,                      // array
				index_tv,                          // index
				result_tv,                         // value (result of operation)
				lv_info.offset,                    // member_offset
				lv_info.is_pointer_to_array,       // is_pointer_to_array
				token
			);
			
			return true;
		}
		
	// Handle Global kind for compound assignments (e.g., g_score += 20)
		if (lv_info.kind == LValueInfo::Kind::Global) {
			if (!std::holds_alternative<StringHandle>(lv_info.base)) {
				FLASH_LOG(Codegen, Debug, "     Global compound assignment: base is not a StringHandle");
				return false;
			}
			StringHandle global_name = std::get<StringHandle>(lv_info.base);
			FLASH_LOG(Codegen, Debug, "     Global compound assignment op=", op);

			// Map compound assignment operator to the corresponding operation
			static const std::unordered_map<std::string_view, IrOpcode> compound_op_map = {
				{"+=", IrOpcode::Add},
				{"-=", IrOpcode::Subtract},
				{"*=", IrOpcode::Multiply},
				{"/=", IrOpcode::Divide},
				{"%=", IrOpcode::Modulo},
				{"&=", IrOpcode::BitwiseAnd},
				{"|=", IrOpcode::BitwiseOr},
				{"^=", IrOpcode::BitwiseXor},
				{"<<=", IrOpcode::ShiftLeft},
				{">>=", IrOpcode::ShiftRight}
			};
			auto op_it = compound_op_map.find(op);
			if (op_it == compound_op_map.end()) {
				FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
				return false;
			}

			// lhs_temp already holds the loaded value (from GlobalLoad in LHS evaluation)
			TempVar result_temp = var_counter.next();
			BinaryOp bin_op;
			bin_op.lhs.type = std::get<Type>(lhs_operands[0]);
			bin_op.lhs.size_in_bits = std::get<int>(lhs_operands[1]);
			bin_op.lhs.value = lhs_temp;
			bin_op.rhs = toTypedValue(rhs_operands);
			bin_op.result = result_temp;
			ir_.addInstruction(IrInstruction(op_it->second, std::move(bin_op), token));

			// Store result back to global
			std::vector<IrOperand> store_operands;
			store_operands.emplace_back(global_name);
			store_operands.emplace_back(result_temp);
			ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_operands), token);

			return true;
		}

		if (lv_info.kind != LValueInfo::Kind::Member) {
			FLASH_LOG(Codegen, Debug, "     Compound assignment only supports Member, Indirect, ArrayElement, or Global kind, got: ", static_cast<int>(lv_info.kind));
			return false;
		}
		
		// For member access, generate MemberAccess (Load) instruction
		if (!lv_info.member_name.has_value()) {
			FLASH_LOG(Codegen, Debug, "     No member_name in metadata for compound assignment");
			return false;
		}
		
		// Lookup member info to get is_reference flags
		bool member_is_reference = false;
		bool member_is_rvalue_reference = false;
		
		// Try to get struct type info from the base object
		if (std::holds_alternative<StringHandle>(lv_info.base)) {
			StringHandle base_name_handle = std::get<StringHandle>(lv_info.base);
			std::string_view base_name = StringTable::getStringView(base_name_handle);
			
			// Look up the base object in symbol table
			std::optional<ASTNode> symbol = lookupSymbol(base_name);
			
			if (symbol.has_value()) {
				const DeclarationNode* decl = get_decl_from_symbol(*symbol);
				if (decl) {
					const TypeSpecifierNode& type_node = decl->type_node().as<TypeSpecifierNode>();
					if (is_struct_type(type_node.type())) {
						TypeIndex type_index = type_node.type_index();
						if (type_index < gTypeInfo.size()) {
							auto result = FlashCpp::gLazyMemberResolver.resolve(type_index, lv_info.member_name.value());
							if (result) {
								member_is_reference = result.member->is_reference();
								member_is_rvalue_reference = result.member->is_rvalue_reference();
							}
						}
					}
				}
			}
		}
		// Note: For TempVar base, we don't have easy access to type info, so we default to false
		// This is acceptable since most compound assignments don't involve reference members
		
		MemberLoadOp load_op;
		load_op.result.value = current_value_temp;
		load_op.result.type = std::get<Type>(lhs_operands[0]);
		load_op.result.size_in_bits = std::get<int>(lhs_operands[1]);
		load_op.object = lv_info.base;
		load_op.member_name = lv_info.member_name.value();
		load_op.offset = lv_info.offset;
		load_op.is_reference = member_is_reference;
		load_op.is_rvalue_reference = member_is_rvalue_reference;
		load_op.struct_type_info = nullptr;
		load_op.bitfield_width = lv_info.bitfield_width;
		load_op.bitfield_bit_offset = lv_info.bitfield_bit_offset;
		
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));
		
		// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
		TempVar result_temp = var_counter.next();
		
		// Map compound assignment operator to the corresponding operation
		static const std::unordered_map<std::string_view, IrOpcode> compound_op_map = {
			{"+=", IrOpcode::Add},
			{"-=", IrOpcode::Subtract},
			{"*=", IrOpcode::Multiply},
			{"/=", IrOpcode::Divide},
			{"%=", IrOpcode::Modulo},
			{"&=", IrOpcode::BitwiseAnd},
			{"|=", IrOpcode::BitwiseOr},
			{"^=", IrOpcode::BitwiseXor},
			{"<<=", IrOpcode::ShiftLeft},
			{">>=", IrOpcode::ShiftRight}
		};
		
		auto op_it = compound_op_map.find(op);
		if (op_it == compound_op_map.end()) {
			FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
			return false;
		}
		IrOpcode operation_opcode = op_it->second;
		
		// Create the binary operation
		BinaryOp bin_op;
		bin_op.lhs.type = std::get<Type>(lhs_operands[0]);
		bin_op.lhs.size_in_bits = std::get<int>(lhs_operands[1]);
		bin_op.lhs.value = current_value_temp;
		bin_op.rhs = toTypedValue(rhs_operands);
		bin_op.result = result_temp;
		
		ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));
		
		// Finally, store the result back to the lvalue
		TypedValue result_tv;
		result_tv.type = std::get<Type>(lhs_operands[0]);
		result_tv.size_in_bits = std::get<int>(lhs_operands[1]);
		result_tv.value = result_temp;
		
		emitMemberStore(
			result_tv,
			lv_info.base,
			lv_info.member_name.value(),
			lv_info.offset,
			member_is_reference,
			member_is_rvalue_reference,
			lv_info.is_pointer_to_member,  // is_pointer_to_member
			token,
			lv_info.bitfield_width,
			lv_info.bitfield_bit_offset
		);
		
		return true;
	}

	// Helper functions to emit store instructions
	// These can be used by both the unified handler and special-case code
	
	// Emit ArrayStore instruction
	void emitArrayStore(Type element_type, int element_size_bits,
	                    std::variant<StringHandle, TempVar> array,
	                    const TypedValue& index, const TypedValue& value,
	                    int64_t member_offset, bool is_pointer_to_array,
	                    const Token& token) {
		ArrayStoreOp payload;
		payload.element_type = element_type;
		payload.element_size_in_bits = element_size_bits;
		payload.array = array;
		payload.index = index;
		payload.value = value;
		payload.member_offset = member_offset;
		payload.is_pointer_to_array = is_pointer_to_array;
		
		ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(payload), token));
	}
	
	// Emit MemberStore instruction
	void emitMemberStore(const TypedValue& value,
	                     std::variant<StringHandle, TempVar> object,
	                     StringHandle member_name, int offset,
	                     bool is_reference = false, bool is_rvalue_reference = false,
	                     bool is_pointer_to_member = false,
	                     const Token& token = Token(),
	                     std::optional<size_t> bitfield_width = std::nullopt,
	                     size_t bitfield_bit_offset = 0) {
		MemberStoreOp member_store;
		member_store.value = value;
		member_store.object = object;
		member_store.member_name = member_name;
		member_store.offset = offset;
		member_store.struct_type_info = nullptr;
		member_store.is_reference = is_reference;
		member_store.is_rvalue_reference = is_rvalue_reference;
		member_store.vtable_symbol = StringHandle();
		member_store.is_pointer_to_member = is_pointer_to_member;
		member_store.bitfield_width = bitfield_width;
		member_store.bitfield_bit_offset = bitfield_bit_offset;
		
		ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
	}
	
	// Emit DereferenceStore instruction
	void emitDereferenceStore(const TypedValue& value, Type pointee_type, [[maybe_unused]] int pointee_size_bits,
	                          std::variant<StringHandle, TempVar> pointer,
	                          const Token& token) {
		DereferenceStoreOp store_op;
		store_op.value = value;
		
		// Populate pointer TypedValue
		store_op.pointer.type = pointee_type;
		store_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
		store_op.pointer.pointer_depth = 1;  // Single pointer dereference
		// Convert std::variant<StringHandle, TempVar> to IrValue
		if (std::holds_alternative<StringHandle>(pointer)) {
			store_op.pointer.value = std::get<StringHandle>(pointer);
		} else {
			store_op.pointer.value = std::get<TempVar>(pointer);
		}
		
		ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));
	}

	const DeclarationNode& requireDeclarationNode(const ASTNode& node, std::string_view context) const {
		try {
			return node.as<DeclarationNode>();
		} catch (...) {
			FLASH_LOG(Codegen, Error, "BAD DeclarationNode cast in ", context,
			          ": type_name=", node.type_name(),
			          " has_value=", node.has_value());
			throw;
		}
	}

	// Helper to generate FunctionAddress IR for a lambda's __invoke function
	// Returns the TempVar holding the function pointer address
	TempVar generateLambdaInvokeFunctionAddress(const LambdaExpressionNode& lambda) {
		std::string_view invoke_name = StringBuilder()
			.append(lambda.generate_lambda_name())
			.append("_invoke")
			.commit();
		
		// Compute the mangled name for the __invoke function
		// Lambda return type defaults to int if not specified
		Type return_type = Type::Int;
		int return_size = 32;
		if (lambda.return_type().has_value()) {
			const auto& ret_type_node = lambda.return_type()->as<TypeSpecifierNode>();
			return_type = ret_type_node.type();
			return_size = ret_type_node.size_in_bits();
		}
		TypeSpecifierNode return_type_node(return_type, 0, return_size, lambda.lambda_token());
		
		// Build parameter types
		std::vector<TypeSpecifierNode> param_type_nodes;
		for (const auto& param : lambda.parameters()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				param_type_nodes.push_back(param_type);
			}
		}
		
		// Generate mangled name
		std::string_view mangled = generateMangledNameForCall(
			invoke_name, return_type_node, param_type_nodes, false, "");
		
		// Generate FunctionAddress instruction to get the address
		TempVar func_addr_var = var_counter.next();
		FunctionAddressOp op;
		op.result.type = Type::FunctionPointer;
		op.result.size_in_bits = 64;
		op.result.value = func_addr_var;
		op.function_name = StringTable::getOrInternStringHandle(invoke_name);
		op.mangled_name = StringTable::getOrInternStringHandle(mangled);
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionAddress, std::move(op), Token()));
		
		return func_addr_var;
	}

	// Helper to find a conversion operator in a struct that converts to the target type
	// Returns nullptr if no suitable conversion operator is found
	// Searches the struct and its base classes for "operator target_type()"
	const StructMemberFunction* findConversionOperator(
		const StructTypeInfo* struct_info,
		Type target_type,
		TypeIndex target_type_index = 0) const {
		
		if (!struct_info) return nullptr;
		
		// Build the operator name we are looking for (e.g., "operator int")
		std::string_view target_type_name;
		if (target_type == Type::Struct && target_type_index < gTypeInfo.size()) {
			target_type_name = StringTable::getStringView(gTypeInfo[target_type_index].name());
		} else {
			// For primitive types, use the helper function to get the type name
			target_type_name = getTypeName(target_type);
			if (target_type_name.empty()) {
				return nullptr;
			}
		}
		
		// Create the operator name string (e.g., "operator int")
		StringBuilder sb;
		sb.append("operator ").append(target_type_name);
		std::string_view operator_name = sb.commit();
		StringHandle operator_name_handle = StringTable::getOrInternStringHandle(operator_name);
		
		// Search member functions for the conversion operator
		for (const auto& member_func : struct_info->member_functions) {
			if (member_func.getName() == operator_name_handle) {
				return &member_func;
			}
		}
		
		// WORKAROUND: Also look for "operator user_defined" which may be a conversion operator
		// that was created with a typedef that wasn't resolved during template instantiation
		// Check if the return type matches the target type
		StringHandle user_defined_handle = StringTable::getOrInternStringHandle("operator user_defined");
		for (const auto& member_func : struct_info->member_functions) {
			if (member_func.getName() == user_defined_handle) {
				// Check if this function's return type matches our target
				if (member_func.function_decl.is<FunctionDeclarationNode>()) {
					const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
					const auto& decl_node = func_decl.decl_node();
					const auto& return_type_node = decl_node.type_node();
					if (return_type_node.is<TypeSpecifierNode>()) {
						const auto& type_spec = return_type_node.as<TypeSpecifierNode>();
						Type resolved_type = type_spec.type();
						
						// If the return type is UserDefined (a type alias), try to resolve it to the actual underlying type
						// This handles cases like `operator value_type()` where `using value_type = T;`
						// Use recursive resolution to handle chains of type aliases
						if (resolved_type == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
							TypeIndex current_type_index = type_spec.type_index();
							int max_depth = 10;  // Prevent infinite loops from circular aliases
							while (resolved_type == Type::UserDefined && current_type_index < gTypeInfo.size() && max_depth-- > 0) {
								const TypeInfo& alias_type_info = gTypeInfo[current_type_index];
								if (alias_type_info.type_ != Type::Void && alias_type_info.type_ != Type::UserDefined) {
									resolved_type = alias_type_info.type_;
									FLASH_LOG(Codegen, Debug, "Resolved type alias in conversion operator return type: UserDefined -> ", static_cast<int>(resolved_type));
									break;
								} else if (alias_type_info.type_ == Type::UserDefined && alias_type_info.type_index_ != current_type_index) {
									// Follow the chain of aliases
									current_type_index = alias_type_info.type_index_;
								} else {
									break;
								}
							}
						}
						
						if (resolved_type == target_type) {
							// Found a match!
							FLASH_LOG(Codegen, Debug, "Found conversion operator via 'operator user_defined' workaround");
							return &member_func;
						}
						
						// FALLBACK: If the return type is still UserDefined (couldn't resolve via gTypeInfo),
						// but the size matches the target primitive type, accept it as a match.
						// This handles template type aliases like `using value_type = T;` where T is substituted
						// but the return type wasn't fully updated in the AST.
						if (resolved_type == Type::UserDefined && target_type != Type::Struct && target_type != Type::Enum) {
							int expected_size = get_type_size_bits(target_type);
							
							if (expected_size > 0 && static_cast<int>(type_spec.size_in_bits()) == expected_size) {
								FLASH_LOG(Codegen, Debug, "Found conversion operator via size matching: UserDefined(size=", 
								          type_spec.size_in_bits(), ") matches target type ", static_cast<int>(target_type), " (size=", expected_size, ")");
								return &member_func;
							}
							// Note: We intentionally don't have a permissive fallback here because it would match
							// conversion operators from pattern templates that don't have generated code, leading
							// to linker errors (undefined reference to operator user_defined).
						}
					}
				}
			}
		}
		
		// Search base classes recursively
		for (const auto& base_spec : struct_info->base_classes) {
			if (base_spec.type_index < gTypeInfo.size()) {
				const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
				if (base_type_info.isStruct()) {
					const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
					const StructMemberFunction* result = findConversionOperator(
						base_struct_info, target_type, target_type_index);
					if (result) return result;
				}
			}
		}
		
		return nullptr;
	}

	// Helper to get the size of a type in bytes
	// Reuses the same logic as sizeof() operator
	// Used for pointer arithmetic (++/-- operators need sizeof(pointee_type))
	size_t getSizeInBytes(Type type, TypeIndex type_index, int size_in_bits) const {
		if (type == Type::Struct) {
			assert(type_index < gTypeInfo.size() && "Invalid type_index for struct");
			const TypeInfo& type_info = gTypeInfo[type_index];
			const StructTypeInfo* struct_info = type_info.getStructInfo();
			assert(struct_info && "Struct type info not found");
			return struct_info->total_size;
		}
		// For primitive types, convert bits to bytes
		return size_in_bits / 8;
	}

	// ========== Lambda Capture Helper Functions ==========

	// Get the current lambda's closure StructTypeInfo, or nullptr if not in a lambda
	const StructTypeInfo* getCurrentClosureStruct() const {
		if (!current_lambda_context_.isActive()) {
			return nullptr;
		}
		auto it = gTypesByName.find(current_lambda_context_.closure_type);
		if (it == gTypesByName.end() || !it->second->isStruct()) {
			return nullptr;
		}
		return it->second->getStructInfo();
	}

	// Check if we're in a lambda with [*this] capture
	bool isInCopyThisLambda() const {
		if (!current_lambda_context_.isActive()) {
			return false;
		}
		if (current_lambda_context_.has_copy_this) {
			return true;
		}
		if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
			return closure->findMember("__copy_this") != nullptr;
		}
		return false;
	}

	// Check if we're in a lambda with [this] pointer capture
	bool isInThisPointerLambda() const {
		return current_lambda_context_.isActive() && current_lambda_context_.has_this_pointer;
	}

	// Get the offset of a member in the current lambda closure struct
	// Returns 0 if not found or not in a lambda context
	int getClosureMemberOffset(std::string_view member_name) const {
		if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
			if (const StructMember* member = closure->findMember(member_name)) {
				return static_cast<int>(member->offset);
			}
		}
		return 0;
	}

	// Emit IR to load __copy_this from current lambda closure into a TempVar.
	// Returns the TempVar holding the copied object, or std::nullopt if not applicable.
	// The Token parameter is used for source location in the IR instruction.
	std::optional<TempVar> emitLoadCopyThis(const Token& token) {
		if (!isInCopyThisLambda()) {
			return std::nullopt;
		}
		const StructTypeInfo* closure_struct = getCurrentClosureStruct();
		if (!closure_struct) {
			return std::nullopt;
		}
		const StructMember* copy_this_member = closure_struct->findMember("__copy_this");
		if (!copy_this_member || current_lambda_context_.enclosing_struct_type_index == 0) {
			return std::nullopt;
		}

		TempVar copy_this_temp = var_counter.next();
		MemberLoadOp load_op;
		load_op.result.value = copy_this_temp;
		load_op.result.type = Type::Struct;
		load_op.result.size_in_bits = static_cast<int>(copy_this_member->size * 8);
		load_op.object = StringTable::getOrInternStringHandle("this");  // Lambda's this (the closure)
		load_op.member_name = StringTable::getOrInternStringHandle("__copy_this");
		load_op.offset = static_cast<int>(copy_this_member->offset);
		load_op.is_reference = false;
		load_op.is_rvalue_reference = false;
		load_op.struct_type_info = nullptr;
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));

		// Mark this temp var as an lvalue pointing to %this.__copy_this
		// This allows subsequent member accesses and stores to properly chain offsets
		LValueInfo lvalue_info(
			LValueInfo::Kind::Member,
			StringTable::getOrInternStringHandle("this"),
			static_cast<int>(copy_this_member->offset)
		);
		lvalue_info.member_name = StringTable::getOrInternStringHandle("__copy_this");
		lvalue_info.is_pointer_to_member = true;  // Treat closure 'this' as a pointer
		setTempVarMetadata(copy_this_temp, TempVarMetadata::makeLValue(lvalue_info));

		return copy_this_temp;
	}

	// Manage lambda context push/pop for nested lambdas
	void pushLambdaContext(const LambdaInfo& lambda_info) {
		lambda_context_stack_.push_back(current_lambda_context_);
		current_lambda_context_ = {};
		current_lambda_context_.closure_type = StringTable::getOrInternStringHandle(lambda_info.closure_type_name);
		current_lambda_context_.enclosing_struct_type_index = lambda_info.enclosing_struct_type_index;
		current_lambda_context_.has_copy_this = lambda_info.enclosing_struct_type_index > 0;
		current_lambda_context_.has_this_pointer = false;
		current_lambda_context_.is_mutable = lambda_info.is_mutable;

		size_t capture_index = 0;
		for (const auto& capture : lambda_info.captures) {
			if (capture.is_capture_all()) {
				continue;
			}
			StringHandle var_name = StringTable::getOrInternStringHandle(capture.identifier_name());
			current_lambda_context_.captures.insert(var_name);
			current_lambda_context_.capture_kinds[var_name] = capture.kind();
			if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
			    capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
				current_lambda_context_.captures.insert(StringTable::getOrInternStringHandle("this"sv));
				current_lambda_context_.capture_kinds[StringTable::getOrInternStringHandle("this"sv)] = capture.kind();
				if (capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
					current_lambda_context_.has_copy_this = true;
				} else if (capture.kind() == LambdaCaptureNode::CaptureKind::This) {
					current_lambda_context_.has_this_pointer = true;
				}
			} else if (capture.has_initializer()) {
				// Init-capture: infer type from initializer expression or closure struct member
				// For init-capture by reference [&y = x], look up x's type
				const ASTNode& init_node = *capture.initializer();
				if (init_node.is<IdentifierNode>()) {
					// Simple identifier like [&y = x] - look up x's type
					const auto& init_id = init_node.as<IdentifierNode>();
					std::optional<ASTNode> init_symbol = symbol_table.lookup(init_id.name());
					if (init_symbol.has_value()) {
						const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
						if (init_decl) {
							current_lambda_context_.capture_types[var_name] = init_decl->type_node().as<TypeSpecifierNode>();
						}
					}
				} else if (init_node.is<ExpressionNode>()) {
					const auto& expr_node = init_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(expr_node)) {
						const auto& init_id = std::get<IdentifierNode>(expr_node);
						std::optional<ASTNode> init_symbol = symbol_table.lookup(init_id.name());
						if (init_symbol.has_value()) {
							const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
							if (init_decl) {
								current_lambda_context_.capture_types[var_name] = init_decl->type_node().as<TypeSpecifierNode>();
							}
						}
					}
				}
				// If type still not set, try to get it from closure struct member
				if (current_lambda_context_.capture_types.find(var_name) == current_lambda_context_.capture_types.end()) {
					auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
					if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
						const StructTypeInfo* struct_info = type_it->second->getStructInfo();
						if (struct_info) {
							const StructMember* member = struct_info->findMember(std::string_view(StringTable::getStringView(var_name)));
							if (member) {
								// Create a TypeSpecifierNode from the member type
								TypeSpecifierNode member_type(member->type, TypeQualifier::None, static_cast<int>(member->size * 8));
								if (member->type == Type::Struct) {
									// Need to set type_index for struct types
									member_type = TypeSpecifierNode(member->type, member->type_index, static_cast<int>(member->size * 8), Token());
								}
								current_lambda_context_.capture_types[var_name] = member_type;
							}
						}
					}
				}
			} else {
				if (capture_index < lambda_info.captured_var_decls.size()) {
					const ASTNode& var_decl = lambda_info.captured_var_decls[capture_index];
					if (const DeclarationNode* decl = get_decl_from_symbol(var_decl)) {
						current_lambda_context_.capture_types[var_name] = decl->type_node().as<TypeSpecifierNode>();
					}
				}
				capture_index++;
			}
		}
		if (!current_lambda_context_.has_copy_this) {
			if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
				if (closure->findMember("__copy_this")) {
					current_lambda_context_.has_copy_this = true;
				}
			}
		}
	}

	void popLambdaContext() {
		if (lambda_context_stack_.empty()) {
			current_lambda_context_ = {};
			return;
		}
		current_lambda_context_ = lambda_context_stack_.back();
		lambda_context_stack_.pop_back();
	}

	// Emit IR to load __this pointer from current lambda closure into a TempVar.
	// Returns the TempVar holding the this pointer, or std::nullopt if not applicable.
	std::optional<TempVar> emitLoadThisPointer(const Token& token) {
		if (!isInThisPointerLambda()) {
			return std::nullopt;
		}

		int this_member_offset = getClosureMemberOffset("__this");

		TempVar this_ptr = var_counter.next();
		MemberLoadOp load_op;
		load_op.result.value = this_ptr;
		load_op.result.type = Type::Void;
		load_op.result.size_in_bits = 64;
		load_op.object = StringTable::getOrInternStringHandle("this");  // Lambda's this (the closure)
		load_op.member_name = StringTable::getOrInternStringHandle("__this");
		load_op.offset = this_member_offset;
		load_op.is_reference = false;
		load_op.is_rvalue_reference = false;
		load_op.struct_type_info = nullptr;
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));

		return this_ptr;
	}

	// ========== Auto Type Deduction Helpers ==========

	// Try to extract a LambdaExpressionNode from an initializer ASTNode.
	// Returns nullptr if the node is not a lambda expression.
	static const LambdaExpressionNode* extractLambdaFromInitializer(const ASTNode& init) {
		if (init.is<LambdaExpressionNode>()) {
			return &init.as<LambdaExpressionNode>();
		}
		if (init.is<ExpressionNode>()) {
			const ExpressionNode& expr = init.as<ExpressionNode>();
			if (std::holds_alternative<LambdaExpressionNode>(expr)) {
				return &std::get<LambdaExpressionNode>(expr);
			}
		}
		return nullptr;
	}

	// Deduce the actual closure type from an auto-typed lambda variable.
	// Given a symbol from the symbol table, if it's an auto-typed variable
	// initialized with a lambda, returns the TypeSpecifierNode for the closure struct.
	// Returns std::nullopt if type cannot be deduced.
	std::optional<TypeSpecifierNode> deduceLambdaClosureType(const ASTNode& symbol,
	                                                          const Token& fallback_token) const {
		if (!symbol.is<VariableDeclarationNode>()) {
			return std::nullopt;
		}
		const VariableDeclarationNode& var_decl = symbol.as<VariableDeclarationNode>();
		const std::optional<ASTNode>& init_opt = var_decl.initializer();
		if (!init_opt.has_value()) {
			return std::nullopt;
		}

		const LambdaExpressionNode* lambda_ptr = extractLambdaFromInitializer(*init_opt);
		if (!lambda_ptr) {
			return std::nullopt;
		}

		StringHandle closure_type_name = lambda_ptr->generate_lambda_name();
		auto type_it = gTypesByName.find(closure_type_name);
		if (type_it == gTypesByName.end()) {
			return std::nullopt;
		}

		const TypeInfo* closure_type = type_it->second;
		int closure_size = closure_type->getStructInfo()
			? closure_type->getStructInfo()->total_size * 8
			: 64;
		return TypeSpecifierNode(
			Type::Struct,
			closure_type->type_index_,
			closure_size,
			fallback_token
		);
	}


	// ── inline private helpers (CodeGen_Lambdas.cpp) ──
	/// Unified symbol lookup: searches local scope first, then falls back to global scope
	std::optional<ASTNode> lookupSymbol(StringHandle handle) const {
		auto symbol = symbol_table.lookup(handle);
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(handle);
		}
		return symbol;
	}

	std::optional<ASTNode> lookupSymbol(std::string_view name) const {
		auto symbol = symbol_table.lookup(name);
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(name);
		}
		return symbol;
	}

	/// Lookup + extract DeclarationNode in one step (returns nullptr if not found or not a declaration)
	const DeclarationNode* lookupDeclaration(StringHandle handle) const {
		auto symbol = lookupSymbol(handle);
		return symbol.has_value() ? get_decl_from_symbol(*symbol) : nullptr;
	}

	const DeclarationNode* lookupDeclaration(std::string_view name) const {
		auto symbol = lookupSymbol(name);
		return symbol.has_value() ? get_decl_from_symbol(*symbol) : nullptr;
	}

	/// Emit an AddressOf IR instruction and return the result TempVar holding the address.
	TempVar emitAddressOf(Type type, int size_in_bits, IrValue source, Token token = Token()) {
		TempVar addr_var = var_counter.next();
		AddressOfOp addr_op;
		addr_op.result = addr_var;
		addr_op.operand.type = type;
		addr_op.operand.size_in_bits = size_in_bits;
		addr_op.operand.pointer_depth = 0;
		addr_op.operand.value = source;
		ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), token));
		return addr_var;
	}

	/// Emit a Dereference IR instruction and return the result TempVar holding the loaded value.
	TempVar emitDereference(Type pointee_type, int pointer_size_bits, int pointer_depth, IrValue pointer_value, Token token = Token()) {
		TempVar result_var = var_counter.next();
		DereferenceOp deref_op;
		deref_op.result = result_var;
		deref_op.pointer.type = pointee_type;
		deref_op.pointer.size_in_bits = pointer_size_bits;
		deref_op.pointer.pointer_depth = pointer_depth;
		deref_op.pointer.value = pointer_value;
		ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), token));
		return result_var;
	}

	// ============================================================================
	// Return IR helper
	// ============================================================================
	void emitReturn(IrValue return_value, Type return_type, int return_size, const Token& token) {
		ReturnOp ret_op;
		ret_op.return_value = return_value;
		ret_op.return_type = return_type;
		ret_op.return_size = return_size;
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), token));
	}

	void emitVoidReturn(const Token& token) {
		ReturnOp ret_op;
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), token));
	}

	Ir ir_;
	TempVar var_counter{ 0 };
	SymbolTable symbol_table;
	SymbolTable* global_symbol_table_;  // Reference to the global symbol table for function overload lookup
	CompileContext* context_;  // Reference to compile context for flags
	Parser* parser_;  // Reference to parser for template instantiation

	// Current function name (for mangling static local variables)
	StringHandle current_function_name_;
	StringHandle current_struct_name_;  // For tracking which struct we're currently visiting member functions for
	Type current_function_return_type_;  // Current function's return type
	int current_function_return_size_;   // Current function's return size in bits
	TypeIndex current_function_return_type_index_ = 0;  // Type index for struct/class return types
	bool current_function_has_hidden_return_param_ = false;  // True if function uses hidden return parameter
	bool current_function_returns_reference_ = false;  // True if function returns a reference type (T& or T&&)
	bool in_return_statement_with_rvo_ = false;  // True when evaluating return expr that should use RVO
	
	// Current namespace path stack (for proper name mangling of namespace-scoped functions)
	std::vector<std::string> current_namespace_stack_;

	// Static local variable information
	struct StaticLocalInfo {
		StringHandle mangled_name;  // Phase 4: Using StringHandle
		Type type;
		int size_in_bits;
	};

	// Map from local static variable name to info
	// Key: local variable name, Value: static local info
	// Phase 4: Using StringHandle for keys
	std::unordered_map<StringHandle, StaticLocalInfo> static_local_names_;

	// Map from simple global variable name to mangled name
	// Key: simple identifier (e.g., "value"), Value: mangled name (e.g., "_ZN12_GLOBAL__N_15valueE")
	// This is needed because anonymous namespace variables need special mangling
	// Phase 4: Using StringHandle
	std::unordered_map<StringHandle, StringHandle> global_variable_names_;

	// Map from function name to deduced auto return type
	// Key: function name (mangled), Value: deduced TypeSpecifierNode
	std::unordered_map<std::string, TypeSpecifierNode> deduced_auto_return_types_;
	
	struct CachedParamInfo {
		bool is_reference = false;
		bool is_rvalue_reference = false;
		bool is_parameter_pack = false;
	};
	// Cache parameter reference info by mangled function name to aid call-site lowering
	std::unordered_map<StringHandle, std::vector<CachedParamInfo>> function_param_cache_;

	// Collected lambdas for deferred generation
	std::vector<LambdaInfo> collected_lambdas_;
	std::unordered_set<int> generated_lambda_ids_;  // Track which lambdas have been generated to prevent duplicates
	
	// Track generated functions to prevent duplicate codegen
	// Key: mangled function name - prevents generating the same function body multiple times
	// Phase 4: Using StringHandle
	std::unordered_set<StringHandle> generated_function_names_;
	
	// Generic lambda instantiation tracking
	// Key: lambda_id concatenated with deduced type signature (e.g., "0_int_double")
	// Value: The deduced parameter types for that instantiation
	struct GenericLambdaInstantiation {
		size_t lambda_id;
		std::vector<std::pair<size_t, TypeSpecifierNode>> deduced_types;  // param_index -> deduced type
		StringHandle instantiation_key;  // Unique key for this instantiation
	};
	std::vector<GenericLambdaInstantiation> pending_generic_lambda_instantiations_;
	std::unordered_set<std::string> generated_generic_lambda_instantiations_;  // Track already generated ones
	
	// Structure to hold info for local struct member functions
	struct LocalStructMemberInfo {
		StringHandle struct_name;
		StringHandle enclosing_function_name;
		ASTNode member_function_node;
	};
	
	// Collected local struct member functions for deferred generation
	std::vector<LocalStructMemberInfo> collected_local_struct_members_;
	
	// Deferred member functions discovered during function call resolution
	// When generateFunctionCallIr resolves a static member call via struct search,
	// the function body may not have been generated yet (e.g., for template specializations).
	// These are collected here and generated in a deferred pass.
	struct DeferredMemberFunctionInfo {
		StringHandle struct_name;
		ASTNode function_node;
		std::vector<std::string> namespace_stack;
	};
	std::vector<DeferredMemberFunctionInfo> deferred_member_functions_;
	
	// Structure to hold template instantiation info for deferred generation
	struct TemplateInstantiationInfo {
		StringHandle qualified_template_name;  // e.g., "Container::insert"
		StringHandle mangled_name;  // e.g., "insert_int"
		StringHandle struct_name;   // e.g., "Container"
		std::vector<Type> template_args;  // Concrete types
		SaveHandle body_position;  // Handle to saved position where the template body starts
		std::vector<std::string_view> template_param_names;  // e.g., ["U"]
		const TemplateFunctionDeclarationNode* template_node_ptr;  // Pointer to the template
	};
	
	// Collected template instantiations for deferred generation
	std::vector<TemplateInstantiationInfo> collected_template_instantiations_;

	// Track emitted static members to avoid duplicates
	std::unordered_set<StringHandle> emitted_static_members_;
	
	// Track processed TypeInfo pointers to avoid processing the same struct twice
	// (same struct can be registered under multiple keys in gTypesByName)
	std::unordered_set<const TypeInfo*> processed_type_infos_;

	// Current lambda context (for tracking captured variables)
	// When generating lambda body, this contains the closure type name and capture metadata
	struct LambdaContext {
		StringHandle closure_type;
		std::unordered_set<StringHandle> captures;
		std::unordered_map<StringHandle, LambdaCaptureNode::CaptureKind> capture_kinds;
		std::unordered_map<StringHandle, TypeSpecifierNode> capture_types;
		TypeIndex enclosing_struct_type_index = 0;  // For [this] capture type resolution
		bool has_copy_this = false;
		bool has_this_pointer = false;
		bool is_mutable = false;  // Whether the lambda is mutable (allows modifying captures)
		bool isActive() const { return closure_type.isValid(); }
	};
	LambdaContext current_lambda_context_;
	std::vector<LambdaContext> lambda_context_stack_;

	// SEH (Structured Exception Handling) context tracking
	// Tracks the current __try block context for __leave statement resolution
	struct SehContext {
		std::string_view try_end_label;      // Label at the end of the __try block (where __leave jumps to)
		std::string_view finally_label;      // Label for __finally handler (empty if no __finally)
		bool has_finally;                    // True if this __try has a __finally clause
	};
	std::vector<SehContext> seh_context_stack_;  // Stack of active SEH contexts

	// Loop-SEH depth tracking: records seh_context_stack_ size at each loop entry
	// Used by break/continue to know which __finally blocks need calling
	std::vector<size_t> loop_seh_depth_stack_;

	// SEH filter/except body context tracking for GetExceptionCode() disambiguation
	bool seh_in_filter_funclet_ = false;       // True while visiting the filter expression inside a filter funclet
	bool seh_has_saved_exception_code_ = false; // True when a saved exception code var is available
	TempVar seh_saved_exception_code_var_;     // Temp var holding exception code saved during filter, usable in except body

	// SEH context helper methods
	void pushSehContext(std::string_view end_label, std::string_view finally_label, bool has_finally) {
		SehContext ctx;
		ctx.try_end_label = end_label;
		ctx.finally_label = finally_label;
		ctx.has_finally = has_finally;
		seh_context_stack_.push_back(ctx);
	}

	void popSehContext() {
		if (!seh_context_stack_.empty()) {
			seh_context_stack_.pop_back();
		}
	}

	const SehContext* getCurrentSehContext() const {
		if (seh_context_stack_.empty()) {
			return nullptr;
		}
		return &seh_context_stack_.back();
	}

	// Emit SehFinallyCall for all enclosing __try/__finally blocks before a return statement.
	// Walks from innermost to outermost, calling each __finally funclet in order.
	// Returns true if any finally calls were emitted.
	bool emitSehFinallyCallsBeforeReturn(const Token& token) {
		bool emitted = false;
		// Walk from innermost (back) to outermost (front)
		for (int i = static_cast<int>(seh_context_stack_.size()) - 1; i >= 0; --i) {
			const SehContext& ctx = seh_context_stack_[i];
			if (ctx.has_finally) {
				// Generate a unique post-finally label for this return point
				static size_t seh_return_finally_counter = 0;
				size_t id = seh_return_finally_counter++;

				StringBuilder post_sb;
				post_sb.append("__seh_ret_finally_").append(id);
				std::string_view post_label = post_sb.commit();

				SehFinallyCallOp call_op;
				call_op.funclet_label = ctx.finally_label;
				call_op.end_label = post_label;
				ir_.addInstruction(IrInstruction(IrOpcode::SehFinallyCall, std::move(call_op), token));

				// Emit the post-finally label so execution continues here after the funclet returns
				ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(post_label)}, token));

				emitted = true;
			}
		}
		return emitted;
	}

	// Track SEH context depth when entering/leaving loops
	void pushLoopSehDepth() {
		loop_seh_depth_stack_.push_back(seh_context_stack_.size());
	}

	void popLoopSehDepth() {
		if (!loop_seh_depth_stack_.empty()) {
			loop_seh_depth_stack_.pop_back();
		}
	}

	// Emit SehFinallyCall for __try/__finally blocks between break/continue and the enclosing loop.
	// Only calls finally blocks that were pushed AFTER the loop began (i.e., inside the loop body).
	bool emitSehFinallyCallsBeforeBreakContinue(const Token& token) {
		if (loop_seh_depth_stack_.empty()) return false;

		size_t loop_seh_depth = loop_seh_depth_stack_.back();
		bool emitted = false;
		// Walk from innermost SEH context down to (but not including) the loop's entry depth
		for (int i = static_cast<int>(seh_context_stack_.size()) - 1; i >= static_cast<int>(loop_seh_depth); --i) {
			const SehContext& ctx = seh_context_stack_[i];
			if (ctx.has_finally) {
				static size_t seh_break_finally_counter = 0;
				size_t id = seh_break_finally_counter++;

				StringBuilder post_sb;
				post_sb.append("__seh_brk_finally_").append(id);
				std::string_view post_label = post_sb.commit();

				SehFinallyCallOp call_op;
				call_op.funclet_label = ctx.finally_label;
				call_op.end_label = post_label;
				ir_.addInstruction(IrInstruction(IrOpcode::SehFinallyCall, std::move(call_op), token));

				ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(post_label)}, token));

				emitted = true;
			}
		}
		return emitted;
	}

	// Generate just the function declaration for a template instantiation (without body)
	// This is called immediately when a template call is detected, so the IR converter
	// knows the full function signature before the call is converted to object code
	void generateTemplateFunctionDecl(const TemplateInstantiationInfo& inst_info) {
		const FunctionDeclarationNode& template_func_decl = inst_info.template_node_ptr->function_decl_node();
		const DeclarationNode& template_decl = template_func_decl.decl_node();

		// Create mangled name token
		Token mangled_token(
			Token::Type::Identifier,
			StringTable::getStringView(inst_info.mangled_name),
			template_decl.identifier_token().line(),
			template_decl.identifier_token().column(),
			template_decl.identifier_token().file_index()
		);

		StringHandle full_func_name = inst_info.mangled_name;
		StringHandle struct_name = inst_info.struct_name;

		// Generate function declaration IR using typed payload
		FunctionDeclOp func_decl_op;
		
		// Add return type
		const TypeSpecifierNode& return_type = template_decl.type_node().as<TypeSpecifierNode>();
		func_decl_op.return_type = return_type.type();
		func_decl_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
		func_decl_op.return_pointer_depth = static_cast<int>(return_type.pointer_depth());
		
		// Add function name and struct name
		func_decl_op.function_name = full_func_name;
		func_decl_op.struct_name = struct_name;
		
		// Add linkage (C++)
		func_decl_op.linkage = Linkage::None;

		// Add variadic flag (template functions are typically not variadic, but check anyway)
		func_decl_op.is_variadic = template_func_decl.is_variadic();

		// Mangled name is the full function name (already stored in StringBuilder's stable storage)
		func_decl_op.mangled_name = full_func_name;

		// Add function parameters with concrete types
		size_t template_unnamed_param_counter = 0;
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
				} else {
					// Use original parameter type
					const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
				}
				
				// Handle empty parameter names
				std::string_view param_name = param_decl.identifier_token().value();
				if (param_name.empty()) {
					func_param.name = StringTable::getOrInternStringHandle(
						StringBuilder().append("__param_").append(template_unnamed_param_counter++).commit());
				} else {
					func_param.name = StringTable::getOrInternStringHandle(param_name);
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
		auto saved_namespace_stack = current_namespace_stack_;
		auto parse_namespace_components = [](std::string_view qualified_prefix) {
			std::vector<std::string> components;
			size_t start = 0;
			while (start < qualified_prefix.size()) {
				size_t sep = qualified_prefix.find("::", start);
				if (sep == std::string_view::npos) {
					components.emplace_back(qualified_prefix.substr(start));
					break;
				}
				components.emplace_back(qualified_prefix.substr(start, sep - start));
				start = sep + 2;
			}
			return components;
		};
		auto extract_namespace_prefix = [](std::string_view qualified_name) -> std::string_view {
			size_t scope_pos = qualified_name.rfind("::");
			if (scope_pos == std::string_view::npos) {
				return {};
			}
			return qualified_name.substr(0, scope_pos);
		};

		std::string_view namespace_source;
		if (inst_info.struct_name.isValid()) {
			namespace_source = extract_namespace_prefix(StringTable::getStringView(inst_info.struct_name));
		} else {
			namespace_source = extract_namespace_prefix(StringTable::getStringView(inst_info.qualified_template_name));
		}
		if (!namespace_source.empty()) {
			current_namespace_stack_ = parse_namespace_components(namespace_source);
		} else {
			current_namespace_stack_.clear();
		}

		// First, generate the FunctionDecl IR for the template instantiation
		// This must be done at the top level, BEFORE any function bodies that might call it
		generateTemplateFunctionDecl(inst_info);
		
		// Get the template function declaration
		const FunctionDeclarationNode& template_func_decl = inst_info.template_node_ptr->function_decl_node();
		const DeclarationNode& template_decl = template_func_decl.decl_node();

		// Create mangled name token
		Token mangled_token(
			Token::Type::Identifier,
			StringTable::getStringView(inst_info.mangled_name),
			template_decl.identifier_token().line(),
			template_decl.identifier_token().column(),
			template_decl.identifier_token().file_index()
		);

		// Enter function scope
		symbol_table.enter_scope(ScopeType::Function);

		// Get struct type info for member functions
		const TypeInfo* struct_type_info = nullptr;
		if (inst_info.struct_name.isValid()) {
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
			Token this_token(Token::Type::Identifier, "this"sv, 
				template_decl.identifier_token().line(),
				template_decl.identifier_token().column(),
				template_decl.identifier_token().file_index());
			auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type_node, this_token);
			
			// Add 'this' to symbol table
			symbol_table.insert("this"sv, this_decl);
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
			inst_info.struct_name.isValid() ? inst_info.struct_name : StringHandle(),  // Pass struct name
			struct_type_info ? struct_type_info->type_index_ : 0  // Pass type index
		);

		if (body_node_opt.has_value()) {
			if (body_node_opt->is<BlockNode>()) {
				const BlockNode& block = body_node_opt->as<BlockNode>();
				const auto& stmts = block.get_statements();
				
				// Visit each statement in the block to generate IR
				for (size_t i = 0; i < stmts.size(); ++i) {
					visit(stmts[i]);
				}
			}
		} else {
			std::cerr << "Warning: Template body does NOT have value!\n";
		}

		// Add implicit return for void functions
		const TypeSpecifierNode& return_type = template_decl.type_node().as<TypeSpecifierNode>();
		if (return_type.type() == Type::Void) {
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), mangled_token));
		}

		// Exit function scope
		symbol_table.exit_scope();
		current_namespace_stack_ = saved_namespace_stack;
	}

	std::vector<IrOperand> generateTemplateParameterReferenceIr(const TemplateParameterReferenceNode& templateParamRefNode) {
		// This should not happen during normal code generation - template parameters should be substituted
		// during template instantiation. If we get here, it means template instantiation failed.
		std::string param_name = std::string(templateParamRefNode.param_name().view());
		std::cerr << "Error: Template parameter '" << param_name << "' was not substituted during template instantiation\n";
		std::cerr << "This indicates a bug in template instantiation - template parameters should be replaced with concrete types/values\n";
		assert(false && "Template parameter reference found during code generation - should have been substituted");
		return {};
	}

	// Generate IR for std::initializer_list construction
	// This is the "compiler magic" that creates a backing array on the stack
	// and constructs an initializer_list pointing to it
	std::vector<IrOperand> generateInitializerListConstructionIr(const InitializerListConstructionNode& init_list) {
		FLASH_LOG(Codegen, Debug, "Generating IR for InitializerListConstructionNode with ", 
		          init_list.size(), " elements");
		
		// Get the target initializer_list type
		const ASTNode& target_type_node = init_list.target_type();
		if (!target_type_node.is<TypeSpecifierNode>()) {
			FLASH_LOG(Codegen, Error, "InitializerListConstructionNode: target_type is not TypeSpecifierNode");
			return {};
		}
		const TypeSpecifierNode& target_type = target_type_node.as<TypeSpecifierNode>();
		
		// Get element type (default to int for now)
		int element_size_bits = 32;  // Default: int
		Type element_type = Type::Int;
		
		// Infer element type from first element if available
		std::vector<std::vector<IrOperand>> element_operands;
		for (size_t i = 0; i < init_list.elements().size(); ++i) {
			const ASTNode& elem = init_list.elements()[i];
			if (elem.is<ExpressionNode>()) {
				auto operands = visitExpressionNode(elem.as<ExpressionNode>());
				element_operands.push_back(operands);
				if (i == 0 && operands.size() >= 2) {
					if (std::holds_alternative<Type>(operands[0])) {
						element_type = std::get<Type>(operands[0]);
					}
					if (std::holds_alternative<int>(operands[1])) {
						element_size_bits = std::get<int>(operands[1]);
					}
				}
			}
		}
		
		// Step 1: Create a backing array on the stack using VariableDecl
		size_t array_size = init_list.size();
		size_t total_size_bits = array_size * element_size_bits;
		
		// Create a unique name for the backing array using the temp var number
		TempVar array_var = var_counter.next();
		StringBuilder array_name_builder;
		array_name_builder.append("__init_list_array_"sv).append(array_var.var_number);
		StringHandle array_name = StringTable::getOrInternStringHandle(array_name_builder.commit());
		
		VariableDeclOp array_decl;
		array_decl.var_name = array_name;
		array_decl.type = element_type;
		array_decl.size_in_bits = static_cast<int>(total_size_bits);
		array_decl.is_array = true;
		array_decl.array_element_type = element_type;
		array_decl.array_element_size = element_size_bits;
		array_decl.array_count = array_size;
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(array_decl), init_list.called_from()));
		
		// Step 2: Store each element into the backing array using ArrayStore
		for (size_t i = 0; i < element_operands.size(); ++i) {
			if (element_operands[i].size() < 3) continue;
			
			ArrayStoreOp store_op;
			store_op.element_type = element_type;
			store_op.element_size_in_bits = element_size_bits;
			store_op.array = array_name;
			store_op.index = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(i)};
			store_op.value = toTypedValue(element_operands[i]);
			store_op.member_offset = 0;  // Not a member array - direct local array
			store_op.is_pointer_to_array = false;  // This is an actual array, not a pointer
			ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(store_op), init_list.called_from()));
		}
		
		// Step 3: Create the initializer_list struct
		TypeIndex init_list_type_index = target_type.type_index();
		if (init_list_type_index >= gTypeInfo.size()) {
			FLASH_LOG(Codegen, Error, "InitializerListConstructionNode: invalid type index");
			return {};
		}
		
		const TypeInfo& init_list_type_info = gTypeInfo[init_list_type_index];
		const StructTypeInfo* init_list_struct_info = init_list_type_info.getStructInfo();
		if (!init_list_struct_info) {
			FLASH_LOG(Codegen, Error, "InitializerListConstructionNode: target type is not a struct");
			return {};
		}
		
		int init_list_size_bits = static_cast<int>(init_list_struct_info->total_size * 8);
		
		// Create a unique name for the initializer_list struct using the temp var number
		TempVar init_list_var = var_counter.next();
		StringBuilder init_list_name_builder;
		init_list_name_builder.append("__init_list_"sv).append(init_list_var.var_number);
		StringHandle init_list_name = StringTable::getOrInternStringHandle(init_list_name_builder.commit());
		
		VariableDeclOp init_list_decl;
		init_list_decl.var_name = init_list_name;
		init_list_decl.type = Type::Struct;
		init_list_decl.size_in_bits = init_list_size_bits;
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(init_list_decl), init_list.called_from()));
		
		// Store pointer to array (first member)
		if (init_list_struct_info->members.size() >= 1) {
			const auto& ptr_member = init_list_struct_info->members[0];
			MemberStoreOp store_ptr;
			store_ptr.object = init_list_name;  // Use StringHandle
			store_ptr.member_name = ptr_member.getName();
			store_ptr.offset = static_cast<int>(ptr_member.offset);
			// Create TypedValue for pointer to array - need to set pointer_depth explicitly
			TypedValue ptr_value;
			ptr_value.type = element_type;
			ptr_value.size_in_bits = 64;  // pointer size
			ptr_value.value = array_name;
			ptr_value.pointer_depth = 1;  // This is a pointer to the array
			store_ptr.value = ptr_value;
			store_ptr.struct_type_info = nullptr;
			store_ptr.is_reference = false;
			store_ptr.is_rvalue_reference = false;
			ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_ptr), init_list.called_from()));
		}
		
		// Store size (second member)
		if (init_list_struct_info->members.size() >= 2) {
			const auto& size_member = init_list_struct_info->members[1];
			MemberStoreOp store_size;
			store_size.object = init_list_name;  // Use StringHandle
			store_size.member_name = size_member.getName();
			store_size.offset = static_cast<int>(size_member.offset);
			store_size.value = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(array_size)};
			store_size.struct_type_info = nullptr;
			store_size.is_reference = false;
			store_size.is_rvalue_reference = false;
			ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_size), init_list.called_from()));
		}
		
		// Return operands for the constructed initializer_list
		// Return the StringHandle for the variable name so the caller can use it
		return { Type::Struct, init_list_size_bits, init_list_name, static_cast<unsigned long long>(init_list_type_index) };
	}

	std::vector<IrOperand> generateConstructorCallIr(const ConstructorCallNode& constructorCallNode) {
		// Get the type being constructed
		const ASTNode& type_node = constructorCallNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			assert(false && "Constructor call type node must be a TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();

		// For constructor calls, we need to generate a constructor call instruction
		// In C++, constructors are named after the class
		StringHandle constructor_name;
		if (is_struct_type(type_spec.type())) {
			// If type_index is set, use it
			if (type_spec.type_index() != 0) {
				constructor_name = gTypeInfo[type_spec.type_index()].name();
			} else {
				// Otherwise, use the token value (the identifier name)
				constructor_name = type_spec.token().handle();
			}
		} else {
			// For basic types, constructors might not exist, but we can handle them as value construction
			constructor_name = gTypeInfo[type_spec.type_index()].name();
		}

		// Create a temporary variable for the result (the constructed object)
		TempVar ret_var = var_counter.next();
		
		// Get the actual size of the struct from gTypeInfo
		int actual_size_bits = static_cast<int>(type_spec.size_in_bits());
		const StructTypeInfo* struct_info = nullptr;
		if (type_spec.type() == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
			if (type_info.struct_info_) {
				actual_size_bits = static_cast<int>(type_info.struct_info_->total_size * 8);
				struct_info = type_info.struct_info_.get();
			}
		} else {
			// Fallback: look up by name
			auto type_it = gTypesByName.find(constructor_name);
			if (type_it != gTypesByName.end() && type_it->second->struct_info_) {
				actual_size_bits = static_cast<int>(type_it->second->struct_info_->total_size * 8);
				struct_info = type_it->second->struct_info_.get();
			}
		}
		
		// Build ConstructorCallOp
		ConstructorCallOp ctor_op;
		ctor_op.struct_name = constructor_name;
		ctor_op.object = ret_var;  // The temporary variable that will hold the result

		// Find the matching constructor to get parameter types for reference handling
		const ConstructorDeclarationNode* matching_ctor = nullptr;
		size_t num_args = 0;
		constructorCallNode.arguments().visit([&](ASTNode) { num_args++; });
		
		if (struct_info) {
			for (const auto& func : struct_info->member_functions) {
				if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
					const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
					const auto& params = ctor_node.parameter_nodes();
					
					// Skip implicit copy/move constructors — they only apply when the
					// argument is actually the same struct type, not for aggregate-like
					// brace init with scalar values like my_type{0}
					if (ctor_node.is_implicit() && params.size() == 1 && params[0].is<DeclarationNode>()) {
						const auto& param_type = params[0].as<DeclarationNode>().type_node();
						if (param_type.is<TypeSpecifierNode>()) {
							const auto& pts = param_type.as<TypeSpecifierNode>();
							if ((pts.is_reference() || pts.is_rvalue_reference()) &&
							    is_struct_type(pts.type())) {
								continue;  // Skip implicit copy/move ctors
							}
						}
					}
					
					// Match constructor with same number of parameters or with default parameters
					if (params.size() == num_args) {
						matching_ctor = &ctor_node;
						break;
					} else if (params.size() > num_args) {
						// Check if remaining params have defaults
						bool all_have_defaults = true;
						for (size_t i = num_args; i < params.size(); ++i) {
							if (!params[i].is<DeclarationNode>() || 
							    !params[i].as<DeclarationNode>().has_default_value()) {
								all_have_defaults = false;
								break;
							}
						}
						if (all_have_defaults) {
							matching_ctor = &ctor_node;
							break;
						}
					}
				}
			}
		}
		
		// Get constructor parameter types for reference handling
		// But first check for aggregate initialization: if no matching constructor was found
		// (excluding implicit copy/move), and the struct has public members, generate direct
		// member stores instead of a constructor call. This handles: return my_type{0}
		if (!matching_ctor && struct_info && num_args > 0 && !struct_info->members.empty()) {
			bool is_aggregate = true;
			for (const auto& func : struct_info->member_functions) {
				if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
					if (!func.function_decl.as<ConstructorDeclarationNode>().is_implicit()) {
						is_aggregate = false;
						break;
					}
				}
			}
			
			if (is_aggregate && num_args <= struct_info->members.size()) {
				// Emit default constructor call first (zero-initializes the object)
				ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), constructorCallNode.called_from()));
				
				// Then emit member stores for each argument
				size_t member_idx = 0;
				constructorCallNode.arguments().visit([&](ASTNode argument) {
					if (member_idx >= struct_info->members.size()) {
						member_idx++;
						return;
					}
					const StructMember& member = struct_info->members[member_idx];
					auto arg_operands = visitExpressionNode(argument.as<ExpressionNode>());
					if (arg_operands.size() >= 3) {
						MemberStoreOp store_op;
						store_op.object = ret_var;
						store_op.member_name = member.getName();
						store_op.offset = static_cast<int>(member.offset);
						store_op.value = toTypedValue(arg_operands);
						store_op.struct_type_info = nullptr;
						store_op.is_reference = false;
						store_op.is_rvalue_reference = false;
						store_op.is_pointer_to_member = false;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_op), constructorCallNode.called_from()));
					}
					member_idx++;
				});
				
				setTempVarMetadata(ret_var, TempVarMetadata::makeRVOEligiblePRValue());
				
				TypeIndex result_type_index = type_spec.type_index();
				return { type_spec.type(), actual_size_bits, ret_var, static_cast<unsigned long long>(result_type_index) };
			}
		}
		
		const auto& ctor_params = matching_ctor ? matching_ctor->parameter_nodes() : std::vector<ASTNode>{};

		// Generate IR for constructor arguments and add them to ctor_op.arguments
		size_t arg_index = 0;
		constructorCallNode.arguments().visit([&](ASTNode argument) {
			// Get the parameter type for this argument (if it exists)
			const TypeSpecifierNode* param_type = nullptr;
			if (arg_index < ctor_params.size() && ctor_params[arg_index].is<DeclarationNode>()) {
				param_type = &ctor_params[arg_index].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			}
			
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			// argumentIrOperands = [type, size, value]
			if (argumentIrOperands.size() >= 3) {
				TypedValue tv;
				
				// Check if parameter expects a reference and argument is an identifier
				if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference()) &&
				    std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
					const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
					std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& arg_decl = symbol->as<DeclarationNode>();
						const auto& arg_type = arg_decl.type_node().as<TypeSpecifierNode>();
						
						if (arg_type.is_reference() || arg_type.is_rvalue_reference()) {
							// Argument is already a reference - just pass it through
							tv = toTypedValue(argumentIrOperands);
						} else {
							// Argument is a value - take its address
							TempVar addr_var = var_counter.next();
							AddressOfOp addr_op;
							addr_op.result = addr_var;
							addr_op.operand.type = arg_type.type();
							addr_op.operand.size_in_bits = static_cast<int>(arg_type.size_in_bits());
							addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
							addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
							ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), constructorCallNode.called_from()));
							
							// Create TypedValue with the address
							tv.type = arg_type.type();
							tv.size_in_bits = 64;  // Pointer size
							tv.value = addr_var;
							tv.ref_qualifier = ReferenceQualifier::LValueReference;  // Mark as reference parameter
							tv.cv_qualifier = param_type->cv_qualifier();  // Set CV qualifier from parameter
						}
					} else {
						// Not a simple identifier or not found - use as-is
						tv = toTypedValue(argumentIrOperands);
					}
				} else {
					// Not a reference parameter or not an identifier - use as-is
					tv = toTypedValue(argumentIrOperands);
				}
				
				// If we have parameter type information, use it to set pointer depth and CV qualifiers
				if (param_type) {
					tv.pointer_depth = static_cast<int>(param_type->pointer_depth());
					// For pointer types, also extract CV qualifiers from pointer levels
					if (param_type->is_pointer() && !param_type->pointer_levels().empty()) {
						// Use CV qualifier from the first pointer level (T* const -> const)
						// For now, we'll use the main CV qualifier
						if (!tv.is_reference()) {
							tv.cv_qualifier = param_type->cv_qualifier();
						}
					}
					// For reference types, use the CV qualifier
					if (param_type->is_reference() || param_type->is_rvalue_reference()) {
						tv.cv_qualifier = param_type->cv_qualifier();
					}
					// Also update type_index if it's a struct type
					if (param_type->type() == Type::Struct && param_type->type_index() != 0) {
						tv.type_index = param_type->type_index();
					}
				}
				
				ctor_op.arguments.push_back(std::move(tv));
			}
			arg_index++;
		});

		// Fill in default arguments for parameters that weren't explicitly provided
		// Find the matching constructor and add default values for missing parameters
		if (struct_info) {
			size_t num_explicit_args = ctor_op.arguments.size();
			
			// Find a constructor that has MORE parameters than explicit arguments
			// and has default values for those extra parameters
			for (const auto& func : struct_info->member_functions) {
				if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
					const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
					const auto& params = ctor_node.parameter_nodes();
					
					// Only consider constructors that have MORE parameters than explicit args
					// (constructors with exact match don't need default argument filling)
					if (params.size() > num_explicit_args) {
						// Check if the remaining parameters all have default values
						bool all_remaining_have_defaults = true;
						for (size_t i = num_explicit_args; i < params.size(); ++i) {
							if (params[i].is<DeclarationNode>()) {
								if (!params[i].as<DeclarationNode>().has_default_value()) {
									all_remaining_have_defaults = false;
									break;
								}
							} else {
								all_remaining_have_defaults = false;
								break;
							}
						}
						
						if (all_remaining_have_defaults) {
							// Generate IR for the default values of the remaining parameters
							for (size_t i = num_explicit_args; i < params.size(); ++i) {
								const auto& param_decl = params[i].as<DeclarationNode>();
								const ASTNode& default_node = param_decl.default_value();
								if (default_node.is<ExpressionNode>()) {
									auto default_operands = visitExpressionNode(default_node.as<ExpressionNode>());
									if (default_operands.size() >= 3) {
										TypedValue default_arg = toTypedValue(default_operands);
										ctor_op.arguments.push_back(std::move(default_arg));
									}
								}
							}
							break;  // Found a matching constructor
						}
					}
				}
			}
		}

		// Check if we should use RVO (Return Value Optimization)
		// If we're in a return statement and the function has a hidden return parameter,
		// construct directly into the return slot instead of into a temporary
		if (in_return_statement_with_rvo_) {
			ctor_op.use_return_slot = true;
			// The return slot offset will be set by IRConverter when it processes the return
			// For now, we just mark that RVO should be used
			FLASH_LOG(Codegen, Debug,
				"Constructor call will use RVO (construct directly in return slot)");
		}

		// Add the constructor call instruction (use ConstructorCall opcode)
		ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), constructorCallNode.called_from()));

		// Mark the result as a prvalue eligible for RVO (C++17 mandatory copy elision)
		// Constructor calls always produce prvalues, which are eligible for copy elision
		// when returned from a function
		setTempVarMetadata(ret_var, TempVarMetadata::makeRVOEligiblePRValue());
		
		FLASH_LOG_FORMAT(Codegen, Debug,
			"Marked constructor call result {} as RVO-eligible prvalue", ret_var.name());

		// Return the result variable with the constructed type, including type_index for struct types
		TypeIndex result_type_index = type_spec.type_index();
		return { type_spec.type(), actual_size_bits, ret_var, static_cast<unsigned long long>(result_type_index) };
	}

};
