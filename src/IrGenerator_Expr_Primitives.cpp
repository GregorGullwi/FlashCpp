#include "Parser.h"
#include "IrGenerator.h"

	ExprResult AstToIr::visitExpressionNode(const ExpressionNode& exprNode,
	ExpressionContext context) {
		return std::visit([this, context](const auto& expr) -> ExprResult {
			using T = std::decay_t<decltype(expr)>;
			if constexpr (std::is_same_v<T, IdentifierNode>) {
				return generateIdentifierIr(expr, context);
			} else if constexpr (std::is_same_v<T, QualifiedIdentifierNode>) {
				return generateQualifiedIdentifierIr(expr);
			} else if constexpr (std::is_same_v<T, BoolLiteralNode>) {
				return makeExprResult(Type::Bool, SizeInBits{8}, IrOperand{expr.value() ? 1ULL : 0ULL}, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
			} else if constexpr (std::is_same_v<T, NumericLiteralNode>) {
				return generateNumericLiteralIr(expr);
			} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
				return generateStringLiteralIr(expr);
			} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				return generateBinaryOperatorIr(expr);
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				return generateUnaryOperatorIr(expr, context);
			} else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
				return generateTernaryOperatorIr(expr);
			} else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				return generateFunctionCallIr(expr);
			} else if constexpr (std::is_same_v<T, MemberFunctionCallNode>) {
				return generateMemberFunctionCallIr(expr);
			} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
				return generateArraySubscriptIr(expr, context);
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				return generateMemberAccessIr(expr, context);
			} else if constexpr (std::is_same_v<T, SizeofExprNode>) {
				auto const_result = tryEvaluateAsConstExpr(expr);
				return const_result.effectiveIrType() == IrType::Void ? generateSizeofIr(expr) : const_result;
			} else if constexpr (std::is_same_v<T, SizeofPackNode>) {
				FLASH_LOG(Codegen, Error, "sizeof... operator found during code generation - should have been substituted during template instantiation");
				return ExprResult{};
			} else if constexpr (std::is_same_v<T, AlignofExprNode>) {
				auto const_result = tryEvaluateAsConstExpr(expr);
				return const_result.effectiveIrType() == IrType::Void ? generateAlignofIr(expr) : const_result;
			} else if constexpr (std::is_same_v<T, NoexceptExprNode>) {
				return generateNoexceptExprIr(expr);
			} else if constexpr (std::is_same_v<T, OffsetofExprNode>) {
				return generateOffsetofIr(expr);
			} else if constexpr (std::is_same_v<T, TypeTraitExprNode>) {
				return generateTypeTraitIr(expr);
			} else if constexpr (std::is_same_v<T, NewExpressionNode>) {
				return generateNewExpressionIr(expr);
			} else if constexpr (std::is_same_v<T, DeleteExpressionNode>) {
				return generateDeleteExpressionIr(expr);
			} else if constexpr (std::is_same_v<T, StaticCastNode>) {
				return generateStaticCastIr(expr);
			} else if constexpr (std::is_same_v<T, DynamicCastNode>) {
				return generateDynamicCastIr(expr);
			} else if constexpr (std::is_same_v<T, ConstCastNode>) {
				return generateConstCastIr(expr);
			} else if constexpr (std::is_same_v<T, ReinterpretCastNode>) {
				return generateReinterpretCastIr(expr);
			} else if constexpr (std::is_same_v<T, TypeidNode>) {
				return generateTypeidIr(expr);
			} else if constexpr (std::is_same_v<T, LambdaExpressionNode>) {
				return generateLambdaExpressionIr(expr);
			} else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				return generateConstructorCallIr(expr);
			} else if constexpr (std::is_same_v<T, TemplateParameterReferenceNode>) {
				return generateTemplateParameterReferenceIr(expr);
			} else if constexpr (std::is_same_v<T, FoldExpressionNode>) {
				// Phase 4: unreachable after pre-sema boundary enforcement.
				throw InternalError("FoldExpressionNode survived into codegen after pre-sema boundary enforcement");
			} else if constexpr (std::is_same_v<T, PseudoDestructorCallNode>) {
				return generatePseudoDestructorCallIr(expr);
			} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
				return generatePointerToMemberAccessIr(expr);
			} else if constexpr (std::is_same_v<T, PackExpansionExprNode>) {
				// Phase 4: unreachable after pre-sema boundary enforcement.
				throw InternalError("PackExpansionExprNode survived into codegen after pre-sema boundary enforcement");
			} else if constexpr (std::is_same_v<T, InitializerListConstructionNode>) {
				return generateInitializerListConstructionIr(expr);
			} else if constexpr (std::is_same_v<T, ThrowExpressionNode>) {
				FLASH_LOG(Codegen, Debug, "ThrowExpressionNode encountered in expression context - skipping codegen");
				return ExprResult{};
			} else {
				static_assert(!std::is_same_v<T, T>, "Unhandled ExpressionNode variant");
			}
		}, exprNode);
	}

	ExprResult AstToIr::generateNoexceptExprIr(const NoexceptExprNode& noexcept_node) {
		bool is_noexcept = true;
		if (noexcept_node.expr().is<ExpressionNode>()) {
			is_noexcept = isExpressionNoexcept(noexcept_node.expr().as<ExpressionNode>());
		}
		return makeExprResult(Type::Bool, SizeInBits{8}, IrOperand{is_noexcept ? 1ULL : 0ULL}, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
	}

	ExprResult AstToIr::generatePseudoDestructorCallIr(const PseudoDestructorCallNode& dtor) {
		std::string_view type_name = dtor.has_qualified_name()
			? dtor.qualified_type_name().view()
			: dtor.type_name();
		FLASH_LOG(Codegen, Debug, "Generating explicit destructor call for type: ", type_name);

		ASTNode object_node = dtor.object();
		std::string_view object_name;
		const DeclarationNode* object_decl = nullptr;
		TypeSpecifierNode object_type(Type::Void, TypeQualifier::None, 0);

		if (object_node.is<ExpressionNode>()) {
			const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(object_expr)) {
				const IdentifierNode& object_ident = std::get<IdentifierNode>(object_expr);
				object_name = object_ident.name();
				const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
				if (symbol.has_value()) {
					object_decl = get_decl_from_symbol(*symbol);
					if (object_decl) {
						object_type = object_decl->type_node().as<TypeSpecifierNode>();
						if (dtor.is_arrow_access() && object_type.pointer_levels().size() > 0) {
							object_type.remove_pointer_level();
						}
					}
				}
			}
		}

		if (is_struct_type(object_type.category())) {
			size_t struct_type_index = object_type.type_index().index();
			if (struct_type_index > 0 && struct_type_index < getTypeInfoCount()) {
				const TypeInfo& type_info = getTypeInfo(TypeIndex{struct_type_index});
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info && struct_info->hasDestructor()) {
					FLASH_LOG(Codegen, Debug, "Generating IR for destructor call on struct: ",
					StringTable::getStringView(struct_info->getName()));
					DestructorCallOp dtor_op;
					dtor_op.struct_name = struct_info->getName();
					dtor_op.object = StringTable::getOrInternStringHandle(object_name);
					ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), dtor.type_name_token()));
				} else {
					FLASH_LOG(Codegen, Debug, "Struct ", type_name, " has no destructor, skipping call");
				}
			}
		} else {
			FLASH_LOG(Codegen, Debug, "Non-class type ", type_name, " - destructor call is no-op");
		}
		return ExprResult{};
	}

	ExprResult AstToIr::generatePointerToMemberAccessIr(const PointerToMemberAccessNode& ptmNode) {
		ExprResult object_result = visitExpressionNode(ptmNode.object().as<ExpressionNode>(), ExpressionContext::LValueAddress);
		if (object_result.effectiveIrType() == IrType::Void && !object_result.size_in_bits.is_set()) {
			FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: object expression returned empty operands");
			return ExprResult{};
		}

		ExprResult ptr_result = visitExpressionNode(ptmNode.member_pointer().as<ExpressionNode>());
		if (ptr_result.effectiveIrType() == IrType::Void && !ptr_result.size_in_bits.is_set()) {
			FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: member pointer expression returned empty operands");
			return ExprResult{};
		}

		TempVar object_addr = var_counter.next();
		if (ptmNode.is_arrow()) {
			if (std::holds_alternative<StringHandle>(object_result.value)) {
				StringHandle obj_ptr_name = std::get<StringHandle>(object_result.value);
				AssignmentOp assign_op;
				assign_op.result = object_addr;
				assign_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, object_addr);
				assign_op.rhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, obj_ptr_name);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
			} else if (const auto* temp_var = std::get_if<TempVar>(&object_result.value)) {
				object_addr = *temp_var;
			} else {
				FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: unexpected object operand type for ->*");
				return ExprResult{};
			}
		} else {
			if (std::holds_alternative<StringHandle>(object_result.value)) {
				StringHandle obj_name = std::get<StringHandle>(object_result.value);
				AddressOfOp addr_op;
				addr_op.result = object_addr;
				addr_op.operand = TypedValue{
					.type = object_result.typeEnum(),
					.size_in_bits = object_result.size_in_bits,
					.value = obj_name,
					.pointer_depth = PointerDepth{},
					.ir_type = toIrType(object_result.typeEnum())
				};
				ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
			} else if (const auto* temp_var = std::get_if<TempVar>(&object_result.value)) {
				object_addr = *temp_var;
			} else {
				FLASH_LOG(Codegen, Error, "PointerToMemberAccessNode: unexpected object operand type for .*");
				return ExprResult{};
			}
		}

		TempVar member_addr = var_counter.next();
		BinaryOp add_op;
		add_op.lhs = makeTypedValue(TypeCategory::UnsignedLongLong, SizeInBits{64}, object_addr);
		add_op.rhs = toTypedValue(ptr_result);
		add_op.result = member_addr;
		ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), ptmNode.operator_token()));

		Type member_type = ptr_result.typeEnum();
		int member_size = ptr_result.size_in_bits.value;
		TypeIndex member_type_index = ptr_result.type_index;

		TempVar result_var = emitDereference(typeToCategory(member_type), member_size, 1, member_addr, ptmNode.operator_token());
		return makeExprResult(
			member_type,
			SizeInBits{static_cast<int>(member_size)},
			IrOperand{result_var},
			member_type_index
		, PointerDepth{}, ValueStorage::ContainsData);
	}

	static void requireResolvedCodegenType(Type type, std::string_view context) {
		if (isPlaceholderAutoType(type)) {
			throw InternalError(std::string(StringBuilder()
				.append("Unresolved placeholder type reached codegen in ")
				.append(context)
				.append(" (type=")
				.append(static_cast<int64_t>(type))
				.append(")")
				.commit()));
		}
	}

	static int resolveCodegenSizeBits(const TypeSpecifierNode& type_node, std::string_view context) {
		requireResolvedCodegenType(type_node.type(), context);

		const int resolved_size = getTypeSpecSizeBits(type_node);
		if (resolved_size != 0) {
			return resolved_size;
		}

		throw InternalError(std::string(StringBuilder()
			.append("Type with no runtime size reached codegen in ")
			.append(context)
			.append(" (type=")
			.append(static_cast<int64_t>(type_node.type()))
			.append(", pointer_depth=")
			.append(static_cast<int64_t>(type_node.pointer_depth()))
			.append(")")
			.commit()));
	}

	int AstToIr::calculateIdentifierSizeBits(const TypeSpecifierNode& type_node, bool is_array, std::string_view identifier_name) {
		bool is_array_type = is_array || type_node.is_array();
		int size_bits;

		if (is_array_type || type_node.pointer_depth() > 0) {
			// For arrays and pointers, the identifier itself is a pointer (64 bits on x64)
			// The element/pointee size is stored separately and used for pointer arithmetic
			size_bits = 64;  // Pointer size on x64 architecture
		} else {
			// For regular variables, return the variable size
			size_bits = static_cast<int>(type_node.size_in_bits());
			// Fallback: if size_bits is 0, calculate from type. Placeholder types must
			// be resolved before codegen reaches identifier lowering.
			if (size_bits == 0) {
				requireResolvedCodegenType(type_node.type(), "identifier size calculation");
				const int fallback_size = get_type_size_bits(type_node.category());
				FLASH_LOG(Codegen, Warning, "Parser returned size_bits=0 for identifier '", identifier_name,
					"' (type=", static_cast<int>(type_node.type()), ") - using fallback calculation (fallback_size=",
					fallback_size, ")");
				size_bits = fallback_size;
			}
		}

		return size_bits;
	}

	ExprResult AstToIr::generateIdentifierIr(const IdentifierNode& identifierNode,
	ExpressionContext context) {
		auto makeIdentifierResult = [](
			Type type,
			int size_bits,
			IrOperand value,
			TypeIndex type_index = TypeIndex{},
			PointerDepth pointer_depth = PointerDepth{}
		) -> ExprResult {
			return makeExprResult(type, SizeInBits{static_cast<int>(size_bits)}, std::move(value), type_index, pointer_depth, ValueStorage::ContainsData);
		};
		auto makeIdentifierResultFromTypeNode = [&](const TypeSpecifierNode& type_node, int size_bits, IrOperand value,
			bool preserve_pointer_depth = false) -> ExprResult {
			// Preserve the original direct-identifier encoding rule from these sites:
			// semantic identity types carry type_index in ExprResult, while legacy slot-4 metadata
			// keeps enum values identifiable after integral lowering and keeps enum pointers usable
			// for still-unmigrated pointer-depth consumers.
			Type result_type = type_node.type();
			const bool is_enum_pointer = type_node.category() == TypeCategory::Enum && type_node.pointer_depth() > 0;
			if (!is_enum_pointer && type_node.category() == TypeCategory::Enum && type_node.type_index().index() < getTypeInfoCount()) {
				if (const EnumTypeInfo* enum_info = getTypeInfo(type_node.type_index()).getEnumInfo()) {
					result_type = enum_info->underlying_type;
				}
			}
			Type semantic_type = resolve_type_alias(type_node.type(), type_node.type_index());
			if (type_node.type_index().is_valid() && type_node.type_index().index() < getTypeInfoCount()) {
				semantic_type = resolve_type_alias(getTypeInfo(type_node.type_index()).type_, type_node.type_index());
			}
			const bool carries_type_index = carriesSemanticTypeIndex(semantic_type);
			const PointerDepth pointer_depth{preserve_pointer_depth ? static_cast<int>(type_node.pointer_depth()) : 0};
			return makeIdentifierResult(
				result_type,
				size_bits,
				std::move(value),
				carries_type_index ? type_node.type_index() : TypeIndex{},
				pointer_depth
			);
		};
		// Check if this is a captured variable in a lambda.
		// Explicit captures ([x], [&x]) have binding set at parse time.
		// Capture-all ([=], [&]) variables are expanded at parse time into current_lambda_context_
		// but keep Local binding, so we fall back to the runtime captures map for those.
		StringHandle var_name_str = StringTable::getOrInternStringHandle(identifierNode.name());
		auto preserveSemanticTypeIndex = [](Type type, TypeIndex type_index) {
			return carriesSemanticTypeIndex(type) ? type_index : TypeIndex{};
		};
		bool is_explicit_capture = (identifierNode.binding() == IdentifierBinding::CapturedByValue ||
		                            identifierNode.binding() == IdentifierBinding::CapturedByRef);
		bool is_implicit_capture = !is_explicit_capture && current_lambda_context_.isActive() &&
		    current_lambda_context_.captures.find(var_name_str) != current_lambda_context_.captures.end();
		if (is_explicit_capture || is_implicit_capture) {
			// This is a captured variable - generate member access (this->x)
			// Look up the closure struct type
			auto type_it = getTypesByNameMap().find(current_lambda_context_.closure_type);
			if (type_it != getTypesByNameMap().end() && type_it->second->isStruct()) {
				TypeIndex closure_type_index = type_it->second->type_index_;
				// Find the member
				auto result = FlashCpp::gLazyMemberResolver.resolve(closure_type_index, var_name_str);
				if (result) {
					const StructMember* member = result.member;
					// Explicit captures: use binding. Implicit (capture-all): check the runtime map.
					bool is_reference = (identifierNode.binding() == IdentifierBinding::CapturedByRef);
					if (!is_reference && is_implicit_capture) {
						auto kind_it = current_lambda_context_.capture_kinds.find(var_name_str);
						is_reference = (kind_it != current_lambda_context_.capture_kinds.end() &&
						    kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);
					}

					if (is_reference) {
						// By-reference capture: member is a pointer, need to dereference
						// First, load the pointer from the closure
						TempVar ptr_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = ptr_temp;
						member_load.result.setType(member->type_index.category());  // Base type (e.g., Int)
						member_load.result.size_in_bits = SizeInBits{64};  // pointer size in bits
						member_load.object = StringTable::getOrInternStringHandle("this");
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(result.adjusted_offset);
						member_load.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

						// The ptr_temp now contains the address of the captured variable
						// We need to dereference it using PointerDereference
						auto capture_type_it = current_lambda_context_.capture_types.find(var_name_str);
						if (capture_type_it != current_lambda_context_.capture_types.end()) {
							const TypeSpecifierNode& orig_type = capture_type_it->second;

							// Generate Dereference to load the value
							TempVar result_temp = emitDereference(orig_type.category(), 64, 0, ptr_temp);

							// Mark as lvalue with Indirect metadata for unified assignment handler
							// This represents dereferencing a pointer: *ptr
							LValueInfo lvalue_info(
								LValueInfo::Kind::Indirect,
								ptr_temp,  // The pointer temp var
								0  // offset is 0 for dereference
							);
							setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));

							TypeIndex type_index = (orig_type.category() == TypeCategory::Struct) ? orig_type.type_index() : TypeIndex{};
							return makeIdentifierResult(orig_type.type(), static_cast<int>(orig_type.size_in_bits()), result_temp, type_index);
						}

						// Fallback: return the pointer temp
						return makeExprResult(member->memberType(), SizeInBits{64}, IrOperand{ptr_temp}, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
					} else {
						// By-value capture: direct member access
						TempVar result_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = result_temp;
						member_load.result.setType(member->type_index.category());
						member_load.result.size_in_bits = SizeInBits{static_cast<int>(member->size * 8)};
						member_load.object = StringTable::getOrInternStringHandle("this");  // implicit this pointer
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(result.adjusted_offset);
						member_load.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

						// For mutable lambdas, set LValue metadata so assignments write back to the member
						if (current_lambda_context_.is_mutable) {
							// Use 'this' as the base object (StringHandle version)
							// The assignment handler will emit MemberStore using this info
							LValueInfo lvalue_info(
								LValueInfo::Kind::Member,
								StringTable::getOrInternStringHandle("this"),  // object name (this pointer)
								static_cast<int>(result.adjusted_offset)
							);
							lvalue_info.member_name = member->getName();
							lvalue_info.is_pointer_to_member = true;  // 'this' is a pointer, need to dereference
							setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
						}

						TypeIndex type_index = (is_struct_type(member->type_index.category())) ? member->type_index : TypeIndex{};
						return makeIdentifierResult(member->memberType(), static_cast<int>(member->size * 8), result_temp, type_index);
					}
				}
			}
		}

		// If we're inside a [*this] lambda, prefer resolving to members of the copied object
		if (isInCopyThisLambda() && current_lambda_context_.enclosing_struct_type_index.is_valid()) {
			auto result = FlashCpp::gLazyMemberResolver.resolve(current_lambda_context_.enclosing_struct_type_index, var_name_str);
			if (result) {
				const StructMember* member = result.member;
				if (auto copy_this_temp = emitLoadCopyThis(Token())) {
					TempVar result_temp = var_counter.next();
					MemberLoadOp member_load;
					member_load.result.value = result_temp;
					member_load.result.setType(member->type_index.category());
					member_load.result.size_in_bits = SizeInBits{static_cast<int>(member->size * 8)};
					member_load.object = *copy_this_temp;
					member_load.member_name = member->getName();
					member_load.offset = static_cast<int>(result.adjusted_offset);
					member_load.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
					member_load.struct_type_info = nullptr;
					ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

					LValueInfo lvalue_info(
						LValueInfo::Kind::Member,
						*copy_this_temp,
						static_cast<int>(result.adjusted_offset)
					);
					lvalue_info.member_name = member->getName();
					setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));

					TypeIndex type_index = (is_struct_type(member->type_index.category())) ? member->type_index : TypeIndex{};
					return makeIdentifierResult(member->memberType(), static_cast<int>(member->size * 8), result_temp, type_index);
				}
			}
		}

		// Check if this is a static local variable FIRST (before any other lookups)
		// Phase 4: Using StringHandle for lookup
		StringHandle identifier_handle = StringTable::getOrInternStringHandle(identifierNode.name());
		auto static_local_it = static_local_names_.find(identifier_handle);
		if (static_local_it != static_local_names_.end()) {
			// This is a static local - generate GlobalLoad with mangled name
			const StaticLocalInfo& info = static_local_it->second;

			// For LValueAddress context (assignment LHS), return the mangled name directly
			// This allows the assignment instruction to store to the global variable
			if (context == ExpressionContext::LValueAddress) {
				return makeExprResult(info.type(), info.size_in_bits, IrOperand{info.mangled_name}, info.type_index, PointerDepth{}, ValueStorage::ContainsData);
			}

			// For Load context (normal read), generate GlobalLoad with mangled name
			TempVar result_temp = var_counter.next();
			GlobalLoadOp op;
			op.result.setType(info.type_index.category());
			op.result.ir_type = toIrType(info.type());
			op.result.size_in_bits = info.size_in_bits;
			op.result.value = result_temp;
			op.global_name = info.mangled_name;  // Use mangled name
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));
			setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
				LValueInfo(LValueInfo::Kind::Global, info.mangled_name),
				info.type_index.category(),
				info.size_in_bits.value));

			// Return the temp variable that will hold the loaded value
			return makeExprResult(info.type(), info.size_in_bits, IrOperand{result_temp}, info.type_index, PointerDepth{}, ValueStorage::ContainsData);
		}

		// Fast-path: if binding is resolved as Global, try a direct lookup to skip the
		// multi-table cascade. Handles the common case (simple global variables).
		// Falls through when using-declarations override the name (e.g., using ::globalValue
		// inside a namespace with its own globalValue), or when the direct lookup fails.
		if (identifierNode.binding() == IdentifierBinding::Global && global_symbol_table_) {
			// If any local using-declaration overrides this identifier's resolution,
			// the cascade (not this fast-path) is needed to get the correct qualified name.
			bool has_using_override = false;
			for (const auto& [local_name, target_info] : symbol_table.get_current_using_declaration_handles()) {
				if (local_name == identifierNode.name()) {
					has_using_override = true;
					break;
				}
			}

			if (!has_using_override) {
				const std::optional<ASTNode> fast_sym = global_symbol_table_->lookup(identifierNode.name());
				if (fast_sym.has_value()) {
					StringHandle effective_name;
					auto mangle_it = global_variable_names_.find(identifier_handle);
					effective_name = (mangle_it != global_variable_names_.end()) ? mangle_it->second : identifier_handle;

					if (fast_sym->is<VariableDeclarationNode>()) {
						const auto& vd = fast_sym->as<VariableDeclarationNode>();
						const auto& decl_n = vd.declaration();
						const auto& type_n = decl_n.type_node().as<TypeSpecifierNode>();
						bool is_array_type = decl_n.is_array() || type_n.is_array();
						bool is_ptr_or_ref = type_n.is_pointer() || type_n.is_reference() || type_n.is_function_pointer();
						int size_bits = (is_array_type || is_ptr_or_ref) ? 64 : static_cast<int>(type_n.size_in_bits());
						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.setType(type_n.category());
						op.result.ir_type = toIrType(type_n.type());
						op.result.size_in_bits = SizeInBits{static_cast<int>(size_bits)};
						op.result.value = result_temp;
						op.global_name = effective_name;
						op.is_array = is_array_type;
						StringHandle saved_name = op.global_name;
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));
						if (!is_array_type) {
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
								LValueInfo(LValueInfo::Kind::Global, saved_name),
								type_n.category(), size_bits));
						}
						return makeIdentifierResultFromTypeNode(type_n, size_bits, result_temp, true);
					}

					if (fast_sym->is<DeclarationNode>()) {
						const auto& decl_n = fast_sym->as<DeclarationNode>();
						const auto& type_n = decl_n.type_node().as<TypeSpecifierNode>();
						if (std::optional<ExprResult> enumerator_constant = tryMakeEnumeratorConstantExpr(type_n, identifier_handle)) {
							return *enumerator_constant;
						}
						bool is_array_type = decl_n.is_array() || type_n.is_array();
						int size_bits = (type_n.pointer_depth() > 0 || is_array_type) ? 64 : static_cast<int>(type_n.size_in_bits());
						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.setType(type_n.category());
						op.result.ir_type = toIrType(type_n.type());
						op.result.size_in_bits = SizeInBits{static_cast<int>(size_bits)};
						op.result.value = result_temp;
						op.global_name = effective_name;
						op.is_array = is_array_type;
						StringHandle saved_global_name = op.global_name;
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));
						if (!is_array_type) {
							setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
								LValueInfo(LValueInfo::Kind::Global, saved_global_name),
								type_n.category(), size_bits));
						}
						return makeIdentifierResultFromTypeNode(type_n, size_bits, result_temp, true);
					}
					// Other symbol types (FunctionDeclarationNode, etc.): fall through to cascade
				}
				// Not found by direct lookup: fall through to cascade (handles namespace-scoped globals)
			}
			// Has using override: fall through to cascade for correct qualified name resolution
		}

		// If binding is NonStaticMember, handle member access directly
		if (identifierNode.binding() == IdentifierBinding::NonStaticMember) {
			if (current_lambda_context_.isActive() && current_lambda_context_.has_this_pointer &&
				current_lambda_context_.enclosing_struct_type_index.is_valid()) {
				if (auto result = FlashCpp::gLazyMemberResolver.resolve(current_lambda_context_.enclosing_struct_type_index, var_name_str)) {
					const StructMember* member = result.member;
					if (auto this_ptr = emitLoadThisPointer(Token())) {
						TempVar result_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = result_temp;
						member_load.result.setType(member->type_index.category());
						member_load.result.size_in_bits = SizeInBits{static_cast<int>(member->size * 8)};
						member_load.object = *this_ptr;
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(result.adjusted_offset);
						member_load.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
						member_load.struct_type_info = nullptr;
						member_load.is_pointer_to_member = true;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
						LValueInfo lvalue_info(
							LValueInfo::Kind::Member,
							*this_ptr,
							static_cast<int>(result.adjusted_offset)
						);
						lvalue_info.member_name = member->getName();
						lvalue_info.is_pointer_to_member = true;
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
						if (context == ExpressionContext::LValueAddress && member->is_reference()) {
							LValueInfo reference_lvalue_info(LValueInfo::Kind::Indirect, result_temp, 0);
							setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(reference_lvalue_info));
						}
						TypeIndex type_index = (is_struct_type(member->type_index.category())) ? member->type_index : TypeIndex{};
						return makeIdentifierResult(member->memberType(), static_cast<int>(member->size * 8), result_temp, type_index);
					}
				}
			}

			if (current_struct_name_.isValid()) {
				auto type_it = getTypesByNameMap().find(current_struct_name_);
				if (type_it != getTypesByNameMap().end() && type_it->second->isStruct()) {
					TypeIndex struct_type_index = type_it->second->type_index_;
					if (auto result = FlashCpp::gLazyMemberResolver.resolve(struct_type_index, var_name_str)) {
						const StructMember* member = result.member;
						TempVar result_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = result_temp;
						member_load.result.setType(member->type_index.category());
						member_load.result.size_in_bits = SizeInBits{static_cast<int>(member->size * 8)};
						member_load.object = StringTable::getOrInternStringHandle("this");
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(result.adjusted_offset);
						member_load.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
						member_load.struct_type_info = nullptr;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
						LValueInfo lvalue_info(
							LValueInfo::Kind::Member,
							StringTable::getOrInternStringHandle("this"),
							static_cast<int>(result.adjusted_offset)
						);
						lvalue_info.member_name = member->getName();
						lvalue_info.is_pointer_to_member = true;
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
						if (context == ExpressionContext::LValueAddress && member->is_reference()) {
							LValueInfo reference_lvalue_info(LValueInfo::Kind::Indirect, result_temp, 0);
							setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(reference_lvalue_info));
						}
						TypeIndex type_index = (is_struct_type(member->type_index.category())) ? member->type_index : TypeIndex{};
						return makeIdentifierResult(member->memberType(), static_cast<int>(member->size * 8), result_temp, type_index);
					}
				}
			}
		}

		// Check using declarations from local scope FIRST, before local symbol table lookup
		// This handles cases like: using ::globalValue; return globalValue;
		// where globalValue should resolve to the global namespace version even if there's
		// a namespace-scoped version with the same name
		std::optional<ASTNode> symbol;
		bool is_global = false;
		std::optional<StringHandle> resolved_qualified_name;  // Track the qualified name from using declaration

		if (global_symbol_table_) {
			auto using_declarations = symbol_table.get_current_using_declaration_handles();
			for (const auto& [local_name, target_info] : using_declarations) {
				if (local_name == identifierNode.name()) {
					const auto& [namespace_handle, original_name] = target_info;
					StringHandle original_handle = StringTable::getOrInternStringHandle(original_name);
					resolved_qualified_name = namespace_handle.isGlobal()
						? original_handle
						: gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, original_handle);

					// Resolve using the global symbol table
					symbol = global_symbol_table_->lookup_qualified(namespace_handle, original_handle);
					if (symbol.has_value()) {
						is_global = true;
						break;
					}
				}
			}
		}

		// If not resolved via using declaration, try local symbol table (for local variables, parameters, etc.)
		// This ensures constructor parameters shadow member variables in initializer expressions
		if (!symbol.has_value()) {
			symbol = symbol_table.lookup(identifierNode.name());
		}

		// If not found locally, try global symbol table (for enum values, global variables, namespace-scoped variables, etc.)
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(identifierNode.name());
			is_global = symbol.has_value();  // If found in global table, it's a global

			// If still not found, check using directives from local scope in the global symbol table
			// This handles cases like: using namespace X; int y = X_var;
			// where X_var is defined in namespace X
			if (!symbol.has_value()) {
				auto using_directives = symbol_table.get_current_using_directive_handles();
				for (NamespaceHandle ns_handle : using_directives) {
					symbol = global_symbol_table_->lookup_qualified(ns_handle, identifierNode.name());
					if (symbol.has_value()) {
						is_global = true;
						break;
					}
				}
			}

			// If still unresolved, try unqualified lookup through the current namespace chain.
			// This handles unscoped enum enumerators in namespace scope (e.g., memory_order_relaxed in std).
			if (!symbol.has_value() && !current_namespace_stack_.empty()) {
				NamespaceHandle current_ns = NamespaceRegistry::GLOBAL_NAMESPACE;
				bool namespace_path_valid = true;
				for (const auto& ns_name : current_namespace_stack_) {
					NamespaceHandle next_ns = gNamespaceRegistry.lookupNamespace(
						current_ns, StringTable::getOrInternStringHandle(ns_name));
					if (!next_ns.isValid()) {
						namespace_path_valid = false;
						break;
					}
					current_ns = next_ns;
				}

				if (namespace_path_valid) {
					NamespaceHandle search_ns = current_ns;
					while (search_ns.isValid()) {
						symbol = global_symbol_table_->lookup_qualified(search_ns, identifier_handle);
						if (symbol.has_value()) {
							is_global = true;
							resolved_qualified_name = search_ns.isGlobal()
								? identifier_handle
								: gNamespaceRegistry.buildQualifiedIdentifier(search_ns, identifier_handle);
							break;
						}
						if (search_ns.isGlobal()) {
							break;
						}
						search_ns = gNamespaceRegistry.getParent(search_ns);
					}
				}
			}

			// If still unresolved, consult namespace-scope using declarations/directives
			// recorded in the global symbol table (e.g. using std::memory_order_relaxed;).
			if (!symbol.has_value()) {
				auto global_using_declarations = global_symbol_table_->get_current_using_declaration_handles();
				for (const auto& [local_name, target_info] : global_using_declarations) {
					if (local_name == identifierNode.name()) {
						const auto& [namespace_handle, original_name] = target_info;
						symbol = global_symbol_table_->lookup_qualified(namespace_handle, original_name);
						if (symbol.has_value()) {
							is_global = true;
							StringHandle original_handle = StringTable::getOrInternStringHandle(original_name);
							resolved_qualified_name = namespace_handle.isGlobal()
								? original_handle
								: gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, original_handle);
							break;
						}
					}
				}
			}
			if (!symbol.has_value()) {
				auto global_using_directives = global_symbol_table_->get_current_using_directive_handles();
				for (NamespaceHandle ns_handle : global_using_directives) {
					symbol = global_symbol_table_->lookup_qualified(ns_handle, identifierNode.name());
					if (symbol.has_value()) {
						is_global = true;
						resolved_qualified_name = ns_handle.isGlobal()
							? identifier_handle
							: gNamespaceRegistry.buildQualifiedIdentifier(ns_handle, identifier_handle);
						break;
					}
				}
			}

		}

		// Only check if it's a member variable if NOT found in symbol tables
		// This gives priority to parameters and local variables over member variables
		// Skip this for [*this] lambdas - they need to access through __copy_this instead
		// Also check that we're not in a lambda context where this would be an enclosing struct member
		if (!symbol.has_value() && current_struct_name_.isValid() &&
		!isInCopyThisLambda() && !current_lambda_context_.isActive()) {
			// Look up the struct type
			auto type_it = getTypesByNameMap().find(current_struct_name_);
			if (type_it != getTypesByNameMap().end() && type_it->second->isStruct()) {
				TypeIndex struct_type_index = type_it->second->type_index_;
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					// Check if this identifier is a member of the struct
					auto result = FlashCpp::gLazyMemberResolver.resolve(struct_type_index, var_name_str);
					if (result) {
						const StructMember* member = result.member;
						// This is a member variable access - generate MemberAccess IR with implicit 'this'
						TempVar result_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = result_temp;
						member_load.result.setType(member->type_index.category());
						member_load.result.size_in_bits = SizeInBits{static_cast<int>(member->size * 8)};
						member_load.object = StringTable::getOrInternStringHandle("this");  // implicit this pointer
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(result.adjusted_offset);
						member_load.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

						// Mark as lvalue with member metadata for unified assignment handler
						LValueInfo lvalue_info(
							LValueInfo::Kind::Member,
							StringTable::getOrInternStringHandle("this"),
							static_cast<int>(result.adjusted_offset)
						);
						lvalue_info.member_name = member->getName();
						lvalue_info.is_pointer_to_member = true;
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
						if (context == ExpressionContext::LValueAddress && member->is_reference()) {
							LValueInfo reference_lvalue_info(
								LValueInfo::Kind::Indirect,
								result_temp,
								0
							);
							setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(reference_lvalue_info));
						}

						TypeIndex type_index = (is_struct_type(member->type_index.category())) ? member->type_index : TypeIndex{};
						return makeIdentifierResult(member->memberType(), static_cast<int>(member->size * 8), result_temp, type_index);
					}

					// Check if this identifier is a static member
					const StructStaticMember* static_member = struct_info->findStaticMember(var_name_str);
					if (static_member) {
						// This is a static member access - generate GlobalLoad IR
						// Static members are stored as globals with qualified names
						// Note: Namespaces are already included in current_struct_name_ via mangling
						auto qualified_name = StringTable::getOrInternStringHandle(StringBuilder().append(current_struct_name_).append("::"sv).append(var_name_str));

						int member_size_bits = static_cast<int>(static_member->size * 8);
						// If size is 0 for struct types, look up from type info
						if (member_size_bits == 0 && static_member->type_index.is_valid() && static_member->type_index.index() < getTypeInfoCount()) {
							const StructTypeInfo* member_si = getTypeInfo(static_member->type_index).getStructInfo();
							if (member_si) {
								member_size_bits = static_cast<int>(member_si->total_size * 8);
							}
						}

						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.setType(static_member->type_index.category());
						op.result.ir_type = toIrType(static_member->memberType());
						op.result.size_in_bits = SizeInBits{static_cast<int>(member_size_bits)};
						op.result.value = result_temp;
						op.global_name = qualified_name;
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

						TypeIndex type_index = (is_struct_type(static_member->type_index.category())) ? static_member->type_index : TypeIndex{};
						return makeIdentifierResult(static_member->memberType(), member_size_bits, result_temp, type_index);
					}
				}
			}
		}
		// If still not found and we're in a struct, check nested enum enumerators
		// Unscoped enums declared inside a class make their enumerators accessible in the class scope
		// Only search enums tracked as nested within the current struct to avoid
		// incorrectly resolving enumerators from unrelated structs.
		if (!symbol.has_value() && current_struct_name_.isValid()) {
			auto type_it = getTypesByNameMap().find(current_struct_name_);
			if (type_it != getTypesByNameMap().end() && type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					StringHandle id_handle = StringTable::getOrInternStringHandle(identifierNode.name());
					for (TypeIndex enum_idx : struct_info->getNestedEnumIndices()) {
						if (enum_idx.index() < getTypeInfoCount()) {
							const EnumTypeInfo* enum_info = getTypeInfo(enum_idx).getEnumInfo();
							if (enum_info && !enum_info->is_scoped) {
								if (std::optional<ExprResult> enumerator_constant = tryMakeEnumeratorConstantExpr(*enum_info, id_handle)) {
									return *enumerator_constant;
								}
							}
						}
					}
				}
			}
		}
		if (!symbol.has_value()) {
			FLASH_LOG(Codegen, Error, "Symbol '", identifierNode.name(), "' not found in symbol table during code generation");
			FLASH_LOG(Codegen, Error, "  Current function: ", current_function_name_);
			FLASH_LOG(Codegen, Error, "  Current struct: ", current_struct_name_);
			throw InternalError("Expected symbol '" + std::string(identifierNode.name()) + "' to exist in code generation");
			return ExprResult{};
		}

		if (symbol->is<DeclarationNode>()) {
			const auto& decl_node = symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Check if this is an enum value (enumerator constant)
			// IMPORTANT: References and pointers to enum are VARIABLES, not enumerator constants
			// Only non-reference, non-pointer enum-typed identifiers CAN BE enumerators
			// We must verify the identifier actually exists as an enumerator before treating it as a constant
			if (std::optional<ExprResult> enumerator_constant = tryMakeEnumeratorConstantExpr(
				type_node,
				StringTable::getOrInternStringHandle(identifierNode.name()))) {
				return *enumerator_constant;
			}

			// Check if this is a global variable
			if (is_global) {
				// Generate GlobalLoad IR instruction
				TempVar result_temp = var_counter.next();
				// For arrays, result is a pointer (64-bit address)
				bool is_array_type = decl_node.is_array() || type_node.is_array();
				int size_bits = (type_node.pointer_depth() > 0 || is_array_type) ? 64 : static_cast<int>(type_node.size_in_bits());
				GlobalLoadOp op;
				op.result.setType(type_node.category());
				op.result.ir_type = toIrType(type_node.type());
				op.result.size_in_bits = SizeInBits{static_cast<int>(size_bits)};
				op.result.value = result_temp;

				// If we resolved this via a using declaration, use the resolved qualified name
				// Otherwise, check if this global has a mangled name (e.g., anonymous namespace variable)
				if (resolved_qualified_name.has_value()) {
					op.global_name = *resolved_qualified_name;
				} else {
					// Phase 4: Using StringHandle for lookup
					StringHandle simple_name_handle = StringTable::getOrInternStringHandle(identifierNode.name());
					auto it = global_variable_names_.find(simple_name_handle);
					if (it != global_variable_names_.end()) {
						op.global_name = it->second;  // Use mangled StringHandle
					} else {
						op.global_name = StringTable::getOrInternStringHandle(identifierNode.name());  // Use simple name as StringHandle
					}
				}

				op.is_array = is_array_type;  // Arrays need LEA to get address
					StringHandle saved_global_name = op.global_name;
					ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));
					if (!is_array_type) {
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
							LValueInfo(LValueInfo::Kind::Global, saved_global_name),
							type_node.category(), size_bits));
					}

				// Return the temp variable that will hold the loaded value
				// For pointers and arrays, return 64 bits (pointer size)
				// Include type_index for struct types
				return makeIdentifierResultFromTypeNode(type_node, size_bits, result_temp, true);
			}

			// Check if this is a reference parameter - if so, we need to dereference it
			// EXCEPT for array references, where the reference IS the array pointer
			// IMPORTANT: When context is LValueAddress (e.g., LHS of assignment), DON'T dereference - return the parameter name directly
			//
			// NOTE: This handles both reference PARAMETERS and local reference VARIABLES (like structured binding references)
			// The distinction is:
			// - Reference parameters: stored in VariableDeclOp with is_reference=true during code generation
			// - Local reference variables: created with DeclarationNode that has reference TypeSpecifierNode
			if (type_node.is_reference()) {
				// Check if this is actually a local reference variable (not a parameter)
				// Local reference variables are stored on the stack and hold a pointer value
				// We can detect this by checking if a VariableDeclOp was generated with is_reference=true
				// For now, we'll treat all references as parameters unless they fail the parameter test

				// For references to arrays (e.g., int (&arr)[3]), the reference parameter
				// already holds the array address directly. We don't dereference it.
				// Just return it as a pointer (64 bits on x64 architecture).
				if (type_node.is_array()) {
					// Return the array reference as a 64-bit pointer
					return makeExprResult(type_node.type(), SizeInBits{POINTER_SIZE_BITS}, IrOperand{StringTable::getOrInternStringHandle(identifierNode.name())}, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
				}

				// For LValueAddress context (e.g., LHS of assignment, function call with reference parameter)
				// For compound assignments, we need to return a TempVar with lvalue metadata
				// For simple assignments and function calls, we can return the reference directly
				if (context == ExpressionContext::LValueAddress) {
					Type pointee_type = type_node.type();
					int pointee_size = resolveCodegenSizeBits(type_node, "reference identifier lvalue lowering");

					TypeIndex type_index = preserveSemanticTypeIndex(pointee_type, type_node.type_index());

					// Create a TempVar with Indirect lvalue metadata for compound assignments
					// This allows handleLValueCompoundAssignment to work with reference variables
					TempVar lvalue_temp = var_counter.next();
					FLASH_LOG_FORMAT(Codegen, Debug, "Reference LValueAddress: Creating TempVar {} for reference '{}'",
						lvalue_temp.var_number, identifierNode.name());

					// Generate Assignment to copy the pointer value from the reference parameter to the temp
					StringHandle var_handle = StringTable::getOrInternStringHandle(identifierNode.name());
					AssignmentOp assign_op;
					assign_op.result = lvalue_temp;
					assign_op.lhs = makeTypedValue(pointee_type, SizeInBits{64}, lvalue_temp);  // 64-bit pointer dest
					assign_op.rhs = makeTypedValue(pointee_type, SizeInBits{64}, var_handle);  // 64-bit pointer source
					assign_op.is_pointer_store = false;
					assign_op.dereference_rhs_references = false;  // Don't dereference - just copy the pointer!
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));

					LValueInfo lvalue_info(
						LValueInfo::Kind::Indirect,
						lvalue_temp,  // Use the temp var holding the address, not the parameter name
						0  // offset is 0 for simple dereference
					);
					setTempVarMetadata(lvalue_temp, TempVarMetadata::makeLValue(lvalue_info));
					FLASH_LOG_FORMAT(Codegen, Debug, "Reference LValueAddress: Set metadata on TempVar {}", lvalue_temp.var_number);

					// Return with TempVar that has lvalue metadata
					// The type/size are for the pointee (what the reference refers to)
					return makeExprResult(pointee_type, SizeInBits{pointee_size}, IrOperand{lvalue_temp}, type_index, PointerDepth{}, ValueStorage::ContainsAddress);
				}

				// For non-array references in Load context, we need to dereference to get the value

				TypeCategory pointee_type = type_node.category();
				int pointee_size = resolveCodegenSizeBits(type_node, "reference identifier load lowering");

				Type semantic_pointee_type = type_node.type();

				// For enum references, lower the dereferenced value to its runtime
				// representation so arithmetic/bitwise operations consume the
				// underlying integer type.
				pointee_type = getRuntimeValueType(typeToCategory(semantic_pointee_type), type_node.type_index(), PointerDepth{});
				pointee_size = getRuntimeValueSizeBits(semantic_pointee_type, type_node.type_index(), pointee_size, PointerDepth{});

				int ptr_depth = type_node.pointer_depth() > 0 ? type_node.pointer_depth() : 1;
				TempVar result_temp = emitDereference(pointee_type, 64, ptr_depth,
					StringTable::getOrInternStringHandle(identifierNode.name()));

				// Mark as lvalue with Indirect metadata for unified assignment handler
				// This allows compound assignments (like x *= 2) to work on dereferenced references
				LValueInfo lvalue_info(
					LValueInfo::Kind::Indirect,
					StringTable::getOrInternStringHandle(identifierNode.name()),  // The reference variable name
					0  // offset is 0 for simple dereference
				);
				setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));

				TypeIndex type_index = preserveSemanticTypeIndex(type_node.type(), type_node.type_index());
				return makeIdentifierResult(categoryToType(pointee_type), pointee_size, result_temp, type_index);
			}

			// Regular local variable
			// Use helper function to calculate size_bits with proper fallback handling
			int size_bits = calculateIdentifierSizeBits(type_node, decl_node.is_array(), identifierNode.name());

			// Lower non-pointer variables to their runtime representation. For enums
			// this produces the underlying integer type/size while preserving
			// semantic type metadata separately via type_index below.
			assert(type_node.pointer_depth() <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
				"Pointer depth exceeds maximum int value for PointerDepth construction");
			PointerDepth identifier_pointer_depth{static_cast<int>(type_node.pointer_depth())};
			TypeCategory return_type = getRuntimeValueType(
				type_node.category(),
				type_node.type_index(),
				identifier_pointer_depth);
			size_bits = getRuntimeValueSizeBits(
				type_node.type(),
				type_node.type_index(),
				size_bits,
				identifier_pointer_depth);

			// For the 4th element:
			// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
			// - For enum types, return type_index to preserve type information
			// - For non-struct/enum pointer types, return pointer_depth
			// - Otherwise return 0
			// Guard with is_valid() so primitive typedefs (Type::UserDefined but
			// no struct/enum info) don't propagate a stale type_index.
			TypeIndex type_index = (carriesSemanticTypeIndex(type_node.type()) && type_node.type_index().is_valid())
				? type_node.type_index()
				: TypeIndex{};
			// Enums are scalar (carry pointer_depth like integers), while structs
			// use type_index for layout and zero pointer_depth here.
			PointerDepth pointer_depth{isIrStructType(toIrType(type_node.type()))
				? 0
				: static_cast<int>(type_node.pointer_depth())};
			return makeIdentifierResult(
				categoryToType(return_type),
				size_bits,
				StringTable::getOrInternStringHandle(identifierNode.name()),
				type_index,
				pointer_depth
			);
		}

		// Check if it's a VariableDeclarationNode
		if (symbol->is<VariableDeclarationNode>()) {
			const auto& var_decl_node = symbol->as<VariableDeclarationNode>();
			const auto& decl_node = var_decl_node.declaration();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Check if this is actually a global variable
			if (is_global) {
				// This is a global variable - generate GlobalLoad
				TempVar result_temp = var_counter.next();
				// For arrays, pointers, and references, result is a pointer (64-bit address)
				bool is_array_type = decl_node.is_array() || type_node.is_array();
				bool is_ptr_or_ref = type_node.is_pointer() || type_node.is_reference() || type_node.is_function_pointer();
				int size_bits = (is_array_type || is_ptr_or_ref) ? 64 : static_cast<int>(type_node.size_in_bits());
				GlobalLoadOp op;
				op.result.setType(type_node.category());
				op.result.ir_type = toIrType(type_node.type());
				op.result.size_in_bits = SizeInBits{static_cast<int>(size_bits)};
				op.result.value = result_temp;

				// If we resolved this via a using declaration, use the resolved qualified name
				// Otherwise, check if this global has a mangled name (e.g., anonymous namespace variable)
				if (resolved_qualified_name.has_value()) {
					op.global_name = *resolved_qualified_name;
				} else {
					// Phase 4: Using StringHandle for lookup
					StringHandle simple_name_handle = StringTable::getOrInternStringHandle(identifierNode.name());
					auto it = global_variable_names_.find(simple_name_handle);
					if (it != global_variable_names_.end()) {
						op.global_name = it->second;  // Use mangled StringHandle
					} else {
						op.global_name = StringTable::getOrInternStringHandle(identifierNode.name());  // Use simple name as StringHandle
					}
				}

				op.is_array = is_array_type;  // Arrays need LEA to get address
				StringHandle saved_global_name = op.global_name;  // save before move
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Register Global lvalue metadata so compound assignments (+=, -=, etc.) can write back
				if (!is_array_type) {
					setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
						LValueInfo(LValueInfo::Kind::Global, saved_global_name),
						type_node.category(), size_bits));
				}

				// Return the temp variable that will hold the loaded value
				// Include type_index for struct types
				return makeIdentifierResultFromTypeNode(type_node, size_bits, result_temp, true);
			} else {
				// This is a local variable

				// Check if this is a reference variable - if so, we need to dereference it
				// Reference variables (both lvalue & and rvalue &&) hold an address, and we need to load the value from that address
				// EXCEPT for array references, where the reference IS the array pointer
				if (type_node.is_reference()) {
					FLASH_LOG_FORMAT(Codegen, Debug, "VariableDecl reference '{}': context={}",
						identifierNode.name(), context == ExpressionContext::LValueAddress ? "LValueAddress" : "Load");

					// For references to arrays (e.g., int (&arr)[3]), the reference variable
					// already holds the array address directly. We don't dereference it.
					// Just return it as a pointer (64 bits on x64 architecture).
					if (type_node.is_array()) {
						// Return the array reference as a 64-bit pointer
						return makeExprResult(type_node.type(), SizeInBits{POINTER_SIZE_BITS}, IrOperand{StringTable::getOrInternStringHandle(identifierNode.name())}, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
					}

					// For LValueAddress context (assignment LHS), we need to treat the reference variable
					// as an indirect lvalue (pointer that needs dereferencing for stores)
					if (context == ExpressionContext::LValueAddress) {
						FLASH_LOG_FORMAT(Codegen, Debug, "VariableDecl reference '{}': Creating addr_temp for LValueAddress", identifierNode.name());
						Type pointee_type = type_node.type();
						int pointee_size = resolveCodegenSizeBits(type_node, "reference variable lvalue lowering");

						// The reference variable holds a pointer address
						// We need to load it into a temp and mark it with Indirect LValue metadata
						TempVar addr_temp = var_counter.next();
						StringHandle var_handle = StringTable::getOrInternStringHandle(identifierNode.name());

						// Use AssignmentOp to copy the pointer value to a temp
						AssignmentOp assign_op;
						assign_op.result = addr_temp;
						assign_op.lhs = makeTypedValue(pointee_type, SizeInBits{64}, addr_temp);  // 64-bit pointer dest
						assign_op.rhs = makeTypedValue(pointee_type, SizeInBits{64}, var_handle);  // 64-bit pointer source
						assign_op.is_pointer_store = false;
						assign_op.dereference_rhs_references = false;  // Don't dereference - just copy the pointer!
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));

						// Mark the temp with Indirect LValue metadata
						// This tells the assignment handler to use DereferenceStore
						LValueInfo lvalue_info(
							LValueInfo::Kind::Indirect,
							addr_temp,  // The temp holding the pointer address
							0  // offset is 0 for dereference
						);
						setTempVarMetadata(addr_temp, TempVarMetadata::makeLValue(lvalue_info));

						TypeIndex type_index = preserveSemanticTypeIndex(pointee_type, type_node.type_index());
						return makeExprResult(pointee_type, SizeInBits{pointee_size}, IrOperand{addr_temp}, type_index, PointerDepth{}, ValueStorage::ContainsAddress);
					}

					// For Load context (reading the value), dereference to get the value

					Type pointee_type = type_node.type();
					int pointee_size = resolveCodegenSizeBits(type_node, "reference variable load lowering");

					int ptr_depth = type_node.pointer_depth() > 0 ? type_node.pointer_depth() : 1;
					TempVar result_temp = emitDereference(typeToCategory(pointee_type), 64, ptr_depth,
						StringTable::getOrInternStringHandle(identifierNode.name()));

					// Mark as lvalue with Indirect metadata for unified assignment handler
					// This allows compound assignments (like x *= 2) to work on dereferenced references
					LValueInfo lvalue_info(
						LValueInfo::Kind::Indirect,
						StringTable::getOrInternStringHandle(identifierNode.name()),  // The reference variable name
						0  // offset is 0 for simple dereference
					);
					setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));

					TypeIndex type_index = preserveSemanticTypeIndex(type_node.type(), type_node.type_index());
					return makeIdentifierResult(pointee_type, pointee_size, result_temp, type_index);
				}

				// Regular local variable (not a reference) - return variable name.
				int size_bits = calculateIdentifierSizeBits(type_node, decl_node.is_array(), identifierNode.name());
				Type result_type = type_node.type();
				TypeIndex result_type_index = type_node.type_index();
				requireResolvedCodegenType(result_type, "identifier lowering");


				// For the 4th element:
				// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
				// - For non-struct pointer types, return pointer_depth
				// - Otherwise return 0
				return makeIdentifierResult(
					result_type,
					size_bits,
					StringTable::getOrInternStringHandle(identifierNode.name()),
					carriesSemanticTypeIndex(result_type)
						? result_type_index
						: TypeIndex{},
					PointerDepth{isIrStructType(toIrType(result_type))
						? 0
						: static_cast<int>(type_node.pointer_depth())});
			}
		}

		// Check if it's a FunctionDeclarationNode (function name used as value)
		if (symbol->is<FunctionDeclarationNode>()) {
			// This is a function name being used as a value (e.g., fp = add)
			// Generate FunctionAddress IR instruction
			const auto& func_decl = symbol->as<FunctionDeclarationNode>();

			// Compute mangled name from the function declaration, respecting linkage
			// (extern "C" functions must not be mangled)
			std::string_view mangled = generateMangledNameForCall(func_decl, "", {});

			TempVar func_addr_var = var_counter.next();
			FunctionAddressOp op;
			op.result.setType(TypeCategory::FunctionPointer);
			op.result.ir_type = IrType::FunctionPointer;
			op.result.size_in_bits = SizeInBits{64};
			op.result.value = func_addr_var;
			op.function_name = StringTable::getOrInternStringHandle(identifierNode.name());
			op.mangled_name = StringTable::getOrInternStringHandle(mangled);
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionAddress, std::move(op), Token()));

			// Return the function address as a pointer (64 bits).
			// The TempVar holds the address of the function → ContainsAddress so that
			// reference binding (func_ref_t ref = get_value) uses MOV, not LEA.
			return makeExprResult(Type::FunctionPointer, SizeInBits{64}, IrOperand{func_addr_var}, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsAddress);
		}

		// Check if it's a TemplateVariableDeclarationNode (variable template)
		if (symbol->is<TemplateVariableDeclarationNode>()) {
			// Variable template without instantiation - should not reach codegen
			// The parser should have instantiated it already
			throw InternalError("Uninstantiated variable template in codegen");
			return ExprResult{};
		}

		// If we get here, the symbol is not a known type
		FLASH_LOG(Codegen, Error, "Unknown symbol type for identifier '", identifierNode.name(), "'");
		throw InternalError("Identifier is not a DeclarationNode");
		return ExprResult{};
	}

	ExprResult AstToIr::generateQualifiedIdentifierIr(const QualifiedIdentifierNode& qualifiedIdNode) {
		// Check if this is a scoped enum value (e.g., Direction::North)
		NamespaceHandle ns_handle = qualifiedIdNode.namespace_handle();
		if (!ns_handle.isGlobal()) {
			// The struct/enum name is the last namespace component (the name of the namespace handle)
			std::string_view struct_or_enum_name = gNamespaceRegistry.getName(ns_handle);

			// Could be EnumName::EnumeratorName
			// Check the codegen-local symbol table first before getTypesByNameMap().
			// getTypesByNameMap() uses unordered_map::emplace which is a no-op on duplicate
			// keys, so when two functions define `enum class Priority`, the second
			// function's TypeInfo is never registered under "Priority" — it always
			// resolves to the first function's TypeInfo.
			// visitEnumDeclarationNode inserts the correct TypeInfo (via TypeIndex
			// stored in the AST node at parse time) into the local symbol table,
			// so we must prefer that over the global getTypesByNameMap() lookup.
			const TypeInfo* scoped_enum_type_info = nullptr;
			{
				const std::optional<ASTNode> local_sym = symbol_table.lookup(struct_or_enum_name);
				if (local_sym && local_sym->is<DeclarationNode>()) {
					const auto& decl = local_sym->as<DeclarationNode>();
					if (decl.type_node().is<TypeSpecifierNode>()) {
						const auto& ts = decl.type_node().as<TypeSpecifierNode>();
						if (ts.category() == TypeCategory::Enum && ts.type_index().is_valid() && ts.type_index().index() < getTypeInfoCount())
							scoped_enum_type_info = &getTypeInfo(ts.type_index());
					}
				}
			}
			if (!scoped_enum_type_info) {
				auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_or_enum_name));
				if (type_it != getTypesByNameMap().end() && type_it->second->isEnum())
					scoped_enum_type_info = type_it->second;
			}
			if (scoped_enum_type_info) {
				const EnumTypeInfo* enum_info = scoped_enum_type_info->getEnumInfo();
				if (enum_info) {
					// Qualified enum access is valid for both scoped and unscoped enums.
					long long enum_value = enum_info->getEnumeratorValue(StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
					// Return the enum value as a constant
					return makeExprResult(enum_info->underlying_type, SizeInBits{static_cast<int>(enum_info->underlying_size)},
					static_cast<unsigned long long>(enum_value), TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
				}
			}

			// Check if this is a static member access (e.g., StructName::static_member or ns::StructName::static_member)
			// For nested types (depth > 1), try fully qualified name FIRST to avoid ambiguity
			// This handles member template specializations like MakeUnsigned::List_int_char
			auto struct_type_it = getTypesByNameMap().end();

			if (gNamespaceRegistry.getDepth(ns_handle) > 1) {
				StringHandle ns_qualified_handle = gNamespaceRegistry.getQualifiedNameHandle(ns_handle);
				std::string_view full_qualified_name = StringTable::getStringView(ns_qualified_handle);

				// First try with the namespace handle directly
				struct_type_it = getTypesByNameMap().find(ns_qualified_handle);
				if (struct_type_it != getTypesByNameMap().end()) {
					struct_or_enum_name = full_qualified_name;
					FLASH_LOG(Codegen, Debug, "Found struct with full qualified name: ", full_qualified_name);
				} else {
					// Fallback: search by string content
					// This handles cases where the type was registered with a different StringHandle
					// but has the same string content (e.g., type aliases in templates)
					for (const auto& [key, val] : getTypesByNameMap()) {
						std::string_view key_str = StringTable::getStringView(key);
						if (key_str == full_qualified_name) {
							struct_type_it = getTypesByNameMap().find(key);
							if (struct_type_it != getTypesByNameMap().end()) {
								struct_or_enum_name = key_str;
								FLASH_LOG(Codegen, Debug, "Found struct by string content: ", full_qualified_name);
							}
							break;
						}
					}
				}
			}

			// If not found with fully qualified name, try simple name
			if (struct_type_it == getTypesByNameMap().end()) {
				struct_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_or_enum_name));
				FLASH_LOG(Codegen, Debug, "generateQualifiedIdentifierIr: struct_or_enum_name='", struct_or_enum_name, "', found=", (struct_type_it != getTypesByNameMap().end()));
			}

			// If not found directly, search for template instantiation using TypeInfo metadata
			// This handles cases like has_type<T>::value where T has a default = void argument
			// Uses TypeInfo::baseTemplateName() for deterministic lookup instead of prefix scanning
			// Selection is deterministic by choosing the instantiation with the smallest type_index_
			if (struct_type_it == getTypesByNameMap().end()) {
				// Use TypeInfo metadata to find instantiation with matching base template name
				// We select deterministically by choosing the smallest type_index_ among matches
				StringHandle base_name_handle = StringTable::getOrInternStringHandle(struct_or_enum_name);
				TypeIndex best_type_index = std::numeric_limits<TypeIndex>::max();
				for (auto it = getTypesByNameMap().begin(); it != getTypesByNameMap().end(); ++it) {
					if (it->second->isStruct() && it->second->isTemplateInstantiation()) {
						// Use TypeInfo metadata for matching
						if (it->second->baseTemplateName() == base_name_handle) {
							// Deterministic selection: prefer smallest type_index_
							if (it->second->type_index_ < best_type_index) {
								best_type_index = it->second->type_index_;
								struct_type_it = it;
								FLASH_LOG(Codegen, Debug, "Found struct via TypeInfo metadata: baseTemplate=",
									struct_or_enum_name, " -> ", StringTable::getStringView(it->first),
									" (type_index=", it->second->type_index_, ")");
							}
						}
					}
				}
			}

			// Fallback: try old-style _void suffix for backward compatibility with legacy code
			if (struct_type_it == getTypesByNameMap().end()) {
				std::string_view struct_name_with_void = StringBuilder().append(struct_or_enum_name).append("_void"sv).commit();
				struct_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(struct_name_with_void));
				if (struct_type_it != getTypesByNameMap().end()) {
					FLASH_LOG(Codegen, Debug, "Found struct with _void suffix: ", struct_name_with_void);
				}
			}

			if (struct_type_it != getTypesByNameMap().end() && struct_type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				// If struct_info is null, this might be a type alias - resolve it via type_index
				if (!struct_info && struct_type_it->second->type_index_.index() < getTypeInfoCount()) {
					const TypeInfo* resolved_type = &getTypeInfo(struct_type_it->second->type_index_);
					if (resolved_type && resolved_type->isStruct()) {
						struct_info = resolved_type->getStructInfo();
					}
				}
				if (struct_info) {
					FLASH_LOG(Codegen, Debug, "Looking for static member '", qualifiedIdNode.name(), "' in struct '", struct_or_enum_name, "'");
					// Look for static member recursively (checks base classes too)
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
					FLASH_LOG(Codegen, Debug, "findStaticMemberRecursive result: static_member=", (static_member != nullptr), ", owner_struct=", (owner_struct != nullptr));
					if (static_member && owner_struct) {
						// Check if the owner struct is an incomplete template instantiation
						auto owner_type_it = getTypesByNameMap().find(owner_struct->getName());
						if (owner_type_it != getTypesByNameMap().end() && owner_type_it->second->is_incomplete_instantiation_) {
							std::string_view owner_name = StringTable::getStringView(owner_struct->getName());
							FLASH_LOG(Codegen, Error, "Cannot access static member '", qualifiedIdNode.name(),
							"' from incomplete template instantiation '", owner_name, "'");
							// Return a placeholder value instead of generating GlobalLoad
							// This prevents linker errors from undefined references to incomplete instantiations
							return makeExprResult(Type::Bool, SizeInBits{8}, 0ULL, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
						}

						// Determine the correct qualified name to use
						// If we accessed through a type alias (struct_type_it->second) that resolves to
						// a different struct than the owner, we should use the resolved struct name
						StringHandle qualified_struct_name = owner_struct->getName();

						// Check if we're accessing through a type alias by comparing names
						if (struct_type_it->second->name() != owner_struct->getName()) {
							// Accessing through type alias or derived class
							// First, check if this is inheritance (owner_struct is a base class of accessed struct)
							// In that case, we should use owner_struct's name directly, not do type alias resolution
							bool is_inheritance = false;
							const StructTypeInfo* accessed_struct = struct_type_it->second->getStructInfo();
							if (accessed_struct) {
								for (const auto& base : accessed_struct->base_classes) {
									if (base.type_index.index() < getTypeInfoCount()) {
										const TypeInfo& base_type = getTypeInfo(base.type_index);
										const StructTypeInfo* base_struct = base_type.getStructInfo();
										if (base_struct && base_struct->getName() == owner_struct->getName()) {
											is_inheritance = true;
											FLASH_LOG(Codegen, Debug, "Static member found via inheritance from base class: ", owner_struct->getName());
											break;
										}
									}
								}
							}

							// Skip type alias resolution for inheritance - use owner_struct's name directly
							if (!is_inheritance) {
								// Try to resolve to the actual instantiated type
								const TypeInfo* resolved_type = struct_type_it->second;

								// Special handling for true_type and false_type
								// These should resolve to integral_constant<bool, 1> and integral_constant<bool, 0>
								// but the template system doesn't instantiate them properly
								std::string_view alias_name = StringTable::getStringView(resolved_type->name());
								if (alias_name == "true_type" || alias_name == "false_type") {
									// Generate the value directly without needing a static member
									// true_type -> 1, false_type -> 0
									bool value = (alias_name == "true_type") ? true : false;
									FLASH_LOG(Codegen, Debug, "Special handling for ", alias_name, " -> value=", value);
									return makeExprResult(Type::Bool, SizeInBits{8}, static_cast<unsigned long long>(value), TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
								}

								// Follow the full type alias chain (e.g., true_type -> bool_constant -> integral_constant)
								std::unordered_set<TypeIndex> visited;
								while (resolved_type &&
								resolved_type->type_index_.index() < getTypeInfoCount() &&
								resolved_type->type_index_.is_valid() &&
								!visited.contains(resolved_type->type_index_)) {
									visited.insert(resolved_type->type_index_);
									const TypeInfo* target_type = &getTypeInfo(resolved_type->type_index_);

									if (target_type && target_type->isStruct() && target_type->getStructInfo()) {
										// Use the target struct's name
										qualified_struct_name = target_type->name();
										FLASH_LOG(Codegen, Debug, "Resolved type alias to: ", qualified_struct_name);

										// If target is also an alias, continue following
										if (target_type->type_index_.is_valid() && target_type->type_index_ != resolved_type->type_index_) {
											resolved_type = target_type;
										} else {
											break;
										}
									} else {
										break;
									}
								}

								// If still resolving to a primary template (no template args in name),
								// try to find a properly instantiated version by checking emitted static members
								std::string_view owner_name_str = StringTable::getStringView(qualified_struct_name);
								bool looks_like_primary_template =
									(owner_name_str.find('_') == std::string_view::npos ||
									owner_name_str == StringTable::getStringView(owner_struct->getName()));

								if (looks_like_primary_template) {
									// Search for an instantiated version that has this static member
									std::string search_suffix = std::string("::") + std::string(StringTable::getStringView(StringTable::getOrInternStringHandle(qualifiedIdNode.name())));
									for (const auto& emitted_handle : emitted_static_members_) {
										std::string_view emitted = StringTable::getStringView(emitted_handle);
										if (emitted.find(search_suffix) != std::string::npos &&
										emitted.find(std::string(owner_name_str) + "_") == 0) {
											// Found an instantiated version - extract the struct name
											size_t colon_pos = emitted.find("::");
											if (colon_pos != std::string::npos) {
												std::string inst_name = std::string(emitted.substr(0, colon_pos));
												qualified_struct_name = StringTable::getOrInternStringHandle(inst_name);
												FLASH_LOG(Codegen, Debug, "Using instantiated version: ", inst_name, " instead of primary template");
												break;
											}
										}
									}
								}
							}
						}

						// This is a static member access - generate GlobalLoad
						FLASH_LOG(Codegen, Debug, "Found static member in owner struct: ", owner_struct->getName(), ", using qualified name with: ", qualified_struct_name);
						int qsm_size_bits = static_cast<int>(static_member->size * 8);
						// If size is 0 for struct types, look up from type info
						if (qsm_size_bits == 0 && static_member->type_index.is_valid() && static_member->type_index.index() < getTypeInfoCount()) {
							const StructTypeInfo* qsm_si = getTypeInfo(static_member->type_index).getStructInfo();
							if (qsm_si) {
								qsm_size_bits = static_cast<int>(qsm_si->total_size * 8);
							}
						}
						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.setType(static_member->type_index.category());
						op.result.ir_type = toIrType(static_member->memberType());
						op.result.size_in_bits = SizeInBits{static_cast<int>(qsm_size_bits)};
						op.result.value = result_temp;
						// Use qualified name as the global symbol name: StructName::static_member
						op.global_name = StringTable::getOrInternStringHandle(StringBuilder().append(qualified_struct_name).append("::"sv).append(qualifiedIdNode.name()));
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

						// For reference members, the global holds a pointer — dereference it
						if (static_member->is_reference()) {
							TempVar deref_temp = var_counter.next();
							DereferenceOp deref_op;
							deref_op.result = deref_temp;
							deref_op.pointer.setType(static_member->type_index.category());
							deref_op.pointer.type_index = TypeIndex::fromTypeAndIndex(static_member->memberType(), static_member->type_index);
							deref_op.pointer.size_in_bits = SizeInBits{get_type_size_bits(static_member->memberType())};
							deref_op.pointer.pointer_depth = PointerDepth{1};
							deref_op.pointer.value = result_temp;
							ir_.addInstruction(IrInstruction(IrOpcode::Dereference, deref_op, Token()));
							TypeIndex type_index = (is_struct_type(static_member->type_index.category())) ? static_member->type_index : TypeIndex{};
							return makeExprResult(static_member->memberType(), SizeInBits{get_type_size_bits(static_member->memberType())}, IrOperand{deref_temp}, type_index, PointerDepth{}, ValueStorage::ContainsData);
						}

						// Return the temp variable that will hold the loaded value
						TypeIndex type_index = (is_struct_type(static_member->type_index.category())) ? static_member->type_index : TypeIndex{};
						return makeExprResult(static_member->memberType(), SizeInBits{qsm_size_bits}, IrOperand{result_temp}, type_index, PointerDepth{}, ValueStorage::ContainsData);
					}
				}
			}

		// If the qualifier resolved to a struct, check unscoped-enum enumerators in
		// nested enum indices (e.g., Container::Ok from `typedef enum {Ok} Status;`).
		// This mirrors the simple-identifier path that checks getNestedEnumIndices()
		// when resolving bare names inside a struct body.
		if (struct_type_it != getTypesByNameMap().end() && struct_type_it->second->isStruct()) {
			const StructTypeInfo* struct_info_ne = struct_type_it->second->getStructInfo();
			if (struct_info_ne) {
				StringHandle member_handle = StringTable::getOrInternStringHandle(qualifiedIdNode.name());
				for (TypeIndex enum_idx : struct_info_ne->getNestedEnumIndices()) {
					if (enum_idx.index() < getTypeInfoCount()) {
						const EnumTypeInfo* enum_info = getTypeInfo(enum_idx).getEnumInfo();
						if (enum_info && !enum_info->is_scoped) {
							if (std::optional<ExprResult> enumerator_result =
									tryMakeEnumeratorConstantExpr(*enum_info, member_handle)) {
								return *enumerator_result;
							}
						}
					}
				}
			}
		}
		}

		// Look up the qualified identifier in the symbol table
		const std::optional<ASTNode> symbol = symbol_table.lookup_qualified(qualifiedIdNode.qualifiedIdentifier());

		// Also try global symbol table for namespace-qualified globals
		std::optional<ASTNode> global_symbol;
		if (!symbol.has_value() && global_symbol_table_) {
			global_symbol = global_symbol_table_->lookup_qualified(qualifiedIdNode.qualifiedIdentifier());
		}

		const std::optional<ASTNode>& found_symbol = symbol.has_value() ? symbol : global_symbol;

		if (!found_symbol.has_value()) {
			// For external functions (like std::print), we might not have them in our symbol table
			// Return a placeholder - the actual linking will happen later
			return makeExprResult(Type::Int, SizeInBits{32}, IrOperand{StringTable::getOrInternStringHandle(qualifiedIdNode.name())}, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
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
					bool is_array_type = decl_node.is_array() || type_node.is_array();
					bool is_ptr_or_ref = type_node.is_pointer() || type_node.is_reference() || type_node.is_function_pointer();
					int size_bits = (is_array_type || is_ptr_or_ref) ? 64 : static_cast<int>(type_node.size_in_bits());
				GlobalLoadOp op;
				op.result.setType(type_node.category());
				op.result.ir_type = toIrType(type_node.type());
					op.result.size_in_bits = SizeInBits{static_cast<int>(size_bits)};
				op.result.value = result_temp;
				// Use fully qualified name (ns::value) to match the global variable symbol
				op.global_name = gNamespaceRegistry.buildQualifiedIdentifier(
					qualifiedIdNode.namespace_handle(),
					StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
					op.is_array = is_array_type;
					StringHandle saved_global_name = op.global_name;
					ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));
					if (!is_array_type) {
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
							LValueInfo(LValueInfo::Kind::Global, saved_global_name),
							type_node.category(), size_bits));
					}

				// Return the temp variable that will hold the loaded value
				TypeIndex type_index = (type_node.category() == TypeCategory::Struct) ? type_node.type_index() : TypeIndex{};
					return makeExprResult(type_node.type(), SizeInBits{size_bits}, IrOperand{result_temp}, type_index, PointerDepth{}, ValueStorage::ContainsData);
			} else {
				// Local variable - just return the name
				TypeIndex type_index = (type_node.category() == TypeCategory::Struct) ? type_node.type_index() : TypeIndex{};
				return makeExprResult(type_node.type(), SizeInBits{static_cast<int>(type_node.size_in_bits())}, IrOperand{StringTable::getOrInternStringHandle(qualifiedIdNode.name())}, type_index, PointerDepth{}, ValueStorage::ContainsData);
			}
		}

		if (found_symbol->is<VariableDeclarationNode>()) {
			const auto& var_decl_node = found_symbol->as<VariableDeclarationNode>();
			const auto& decl_node = var_decl_node.declaration_node().as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Namespace-scoped variables are always global
			// Generate GlobalLoad for namespace-qualified global variable
			TempVar result_temp = var_counter.next();
				bool is_array_type = decl_node.is_array() || type_node.is_array();
				bool is_ptr_or_ref = type_node.is_pointer() || type_node.is_reference() || type_node.is_function_pointer();
				int size_bits = (is_array_type || is_ptr_or_ref) ? 64 : static_cast<int>(type_node.size_in_bits());
			GlobalLoadOp op;
			op.result.setType(type_node.category());
			op.result.ir_type = toIrType(type_node.type());
			op.result.size_in_bits = SizeInBits{static_cast<int>(size_bits)};
			op.result.value = result_temp;
			// Use fully qualified name (ns::value) to match the global variable symbol
			op.global_name = gNamespaceRegistry.buildQualifiedIdentifier(
				qualifiedIdNode.namespace_handle(),
				StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
				op.is_array = is_array_type;
				StringHandle saved_global_name = op.global_name;
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));
				if (!is_array_type) {
					setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(
						LValueInfo(LValueInfo::Kind::Global, saved_global_name),
						type_node.category(), size_bits));
				}

			// Return the temp variable that will hold the loaded value
			// For pointers, return 64 bits (pointer size)
			TypeIndex type_index = (type_node.category() == TypeCategory::Struct) ? type_node.type_index() : TypeIndex{};
			return makeExprResult(type_node.type(), SizeInBits{size_bits}, IrOperand{result_temp}, type_index, PointerDepth{}, ValueStorage::ContainsData);
		}

		if (found_symbol->is<FunctionDeclarationNode>()) {
			// This is a function - just return the name for function calls
			// The actual function call handling is done elsewhere
			return makeExprResult(Type::Function, SizeInBits{64}, IrOperand{StringTable::getOrInternStringHandle(qualifiedIdNode.name())}, TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
		}

		// If we get here, the symbol is not a supported type
		throw InternalError("Qualified identifier is not a supported type");
		return ExprResult{};
	}

	ExprResult
		AstToIr::generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode) {
		// Generate IR for numeric literal using the actual type from the literal
		// Check if it's a floating-point type
		if (is_floating_point_type(typeToCategory(numericLiteralNode.type()))) {
			// For floating-point literals, the value is stored as double
			return makeExprResult(numericLiteralNode.type(), SizeInBits{static_cast<int>(numericLiteralNode.sizeInBits())}, std::get<double>(numericLiteralNode.value()), TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
		} else {
			// For integer literals, the value is stored as unsigned long long
			return makeExprResult(numericLiteralNode.type(), SizeInBits{static_cast<int>(numericLiteralNode.sizeInBits())}, std::get<unsigned long long>(numericLiteralNode.value()), TypeIndex{}, PointerDepth{}, ValueStorage::ContainsData);
		}
	}
