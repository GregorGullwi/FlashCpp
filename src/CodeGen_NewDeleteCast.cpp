#include "CodeGen.h"

	std::vector<IrOperand> AstToIr::generateNewExpressionIr(const NewExpressionNode& newExpr) {
		if (!newExpr.type_node().is<TypeSpecifierNode>()) {
			FLASH_LOG(Codegen, Error, "New expression type node is not a TypeSpecifierNode");
			return {};
		}
		
		const TypeSpecifierNode& type_spec = newExpr.type_node().as<TypeSpecifierNode>();
		Type type = type_spec.type();
		int size_in_bits = static_cast<int>(type_spec.size_in_bits());
		int pointer_depth = static_cast<int>(type_spec.pointer_depth());

		// Create a temporary variable for the result (pointer to allocated memory)
		TempVar result_var = var_counter.next();
		auto emit_scalar_new_initializer = [&](TempVar pointer_var) {
			constexpr size_t kInitOperandCount = 3;  // [type, size_in_bits, value]
			if (type == Type::Struct || newExpr.constructor_args().size() == 0) {
				return;
			}

			const auto& ctor_args = newExpr.constructor_args();
			if (ctor_args.size() > 1) {
				FLASH_LOG(Codegen, Warning, "Scalar new initializer has extra arguments; using first");
			}

			auto init_operands = visitExpressionNode(ctor_args[0].as<ExpressionNode>());
			if (init_operands.size() >= kInitOperandCount) {
				TypedValue init_value = toTypedValue(init_operands);
				emitDereferenceStore(init_value, type, size_in_bits, pointer_var, Token());
			} else {
				FLASH_LOG(Codegen, Warning, "Scalar new initializer returned insufficient operands");
			}
		};

		// Check if this is an array allocation (with or without placement)
		if (newExpr.is_array()) {
			// Array allocation: new Type[size] or placement array: new (addr) Type[size]
			// Evaluate the size expression
			if (!newExpr.size_expr().has_value()) {
				FLASH_LOG(Codegen, Error, "Array new without size expression");
				return {};
			}
			if (!newExpr.size_expr()->is<ExpressionNode>()) {
				FLASH_LOG(Codegen, Error, "Array size is not an ExpressionNode");
				return {};
			}
			
			auto size_operands = visitExpressionNode(newExpr.size_expr()->as<ExpressionNode>());

			// Check if this is placement array new
			if (newExpr.placement_address().has_value()) {
				// Placement array new: new (address) Type[size]
				// Check that placement_address is an ExpressionNode
				if (!newExpr.placement_address()->is<ExpressionNode>()) {
					FLASH_LOG(Codegen, Error, "Placement address is not an ExpressionNode");
					return {};
				}
				
				auto address_operands = visitExpressionNode(newExpr.placement_address()->as<ExpressionNode>());

				// Create PlacementNewOp for array
				PlacementNewOp op;
				op.result = result_var;
				op.type = type;
				op.size_in_bytes = size_in_bits / 8;
				op.pointer_depth = pointer_depth;
				// Convert IrOperand to IrValue
				if (address_operands.size() < 3) {
					FLASH_LOG(Codegen, Error, "Placement address operands insufficient (expected 3, got ", address_operands.size(), ")");
					return {};
				}
				op.address = toIrValue(address_operands[2]);

				ir_.addInstruction(IrInstruction(IrOpcode::PlacementNew, std::move(op), Token()));
				
				// Handle array initializers for placement new arrays
				// Initialize each array element with the provided initializers
				const auto& array_inits = newExpr.constructor_args();
				if (array_inits.size() > 0) {
					// For struct types, call constructor for each element
					if (type == Type::Struct) {
						TypeIndex type_index = type_spec.type_index();
						if (type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index];
							if (type_info.struct_info_) {
								const StructTypeInfo* struct_info = type_info.struct_info_.get();
								size_t element_size = struct_info->total_size;
								
								// Generate initialization for each element
								for (size_t i = 0; i < array_inits.size(); ++i) {
									const ASTNode& init = array_inits[i];
									
									// Skip if the initializer is not supported
									if (!init.is<InitializerListNode>() && !init.is<ExpressionNode>()) {
										FLASH_LOG(Codegen, Warning, "Unsupported array initializer type, skipping element ", i);
										continue;
									}
									
									// Calculate offset for this element: base_pointer + i * element_size
									TempVar element_ptr = var_counter.next();
									
									// Generate: element_ptr = result_var + (i * element_size)
									BinaryOp offset_op{
										.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
										.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(i * element_size)},
										.result = element_ptr,
									};
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(offset_op), Token()));
									
									// Check if initializer is a brace initializer list
									if (init.is<InitializerListNode>()) {
										const InitializerListNode& init_list = init.as<InitializerListNode>();
										
										// If struct has a constructor, call it with initializer list elements
										if (struct_info->hasAnyConstructor()) {
											ConstructorCallOp ctor_op;
											ctor_op.struct_name = type_info.name();
											ctor_op.object = element_ptr;
											ctor_op.is_heap_allocated = true;
											
											// Add each initializer as a constructor argument
											for (const auto& elem_init : init_list.initializers()) {
												// Safety check: ensure elem_init is an ExpressionNode
												if (!elem_init.is<ExpressionNode>()) {
													FLASH_LOG(Codegen, Warning, "Element initializer is not an ExpressionNode, skipping");
													continue;
												}
												
												auto arg_operands = visitExpressionNode(elem_init.as<ExpressionNode>());
												if (arg_operands.size() >= 3) {
													TypedValue tv = toTypedValue(arg_operands);
													ctor_op.arguments.push_back(std::move(tv));
												}
											}
											
											ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));
										}
									} else if (init.is<ExpressionNode>()) {
										// Handle direct expression initializer
										FLASH_LOG(Codegen, Warning, "Array element initialized with expression, not initializer list");
									} else {
										FLASH_LOG(Codegen, Warning, "Unexpected array initializer type");
									}
								}
							}
						}
					} else {
						// For primitive types, initialize each element
						size_t element_size = size_in_bits / 8;
						
						for (size_t i = 0; i < array_inits.size(); ++i) {
							const ASTNode& init = array_inits[i];
							
							if (init.is<ExpressionNode>()) {
								// Calculate offset for this element
								TempVar element_ptr = var_counter.next();
								
								// Generate: element_ptr = result_var + (i * element_size)
								BinaryOp offset_op{
									.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
									.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(i * element_size)},
									.result = element_ptr,
								};
								ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(offset_op), Token()));
								
								// Evaluate the initializer expression
								auto init_operands = visitExpressionNode(init.as<ExpressionNode>());
								if (init_operands.size() >= 3) {
									TypedValue init_value = toTypedValue(init_operands);
									emitDereferenceStore(init_value, type, size_in_bits, element_ptr, Token());
								}
							}
						}
					}
				}
			} else {
				// Regular heap-allocated array: new Type[size]
				// Create HeapAllocArrayOp
				HeapAllocArrayOp op;
				op.result = result_var;
				op.type = type;
				op.size_in_bytes = size_in_bits / 8;
				op.pointer_depth = pointer_depth;
				// Convert IrOperand to IrValue for count
				if (size_operands.size() < 3) {
					FLASH_LOG(Codegen, Error, "Array size operands insufficient (expected 3, got ", size_operands.size(), ")");
					return {};
				}
				IrValue count_value = op.count = toIrValue(size_operands[2]);

				// Check if struct type needs a cookie (has destructor)
				bool needs_ctor_loop = false;
				const StructTypeInfo* array_struct_info = nullptr;
				StringHandle array_struct_name_handle{};
				if (type == Type::Struct) {
					TypeIndex type_index = type_spec.type_index();
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						if (type_info.struct_info_ && type_info.struct_info_->hasAnyConstructor()) {
							array_struct_info = type_info.struct_info_.get();
							array_struct_name_handle = type_info.name();
							needs_ctor_loop = true;
							op.needs_cookie = type_info.struct_info_->hasDestructor();
						}
					}
				}

				ir_.addInstruction(IrInstruction(IrOpcode::HeapAllocArray, std::move(op), Token()));
				
				// Handle array initializers for heap-allocated arrays
				const auto& array_inits = newExpr.constructor_args();
				if (array_inits.size() > 0) {
					// For struct types, call constructor for each element
					if (type == Type::Struct) {
						TypeIndex type_index = type_spec.type_index();
						if (type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index];
							if (type_info.struct_info_) {
								const StructTypeInfo* struct_info = type_info.struct_info_.get();
								size_t element_size = struct_info->total_size;
								
								for (size_t i = 0; i < array_inits.size(); ++i) {
									const ASTNode& init = array_inits[i];
									
									// Skip if the initializer is not supported
									if (!init.is<InitializerListNode>() && !init.is<ExpressionNode>()) {
										FLASH_LOG(Codegen, Warning, "Unsupported array initializer type in heap array, skipping element ", i);
										continue;
									}
									
									TempVar element_ptr = var_counter.next();
									BinaryOp offset_op{
										.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
										.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(i * element_size)},
										.result = element_ptr,
									};
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(offset_op), Token()));
									
									if (init.is<InitializerListNode>() && struct_info->hasAnyConstructor()) {
										const InitializerListNode& init_list = init.as<InitializerListNode>();
										ConstructorCallOp ctor_op;
										ctor_op.struct_name = type_info.name();
										ctor_op.object = element_ptr;
										ctor_op.is_heap_allocated = true;
										
										for (const auto& elem_init : init_list.initializers()) {
											// Safety check: ensure elem_init is an ExpressionNode
											if (!elem_init.is<ExpressionNode>()) {
												FLASH_LOG(Codegen, Warning, "Element initializer in heap array is not an ExpressionNode, skipping");
												continue;
											}
											
											auto arg_operands = visitExpressionNode(elem_init.as<ExpressionNode>());
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
					} else {
						// For primitive types, initialize each element
						size_t element_size = size_in_bits / 8;
						for (size_t i = 0; i < array_inits.size(); ++i) {
							const ASTNode& init = array_inits[i];
							if (init.is<ExpressionNode>()) {
								TempVar element_ptr = var_counter.next();
								BinaryOp offset_op{
									.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
									.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(i * element_size)},
									.result = element_ptr,
								};
								ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(offset_op), Token()));
								
								auto init_operands = visitExpressionNode(init.as<ExpressionNode>());
								if (init_operands.size() >= 3) {
									TypedValue init_value = toTypedValue(init_operands);
									emitDereferenceStore(init_value, type, size_in_bits, element_ptr, Token());
								}
							}
						}
					}
				} else if (needs_ctor_loop && array_struct_info) {
					// No explicit initializers: emit a loop calling the default constructor for each element
					static size_t new_array_counter = 0;
					size_t loop_id = new_array_counter++;
					size_t elem_sz = array_struct_info->total_size;

					auto loop_start = StringTable::createStringHandle(StringBuilder().append("new_arr_start_").append(loop_id));
					auto loop_end   = StringTable::createStringHandle(StringBuilder().append("new_arr_end_").append(loop_id));

					// i_var = 0
					TempVar i_var = var_counter.next();
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, AssignmentOp{
						.result = i_var,
						.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = i_var},
						.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = 0ULL},
					}, Token()));

					ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start}, Token()));

					// cmp = (i_var < count)
					TempVar cmp_var = var_counter.next();
					ir_.addInstruction(IrInstruction(IrOpcode::UnsignedLessThan, BinaryOp{
						.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = i_var},
						.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = count_value},
						.result = cmp_var,
					}, Token()));

					CondBranchOp cond;
					cond.label_true = loop_start;  // placeholder - will immediately follow with body inline
					cond.label_false = loop_end;
					cond.condition = TypedValue{.type = Type::Bool, .size_in_bits = 1, .value = cmp_var};
					// We use a body label right after the branch
					auto loop_body = StringTable::createStringHandle(StringBuilder().append("new_arr_body_").append(loop_id));
					cond.label_true = loop_body;
					ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond), Token()));

					ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body}, Token()));

					// offset_var = i_var * elem_sz
					TempVar offset_var = var_counter.next();
					ir_.addInstruction(IrInstruction(IrOpcode::Multiply, BinaryOp{
						.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = i_var},
						.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(elem_sz)},
						.result = offset_var,
					}, Token()));

					// elem_ptr = result_var + offset_var
					TempVar elem_ptr = var_counter.next();
					ir_.addInstruction(IrInstruction(IrOpcode::Add, BinaryOp{
						.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
						.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = offset_var},
						.result = elem_ptr,
					}, Token()));

					// Call default constructor
					ConstructorCallOp ctor_op;
					ctor_op.struct_name = array_struct_name_handle;
					ctor_op.object = elem_ptr;
					ctor_op.is_heap_allocated = true;
					ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));

					// i_var = i_var + 1  (write back to same TempVar slot)
					ir_.addInstruction(IrInstruction(IrOpcode::Add, BinaryOp{
						.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = i_var},
						.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = 1ULL},
						.result = i_var,
					}, Token()));

					ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start}, Token()));
					ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end}, Token()));
				}
			}
		} else if (newExpr.placement_address().has_value()) {
			// Single object placement new: new (address) Type or new (address) Type(args)
			// Evaluate the placement address expression
			auto address_operands = visitExpressionNode(newExpr.placement_address()->as<ExpressionNode>());

			// Create PlacementNewOp
			PlacementNewOp op;
			op.result = result_var;
			op.type = type;
			op.size_in_bytes = size_in_bits / 8;
			op.pointer_depth = pointer_depth;
			// Convert IrOperand to IrValue
			if (address_operands.size() < 3) {
				FLASH_LOG(Codegen, Error, "Placement address operands insufficient for single object (expected 3, got ", address_operands.size(), ")");
				return {};
			}
			op.address = toIrValue(address_operands[2]);

			ir_.addInstruction(IrInstruction(IrOpcode::PlacementNew, std::move(op), Token()));

			// If this is a struct type with a constructor, generate constructor call
			if (type == Type::Struct) {
				TypeIndex type_index = type_spec.type_index();
				if (type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_index];
					if (type_info.struct_info_) {
						// Check if this is an abstract class
						if (type_info.struct_info_->is_abstract) {
							std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name() << "'\n";
							throw CompileError("Cannot instantiate abstract class");
						}

						if (type_info.struct_info_->hasAnyConstructor()) {
							// Generate constructor call on the placement address
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name();
							ctor_op.object = result_var;
							ctor_op.is_heap_allocated = true;  // Object is at pointer location (placement new provides address)
							
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

			emit_scalar_new_initializer(result_var);
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
							std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name() << "'\n";
							throw CompileError("Cannot instantiate abstract class");
						}

						if (type_info.struct_info_->hasAnyConstructor()) {
							// Generate constructor call on the newly allocated object
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name();
							ctor_op.object = result_var;
							ctor_op.is_heap_allocated = true;  // Object is at pointer location (new allocates and returns pointer)

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

			emit_scalar_new_initializer(result_var);
		}
		
		// Return pointer to allocated memory
		// The result is a pointer, so we return it with pointer_depth + 1
		return { type, size_in_bits, result_var, 0ULL };
	}

	std::vector<IrOperand> AstToIr::generateDeleteExpressionIr(const DeleteExpressionNode& deleteExpr) {
		// Evaluate the expression to get the pointer to delete
		auto ptr_operands = visitExpressionNode(deleteExpr.expr().as<ExpressionNode>());

		// Get the pointer type
		Type ptr_type = std::get<Type>(ptr_operands[0]);

		// Convert IrOperand to IrValue
		IrValue ptr_value = toIrValue(ptr_operands[2]);

		// Check if we need to call destructor (for struct types with a user-defined destructor).
		// ptr_operands[3] is the type_index when the expression type is Type::Struct (index 0 is invalid).
		// The 4th operand (index 3) is present when the expression type returns a struct type_index.
		if (ptr_type == Type::Struct && !deleteExpr.is_array() &&
		ptr_operands.size() >= 4 && std::holds_alternative<unsigned long long>(ptr_operands[3])) {
			unsigned long long type_idx_val = std::get<unsigned long long>(ptr_operands[3]);
			// type_idx_val == 0 means no type information (invalid/non-struct pointer)
			if (type_idx_val > 0 && type_idx_val < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_idx_val];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info && struct_info->hasDestructor()) {
					DestructorCallOp dtor_op;
					dtor_op.struct_name = type_info.name();
					dtor_op.object_is_pointer = true;
					if (std::holds_alternative<TempVar>(ptr_value)) {
						dtor_op.object = std::get<TempVar>(ptr_value);
					} else if (std::holds_alternative<StringHandle>(ptr_value)) {
						dtor_op.object = std::get<StringHandle>(ptr_value);
					} else {
						// ptr_value is a literal (unsigned long long or double) - skip destructor call
						// ptr_value is a literal (unsigned long long or double) - skip destructor call
					}
					ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), Token()));
				}
			}
		}

		if (deleteExpr.is_array()) {
			// Array delete: call destructor for each element if the type has one, using cookie
			bool has_dtor_loop = false;
			if (ptr_type == Type::Struct &&
			ptr_operands.size() >= 4 && std::holds_alternative<unsigned long long>(ptr_operands[3])) {
				unsigned long long type_idx_val = std::get<unsigned long long>(ptr_operands[3]);
				if (type_idx_val > 0 && type_idx_val < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_idx_val];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && struct_info->hasDestructor()) {
						has_dtor_loop = true;
						size_t elem_sz = struct_info->total_size;

						// Read count from cookie: raw_ptr = ptr - 8
						TempVar raw_ptr = var_counter.next();
						ir_.addInstruction(IrInstruction(IrOpcode::Subtract, BinaryOp{
							.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = ptr_value},
							.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = 8ULL},
							.result = raw_ptr,
						}, Token()));

						// count_var = *raw_ptr  (load 64-bit cookie)
						TempVar count_var = var_counter.next();
						ir_.addInstruction(IrInstruction(IrOpcode::Dereference, DereferenceOp{
							.result = count_var,
							.pointer = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = raw_ptr, .pointer_depth = 1},
						}, Token()));

						// Emit reverse-order destructor loop: i = count-1 down to 0
						static size_t del_array_counter = 0;
						size_t loop_id = del_array_counter++;

						auto loop_start = StringTable::createStringHandle(StringBuilder().append("del_arr_start_").append(loop_id));
						auto loop_body  = StringTable::createStringHandle(StringBuilder().append("del_arr_body_").append(loop_id));
						auto loop_end   = StringTable::createStringHandle(StringBuilder().append("del_arr_end_").append(loop_id));

						// i_var = count_var  (will decrement before use, so start at count)
						TempVar i_var = var_counter.next();
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, AssignmentOp{
							.result = i_var,
							.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = i_var},
							.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = count_var},
						}, Token()));

						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start}, Token()));

						// if i_var == 0 goto loop_end
						TempVar cmp_var = var_counter.next();
						ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, BinaryOp{
							.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = i_var},
							.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = 0ULL},
							.result = cmp_var,
						}, Token()));
						CondBranchOp cond;
						cond.label_true  = loop_body;
						cond.label_false = loop_end;
						cond.condition   = TypedValue{.type = Type::Bool, .size_in_bits = 1, .value = cmp_var};
						ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond), Token()));

						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body}, Token()));

						// i_var = i_var - 1  (decrement first, so index runs count-1..0)
						ir_.addInstruction(IrInstruction(IrOpcode::Subtract, BinaryOp{
							.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = i_var},
							.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = 1ULL},
							.result = i_var,
						}, Token()));

						// elem_ptr = ptr + i_var * elem_sz
						TempVar offset_var = var_counter.next();
						ir_.addInstruction(IrInstruction(IrOpcode::Multiply, BinaryOp{
							.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = i_var},
							.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(elem_sz)},
							.result = offset_var,
						}, Token()));
						TempVar elem_ptr = var_counter.next();
						ir_.addInstruction(IrInstruction(IrOpcode::Add, BinaryOp{
							.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = ptr_value},
							.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = offset_var},
							.result = elem_ptr,
						}, Token()));

						DestructorCallOp dtor_op;
						dtor_op.struct_name      = type_info.name();
						dtor_op.object           = elem_ptr;
						dtor_op.object_is_pointer = true;
						ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), Token()));

						ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start}, Token()));
						ir_.addInstruction(IrInstruction(IrOpcode::Label,  LabelOp{.label_name = loop_end},      Token()));

						// Free using the raw (cookie) pointer — raw_ptr already points to start of allocation
						HeapFreeArrayOp free_op;
						free_op.pointer    = raw_ptr;
						free_op.has_cookie = false;
						ir_.addInstruction(IrInstruction(IrOpcode::HeapFreeArray, std::move(free_op), Token()));
					}
				}
			}
			if (!has_dtor_loop) {
				HeapFreeArrayOp op;
				op.pointer = ptr_value;
				ir_.addInstruction(IrInstruction(IrOpcode::HeapFreeArray, std::move(op), Token()));
			}
		} else {
			HeapFreeOp op;
			op.pointer = ptr_value;
			ir_.addInstruction(IrInstruction(IrOpcode::HeapFree, std::move(op), Token()));
		}

		// delete is a statement, not an expression, so return empty
		return {};
	}

	std::variant<StringHandle, TempVar> AstToIr::extractBaseOperand(
		const std::vector<IrOperand>& expr_operands,
		TempVar fallback_var,
		const char* cast_name) {
		
		std::variant<StringHandle, TempVar> base;
		if (std::holds_alternative<StringHandle>(expr_operands[2])) {
			base = std::get<StringHandle>(expr_operands[2]);
		} else if (std::holds_alternative<TempVar>(expr_operands[2])) {
			base = std::get<TempVar>(expr_operands[2]);
		} else {
			FLASH_LOG_FORMAT(Codegen, Warning, "{}:  unexpected value type in expr_operands[2]", cast_name);
			base = fallback_var;
		}
		return base;
	}

	void AstToIr::markReferenceMetadata(
		const std::vector<IrOperand>& expr_operands,
		TempVar result_var,
		Type target_type,
		int target_size,
		bool is_rvalue_ref,
		const char* cast_name) {
		
		auto base = extractBaseOperand(expr_operands, result_var, cast_name);
		LValueInfo lvalue_info(LValueInfo::Kind::Direct, base, 0);
		
		if (is_rvalue_ref) {
			FLASH_LOG_FORMAT(Codegen, Debug, "{} to rvalue reference: marking as xvalue", cast_name);
			setTempVarMetadata(result_var, TempVarMetadata::makeXValue(lvalue_info, target_type, target_size));
		} else {
			FLASH_LOG_FORMAT(Codegen, Debug, "{} to lvalue reference: marking as lvalue", cast_name);
			setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info, target_type, target_size));
		}
	}

	void AstToIr::generateAddressOfForReference(
		const std::variant<StringHandle, TempVar>& base,
		TempVar result_var,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name) {
		
		if (std::holds_alternative<StringHandle>(base)) {
			AddressOfOp addr_op;
			addr_op.result = result_var;
			addr_op.operand.type = target_type;
			addr_op.operand.size_in_bits = target_size;
			addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
			addr_op.operand.value = std::get<StringHandle>(base);
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), token));
		} else {
			// source is TempVar - it already holds an address, copy it to result_var
			FLASH_LOG_FORMAT(Codegen, Debug, "{}: source is TempVar (address already computed), copying to result", cast_name);
			const TempVar& source_var = std::get<TempVar>(base);
			AssignmentOp assign_op;
			assign_op.result = result_var;
			assign_op.lhs = TypedValue{target_type, 64, result_var};  // 64-bit pointer dest
			assign_op.rhs = TypedValue{target_type, 64, source_var};  // 64-bit pointer source
			assign_op.is_pointer_store = false;
			assign_op.dereference_rhs_references = false;  // Don't dereference - just copy the pointer!
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), token));
		}
	}

	std::vector<IrOperand> AstToIr::handleRValueReferenceCast(
		const std::vector<IrOperand>& expr_operands,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name) {
		
		// Create a new TempVar to hold the xvalue result
		TempVar result_var = var_counter.next();
		
		// Extract base operand and mark as xvalue
		auto base = extractBaseOperand(expr_operands, result_var, cast_name);
		LValueInfo lvalue_info(LValueInfo::Kind::Direct, base, 0);
		FLASH_LOG_FORMAT(Codegen, Debug, "{} to rvalue reference: marking as xvalue", cast_name);
		setTempVarMetadata(result_var, TempVarMetadata::makeXValue(lvalue_info, target_type, target_size));
		
		// Generate AddressOf operation if needed
		generateAddressOfForReference(base, result_var, target_type, target_size, token, cast_name);
		
		// Return the xvalue with reference semantics (64-bit pointer size)
		return { target_type, 64, result_var, 0ULL };
	}

	std::vector<IrOperand> AstToIr::handleLValueReferenceCast(
		const std::vector<IrOperand>& expr_operands,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name) {
		
		// Create a new TempVar to hold the lvalue result
		TempVar result_var = var_counter.next();
		
		// Extract base operand and mark as lvalue
		auto base = extractBaseOperand(expr_operands, result_var, cast_name);
		LValueInfo lvalue_info(LValueInfo::Kind::Direct, base, 0);
		FLASH_LOG_FORMAT(Codegen, Debug, "{} to lvalue reference", cast_name);
		setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info, target_type, target_size));
		
		// Generate AddressOf operation if needed
		generateAddressOfForReference(base, result_var, target_type, target_size, token, cast_name);
		
		// Return the lvalue with reference semantics (64-bit pointer size)
		return { target_type, 64, result_var, 0ULL };
	}

	std::vector<IrOperand> AstToIr::generateStaticCastIr(const StaticCastNode& staticCastNode) {
		// Get the target type from the type specifier first
		const auto& target_type_node = staticCastNode.target_type().as<TypeSpecifierNode>();
		Type target_type = target_type_node.type();
		int target_size = static_cast<int>(target_type_node.size_in_bits());
		size_t target_pointer_depth = target_type_node.pointer_depth();
		
		// For reference casts (both lvalue and rvalue), we need the address of the expression,
		// not its loaded value. Use LValueAddress context to get the address without dereferencing.
		ExpressionContext eval_context = ExpressionContext::Load;
		if (target_type_node.is_reference()) {
			eval_context = ExpressionContext::LValueAddress;
		}
		
		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(staticCastNode.expr().as<ExpressionNode>(), eval_context);

		// Get the source type
		Type source_type = std::get<Type>(expr_operands[0]);
		int source_size = std::get<int>(expr_operands[1]);

		// Special handling for rvalue reference casts: static_cast<T&&>(expr)
		// This produces an xvalue - has identity but can be moved from
		// Equivalent to std::move
		if (target_type_node.is_rvalue_reference()) {
			return handleRValueReferenceCast(expr_operands, target_type, target_size, staticCastNode.cast_token(), "static_cast");
		}

		// Special handling for lvalue reference casts: static_cast<T&>(expr)
		// This produces an lvalue
		if (target_type_node.is_lvalue_reference()) {
			return handleLValueReferenceCast(expr_operands, target_type, target_size, staticCastNode.cast_token(), "static_cast");
		}

		// Special handling for pointer casts (e.g., char* to double*, int* to void*, etc.)
		// Pointer casts should NOT generate type conversions - they're just reinterpretations
		if (target_pointer_depth > 0) {
			// Target is a pointer type - this is a pointer cast, not a value conversion
			// Pointer casts are bitcasts - the value stays the same, only the type changes
			// Return the expression with the target pointer type (char64, int64, etc.)
			// All pointers are 64-bit on x64, so size should be 64
			FLASH_LOG_FORMAT(Codegen, Debug, "[PTR_CAST_DEBUG] Pointer cast: source={}, target={}, target_ptr_depth={}", 
				static_cast<int>(source_type), static_cast<int>(target_type), target_pointer_depth);
			return { target_type, 64, expr_operands[2], 0ULL };
		}

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
			return { target_type, target_size, expr_operands[2], 0ULL };
		}

		// For float-to-int conversions, generate FloatToInt IR
		if (is_floating_point_type(source_type) && is_integer_type(target_type)) {
			TempVar result_temp = var_counter.next();
			// Extract IrValue from IrOperand - visitExpressionNode returns [type, size, value]
			// where value is TempVar, string_view, unsigned long long, or double
			IrValue from_value = std::visit([](auto&& arg) -> IrValue {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
				std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
					return arg;
				} else {
					// This shouldn't happen for expression values, but default to 0
					throw InternalError("Couldn't match IrValue to a known type");
					return 0ULL;
				}
			}, expr_operands[2]);
			
			TypeConversionOp op{
				.result = result_temp,
				.from = TypedValue{source_type, source_size, from_value},
				.to_type = target_type,
				.to_size_in_bits = target_size
			};
			ir_.addInstruction(IrOpcode::FloatToInt, std::move(op), staticCastNode.cast_token());
			return { target_type, target_size, result_temp, 0ULL };
		}

		// For int-to-float conversions, generate IntToFloat IR
		if (is_integer_type(source_type) && is_floating_point_type(target_type)) {
			TempVar result_temp = var_counter.next();
			IrValue from_value = std::visit([](auto&& arg) -> IrValue {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
				std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
					return arg;
				} else {
					throw InternalError("Couldn't match IrValue to a known type");
					return 0ULL;
				}
			}, expr_operands[2]);
			
			TypeConversionOp op{
				.result = result_temp,
				.from = TypedValue{source_type, source_size, from_value},
				.to_type = target_type,
				.to_size_in_bits = target_size
			};
			ir_.addInstruction(IrOpcode::IntToFloat, std::move(op), staticCastNode.cast_token());
			return { target_type, target_size, result_temp, 0ULL };
		}

		// For float-to-float conversions (float <-> double), generate FloatToFloat IR
		if (is_floating_point_type(source_type) && is_floating_point_type(target_type) && source_type != target_type) {
			TempVar result_temp = var_counter.next();
			IrValue from_value = std::visit([](auto&& arg) -> IrValue {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
				std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
					return arg;
				} else {
					throw InternalError("Couldn't match IrValue to a known type");
					return 0ULL;
				}
			}, expr_operands[2]);
			
			TypeConversionOp op{
				.result = result_temp,
				.from = TypedValue{source_type, source_size, from_value},
				.to_type = target_type,
				.to_size_in_bits = target_size
			};
			ir_.addInstruction(IrOpcode::FloatToFloat, std::move(op), staticCastNode.cast_token());
			return { target_type, target_size, result_temp, 0ULL };
		}

		// For integer-to-bool conversions, normalize to 0 or 1 via != 0
		// e.g. static_cast<bool>(42) must produce 1, not 42
		if (is_integer_type(source_type) && target_type == Type::Bool) {
			TempVar result_temp = var_counter.next();
			BinaryOp bin_op{
				.lhs = toTypedValue(expr_operands),
				.rhs = TypedValue{source_type, source_size, 0ULL},
				.result = result_temp,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, std::move(bin_op), staticCastNode.cast_token()));
			return { Type::Bool, 8, result_temp, 0ULL };
		}

		// For float-to-bool conversions, normalize to 0 or 1 via != 0.0
		if (is_floating_point_type(source_type) && target_type == Type::Bool) {
			TempVar result_temp = var_counter.next();
			BinaryOp bin_op{
				.lhs = toTypedValue(expr_operands),
				.rhs = TypedValue{source_type, source_size, 0.0},
				.result = result_temp,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::FloatNotEqual, std::move(bin_op), staticCastNode.cast_token()));
			return { Type::Bool, 8, result_temp, 0ULL };
		}

		// For numeric conversions, we might need to generate a conversion instruction
		// For now, just change the type metadata (works for most cases)
		return { target_type, target_size, expr_operands[2], 0ULL };
	}

	std::vector<IrOperand> AstToIr::generateTypeidIr(const TypeidNode& typeidNode) {
		// typeid returns a reference to const std::type_info
		// For polymorphic types, we need to get RTTI from the vtable
		// For non-polymorphic types, we return a compile-time constant

		TempVar result_temp = var_counter.next();

		if (typeidNode.is_type()) {
			// typeid(Type) - compile-time constant
			const auto& type_node = typeidNode.operand().as<TypeSpecifierNode>();

			// Get type information
			StringHandle type_name;
			if (type_node.type() == Type::Struct) {
				TypeIndex type_idx = type_node.type_index();
				if (type_idx < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_idx];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						type_name = struct_info->getName();
					}
				}
			}

			// Generate IR to get compile-time type_info
			TypeidOp op{
				.result = result_temp,
				.operand = type_name,  // Type name for RTTI lookup
				.is_type = true
			};
			ir_.addInstruction(IrOpcode::Typeid, std::move(op), typeidNode.typeid_token());
		}
		else {
			// typeid(expr) - may need runtime lookup for polymorphic types
			auto expr_operands = visitExpressionNode(typeidNode.operand().as<ExpressionNode>());

			// Extract IrValue from expression result
			std::variant<StringHandle, TempVar> operand_value;
			if (std::holds_alternative<TempVar>(expr_operands[2])) {
				operand_value = std::get<TempVar>(expr_operands[2]);
			} else if (std::holds_alternative<StringHandle>(expr_operands[2])) {
				operand_value = std::get<StringHandle>(expr_operands[2]);
			} else {
				// Shouldn't happen - typeid operand should be a variable
				operand_value = TempVar{0};
			}

			TypeidOp op{
				.result = result_temp,
				.operand = operand_value,  // Expression result
				.is_type = false
			};
			ir_.addInstruction(IrOpcode::Typeid, std::move(op), typeidNode.typeid_token());
		}

		// Return pointer to type_info (64-bit pointer)
		// Use void* type for now (Type::Void with pointer depth)
		return { Type::Void, 64, result_temp, 0ULL };
	}

	std::vector<IrOperand> AstToIr::generateDynamicCastIr(const DynamicCastNode& dynamicCastNode) {
		// dynamic_cast<Type>(expr) performs runtime type checking
		// Returns nullptr (for pointers) or throws bad_cast (for references) on failure

		// Get the target type first to determine evaluation context
		const auto& target_type_node = dynamicCastNode.target_type().as<TypeSpecifierNode>();

		// For reference casts (both lvalue and rvalue), we need the address of the expression,
		// not its loaded value. Use LValueAddress context to get the address without dereferencing.
		ExpressionContext eval_context = ExpressionContext::Load;
		if (target_type_node.is_reference()) {
			eval_context = ExpressionContext::LValueAddress;
		}

		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(dynamicCastNode.expr().as<ExpressionNode>(), eval_context);

		// Get target struct type information
		std::string target_type_name;
		if (target_type_node.type() == Type::Struct) {
			TypeIndex type_idx = target_type_node.type_index();
			if (type_idx < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_idx];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					target_type_name = StringTable::getStringView(struct_info->getName());
				}
			}
		}

		TempVar result_temp = var_counter.next();

		// Extract source pointer from expression result
		TempVar source_ptr;
		if (std::holds_alternative<TempVar>(expr_operands[2])) {
			source_ptr = std::get<TempVar>(expr_operands[2]);
		} else if (std::holds_alternative<StringHandle>(expr_operands[2])) {
			// For a named variable, load it into a temp first
			source_ptr = var_counter.next();
			StringHandle var_name_handle = std::get<StringHandle>(expr_operands[2]);
			
			// Generate assignment to load the variable into the temp
			AssignmentOp load_op;
			load_op.result = source_ptr;
			load_op.lhs = TypedValue{std::get<Type>(expr_operands[0]), std::get<int>(expr_operands[1]), source_ptr};
			load_op.rhs = TypedValue{std::get<Type>(expr_operands[0]), std::get<int>(expr_operands[1]), var_name_handle};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_op), dynamicCastNode.cast_token()));
		} else {
			source_ptr = TempVar{0};
		}

		// Generate dynamic_cast IR
		DynamicCastOp op{
			.result = result_temp,
			.source = source_ptr,
			.target_type_name = target_type_name,
			.is_reference = target_type_node.is_reference()
		};
		ir_.addInstruction(IrOpcode::DynamicCast, std::move(op), dynamicCastNode.cast_token());

		// Get result type and size for metadata and return value
		Type result_type = target_type_node.type();
		int result_size = static_cast<int>(target_type_node.size_in_bits());

		// For reference types, the result is a pointer (64 bits), not the struct size
		bool is_reference_cast = target_type_node.is_reference() || target_type_node.is_rvalue_reference();
		if (is_reference_cast) {
			result_size = 64;  // Reference is represented as a pointer
		}

		// Mark value category for reference types
		if (target_type_node.is_rvalue_reference()) {
			markReferenceMetadata(expr_operands, result_temp, result_type, result_size, true, "dynamic_cast");
		} else if (target_type_node.is_lvalue_reference()) {
			markReferenceMetadata(expr_operands, result_temp, result_type, result_size, false, "dynamic_cast");
		}

		// Return the casted pointer/reference
		return { result_type, result_size, result_temp, 0ULL };
	}

	std::vector<IrOperand> AstToIr::generateConstCastIr(const ConstCastNode& constCastNode) {
		// const_cast<Type>(expr) adds or removes const/volatile qualifiers
		// It doesn't change the actual value, just the type metadata
		
		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(constCastNode.expr().as<ExpressionNode>());
		
		// Get the target type from the type specifier
		const auto& target_type_node = constCastNode.target_type().as<TypeSpecifierNode>();
		Type target_type = target_type_node.type();
		int target_size = static_cast<int>(target_type_node.size_in_bits());
		
		// Special handling for rvalue reference casts: const_cast<T&&>(expr)
		if (target_type_node.is_rvalue_reference()) {
			return handleRValueReferenceCast(expr_operands, target_type, target_size, constCastNode.cast_token(), "const_cast");
		}
		
		// Special handling for lvalue reference casts: const_cast<T&>(expr)
		if (target_type_node.is_lvalue_reference()) {
			return handleLValueReferenceCast(expr_operands, target_type, target_size, constCastNode.cast_token(), "const_cast");
		}
		
		// const_cast doesn't modify the value, only the type's const/volatile qualifiers
		// For code generation purposes, we just return the expression with the new type metadata
		// The actual value/address remains the same
		return { target_type, target_size, expr_operands[2], 0ULL };
	}

	std::vector<IrOperand> AstToIr::generateReinterpretCastIr(const ReinterpretCastNode& reinterpretCastNode) {
		// reinterpret_cast<Type>(expr) reinterprets the bit pattern as a different type
		// It doesn't change the actual bits, just the type interpretation
		
		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(reinterpretCastNode.expr().as<ExpressionNode>());
		
		// Get the target type from the type specifier
		const auto& target_type_node = reinterpretCastNode.target_type().as<TypeSpecifierNode>();
		Type target_type = target_type_node.type();
		int target_size = static_cast<int>(target_type_node.size_in_bits());
		int target_pointer_depth = target_type_node.pointer_depth();
		
		// Special handling for rvalue reference casts: reinterpret_cast<T&&>(expr)
		if (target_type_node.is_rvalue_reference()) {
			return handleRValueReferenceCast(expr_operands, target_type, target_size, reinterpretCastNode.cast_token(), "reinterpret_cast");
		}
		
		// Special handling for lvalue reference casts: reinterpret_cast<T&>(expr)
		if (target_type_node.is_lvalue_reference()) {
			return handleLValueReferenceCast(expr_operands, target_type, target_size, reinterpretCastNode.cast_token(), "reinterpret_cast");
		}
		
		// reinterpret_cast reinterprets the bits without conversion
		// For code generation purposes, we just return the expression with the new type metadata
		// The actual bit pattern remains unchanged
		int result_size = (target_pointer_depth > 0) ? 64 : target_size;
		return { target_type, result_size, expr_operands[2], static_cast<unsigned long long>(target_pointer_depth) };
	}

