#pragma once
#include "IrGenerator.h"
#include "InlineVector.h"

// Forward-declare to avoid pulling SemanticAnalysis.h into every TU that includes AstToIr.h
class SemanticAnalysis;

class AstToIr {
public:
	AstToIr() = delete;	// Require valid references
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

	// Wire in semantic-analysis results so that IR lowering can consume
	// pre-computed implicit-cast annotations instead of recomputing them.
	void setSemanticData(SemanticAnalysis* sema) { sema_ = sema; }

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
		std::variant<StringHandle, TempVar> base;			  // Base variable or temp
		std::vector<ComputeAddressOp::ArrayIndex> array_indices;	 // Array indices
		int total_member_offset = 0;						 // Accumulated member offsets
		TypeIndex final_type_index{};					  // Type identity of final result (TypeCategory embedded)
		SizeInBits final_size_bits;							// Size in bits
		PointerDepth pointer_depth;							// Pointer depth of final result
	};

	struct ScopeVariableInfo {
		std::string variable_name;
		std::string struct_name;
	};

	struct FullExpressionTempDestructorInfo {
		StringHandle struct_name;
		std::variant<StringHandle, TempVar> object;
		bool object_is_pointer = false;
	};

	struct GlobalStaticBindingInfo {
		bool is_global_or_static = false;
		StringHandle store_name;
		// TypeCategory is embedded in type_index; default = Void to preserve original sentinel.
		TypeIndex type_index = nativeTypeIndex(TypeCategory::Void);
		SizeInBits size_in_bits;

		// Returns the TypeCategory derived from the embedded TypeIndex.
		TypeCategory bindingType() const { return type_index.category(); }
	};

	// Generate aggregate initialization of a struct from an InitializerListNode as a default argument.
	// Emits ConstructorCallOp + MemberStoreOps for the struct, returns a TypedValue for the temporary.
	std::optional<TypedValue> generateDefaultStructArg(const InitializerListNode& init_list, const TypeSpecifierNode& param_type);

	// Fill in default arguments for parameters that weren't explicitly provided.
	// Iterates from arg_idx to the end of param_nodes, evaluating each parameter's
	// default value and appending it to call_op.args. Stops at a trailing function
	// parameter pack (which may be omitted). Throws InternalError if a non-pack
	// parameter without a default value is encountered (indicates an overload
	// resolution bug).
	void fillInDefaultArguments(CallOp& call_op, const std::vector<ASTNode>& param_nodes, size_t arg_idx);
	void populateReferenceReturnInfo(CallOp& call_op, const TypeSpecifierNode& return_type);
	void populateReferenceReturnInfo(VirtualCallOp& call_op, const TypeSpecifierNode& return_type);
	TypeIndex getFunctionSignatureReturnTypeIndex(const FunctionSignature& signature) const;
	int getFunctionSignatureReturnSizeBits(const FunctionSignature& signature) const;
	void populateIndirectCallReturnInfo(IndirectCallOp& call_op, const FunctionSignature& signature);
	ExprResult buildIndirectCallReturnResult(const FunctionSignature& signature, TempVar ret_var) const;

	// Shared helper: build the ExprResult for a function call given its return type
	// and the TempVar holding the raw call result.  Handles:
	//   1. Reference-returning functions: sets lvalue/xvalue metadata on ret_var,
	//      auto-dereferences when context != LValueAddress, and returns the
	//      appropriate ContainsAddress / ContainsData result.
	//   2. Non-reference returns: computes result_size, type_index, ValueStorage
	//      and returns a ContainsData / ContainsAddress result.
	// Used by both generateFunctionCallIr and generateMemberFunctionCallIr to
	// avoid duplicating the ~40-line return-result epilogue.
	ExprResult buildCallReturnResult(
		const TypeSpecifierNode& return_type,
		TempVar ret_var,
		ExpressionContext context,
		const Token& source_token);
	void fillInDefaultConstructorArguments(ConstructorCallOp& ctor_op, const StructTypeInfo& struct_info);
	// Fill trailing default arguments for a constructor overload that has already
	// been selected, starting after the explicitly provided arguments.
	void fillInConstructorDefaultArguments(
		ConstructorCallOp& ctor_op,
		const ConstructorDeclarationNode& ctor_node,
		size_t explicit_arg_count);
	TypedValue materializeDefaultArgument(
		const ASTNode& default_expr,
		const TypeSpecifierNode& param_type_spec,
		std::string_view error_context);

	std::vector<std::vector<ScopeVariableInfo>> scope_stack_;

	void enterScope() {
		scope_stack_.push_back({});
	}

	void exitScope();
	void emitActiveCatchScopeDestructors();

	// Captures function-body-scope vars (for cleanup LP), then calls exitScope().
	// Stores captured vars in pending_function_cleanup_vars_ for later LP emission.
	void exitFunctionScope();

	// Emits FunctionCleanupLP if pending_function_cleanup_vars_ is non-empty.
	// Must be called AFTER the function's return instruction.
	void emitPendingFunctionCleanupLP(const Token& token);

	void registerVariableWithDestructor(const std::string& var_name, const std::string& struct_name);
	void registerFullExpressionTempDestructor(StringHandle struct_name,
											  std::variant<StringHandle, TempVar> object,
											  bool object_is_pointer = false);
	void emitAndClearFullExpressionTempDestructors();

	struct CatchScopeContext {
		size_t base_depth;
		size_t try_depth;
	};

	// Phase 1 capture state: vars declared in the innermost try block scope
	bool capture_try_cleanup_ = false;
	size_t capture_try_cleanup_depth_ = 0;
	std::vector<ScopeVariableInfo> captured_try_cleanup_vars_;

	// Phase 2 capture state: vars captured by exitFunctionScope() awaiting LP emission
	std::vector<std::pair<StringHandle, StringHandle>> pending_function_cleanup_vars_;
	std::vector<FullExpressionTempDestructorInfo> pending_full_expression_temp_dtors_;
	InlineVector<CatchScopeContext, 4> catch_scope_stack_;
	size_t active_try_statement_depth_ = 0;
	// Set by visitTryStatementNode() when any typed (non-catch-all) handlers are present.
	// Used by emitPendingFunctionCleanupLP() to ensure FunctionCleanupLP is always emitted
	// on ELF when ElfCatchNoMatch references need resolving.
	bool function_has_typed_catch_ = false;

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
	void visitRangedForBeginEnd(const RangedForStatementNode& node, ASTNode range_object_expr,
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
	ExprResult visitExpressionNode(const ExpressionNode& exprNode,
								   ExpressionContext context = ExpressionContext::Load);
	ExprResult generateNoexceptExprIr(const NoexceptExprNode& noexcept_node);
	ExprResult generatePseudoDestructorCallIr(const PseudoDestructorCallNode& dtor);
	ExprResult generatePointerToMemberAccessIr(const PointerToMemberAccessNode& ptmNode);
	ExprResult generateCallExprIr(const CallExprNode& callExprNode, ExpressionContext context);
	int calculateIdentifierSizeBits(const TypeSpecifierNode& type_node, bool is_array, std::string_view identifier_name);
	ExprResult generateIdentifierIr(const IdentifierNode& identifierNode,
									ExpressionContext context = ExpressionContext::Load);
	std::optional<ExprResult> decayLambdaStructToFunctionPointer(const StructTypeInfo& struct_info, const Token& source_token);
	ExprResult generateQualifiedIdentifierIr(const QualifiedIdentifierNode& qualifiedIdNode);
	ExprResult generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode);
	ExprResult generateTypeConversion(const ExprResult& operands, TypeCategory fromType, TypeCategory toType, const Token& source_token);
	// Apply sema-annotated contextual bool conversion to a condition expression.
	// If the sema pass annotated the condition with BooleanConversion (e.g. float→bool),
	// emit the proper type conversion.  Falls back to a local conversion for
	// floating-point types when no annotation is present.
	ExprResult applyConditionBoolConversion(ExprResult condition, const ASTNode& cond_node, const Token& source_token);
	// Apply sema-annotated or standard implicit conversion for a constructor/function
	// argument. Checks the sema slot for the argument expression first; falls back to
	// can_convert_type for plain primitive scalars. Returns the (possibly converted)
	// ExprResult. param_type must not be null.
	ExprResult applyConstructorArgConversion(ExprResult arg_result, const ASTNode& arg_expr,
											 const TypeSpecifierNode& param_type, const Token& source_token);
	std::optional<ExprResult> materializeSelectedConvertingConstructor(
		ExprResult source_result,
		const ASTNode& source_expr,
		const TypeSpecifierNode& target_type,
		const ConstructorDeclarationNode& selected_ctor,
		const Token& source_token,
		bool use_return_slot = false);
	std::optional<ExprResult> tryMaterializeSemaSelectedConvertingConstructor(
		ExprResult source_result,
		const ASTNode& source_expr,
		const TypeSpecifierNode& target_type,
		const Token& source_token,
		bool use_return_slot = false);
	/// Convert a scalar source value to the referred-to type of a const/rvalue-reference
	/// parameter and materialize it into a stack temporary, returning the address as a TypedValue.
	/// Precondition: ref_param_type.is_reference() || ref_param_type.is_rvalue_reference()
	TypedValue materializeConvertedReferenceArgument(
		ExprResult source_result,
		const TypeSpecifierNode& ref_param_type,
		const Token& source_token);
	// Read the sema-annotated conversion target type for an expression node.
	// Returns TypeCategory::Invalid if no annotation exists or if either endpoint is a struct.
	TypeCategory getSemaAnnotatedTargetType(const ASTNode& node) const;
	ExprResult generateStringLiteralIr(const StringLiteralNode& stringLiteralNode);
	GlobalStaticBindingInfo resolveGlobalOrStaticBinding(const IdentifierNode& identifier);
	std::optional<AddressComponents> analyzeAddressExpression(
		const ExpressionNode& expr,
		int accumulated_offset = 0);
	ExprResult generateUnaryOperatorIr(const UnaryOperatorNode& unaryOperatorNode,
									   ExpressionContext context = ExpressionContext::Load);
	ExprResult generateTernaryOperatorIr(const TernaryOperatorNode& ternaryNode);
	ExprResult generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode);
	std::string_view generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& param_types, bool is_variadic, std::string_view struct_name, const std::vector<std::string>& namespace_path, bool is_const_method);
	std::string_view generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<ASTNode>& param_nodes, bool is_variadic, std::string_view struct_name, const std::vector<std::string>& namespace_path, bool is_const_method);
	std::string_view generateMangledNameForCall(const FunctionDeclarationNode& func_node, std::string_view struct_name_override, const std::vector<std::string>& namespace_path);
	std::optional<ExprResult> tryGenerateIntrinsicIr(std::string_view func_name, const CallExprNode& callExprNode);
	ExprResult generateBuiltinAbsIntIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateBuiltinAbsFloatIntrinsic(const CallExprNode& callExprNode, std::string_view func_name);
	bool isVaListPointerType(const ASTNode& arg, const ExprResult& ir_result) const;
	ExprResult generateVaArgIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateVaStartIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateBuiltinUnreachableIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateBuiltinAssumeIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateBuiltinExpectIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateBuiltinLaunderIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateGetExceptionCodeIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateAbnormalTerminationIntrinsic(const CallExprNode& callExprNode);
	ExprResult generateGetExceptionInformationIntrinsic(const CallExprNode& callExprNode);

	ExprResult generateFunctionCallIr(const CallExprNode& callExprNode, ExpressionContext context);

	ExprResult generateMemberFunctionCallIr(const CallExprNode& callExprNode, ExpressionContext context);
	ExprResult generateFunctionCallIr(const CallExprNode& callExprNode, ExpressionContext context, const void* sema_call_key);
	ExprResult generateMemberFunctionCallIr(const CallExprNode& callExprNode, ExpressionContext context, const void* sema_call_key);
	MultiDimMemberArrayAccess collectMultiDimMemberArrayIndices(const ArraySubscriptNode& subscript);
	MultiDimArrayAccess collectMultiDimArrayIndices(const ArraySubscriptNode& subscript);
	ExprResult generateArraySubscriptIr(const ArraySubscriptNode& arraySubscriptNode,
										ExpressionContext context = ExpressionContext::Load);
	bool validateAndSetupIdentifierMemberAccess(
		std::string_view object_name,
		std::variant<StringHandle, TempVar>& base_object,
		TypeIndex& base_type_index,
		bool& is_pointer_dereference);
	bool extractBaseFromOperands(
		const ExprResult& operands,
		std::variant<StringHandle, TempVar>& base_object,
		TypeIndex& base_type_index,
		std::string_view error_context);
	static ExprResult makeMemberResult(SizeInBits size_bits, TempVar result_var, TypeIndex type_index, PointerDepth pointer_depth, ValueStorage storage);
	bool setupBaseFromIdentifier(
		const IdentifierNode& identifier,
		const Token& member_token,
		std::variant<StringHandle, TempVar>& base_object,
		TypeIndex& base_type_index,
		bool& is_pointer_dereference);
	ExprResult generateMemberAccessIr(const MemberAccessNode& memberAccessNode,
									  ExpressionContext context = ExpressionContext::Load);
	std::optional<size_t> calculateArraySize(const DeclarationNode& decl);
	ExprResult generateSizeofIr(const SizeofExprNode& sizeofNode);
	ExprResult generateAlignofIr(const AlignofExprNode& alignofNode);
	ExprResult generateOffsetofIr(const OffsetofExprNode& offsetofNode);
	bool isScalarType(TypeCategory type, bool is_reference, size_t pointer_depth) const;
	bool isArithmeticType(TypeCategory type) const;
	bool isFundamentalType(TypeCategory type) const;
	ExprResult generateTypeTraitIr(const TypeTraitExprNode& traitNode);
	ExprResult generateNewExpressionIr(const NewExpressionNode& newExpr);
	ExprResult generateDeleteExpressionIr(const DeleteExpressionNode& deleteExpr);
	std::variant<StringHandle, TempVar> extractBaseOperand(
		const ExprResult& expr_operands,
		TempVar fallback_var,
		const char* cast_name = "cast");
	void markReferenceMetadata(
		const ExprResult& expr_operands,
		TempVar result_var,
		TypeCategory target_type,
		int target_size,
		bool is_rvalue_ref,
		const char* cast_name = "cast");
	void generateAddressOfForReference(
		const std::variant<StringHandle, TempVar>& base,
		TempVar result_var,
		TypeCategory target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast");
	ExprResult handleRValueReferenceCast(
		const ExprResult& expr_operands,
		int target_size,
		TypeIndex target_type_index,
		const Token& token,
		const char* cast_name);
	ExprResult handleLValueReferenceCast(
		const ExprResult& expr_operands,
		int target_size,
		TypeIndex target_type_index,
		const Token& token,
		const char* cast_name);
	ExprResult generateStaticCastIr(const StaticCastNode& staticCastNode);
	ExprResult generateTypeidIr(const TypeidNode& typeidNode);
	ExprResult generateDynamicCastIr(const DynamicCastNode& dynamicCastNode);
	ExprResult generateConstCastIr(const ConstCastNode& constCastNode);
	ExprResult generateReinterpretCastIr(const ReinterpretCastNode& reinterpretCastNode);
	LambdaInfo collectLambdaForDeferredGeneration(const LambdaExpressionNode& lambda);
	ExprResult generateLambdaExpressionIr(const LambdaExpressionNode& lambda, std::string_view target_var_name = "");
	void generateLambdaFunctions(LambdaInfo& lambda_info);
	void generateLambdaOperatorCallFunction(LambdaInfo& lambda_info);
	void generateLambdaInvokeFunction(LambdaInfo& lambda_info);
	void addCapturedVariablesToSymbolTable(const std::vector<LambdaCaptureNode>& captures,
										   const std::vector<ASTNode>& captured_var_decls);

	// ── inline private helpers (IrGenerator_Visitors_TypeInit.cpp) ──
	// Helper: resolve self-referential struct types in template instantiations.
	// When a template member function references its own class (e.g., const W& in W<T>::operator+=),
	// the type_index may point to the unfinalized template base. This resolves it to the
	// enclosing instantiated struct's type_index by mutating `type` in-place.
	// Important: only resolves when the unfinalized type's name matches the base name of the
	// enclosing struct — avoids incorrectly resolving outer class references in nested classes.
	static void resolveSelfReferentialType(TypeSpecifierNode& type, TypeIndex enclosing_type_index);

	// Helper: generate a member function call for user-defined operator++/-- overloads on structs.
	// Returns the IR operands {result_type, SizeInBits{result_size}, ret_var, result_type_index} on success,
	// or std::nullopt if no overload was found.
	std::optional<ExprResult> generateUnaryIncDecOverloadCall(
		OverloadableOperator op_kind,  // Increment or Decrement
		TypeCategory operandType,
		const ExprResult& operandIrResult,
		bool is_prefix);

	// Helper: generate built-in pointer or integer increment/decrement IR.
	// Handles pointer arithmetic (add/subtract element_size) and integer pre/post inc/dec.
	// is_increment: true for ++, false for --
	ExprResult generateBuiltinIncDec(
		bool is_increment,
		bool is_prefix,
		bool operandHandledAsIdentifier,
		const UnaryOperatorNode& unaryOperatorNode,
		const ExprResult& operandIrResult,
		TypeCategory operandType,
		TempVar result_var);

	// Helper function to resolve template parameter size from struct name
	// This is used by both ConstExpr evaluator and IR generation for sizeof(T)
	// where T is a template parameter in a template class member function
	static size_t resolveTemplateSizeFromStructName(std::string_view struct_name);

	// Helper function to try evaluating sizeof/alignof using ConstExprEvaluator
	// Returns the evaluated operands if successful, empty vector otherwise
	template <typename NodeType>
	ExprResult tryEvaluateAsConstExpr(const NodeType& node) {
		// Try to evaluate as a constant expression first
		ConstExpr::EvaluationContext ctx(symbol_table);

		// Pass global symbol table for resolving global variables in sizeof etc.
		if (global_symbol_table_) {
			ctx.global_symbols = global_symbol_table_;
		}

		// If we're in a member function, set the struct_info in the context
		// This allows sizeof(T) to resolve template parameters from the struct
		if (current_struct_name_.isValid()) {
			auto struct_type_it = getTypesByNameMap().find(current_struct_name_);
			if (struct_type_it != getTypesByNameMap().end()) {
				const TypeInfo* struct_type_info = struct_type_it->second;
				ctx.struct_info = struct_type_info->getStructInfo();
			}
		}

		auto expr_node = ASTNode::emplace_node<ExpressionNode>(node);
		auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);

		if (eval_result.success()) {
			// Return the constant value
			unsigned long long value = 0;
			if (const auto* ll_val = std::get_if<long long>(&eval_result.value)) {
				value = static_cast<unsigned long long>(*ll_val);
			} else if (const auto* ull_val = std::get_if<unsigned long long>(&eval_result.value)) {
				value = *ull_val;
			}
			return makeExprResult(nativeTypeIndex(TypeCategory::UnsignedLongLong), SizeInBits{64}, IrOperand{value}, PointerDepth{}, ValueStorage::ContainsData);
		}

		// Return default ExprResult if evaluation failed
		return ExprResult{};
	}

	// Helper function to evaluate whether an expression is noexcept
	// Returns true if the expression is guaranteed not to throw, false otherwise
	bool isExpressionNoexcept(const ExpressionNode& expr) const;

	// Pack a struct aggregate initializer into raw bytes for static/global storage.
	// Callers provide the scalar evaluator so the same member walk can be reused for
	// globals, inline static members, and template-instantiated static members.
	template <typename EvalFn>
	void fillAggregateInitData(
		std::vector<char>& init_data,
		const StructTypeInfo& struct_info,
		const InitializerListNode& init_list,
		EvalFn& eval_to_value,
		size_t base_offset = 0,
		size_t depth = 0) {
		constexpr size_t kFillStructMaxDepth = 64;
		if (depth >= kFillStructMaxDepth) {
			FLASH_LOG(Codegen, Warning, "fillAggregateInitData: maximum nesting depth (", kFillStructMaxDepth, ") exceeded, skipping remaining members");
			return;
		}

		size_t positional_index = 0;
		for (size_t i = 0; i < init_list.size(); ++i) {
			StringHandle member_name;
			if (init_list.is_designated(i)) {
				member_name = init_list.member_name(i);
			} else if (positional_index < struct_info.members.size()) {
				member_name = struct_info.members[positional_index].getName();
				++positional_index;
			} else {
				break;
			}

			for (const auto& member : struct_info.members) {
				if (member.getName() != member_name) {
					continue;
				}

				size_t abs_offset = base_offset + member.offset;
				const ASTNode& member_init = init_list.initializers()[i];
				if (member_init.is<InitializerListNode>() &&
					isIrStructType(toIrType(member.memberType()))) {
					if (const TypeInfo* nested_type_info = tryGetTypeInfo(member.type_index)) {
						const StructTypeInfo* nested_struct = nested_type_info->getStructInfo();
						if (nested_struct) {
							fillAggregateInitData(init_data, *nested_struct, member_init.as<InitializerListNode>(), eval_to_value, abs_offset, depth + 1);
						}
						break;
					}
				}

				unsigned long long value = eval_to_value(member_init, member.memberType());
				if (member.bitfield_width.has_value()) {
					size_t width = *member.bitfield_width;
					size_t bit_offset = member.bitfield_bit_offset;
					unsigned long long mask = (width < 64) ? ((1ULL << width) - 1) : ~0ULL;
					value &= mask;
					unsigned long long existing = 0;
					for (size_t byte_index = 0; byte_index < member.size && (abs_offset + byte_index) < init_data.size(); ++byte_index) {
						existing |= (static_cast<unsigned long long>(static_cast<unsigned char>(init_data[abs_offset + byte_index])) << (byte_index * 8));
					}
					existing |= (value << bit_offset);
					for (size_t byte_index = 0; byte_index < member.size && (abs_offset + byte_index) < init_data.size(); ++byte_index) {
						init_data[abs_offset + byte_index] = static_cast<char>((existing >> (byte_index * 8)) & 0xFF);
					}
				} else {
					for (size_t byte_index = 0; byte_index < member.size && (abs_offset + byte_index) < init_data.size(); ++byte_index) {
						init_data[abs_offset + byte_index] = static_cast<char>((value >> (byte_index * 8)) & 0xFF);
					}
				}
				break;
			}
		}
	}

	// Recursively zero-initialize all scalar leaf members of a struct.
	// For sub-members that are themselves structs (> 64 bits), recurse instead of
	// emitting a single MemberStore with 0ULL (which would only zero the first 8 bytes).
	void emitRecursiveZeroFill(
		const StructTypeInfo& struct_info,
		StringHandle base_object,
		int base_offset,
		const Token& token);

	// Implementation of recursive nested member store generation
	bool tryEmitArrayMemberStores(
		const StructMember& member,
		const InitializerListNode& init_list,
		StringHandle base_object,
		int base_offset,
		const Token& token);

	void generateNestedMemberStores(
		const StructTypeInfo& struct_info,
		const InitializerListNode& init_list,
		StringHandle base_object,
		int base_offset,
		const Token& token);

	// Helper function to route a misparsed member-call syntax through ordinary free-call lowering.
	// When available, the original unified CallExprNode is preferred so the fallback preserves
	// shared call metadata without re-materializing an ad-hoc legacy-style call node.
	ExprResult convertMemberCallToFunctionCall(const CallExprNode& callExprNode,
											   ExpressionContext context,
											   const void* sema_call_key);

	// Resolve a TypeIndex to the concrete StructTypeInfo*, chasing aliases.
	// Returns nullptr if the type is not a struct.
	const TypeInfo* resolveToConcreteStructTypeInfo(TypeIndex type_idx) const;

	// Convert a scalar ConstExpr::EvalResult value to a raw unsigned long long.
	static unsigned long long evalResultScalarToRaw(const ConstExpr::EvalResult& r);

	// Materialise a successful consteval aggregate/struct EvalResult into IR:
	// emits VariableDecl + MemberStore instructions and returns the ExprResult
	// pointing at the fresh temp variable.  Returns an empty ExprResult if the
	// type is not a recognised struct (caller should fall through to the scalar path).
	ExprResult materializeConstevalAggregateResult(
		const ConstExpr::EvalResult& eval_result,
		const TypeSpecifierNode& ret_spec,
		TypeCategory ret_type,
		SizeInBits ret_size,
		const Token& call_token);

	// Helper function to check if access to a member is allowed
	// Returns true if access is allowed, false otherwise
	bool checkMemberAccess(const StructMember* member,
						   const StructTypeInfo* member_owner_struct,
						   const StructTypeInfo* accessing_struct,
						   [[maybe_unused]] const BaseClassSpecifier* inheritance_path = nullptr,
						   const std::string_view& accessing_function = "") const;

	// Helper: check if accessing_struct is a declared friend class of member_owner_struct.
	//
	// Friend declarations are stored both under the source-level name (typically
	// unqualified, e.g. "__use_cache") AND the namespace-qualified form (e.g.
	// "std::__use_cache") — the parser registers both at addFriendClass time.
	//
	// At codegen time the accessing struct carries its full internal name, which
	// may be:
	//   • namespace-qualified  – "std::__use_cache"
	//   • a $hash instantiation – "std::__use_cache$00a6ac8c5dbe3409"
	//   • a $pattern struct    – "std::__use_cache$pattern_P"
	//
	// The helper therefore tries, in order:
	//   1. Exact match on the full accessing name.
	//   2. The registered base-template name from TypeInfo (strips $hash).
	//   3. A manual $-strip (fallback for instantiations not yet in TypeInfo).
	//   4. For partial-specialisation pattern structs (identified via the registry):
	//      strip the "$pattern" separator to recover the base template name,
	//      preserving the namespace prefix for correct matching.
	bool checkFriendClassAccess(const StructTypeInfo* member_owner_struct,
								const StructTypeInfo* accessing_struct) const;

	// Helper: check if two structs are the same class, including template instantiations.
	// Template instantiations use a '$hash' suffix (e.g., basic_string_view$291eceb35e7234a9)
	// that must be stripped for comparison with the base template.
	bool isSameClassOrInstantiation(const StructTypeInfo* a, const StructTypeInfo* b) const;

	// Helper to check if accessing_struct is nested within member_owner_struct
	bool isNestedWithin(const StructTypeInfo* accessing_struct,
						const StructTypeInfo* member_owner_struct) const;

	// Helper to check if derived_struct can access protected members of base_struct
	bool isAccessibleThroughInheritance(const StructTypeInfo* derived_struct,
										const StructTypeInfo* base_struct) const;

	// Get the current struct context (which class we're currently in)
	const StructTypeInfo* getCurrentStructContext() const;

	// Get the current function name
	std::string_view getCurrentFunctionName() const {
		return current_function_name_.isValid() ? StringTable::getStringView(current_function_name_) : std::string_view();
	}

	// Helper function to check if access to a member function is allowed
	bool checkMemberFunctionAccess(const StructMemberFunction* member_func,
								   const StructTypeInfo* member_owner_struct,
								   const StructTypeInfo* accessing_struct,
								   std::string_view accessing_function = "") const;

	// Helper function to check if a variable is a reference by looking it up in the symbol table
	// Returns true if the variable is declared as a reference (&  or &&)
	bool isVariableReference(std::string_view var_name) const;

	// Helper function to resolve the struct type and member info for a member access chain
	// Handles nested member access like o.inner.callback by recursively resolving types
	// Returns true if successfully resolved, with the struct_info and member populated
	bool resolveMemberAccessType(const MemberAccessNode& member_access,
								 const StructTypeInfo*& out_struct_info,
								 const StructMember*& out_member) const;
	std::optional<TypeSpecifierNode> buildCodegenOverloadResolutionArgType(const ASTNode& arg) const;
	std::optional<bool> getSameTypeConstructorPreference(const ASTNode& init_node, const TypeSpecifierNode& target_type) const;
	bool isSameTypeXValueSource(const ASTNode& init_node, const ExprResult& init_operands, const TypeSpecifierNode& target_type) const;

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
	bool handleLValueAssignment(const ExprResult& lhs_operands,
								const ExprResult& rhs_operands,
								const Token& token);

	// Handle compound assignment to lvalues (e.g., v.x += 5, arr[i] += 5)
	// Supports Member kind (struct member access), Indirect kind (dereferenced pointers - already supported), and ArrayElement kind (array subscripts - added in this function)
	// This is similar to handleLValueAssignment but also performs the arithmetic operation
	bool handleLValueCompoundAssignment(const ExprResult& lhs_operands,
										const ExprResult& rhs_operands,
										const Token& token,
										std::string_view op);

	// Helper functions to emit store instructions
	// These can be used by both the unified handler and special-case code

	// Emit ArrayStore instruction
	void emitArrayStore(TypeCategory element_type, int element_size_bits,
						std::variant<StringHandle, TempVar> array,
						const TypedValue& index, const TypedValue& value,
						int64_t member_offset, bool is_pointer_to_array,
						const Token& token);

	// Emit MemberStore instruction
	void emitMemberStore(const TypedValue& value,
						 std::variant<StringHandle, TempVar> object,
						 StringHandle member_name, int offset,
						 CVReferenceQualifier ref_qualifier = CVReferenceQualifier::None,
						 bool is_pointer_to_member = false,
						 const Token& token = Token(),
						 std::optional<size_t> bitfield_width = std::nullopt,
						 size_t bitfield_bit_offset = 0);

	// Emit DereferenceStore instruction
	void emitDereferenceStore(const TypedValue& value, TypeCategory pointee_type, [[maybe_unused]] int pointee_size_bits,
							  std::variant<StringHandle, TempVar> pointer,
							  const Token& token);

	const DeclarationNode& requireDeclarationNode(const ASTNode& node, std::string_view context) const;

	// Helper to generate FunctionAddress IR for a lambda's __invoke function
	// Returns the TempVar holding the function pointer address
	TempVar generateLambdaInvokeFunctionAddress(const LambdaExpressionNode& lambda);

	// Helper to find a conversion operator in a struct that converts to the target type
	// Returns nullptr if no suitable conversion operator is found
	// Searches the struct and its base classes for "operator target_type()"
	// source_is_const: true when the source object is const-qualified; prefers const overloads and
	//                  refuses to call non-const ones when set.
	const StructMemberFunction* findConversionOperator(
		const StructTypeInfo* struct_info,
		TypeIndex target_type_index,
		bool source_is_const) const;

	// Determine whether an initializer expression node yields a const-qualified object.
	// Returns true for:
	//  - a ConstCastNode whose target type has the 'const' CV-qualifier
	//  - an IdentifierNode that resolves (in symbol_table) to a const-typed VariableDeclarationNode
	// Used to pick the correct const/non-const conversion-operator overload.
	bool isExprConstQualified(const ASTNode& expr_node) const;

	// Emit a call to a user-defined conversion operator and return the converted ExprResult.
	// Returns nullopt if conv_op has no valid FunctionDeclarationNode (should not happen in practice).
	// source           - ExprResult for the source struct object
	// source_type_info - TypeInfo of the source struct (for struct name / mangling)
	// conv_op          - conversion function found via findConversionOperator (must not be nullptr)
	// target_type_index - TypeIndex of target (carrying the TypeCategory; use nativeTypeIndex(cat) for primitives)
	// target_size_bits - size in bits of the target type
	// token            - source token for IR instruction location
	std::optional<ExprResult> emitConversionOperatorCall(
		const ExprResult& source,
		const TypeInfo& source_type_info,
		const StructMemberFunction& conv_op,
		TypeIndex target_type_index,
		int target_size_bits,
		const Token& token);

	// Helper to get the size of a type in bytes
	// Reuses the same logic as sizeof() operator
	// Used for pointer arithmetic (++/-- operators need sizeof(pointee_type))
	size_t getSizeInBytes(TypeIndex type_index, int size_in_bits) const;
	int getPointerElementSize(TypeIndex type_index, int pointer_depth) const;
	TypeCategory getRuntimeValueType(TypeIndex semantic_type_index, PointerDepth pointer_depth) const;
	int getRuntimeValueSizeBits(TypeIndex type_index, int semantic_size_bits, PointerDepth pointer_depth) const;
	std::optional<ExprResult> tryMakeEnumeratorConstantExpr(const TypeSpecifierNode& type_node, StringHandle identifier_handle) const;
	std::optional<ExprResult> tryMakeEnumeratorConstantExpr(const EnumTypeInfo& enum_info, StringHandle identifier_handle) const;
	std::optional<ExprResult> tryApplySemaCallArgReferenceBinding(ExprResult arg_result,
																  const ASTNode& arg_expr,
																  const TypeSpecifierNode& param_type,
																  const CallArgReferenceBindingInfo* binding_info,
																  const Token& source_token);

	// ========== Lambda Capture Helper Functions ==========

	// Get the current lambda's closure StructTypeInfo, or nullptr if not in a lambda
	const StructTypeInfo* getCurrentClosureStruct() const;

	// Check if we're in a lambda with [*this] capture
	bool isInCopyThisLambda() const;

	// Check if we're in a lambda with [this] pointer capture
	bool isInThisPointerLambda() const {
		return current_lambda_context_.isActive() && current_lambda_context_.has_this_pointer;
	}

	// Get the offset of a member in the current lambda closure struct
	// Returns 0 if not found or not in a lambda context
	int getClosureMemberOffset(std::string_view member_name) const;

	// Emit IR to load __copy_this from current lambda closure into a TempVar.
	// Returns the TempVar holding the copied object, or std::nullopt if not applicable.
	// The Token parameter is used for source location in the IR instruction.
	std::optional<TempVar> emitLoadCopyThis(const Token& token);

	// Manage lambda context push/pop for nested lambdas
	void pushLambdaContext(const LambdaInfo& lambda_info);

	void popLambdaContext();

	// Emit IR to load __this pointer from current lambda closure into a TempVar.
	// Returns the TempVar holding the this pointer, or std::nullopt if not applicable.
	std::optional<TempVar> emitLoadThisPointer(const Token& token);

	// ========== Auto Type Deduction Helpers ==========

	// Try to extract a LambdaExpressionNode from an initializer ASTNode.
	// Returns nullptr if the node is not a lambda expression.
	static const LambdaExpressionNode* extractLambdaFromInitializer(const ASTNode& init);

	// Deduce the actual closure type from an auto-typed lambda variable.
	// Given a symbol from the symbol table, if it's an auto-typed variable
	// initialized with a lambda, returns the TypeSpecifierNode for the closure struct.
	// Returns std::nullopt if type cannot be deduced.
	std::optional<TypeSpecifierNode> deduceLambdaClosureType(const ASTNode& symbol,
															 const Token& fallback_token) const;

	// ── inline private helpers (IrGenerator_Lambdas.cpp) ──
	/// Unified symbol lookup: searches local scope first, then falls back to global scope
	std::optional<ASTNode> lookupSymbol(StringHandle handle) const;

	std::optional<ASTNode> lookupSymbol(std::string_view name) const;

	/// Lookup + extract DeclarationNode in one step (returns nullptr if not found or not a declaration)
	const DeclarationNode* lookupDeclaration(StringHandle handle) const {
		auto symbol = lookupSymbol(handle);
		return symbol.has_value() ? get_decl_from_symbol(*symbol) : nullptr;
	}

	const DeclarationNode* lookupDeclaration(std::string_view name) const {
		auto symbol = lookupSymbol(name);
		return symbol.has_value() ? get_decl_from_symbol(*symbol) : nullptr;
	}

#ifndef NDEBUG
	static bool hasInstantiationBindings(const TypeInfo* type_info) {
		if (!type_info) {
			return false;
		}

		for (const auto* inst_ctx = type_info->instantiationContext(); inst_ctx; inst_ctx = inst_ctx->parent) {
			if (!inst_ctx->param_names.empty() || !inst_ctx->param_args.empty()) {
				return true;
			}
		}

		return false;
	}
#endif

	/// Emit an AddressOf IR instruction and return the result TempVar holding the address.
	TempVar emitAddressOf(TypeCategory type, int size_in_bits, IrValue source, Token token = Token());

	/// Emit a Dereference IR instruction and return the result TempVar holding the loaded value.
	TempVar emitDereference(TypeCategory pointee_type, int pointer_size_bits, int pointer_depth, IrValue pointer_value, Token token = Token());

	// ============================================================================
	// Return IR helper
	// ============================================================================
	void emitReturn(IrValue return_value, TypeCategory return_type, int return_size, const Token& token);

	void emitVoidReturn(const Token& token) {
		ReturnOp ret_op;
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), token));
	}

	Ir ir_;
	TempVar var_counter{0};
	uint32_t string_literal_counter_ = 0;  // Counter for unique .str.N global names
	SymbolTable symbol_table;
	SymbolTable* global_symbol_table_;  // Reference to the global symbol table for function overload lookup
	CompileContext* context_;  // Reference to compile context for flags
	Parser* parser_;	 // Reference to parser for template instantiation
	SemanticAnalysis* sema_ = nullptr;  // Optional semantic-analysis results (Phase 2+)
	bool sema_normalized_current_function_ = false;	// Phase 15: true when sema visited the current function body

	// Current function name (plain, used for friend access checks and diagnostics)
	StringHandle current_function_name_;
	// Current function mangled name (used for static local variable namespacing)
	StringHandle current_function_mangled_name_;
	StringHandle current_struct_name_;  // For tracking which struct we're currently visiting member functions for
	int current_function_return_size_;   // Current function's return size in bits
	TypeIndex current_function_return_type_index_{0, TypeCategory::Void};  // Type index for struct/class return types (TypeCategory embedded)
	bool current_function_returns_function_pointer_ = false;

	TypeCategory currentFunctionReturnType() const {
		return current_function_returns_function_pointer_ ? TypeCategory::FunctionPointer : current_function_return_type_index_.category();
	}
	TypeIndex currentFunctionReturnTypeIndex() const {
		return current_function_return_type_index_;
	}
	bool current_function_has_hidden_return_param_ = false;	// True if function uses hidden return parameter
	bool current_function_returns_reference_ = false;  // True if function returns a reference type (T& or T&&)
	bool in_return_statement_with_rvo_ = false;	// True when evaluating return expr that should use RVO

	// Current namespace path stack (for proper name mangling of namespace-scoped functions)
	std::vector<std::string> current_namespace_stack_;

	// Static local variable information
	struct StaticLocalInfo {
		StringHandle mangled_name;  // Phase 4: Using StringHandle
		SizeInBits size_in_bits;
		TypeIndex type_index{};	// TypeCategory embedded; replaces Type type
		TypeCategory type() const { return type_index.category(); }
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

	// Map from global function pointer variable name to the mangled function name it was
	// initialized with.  This enables copy-initialization of one global function pointer
	// from another: int (*fp2)(int,int) = fp1;
	// Key: variable name StringHandle,  Value: reloc target (mangled function name)
	std::unordered_map<StringHandle, StringHandle> global_func_ptr_reloc_map_;

	struct CachedParamInfo {
		StringHandle name{};
		CVReferenceQualifier ref_qualifier = CVReferenceQualifier::None;
		bool is_parameter_pack = false;
		bool has_default_value = false;
		ASTNode default_value;
		ASTNode type_node;

		bool is_reference() const { return ref_qualifier != CVReferenceQualifier::None; }
		bool is_rvalue_reference() const { return ref_qualifier == CVReferenceQualifier::RValueReference; }
		bool is_lvalue_reference() const { return ref_qualifier == CVReferenceQualifier::LValueReference; }
	};
	// Cache parameter reference info by mangled function name to aid call-site lowering
	std::unordered_map<StringHandle, std::vector<CachedParamInfo>> function_param_cache_;
	void applyTypeNodeMetadata(TypedValue& value, const TypeSpecifierNode& type_node);
	TypedValue buildConstructorArgumentValue(const ExprResult& argument_result,
											 const ASTNode& argument,
											 const TypeSpecifierNode* param_type,
											 const Token& token);
	void fillInCachedDefaultArguments(CallOp& call_op, const std::vector<CachedParamInfo>& cached_params, size_t arg_idx);

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
		std::vector<std::pair<size_t, TypeSpecifierNode>> deduced_types;	 // param_index -> deduced type
		StringHandle instantiation_key;	// Unique key for this instantiation
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
		StringHandle struct_name;	  // e.g., "Container"
		std::vector<TypeCategory> template_args;	 // Concrete types
		SaveHandle body_position;  // Handle to saved position where the template body starts
		std::vector<std::string_view> template_param_names;	// e.g., ["U"]
		const TemplateFunctionDeclarationNode* template_node_ptr;  // Pointer to the template
	};

	// Collected template instantiations for deferred generation
	std::vector<TemplateInstantiationInfo> collected_template_instantiations_;

	// Track emitted static members to avoid duplicates
	std::unordered_set<StringHandle> emitted_static_members_;

	// Track processed TypeInfo pointers to avoid processing the same struct twice
	// (same struct can be registered under multiple keys in getTypesByNameMap())
	std::unordered_set<const TypeInfo*> processed_type_infos_;

	// Current lambda context (for tracking captured variables)
	// When generating lambda body, this contains the closure type name and capture metadata
	struct LambdaContext {
		StringHandle closure_type;
		std::unordered_set<StringHandle> captures;
		std::unordered_map<StringHandle, LambdaCaptureNode::CaptureKind> capture_kinds;
		std::unordered_map<StringHandle, TypeSpecifierNode> capture_types;
		TypeIndex enclosing_struct_type_index{};	 // For [this] capture type resolution
		bool has_copy_this = false;
		bool has_this_pointer = false;
		bool is_mutable = false;	 // Whether the lambda is mutable (allows modifying captures)
		bool isActive() const { return closure_type.isValid(); }
	};
	LambdaContext current_lambda_context_;
	std::vector<LambdaContext> lambda_context_stack_;

	// SEH (Structured Exception Handling) context tracking
	// Tracks the current __try block context for __leave statement resolution
	struct SehContext {
		std::string_view try_end_label;		// Label at the end of the __try block (where __leave jumps to)
		std::string_view finally_label;		// Label for __finally handler (empty if no __finally)
		bool has_finally;					  // True if this __try has a __finally clause
	};
	std::vector<SehContext> seh_context_stack_;	// Stack of active SEH contexts

	// Paired loop-entry snapshot: SEH and scope depths are always pushed/popped
	// together, so they are kept in one vector of structs to enforce the invariant.
	struct LoopDepthEntry {
		size_t seh_depth;	  // seh_context_stack_.size() at loop entry
		size_t scope_depth;	// scope_stack_.size() at loop entry
	};
	std::vector<LoopDepthEntry> loop_depth_stack_;

	// SEH filter/except body context tracking for GetExceptionCode() disambiguation
	bool seh_in_filter_funclet_ = false;		 // True while visiting the filter expression inside a filter funclet
	bool seh_has_saved_exception_code_ = false; // True when a saved exception code var is available
	TempVar seh_saved_exception_code_var_;	   // Temp var holding exception code saved during filter, usable in except body

	// SEH context helper methods
	void pushSehContext(std::string_view end_label, std::string_view finally_label, bool has_finally);

	void popSehContext();

	const SehContext* getCurrentSehContext() const;

	// Emit SehFinallyCall for all enclosing __try/__finally blocks before a return statement.
	// Walks from innermost to outermost, calling each __finally funclet in order.
	// Returns true if any finally calls were emitted.
	bool emitSehFinallyCallsBeforeReturn(const Token& token);

	// Track SEH context depth when entering/leaving loops
	void pushLoopSehDepth() {
		loop_depth_stack_.push_back({seh_context_stack_.size(), scope_stack_.size()});
	}

	void popLoopSehDepth();

	// Emit SehFinallyCall for __try/__finally blocks between break/continue and the enclosing loop.
	// Only calls finally blocks that were pushed AFTER the loop began (i.e., inside the loop body).
	bool emitSehFinallyCallsBeforeBreakContinue(const Token& token);

	// Emit destructor calls for all local variables in scopes from the current innermost scope
	// down to (but not including) target_depth, WITHOUT modifying scope_stack_.
	// Used by break, continue, return, and goto to ensure destructors run before non-local jumps.
	void emitDestructorsForNonLocalExit(size_t target_depth);

	// Per-function map from label name → scope_stack_.size() when the label is defined.
	// Populated by prescanLabels() before visiting a function body; cleared at each new function.
	// Allows visitGotoStatementNode to know how deep the target label is so it can emit
	// the correct scope-exit destructors before the unconditional branch.
	std::unordered_map<StringHandle, size_t> label_scope_depth_map_;

	// Walk a statement subtree and record every LabelStatementNode in label_scope_depth_map_
	// using the scope depth that the label would have during the main visitor pass.
	// Called once per function body before the main visit loop.
	void prescanLabels(const ASTNode& node, size_t depth);

	// Generate just the function declaration for a template instantiation (without body)
	// This is called immediately when a template call is detected, so the IR converter
	// knows the full function signature before the call is converted to object code
	void generateTemplateFunctionDecl(const TemplateInstantiationInfo& inst_info);

	// Generate an instantiated member function template
	void generateTemplateInstantiation(const TemplateInstantiationInfo& inst_info);

	ExprResult generateTemplateParameterReferenceIr(const TemplateParameterReferenceNode& templateParamRefNode);

	// Generate IR for std::initializer_list construction
	// This is the "compiler magic" that creates a backing array on the stack
	// and constructs an initializer_list pointing to it
	ExprResult generateInitializerListConstructionIr(const InitializerListConstructionNode& init_list);

	ExprResult generateConstructorCallIr(const ConstructorCallNode& constructorCallNode);
};
