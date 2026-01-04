#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include "TemplateRegistry.h"
#include "ChunkedString.h"
#include "NameMangling.h"
#include "ConstExprEvaluator.h"
#include "OverloadResolution.h"
#include <type_traits>
#include <variant>
#include <vector>
#include <unordered_map>
#include <queue>
#include <unordered_set>
#include <assert.h>
#include <cstdint>
#include <typeinfo>
#include "IRConverter.h"
#include "Log.h"

class Parser;

// MSVC RTTI runtime structures (must match ObjFileWriter.h MSVC format):
// These are the actual structures that exist at runtime in the object file

// ??_R0 - Type Descriptor (runtime view)
struct RTTITypeDescriptor {
	const void* vtable;              // Pointer to type_info vtable (usually null)
	const void* spare;               // Reserved/spare pointer (unused)
	char name[1];                    // Variable-length mangled name (null-terminated)
};

// ??_R1 - Base Class Descriptor (runtime view)
struct RTTIBaseClassDescriptor {
	const RTTITypeDescriptor* type_descriptor;  // Pointer to base class type descriptor
	uint32_t num_contained_bases;    // Number of nested base classes
	int32_t mdisp;                   // Member displacement (offset in class)
	int32_t pdisp;                   // Vbtable displacement (-1 if not virtual base)
	int32_t vdisp;                   // Displacement inside vbtable (0 if not virtual base)
	uint32_t attributes;             // Flags (virtual, ambiguous, etc.)
};

// ??_R2 - Base Class Array (runtime view)
struct RTTIBaseClassArray {
	const RTTIBaseClassDescriptor* base_class_descriptors[1]; // Variable-length array
};

// ??_R3 - Class Hierarchy Descriptor (runtime view)
struct RTTIClassHierarchyDescriptor {
	uint32_t signature;              // Always 0
	uint32_t attributes;             // Bit flags (multiple inheritance, virtual inheritance, etc.)
	uint32_t num_base_classes;       // Number of base classes (including self)
	const RTTIBaseClassArray* base_class_array;  // Pointer to base class array
};

// ??_R4 - Complete Object Locator (runtime view)
struct RTTICompleteObjectLocator {
	uint32_t signature;              // 0 for 32-bit, 1 for 64-bit
	uint32_t offset;                 // Offset of this vtable in the complete class
	uint32_t cd_offset;              // Constructor displacement offset
	const RTTITypeDescriptor* type_descriptor;        // Pointer to type descriptor
	const RTTIClassHierarchyDescriptor* hierarchy;    // Pointer to class hierarchy
};

// Legacy RTTIInfo for backward compatibility with old simple format
struct RTTIInfo {
	uint64_t class_name_hash;
	uint64_t num_bases;
	RTTIInfo* base_ptrs[0];  // Flexible array member - base class RTTI pointers follow
	// Access via: auto** bases = reinterpret_cast<RTTIInfo**>((char*)this + 16);
};

// Note: Runtime helpers __dynamic_cast_check() and __dynamic_cast_throw_bad_cast()
// are now auto-generated as native x64 functions by the compiler when dynamic_cast is used.
// See IRConverter.h: emit_dynamic_cast_check_function() and emit_dynamic_cast_throw_function()

// Structure to hold lambda information for deferred generation
struct LambdaInfo {
	std::string_view closure_type_name;      // e.g., "__lambda_0" (persistent via StringBuilder)
	std::string_view operator_call_name;     // e.g., "__lambda_0_operator_call" (persistent via StringBuilder)
	std::string_view invoke_name;            // e.g., "__lambda_0_invoke" (persistent via StringBuilder)
	std::string_view conversion_op_name;     // e.g., "__lambda_0_conversion" (persistent via StringBuilder)
	Type return_type;
	int return_size;
	TypeIndex return_type_index = 0;    // Type index for struct/enum return types
	bool returns_reference = false;     // True if lambda returns a reference type (T& or T&&)
	std::vector<std::tuple<Type, int, int, std::string>> parameters;  // type, size, pointer_depth, name
	std::vector<ASTNode> parameter_nodes;  // Actual parameter AST nodes for symbol table
	ASTNode lambda_body;                // Copy of the lambda body
	std::vector<LambdaCaptureNode> captures;  // Copy of captures
	std::vector<ASTNode> captured_var_decls;  // Declarations of captured variables (for symbol table)
	size_t lambda_id;
	Token lambda_token;
	std::string_view enclosing_struct_name;  // Name of enclosing struct if lambda is in a member function
	TypeIndex enclosing_struct_type_index = 0;  // Type index of enclosing struct for [this] capture
	bool is_mutable = false;            // Whether the lambda is mutable (allows modifying captures)
	
	// Generic lambda support (lambdas with auto parameters)
	bool is_generic = false;                     // True if lambda has any auto parameters
	std::vector<size_t> auto_param_indices;      // Indices of parameters with auto type
	// Deduced types from call site - store full TypeSpecifierNode to preserve struct type_index and reference flags
	mutable std::vector<std::pair<size_t, TypeSpecifierNode>> deduced_auto_types;
	
	// Get deduced type for a parameter at given index, returns nullopt if not deduced
	std::optional<TypeSpecifierNode> getDeducedType(size_t param_index) const {
		for (const auto& [idx, type_node] : deduced_auto_types) {
			if (idx == param_index) {
				return type_node;
			}
		}
		return std::nullopt;
	}
	
	// Set deduced type for a parameter at given index
	void setDeducedType(size_t param_index, const TypeSpecifierNode& type_node) const {
		// Check if already set
		for (auto& [idx, stored_type] : deduced_auto_types) {
			if (idx == param_index) {
				stored_type = type_node;
				return;
			}
		}
		deduced_auto_types.push_back({param_index, type_node});
	}
};

// Expression evaluation context
// Determines how an expression should be evaluated
enum class ExpressionContext {
	Load,           // Evaluate and load the value (default, rvalue context)
	LValueAddress   // Evaluate to get the address without loading (lvalue context for assignment)
};

class AstToIr {
public:
	AstToIr() = delete;  // Require valid references
	AstToIr(SymbolTable& global_symbol_table, CompileContext& context, Parser& parser)
		: global_symbol_table_(&global_symbol_table), context_(&context), parser_(&parser) {
		// Generate static member declarations for template classes before processing AST
		generateStaticMemberDeclarations();
		// Generate trivial default constructors for structs that need them
		generateTrivialDefaultConstructors();
	}

	void visit(const ASTNode& node) {
		// Skip empty nodes (e.g., from forward declarations)
		if (!node.has_value()) {
			return;
		}

		if (node.is<FunctionDeclarationNode>()) {
			visitFunctionDeclarationNode(node.as<FunctionDeclarationNode>());
			// Clear function context after completing a top-level function
			current_function_name_ = StringHandle();
		}
		else if (node.is<ReturnStatementNode>()) {
			visitReturnStatementNode(node.as<ReturnStatementNode>());
		}
		else if (node.is<VariableDeclarationNode>()) {
			visitVariableDeclarationNode(node);
		}
		else if (node.is<StructuredBindingNode>()) {
			visitStructuredBindingNode(node);
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
		else if (node.is<TryStatementNode>()) {
			visitTryStatementNode(node.as<TryStatementNode>());
		}
		else if (node.is<ThrowStatementNode>()) {
			visitThrowStatementNode(node.as<ThrowStatementNode>());
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
			// Clear function context after completing a top-level constructor
			current_function_name_ = StringHandle();
		}
		else if (node.is<DestructorDeclarationNode>()) {
			visitDestructorDeclarationNode(node.as<DestructorDeclarationNode>());
			// Clear function context after completing a top-level destructor
			current_function_name_ = StringHandle();
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
		else if (node.is<ConceptDeclarationNode>()) {
			// Concept declarations don't generate code - they're compile-time constraints
			// Concepts are evaluated during template instantiation (constraint checking not yet implemented)
			return;
		}
		else if (node.is<RequiresExpressionNode>()) {
			// Requires expressions don't generate code - they're compile-time constraints
			// They are evaluated during constraint checking
			return;
		}
		else if (node.is<CompoundRequirementNode>()) {
			// Compound requirements don't generate code - they're compile-time constraints
			// They are part of requires expressions and evaluated during constraint checking
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
		// Generate lambdas, processing newly added ones as they appear.
		// Nested lambdas are collected during body generation and will be processed
		// in subsequent iterations of this loop.
		// Example: auto maker = []() { return [](int x) { return x; }; };
		//          When generating maker's body, the inner lambda is collected
		//          and will be processed in the next iteration.
		
		// Process until no new lambdas are added
		size_t processed_count = 0;
		while (processed_count < collected_lambdas_.size()) {
			// Process from the end (newly added lambdas) backwards
			size_t current_size = collected_lambdas_.size();
			for (size_t i = current_size; i > processed_count; --i) {
				// CRITICAL: Copy the LambdaInfo before calling generateLambdaFunctions
				// because that function may push new lambdas which can reallocate the vector
				// and invalidate any references
				LambdaInfo lambda_info = collected_lambdas_[i - 1];
				// Skip if this lambda has already been generated (prevents duplicate definitions)
				if (generated_lambda_ids_.find(lambda_info.lambda_id) != generated_lambda_ids_.end()) {
					continue;
				}
				generated_lambda_ids_.insert(lambda_info.lambda_id);
				generateLambdaFunctions(lambda_info);
			}
			processed_count = current_size;
		}
	}
	
	// Generate all collected local struct member functions
	void generateCollectedLocalStructMembers() {
		for (const auto& member_info : collected_local_struct_members_) {
			// Temporarily restore context
			StringHandle saved_function = current_function_name_;
			current_struct_name_ = member_info.struct_name;
			current_function_name_ = member_info.enclosing_function_name;
			
			// Visit the member function
			visit(member_info.member_function_node);
			
			// Restore
			current_function_name_ = saved_function;
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
		auto append_bytes = [](unsigned long long value, int size_in_bits, std::vector<char>& target) {
			size_t byte_count = size_in_bits / 8;
			for (size_t i = 0; i < byte_count; ++i) {
				target.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
			}
		};
		
		auto evaluate_static_initializer = [&](const ASTNode& expr_node, unsigned long long& out_value) -> bool {
			ConstExpr::EvaluationContext ctx(*global_symbol_table_);
			ctx.storage_duration = ConstExpr::StorageDuration::Static;
			
			auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
			if (!eval_result.success) {
				return false;
			}
			
			if (std::holds_alternative<unsigned long long>(eval_result.value)) {
				out_value = std::get<unsigned long long>(eval_result.value);
				return true;
			}
			if (std::holds_alternative<long long>(eval_result.value)) {
				out_value = static_cast<unsigned long long>(std::get<long long>(eval_result.value));
				return true;
			}
			if (std::holds_alternative<bool>(eval_result.value)) {
				out_value = std::get<bool>(eval_result.value) ? 1ULL : 0ULL;
				return true;
			}
			if (std::holds_alternative<double>(eval_result.value)) {
				double d = std::get<double>(eval_result.value);
				out_value = static_cast<unsigned long long>(d);
				return true;
			}
			
			return false;
		};
		
		for (const auto& [type_name, type_info] : gTypesByName) {
			if (!type_info->isStruct()) {
				continue;
			}
			// Skip pattern structs - they're templates and shouldn't generate code
			std::string_view type_name_view = StringTable::getStringView(type_name);
			if (type_name_view.find("_pattern_") != std::string::npos) {
				continue;
			}
			
			// Skip structs with "_unknown" in their name - they're incomplete template instantiations
			// from type alias parsing where template parameters weren't yet known
			if (type_name_view.find("_unknown") != std::string::npos) {
				FLASH_LOG(Codegen, Debug, "Skipping struct '", type_name_view, "' with _unknown in name (incomplete instantiation)");
				continue;
			}
			
			// Skip if we've already processed this TypeInfo pointer
			// (same struct can be registered under multiple keys in gTypesByName)
			if (processed_type_infos_.count(type_info) > 0) {
				continue;
			}
			processed_type_infos_.insert(type_info);
			
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (!struct_info) {
				continue;
			}
			
			// Generate static members that this struct directly owns
			if (!struct_info->static_members.empty()) {
				for (const auto& static_member : struct_info->static_members) {
					bool unresolved_identifier_initializer = false;
					// Skip static members with unsubstituted template parameters, identifiers, or sizeof...
					// These are in pattern templates and should only generate code when instantiated
					if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
						const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();
						if (std::holds_alternative<SizeofPackNode>(expr)) {
							// This is an uninstantiated template - skip
							FLASH_LOG(Codegen, Debug, "Skipping static member '", static_member.getName(), 
							          "' with unsubstituted sizeof... in type '", type_name, "'");
							continue;
						}
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							// Template parameter not substituted - this is a template pattern, not an instantiation
							// Skip it (instantiated versions will have NumericLiteralNode instead)
							const auto& tparam = std::get<TemplateParameterReferenceNode>(expr);
							FLASH_LOG(Codegen, Debug, "Skipping static member '", static_member.getName(), 
							          "' with unsubstituted template parameter '", tparam.param_name(), 
							          "' in type '", type_name, "'");
							continue;
						}
						// Also skip IdentifierNode that looks like an unsubstituted template parameter
						// (pattern templates may have IdentifierNode instead of TemplateParameterReferenceNode)
						if (std::holds_alternative<IdentifierNode>(expr)) {
							const auto& id = std::get<IdentifierNode>(expr);
							// If the identifier is not in the global symbol table and is a simple name (no qualified access),
							// it's likely an unsubstituted template parameter - skip it
							// Instantiated templates will have NumericLiteralNode or other concrete expressions
							auto symbol = global_symbol_table_->lookup(id.name());
							if (!symbol.has_value()) {
								// Not found in global symbol table - likely a template parameter
								FLASH_LOG(Codegen, Debug, "Skipping static member '", static_member.getName(), 
								          "' with identifier initializer '", id.name(), 
								          "' in type '", type_name, "' (identifier not in symbol table - likely template parameter)");
								unresolved_identifier_initializer = true;
							}
						}
					}

					// Build the qualified name for deduplication
					StringBuilder qualified_name_sb;
					qualified_name_sb.append(type_name).append("::").append(StringTable::getStringView(static_member.getName()));
					std::string_view qualified_name = qualified_name_sb.commit();
					StringHandle name_handle = StringTable::getOrInternStringHandle(qualified_name);
					
					// Skip if already emitted
					if (emitted_static_members_.count(name_handle) > 0) {
						continue;
					}
					emitted_static_members_.insert(name_handle);

					GlobalVariableDeclOp op;
					op.type = static_member.type;
					op.size_in_bits = static_cast<int>(static_member.size * 8);
					op.var_name = name_handle;  // Phase 3: Now using StringHandle instead of string_view

					// Check if static member has an initializer
					op.is_initialized = static_member.initializer.has_value() || unresolved_identifier_initializer;
					if (unresolved_identifier_initializer) {
						FLASH_LOG(Codegen, Debug, "Initializer unresolved; zero-initializing static member '", qualified_name, "'");
						size_t byte_count = op.size_in_bits / 8;
						for (size_t i = 0; i < byte_count; ++i) {
							op.init_data.push_back(0);
						}
					} else if (op.is_initialized) {
					const ExpressionNode& init_expr = static_member.initializer->as<ExpressionNode>();
					
					// Check for ConstructorCallNode (e.g., T() which becomes int() after substitution)
					if (std::holds_alternative<ConstructorCallNode>(init_expr)) {
						FLASH_LOG(Codegen, Debug, "Processing ConstructorCallNode initializer for static member '", 
						          qualified_name, "' - initializing to zero");
						// For now, initialize constructor calls to zero (default value)
						// This handles cases like int(), float(), etc. which should zero-initialize
						size_t byte_count = op.size_in_bits / 8;
						for (size_t i = 0; i < byte_count; ++i) {
							op.init_data.push_back(0);
						}
					} else if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
						const auto& bool_lit = std::get<BoolLiteralNode>(init_expr);
						FLASH_LOG(Codegen, Debug, "Processing BoolLiteralNode initializer for static member '", 
						          qualified_name, "' value=", bool_lit.value() ? "true" : "false");
						unsigned long long value = bool_lit.value() ? 1ULL : 0ULL;
						size_t byte_count = op.size_in_bits / 8;
						for (size_t i = 0; i < byte_count; ++i) {
							op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
						}
						FLASH_LOG(Codegen, Debug, "  Wrote ", byte_count, " bytes to init_data");
					} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
						FLASH_LOG(Codegen, Debug, "Processing NumericLiteralNode initializer for static member '", 
						          qualified_name, "'");
						// Evaluate the initializer expression
						auto init_operands = visitExpressionNode(init_expr);
						// Convert to raw bytes
						if (init_operands.size() >= 3) {
							unsigned long long value = 0;
							if (std::holds_alternative<unsigned long long>(init_operands[2])) {
								value = std::get<unsigned long long>(init_operands[2]);
								FLASH_LOG(Codegen, Debug, "  Extracted uint64 value: ", value);
							} else if (std::holds_alternative<double>(init_operands[2])) {
								double d = std::get<double>(init_operands[2]);
								std::memcpy(&value, &d, sizeof(double));
								FLASH_LOG(Codegen, Debug, "  Extracted double value: ", d);
							}
							size_t byte_count = op.size_in_bits / 8;
							for (size_t i = 0; i < byte_count; ++i) {
								op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
							}
							FLASH_LOG(Codegen, Debug, "  Wrote ", byte_count, " bytes to init_data");
						} else {
							FLASH_LOG(Codegen, Debug, "  WARNING: init_operands.size() = ", init_operands.size(), " (expected >= 3)");
						}
					} else if (std::holds_alternative<TemplateParameterReferenceNode>(init_expr)) {
						FLASH_LOG(Codegen, Debug, "WARNING: Processing TemplateParameterReferenceNode initializer for static member '", 
						          qualified_name, "' - should have been substituted!");
						// Try to evaluate anyway
						auto init_operands = visitExpressionNode(init_expr);
						if (init_operands.size() >= 3) {
							unsigned long long value = 0;
							if (std::holds_alternative<unsigned long long>(init_operands[2])) {
								value = std::get<unsigned long long>(init_operands[2]);
							} else if (std::holds_alternative<double>(init_operands[2])) {
								double d = std::get<double>(init_operands[2]);
								std::memcpy(&value, &d, sizeof(double));
							}
							size_t byte_count = op.size_in_bits / 8;
							for (size_t i = 0; i < byte_count; ++i) {
								op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
							}
						}
					} else if (std::holds_alternative<IdentifierNode>(init_expr)) {
						const auto& id = std::get<IdentifierNode>(init_expr);
						FLASH_LOG(Codegen, Debug, "Processing IdentifierNode '", id.name(), "' initializer for static member '", 
						          qualified_name, "'");
						// Evaluate the initializer expression
						auto init_operands = visitExpressionNode(init_expr);
						if (init_operands.size() >= 3) {
							unsigned long long value = 0;
							if (std::holds_alternative<unsigned long long>(init_operands[2])) {
								value = std::get<unsigned long long>(init_operands[2]);
							} else if (std::holds_alternative<double>(init_operands[2])) {
								double d = std::get<double>(init_operands[2]);
								std::memcpy(&value, &d, sizeof(double));
							}
							size_t byte_count = op.size_in_bits / 8;
							for (size_t i = 0; i < byte_count; ++i) {
								op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
							}
						}
					} else {
						unsigned long long evaluated_value = 0;
						if (evaluate_static_initializer(*static_member.initializer, evaluated_value)) {
							FLASH_LOG(Codegen, Debug, "Evaluated constexpr initializer for static member '", 
							          qualified_name, "'");
							append_bytes(evaluated_value, op.size_in_bits, op.init_data);
						} else {
							FLASH_LOG(Codegen, Debug, "Processing unknown expression type initializer for static member '", 
							          qualified_name, "' - skipping evaluation");
							// For unknown expression types, skip evaluation to avoid crashes
							// Initialize to zero as a safe default
							append_bytes(0, op.size_in_bits, op.init_data);
						}
					}
				}
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), Token()));
			}
		}
			
			// Also check if this struct inherits static members from base classes
			// and generate alias definitions if needed (Phase 3: Generate ALL inherited static members)
			if (!struct_info->base_classes.empty()) {
				for (const auto& base : struct_info->base_classes) {
					if (base.type_index >= gTypeInfo.size()) {
						continue;
					}
					
					const TypeInfo& base_type = gTypeInfo[base.type_index];
					const StructTypeInfo* base_info = base_type.getStructInfo();
					
					// If base_type is a type alias (no struct_info), follow type_index_ to get the actual struct
					// This handles cases like `struct Test : wrapper<true_type>::type` where `::type` is a type alias
					if (!base_info && base_type.type_index_ != base.type_index && base_type.type_index_ < gTypeInfo.size()) {
						const TypeInfo& resolved_type = gTypeInfo[base_type.type_index_];
						base_info = resolved_type.getStructInfo();
						FLASH_LOG(Codegen, Debug, "Resolved type alias '", StringTable::getStringView(base_type.name_), 
						          "' to struct '", StringTable::getStringView(resolved_type.name_), "'");
					}
					
					// Special handling for type aliases like "bool_constant_true::type"
					// The StructTypeInfo for the type alias may have static members with unsubstituted initializers
					// In this case, we need to find the actual underlying struct and use its static members instead
					if (base_info && base.name.find("::") != std::string_view::npos) {
						// Extract the struct name before "::" (e.g., "bool_constant_true" from "bool_constant_true::type")
						auto pos = base.name.rfind("::");
						if (pos != std::string_view::npos) {
							std::string_view actual_struct_name = base.name.substr(0, pos);
							auto actual_struct_it = gTypesByName.find(StringTable::getOrInternStringHandle(actual_struct_name));
							if (actual_struct_it != gTypesByName.end()) {
								const StructTypeInfo* actual_info = actual_struct_it->second->getStructInfo();
								if (actual_info) {
									FLASH_LOG(Codegen, Debug, "Using actual struct '", actual_struct_name, 
									          "' instead of type alias '", base.name, "' for static members");
									base_info = actual_info;
								}
							}
						}
					}
					
					// Iterate through ALL static members in the base class hierarchy (Phase 3 fix)
					if (base_info) {
						// Collect all static members recursively from this base and its bases
						std::vector<std::pair<const StructStaticMember*, const StructTypeInfo*>> all_static_members;
						
						// Use a queue to traverse the inheritance hierarchy
						std::queue<const StructTypeInfo*> to_visit;
						std::unordered_set<const StructTypeInfo*> visited;
						to_visit.push(base_info);
						
						while (!to_visit.empty()) {
							const StructTypeInfo* current = to_visit.front();
							to_visit.pop();
							
							if (visited.count(current)) continue;
							visited.insert(current);
							
							// Add all static members from current struct
							for (const auto& static_member : current->static_members) {
								all_static_members.emplace_back(&static_member, current);
							}
							
							// Add base classes to queue
							for (const auto& base_spec : current->base_classes) {
								if (base_spec.type_index < gTypeInfo.size()) {
									const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
									if (const StructTypeInfo* base_struct = base_type_info.getStructInfo()) {
										to_visit.push(base_struct);
									}
								}
							}
						}
						
						// Generate inherited static member definitions for each one found
						for (const auto& [static_member_ptr, owner_struct] : all_static_members) {
							std::string_view member_name = StringTable::getStringView(static_member_ptr->name);
							
							// Generate definition for this derived class
							StringBuilder derived_qualified_name_sb;
							derived_qualified_name_sb.append(type_name).append("::").append(member_name);
							std::string_view derived_qualified_name = derived_qualified_name_sb.commit();
							StringHandle derived_name_handle = StringTable::getOrInternStringHandle(derived_qualified_name);
							
							// Skip if already emitted
							if (emitted_static_members_.count(derived_name_handle) > 0) {
								continue;
							}
							emitted_static_members_.insert(derived_name_handle);
							
							// Use the original base class name from the BaseClassSpecifier, not the resolved type
							std::string_view base_name_str = base.name;
							
							FLASH_LOG(Codegen, Debug, "Generating inherited static member '", member_name, 
							          "' for ", type_name, " from base ", base_name_str);
							
							GlobalVariableDeclOp alias_op;
							alias_op.type = static_member_ptr->type;
							alias_op.size_in_bits = static_cast<int>(static_member_ptr->size * 8);
							alias_op.var_name = derived_name_handle;
							alias_op.is_initialized = true;
							
							// Evaluate the initializer to get the value
							bool found_base_value = false;
							unsigned long long inferred_value = 0;
							
							if (static_member_ptr->initializer.has_value() && 
							    static_member_ptr->initializer->is<ExpressionNode>()) {
								const ExpressionNode& init_expr = static_member_ptr->initializer->as<ExpressionNode>();
								
								if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
									const auto& bool_lit = std::get<BoolLiteralNode>(init_expr);
									inferred_value = bool_lit.value() ? 1ULL : 0ULL;
									found_base_value = true;
									FLASH_LOG(Codegen, Debug, "Found bool literal value: ", bool_lit.value());
								} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
									auto init_operands = visitExpressionNode(init_expr);
									if (init_operands.size() >= 3 && std::holds_alternative<unsigned long long>(init_operands[2])) {
										inferred_value = std::get<unsigned long long>(init_operands[2]);
										found_base_value = true;
										FLASH_LOG(Codegen, Debug, "Found numeric literal value: ", inferred_value);
									} else if (init_operands.size() >= 3 && std::holds_alternative<double>(init_operands[2])) {
										double d = std::get<double>(init_operands[2]);
										inferred_value = static_cast<unsigned long long>(d);
										found_base_value = true;
										FLASH_LOG(Codegen, Debug, "Found double literal value: ", d);
									}
								} else if (evaluate_static_initializer(*static_member_ptr->initializer, inferred_value)) {
									found_base_value = true;
									FLASH_LOG(Codegen, Debug, "Evaluated constexpr initializer for inherited static member '", member_name, "'");
								}
							}
							
							// Write the value to init_data
							append_bytes(inferred_value, alias_op.size_in_bits, alias_op.init_data);
							
							if (!found_base_value) {
								FLASH_LOG(Codegen, Debug, "Using default zero value (no initializer found)");
							}
							
							ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(alias_op), Token()));
						}
					}
				}
			}
		}
	}

	// Generate trivial default constructors for structs that need them
	// This handles template instantiations like Tuple<> that have no user-defined constructors
	void generateTrivialDefaultConstructors() {
		std::unordered_set<const TypeInfo*> processed;
		
		for (const auto& [type_name, type_info] : gTypesByName) {
			if (!type_info->isStruct()) {
				continue;
			}
			
			// Skip pattern structs
			std::string_view type_name_view2 = StringTable::getStringView(type_name);
			if (type_name_view2.find("_pattern_") != std::string::npos) {
				continue;
			}
			
			// Skip structs with "_unknown" in their name - they're incomplete template instantiations
			if (type_name_view2.find("_unknown") != std::string::npos) {
				FLASH_LOG(Codegen, Debug, "Skipping trivial constructor for '", type_name_view2, "' with _unknown in name (incomplete instantiation)");
				continue;
			}
			
			// Skip if already processed
			if (processed.count(type_info) > 0) {
				continue;
			}
			processed.insert(type_info);
			
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (!struct_info) {
				continue;
			}
			
			// Only generate trivial constructor if explicitly marked as needing one
			// The needs_default_constructor flag is set during template instantiation
			// when a struct has no constructors but needs a default one
			if (!struct_info->needs_default_constructor) {
				continue;
			}
			
			// Check if there are already constructors defined
			bool has_constructor = false;
			for (const auto& mem_func : struct_info->member_functions) {
				if (mem_func.is_constructor) {
					has_constructor = true;
					break;
				}
			}
			
			// Generate trivial default constructor if no constructor exists
			if (!has_constructor) {
				FLASH_LOG(Codegen, Debug, "Generating trivial constructor for ", type_name);

				// Use the pattern from visitConstructorDeclarationNode
				// Create function declaration for constructor
				FunctionDeclOp ctor_decl_op;
				ctor_decl_op.function_name = type_info->name();
				ctor_decl_op.struct_name = type_info->name();
				ctor_decl_op.return_type = Type::Void;
				ctor_decl_op.return_size_in_bits = 0;
				ctor_decl_op.return_pointer_depth = 0;
				ctor_decl_op.linkage = Linkage::CPlusPlus;
				ctor_decl_op.is_variadic = false;

				// Generate mangled name for default constructor
				// Use style-aware generateMangledName instead of MSVC-only generateMangledNameForConstructor
				std::vector<TypeSpecifierNode> empty_params;  // Explicit type to avoid ambiguity
				std::vector<std::string_view> empty_namespace_path;  // Explicit type to avoid ambiguity
				std::string_view class_name = StringTable::getStringView(type_info->name());
				// Extract the last component for func_name (handles nested classes like "Outer::Inner")
				std::string_view func_name = class_name;
				auto last_colon = class_name.rfind("::");
				if (last_colon != std::string_view::npos) {
					func_name = class_name.substr(last_colon + 2);
				}
				TypeSpecifierNode void_return(Type::Void, TypeQualifier::None, 0);
				ctor_decl_op.mangled_name = StringTable::getOrInternStringHandle(NameMangling::generateMangledName(
					func_name,
					void_return,
					empty_params,
					false,  // not variadic
					class_name,  // struct_name
					empty_namespace_path,
					Linkage::CPlusPlus
				));

				ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(ctor_decl_op), Token()));
				
				// Call base class constructors if any
				for (const auto& base : struct_info->base_classes) {
					auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base.name));
					if (base_type_it != gTypesByName.end()) {
						// Only call base constructor if the base class actually has constructors
						// This avoids link errors when inheriting from classes without constructors
						const StructTypeInfo* base_struct_info = base_type_it->second->getStructInfo();
						if (base_struct_info && base_struct_info->hasAnyConstructor()) {
							ConstructorCallOp call_op;
							call_op.struct_name = base_type_it->second->name();
							call_op.object = StringTable::getOrInternStringHandle("this");
							// No arguments for default constructor
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(call_op), Token()));
						}
					}
				}
				
				// Initialize members with default initializers
				for (const auto& member : struct_info->members) {
					if (member.default_initializer.has_value()) {
						const ASTNode& init_node = member.default_initializer.value();
						if (init_node.has_value() && init_node.is<ExpressionNode>()) {
							// Use the default member initializer
							auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
							// Extract just the value (third element of init_operands)
							// Verify we have at least 3 elements before accessing
							if (init_operands.size() < 3) {
								FLASH_LOG(Codegen, Warning, "Default initializer expression returned fewer than 3 operands");
								continue;
							}
							
							IrValue member_value;
							if (std::holds_alternative<TempVar>(init_operands[2])) {
								member_value = std::get<TempVar>(init_operands[2]);
							} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
								member_value = std::get<unsigned long long>(init_operands[2]);
							} else if (std::holds_alternative<double>(init_operands[2])) {
								member_value = std::get<double>(init_operands[2]);
							} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
								member_value = std::get<StringHandle>(init_operands[2]);
							} else {
								member_value = 0ULL;  // fallback
							}
							
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference;
							member_store.is_rvalue_reference = member.is_rvalue_reference;
							member_store.struct_type_info = nullptr;
							
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), Token()));
						}
					}
				}
				
				// Emit return
				ReturnOp ret_op;
				// ReturnOp fields: return_value (optional), return_type (optional), return_size
				// For void constructor, leave return_value as nullopt
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), Token()));
			}
		}
	}

private:
	// Helper function to try evaluating sizeof/alignof using ConstExprEvaluator
	// Returns the evaluated operands if successful, empty vector otherwise
	template<typename NodeType>
	std::vector<IrOperand> tryEvaluateAsConstExpr(const NodeType& node) {
		// Try to evaluate as a constant expression first
		ConstExpr::EvaluationContext ctx(symbol_table);
		auto expr_node = ASTNode::emplace_node<ExpressionNode>(node);
		auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
		
		if (eval_result.success) {
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

	// Helper function to convert a MemberFunctionCallNode to a regular FunctionCallNode
	// Used when a member function call syntax is used but the object is not a struct
	std::vector<IrOperand> convertMemberCallToFunctionCall(const MemberFunctionCallNode& memberFunctionCallNode) {
		const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
		DeclarationNode& decl_node = const_cast<DeclarationNode&>(func_decl.decl_node());
		
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
	                       const BaseClassSpecifier* inheritance_path = nullptr,
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
		// 3. Nested classes within the same class (C++ allows nested classes to access protected)
		if (member->access == AccessSpecifier::Protected) {
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
		StringHandle var_handle = StringTable::getOrInternStringHandle(var_name);
		const std::optional<ASTNode> symbol = symbol_table.lookup(var_name);
		
		if (symbol.has_value() && symbol->is<DeclarationNode>()) {
			const auto& decl = symbol->as<DeclarationNode>();
			const auto& type_spec = decl.type_node().as<TypeSpecifierNode>();
			return type_spec.is_lvalue_reference() || type_spec.is_rvalue_reference();
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
					token
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

	// Handle compound assignment to lvalues (e.g., v.x += 5)
	// Currently only supports Member kind (struct member access)
	// Array subscript support can be added later if needed
	// This is similar to handleLValueAssignment but also performs the arithmetic operation
	bool handleLValueCompoundAssignment(const std::vector<IrOperand>& lhs_operands,
	                                     const std::vector<IrOperand>& rhs_operands,
	                                     const Token& token,
	                                     std::string_view op) {
		// Check if LHS has a TempVar with lvalue metadata
		if (lhs_operands.size() < 3 || !std::holds_alternative<TempVar>(lhs_operands[2])) {
			FLASH_LOG(Codegen, Info, "handleLValueCompoundAssignment: FAIL - size=", lhs_operands.size());
			return false;
		}

		TempVar lhs_temp = std::get<TempVar>(lhs_operands[2]);
		auto lvalue_info_opt = getTempVarLValueInfo(lhs_temp);
		
		if (!lvalue_info_opt.has_value()) {
			FLASH_LOG(Codegen, Info, "handleLValueCompoundAssignment: FAIL - no lvalue metadata");
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
			// For Indirect kind (dereferenced pointer), the base is a TempVar holding the address
			// Generate a Dereference instruction to load the current value
			DereferenceOp deref_op;
			deref_op.result = current_value_temp;
			deref_op.pointer.type = std::get<Type>(lhs_operands[0]);
			deref_op.pointer.size_in_bits = 64;  // pointer size
			deref_op.pointer.pointer_depth = 1;
			if (std::holds_alternative<TempVar>(lv_info.base)) {
				deref_op.pointer.value = std::get<TempVar>(lv_info.base);
			} else {
				FLASH_LOG(Codegen, Debug, "     Indirect kind requires TempVar base");
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
			
			emitDereferenceStore(result_tv, std::get<Type>(lhs_operands[0]), std::get<int>(lhs_operands[1]),
			                     std::get<TempVar>(lv_info.base), token);
			
			return true;
		}
		
		if (lv_info.kind != LValueInfo::Kind::Member) {
			FLASH_LOG(Codegen, Debug, "     Compound assignment only supports Member or Indirect kind for now, got: ", static_cast<int>(lv_info.kind));
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
			std::optional<ASTNode> symbol = symbol_table.lookup(base_name);
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(base_name);
			}
			
			if (symbol.has_value()) {
				const DeclarationNode* decl = get_decl_from_symbol(*symbol);
				if (decl) {
					const TypeSpecifierNode& type_node = decl->type_node().as<TypeSpecifierNode>();
					if (is_struct_type(type_node.type())) {
						TypeIndex type_index = type_node.type_index();
						if (type_index < gTypeInfo.size()) {
							const StructTypeInfo* struct_info = gTypeInfo[type_index].getStructInfo();
							if (struct_info) {
								const StructMember* member = struct_info->findMemberRecursive(lv_info.member_name.value());
								if (member) {
									member_is_reference = member->is_reference;
									member_is_rvalue_reference = member->is_rvalue_reference;
								}
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
			token
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
	                     const Token& token = Token()) {
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
		
		ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
	}
	
	// Emit DereferenceStore instruction
	void emitDereferenceStore(const TypedValue& value, Type pointee_type, int pointee_size_bits,
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

	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		if (!node.get_definition().has_value()) {
			return;
		}

		// Reset the temporary variable counter for each new function
		// For member functions, reserve TempVar(1) for the implicit 'this' parameter
		var_counter = node.is_member_function() ? TempVar(2) : TempVar();

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		const DeclarationNode& func_decl = node.decl_node();
		const std::string_view func_name_view = func_decl.identifier_token().value();
		current_function_name_ = StringTable::getOrInternStringHandle(func_name_view);
		
		// Set current function return type and size for type checking in return statements
		const TypeSpecifierNode& ret_type_spec = func_decl.type_node().as<TypeSpecifierNode>();
		current_function_return_type_ = ret_type_spec.type();
		current_function_returns_reference_ = ret_type_spec.is_reference();
		
		// For pointer return types or reference return types, use 64-bit size (pointer size on x64)
		// References are represented as pointers at the IR level
		current_function_return_size_ = (ret_type_spec.pointer_depth() > 0 || ret_type_spec.is_reference()) 
			? 64 
			: static_cast<int>(ret_type_spec.size_in_bits());

		// Clear current_struct_name_ if this is not a member function
		// This prevents struct context from leaking into free functions
		if (!node.is_member_function()) {
			current_struct_name_ = StringHandle();
		}

		if (FLASH_LOG_ENABLED(Codegen, Debug)) {
			const TypeSpecifierNode& debug_ret_type = func_decl.type_node().as<TypeSpecifierNode>();
			FLASH_LOG(Codegen, Debug, "===== CODEGEN visitFunctionDeclarationNode: ", func_decl.identifier_token().value(), " =====");
			FLASH_LOG(Codegen, Debug, "  return_type: ", (int)debug_ret_type.type(), " size: ", (int)debug_ret_type.size_in_bits(), " ptr_depth: ", debug_ret_type.pointer_depth(), " is_ref: ", debug_ret_type.is_reference(), " is_rvalue_ref: ", debug_ret_type.is_rvalue_reference());
			FLASH_LOG(Codegen, Debug, "  is_member_function: ", node.is_member_function());
			if (node.is_member_function()) {
				FLASH_LOG(Codegen, Debug, "  parent_struct_name: ", node.parent_struct_name());
			}
			FLASH_LOG(Codegen, Debug, "  parameter_count: ", node.parameter_nodes().size());
			for (size_t i = 0; i < node.parameter_nodes().size(); ++i) {
				const auto& param = node.parameter_nodes()[i];
				if (param.is<DeclarationNode>()) {
					const DeclarationNode& param_decl = param.as<DeclarationNode>();
					const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					FLASH_LOG(Codegen, Debug, "  param[", i, "]: name='", param_decl.identifier_token().value()
							  , "' type=", (int)param_type.type() 
							  , " size=", (int)param_type.size_in_bits()
							  , " ptr_depth=", param_type.pointer_depth()
							  , " base_cv=", (int)param_type.cv_qualifier()
							  , " is_ref=", param_type.is_reference()
							  , " is_rvalue_ref=", param_type.is_rvalue_reference());
					for (size_t j = 0; j < param_type.pointer_levels().size(); ++j) {
						FLASH_LOG(Codegen, Debug, " ptr[", j, "]_cv=", (int)param_type.pointer_levels()[j].cv_qualifier);
					}
				}
			}
			FLASH_LOG(Codegen, Debug, "=====");
		}

		// Clear static local names map for new function
		static_local_names_.clear();

		const TypeSpecifierNode& ret_type = func_decl.type_node().as<TypeSpecifierNode>();

		// Create function declaration with return type and name
		// Use FunctionDeclOp to store typed payload
		FunctionDeclOp func_decl_op;
		
		// Return type information
		func_decl_op.return_type = ret_type.type();
		// For pointer return types, use 64-bit size (pointer size on x64)
		// For reference return types, keep the base type size (the reference itself is 64-bit at ABI level,
		// but we display it as the base type with a reference qualifier)
		func_decl_op.return_size_in_bits = (ret_type.pointer_depth() > 0) 
			? 64 
			: static_cast<int>(ret_type.size_in_bits());
		func_decl_op.return_pointer_depth = ret_type.pointer_depth();
		func_decl_op.return_type_index = ret_type.type_index();
		func_decl_op.returns_reference = ret_type.is_reference();
		func_decl_op.returns_rvalue_reference = ret_type.is_rvalue_reference();
		
		// Detect if function returns struct by value (needs hidden return parameter for RVO/NRVO)
		// Only non-pointer, non-reference struct returns need this (pointer/reference returns are in RAX like regular pointers)
		bool returns_struct_by_value = (ret_type.type() == Type::Struct && ret_type.pointer_depth() == 0 && !ret_type.is_reference());
		func_decl_op.has_hidden_return_param = returns_struct_by_value;
		
		// Track return type index and hidden parameter flag for current function context
		current_function_return_type_index_ = ret_type.type_index();
		current_function_has_hidden_return_param_ = returns_struct_by_value;
		
		if (returns_struct_by_value) {
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Function {} returns struct by value - will use hidden return parameter (RVO/NRVO)",
				func_decl.identifier_token().value());
		}
		
		// Function name
		func_decl_op.function_name = StringTable::getOrInternStringHandle(func_decl.identifier_token().value());

		// Add struct/class name for member functions
		// Use current_struct_name_ if set (for instantiated template specializations),
		// otherwise use the function node's parent_struct_name
		// For nested classes, we need to use the fully qualified name from TypeInfo
		std::string_view struct_name_for_function;
		if (current_struct_name_.isValid()) {
			struct_name_for_function = StringTable::getStringView(current_struct_name_);
		} else if (node.is_member_function()) {
			struct_name_for_function = node.parent_struct_name();
		} else {
			struct_name_for_function = ""sv;
		}
		func_decl_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_function);
		
		// Linkage and variadic flag
		func_decl_op.linkage = node.linkage();
		func_decl_op.is_variadic = node.is_variadic();
		
		// Member functions defined inside the class body are implicitly inline (C++ standard)
		// Mark them as inline so they get weak linkage in the object file to allow duplicate definitions
		// This includes constructors, destructors, and regular member functions defined inline
		// Also mark functions in std namespace as inline to handle standard library functions that
		// are defined in headers (like std::abs) and may be instantiated multiple times
		bool is_in_std_namespace = false;
		if (!current_namespace_stack_.empty()) {
			is_in_std_namespace = (current_namespace_stack_[0] == "std");
		}
		func_decl_op.is_inline = node.is_member_function() || is_in_std_namespace;

		// Use pre-computed mangled name from AST node if available (Phase 6 migration)
		// Fall back to generating it here if not (for backward compatibility during migration)
		std::string_view mangled_name;
		if (node.has_mangled_name()) {
			mangled_name = node.mangled_name();
		} else if (node.has_non_type_template_args()) {
			// Generate mangled name with template arguments for template specializations (e.g., get<0>)
			const TypeSpecifierNode& return_type = func_decl.type_node().as<TypeSpecifierNode>();
			std::vector<TypeSpecifierNode> param_types;
			for (const auto& param : node.parameter_nodes()) {
				param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
			}
			auto mangled = NameMangling::generateMangledNameWithTemplateArgs(
				func_decl.identifier_token().value(), return_type, param_types, 
				node.non_type_template_args(), node.is_variadic(), 
				struct_name_for_function, current_namespace_stack_);
			mangled_name = mangled.view();
		} else {
			// Generate mangled name using the FunctionDeclarationNode overload
			mangled_name = generateMangledNameForCall(node, struct_name_for_function, current_namespace_stack_);
		}
		func_decl_op.mangled_name = StringTable::getOrInternStringHandle(mangled_name);

		// Skip duplicate function definitions to prevent multiple codegen of the same function
		// This is especially important for inline functions from standard headers (like std::abs)
		// that may be parsed multiple times
		if (generated_function_names_.count(func_decl_op.mangled_name) > 0) {
			FLASH_LOG(Codegen, Debug, "Skipping duplicate function definition: ", func_decl.identifier_token().value(), " (", mangled_name, ")");
			return;
		}
		generated_function_names_.insert(func_decl_op.mangled_name);

		// Add parameters to function declaration
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			FunctionParam param_info;
			param_info.type = param_type.type();
			param_info.size_in_bits = static_cast<int>(param_type.size_in_bits());
			
			// Lvalue references (&) are treated like pointers in the IR (address at the ABI level)
			int pointer_depth = static_cast<int>(param_type.pointer_depth());
			if (param_type.is_lvalue_reference()) {
				pointer_depth += 1;  // Add 1 for lvalue reference (ABI treats it as an additional pointer level)
			}
			// Note: Rvalue references (T&&) are tracked separately via is_rvalue_reference flag.
			// While lvalue references are always implemented as pointers at the ABI level,
			// rvalue references in the context of perfect forwarding can receive values directly
			// when bound to temporaries/literals. The pointer_depth increment is omitted to allow
			// this direct value passing, while the is_rvalue_reference flag enables proper handling
			// in both the caller (materialization + address-taking) and callee (dereferencing).
			param_info.pointer_depth = pointer_depth;
			param_info.name = StringTable::getOrInternStringHandle(param_decl.identifier_token().value());
			param_info.is_reference = param_type.is_reference();  // Tracks ANY reference (lvalue or rvalue)
			param_info.is_rvalue_reference = param_type.is_rvalue_reference();  // Specific rvalue ref flag
			param_info.cv_qualifier = param_type.cv_qualifier();

			func_decl_op.parameters.push_back(std::move(param_info));
			var_counter.next();
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), func_decl.identifier_token()));

		// Short-circuit implicit/defaulted spaceship operators to a safe return value
		if (func_name_view == "operator<=>" && node.is_implicit()) {
			ReturnOp ret_op;
			ret_op.return_value = IrValue{0ULL};
			ret_op.return_type = Type::Int;
			ret_op.return_size = 32;
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
			return;
		}

		// Provide safe defaults for synthesized comparison operators to avoid recursion
		if (node.is_implicit()) {
			auto emit_bool_return = [&](bool value) {
				ReturnOp ret_op;
				ret_op.return_value = IrValue{value ? 1ULL : 0ULL};
				ret_op.return_type = Type::Bool;
				ret_op.return_size = 8;
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
			};

			if (func_name_view == "operator==") {
				emit_bool_return(true);
				return;
			}
			if (func_name_view == "operator!=" ||
				func_name_view == "operator<" ||
				func_name_view == "operator>" ||
				func_name_view == "operator<=" ||
				func_name_view == "operator>=") {
				emit_bool_return(false);
				return;
			}
		}

		symbol_table.enter_scope(ScopeType::Function);

		// For member functions, add implicit 'this' pointer to symbol table
		if (node.is_member_function()) {
			// Look up the struct type to get its type index and size
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
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
					symbol_table.insert("this"sv, this_decl);
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
				auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
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
							member_load.object = StringTable::getOrInternStringHandle("other");  // Load from 'other' parameter
							member_load.member_name = member.getName();
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
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
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
						deref_op.pointer.type = Type::Struct;
						deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
						deref_op.pointer.value = StringTable::getOrInternStringHandle("this");

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
			block.get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		}

		// Add implicit return if needed
		// Check if the last instruction is a return
		bool ends_with_return = false;
		if (!ir_.getInstructions().empty()) {
			const auto& last_instr = ir_.getInstructions().back();
			ends_with_return = (last_instr.getOpcode() == IrOpcode::Return);
		}

		if (!ends_with_return) {
			// Add implicit return for void functions
			if (ret_type.type() == Type::Void) {
				ReturnOp ret_op;  // No return value for void
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
			}
			// Special case: main() implicitly returns 0 if no return statement
			else if (func_decl.identifier_token().value() == "main") {
				ReturnOp ret_op;
				ret_op.return_value = 0ULL;  // Implicit return 0
				ret_op.return_type = Type::Int;
				ret_op.return_size = 32;
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
			}
			// For other non-void functions, this is an error (missing return statement)
			// TODO: This should be a compile error, but for now we'll allow it
			// Full implementation requires control flow analysis to check all paths
		}

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
		// This allows nested contexts (like local struct member functions) to work properly
	}

	void visitStructDeclarationNode(const StructDeclarationNode& node) {
		// Struct declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system



		// Skip pattern structs - they're templates and shouldn't generate code
		// Pattern structs have "_pattern_" in their name
		std::string_view struct_name = StringTable::getStringView(node.name());
		if (struct_name.find("_pattern_") != std::string_view::npos) {
			return;
		}
		
		// Skip structs with "_unknown" in their name - they're incomplete template instantiations
		// from type alias parsing where template parameters weren't yet known
		if (struct_name.find("_unknown") != std::string_view::npos) {
			FLASH_LOG(Codegen, Debug, "Skipping struct '", struct_name, "' with _unknown in name (incomplete instantiation)");
			return;
		}

		// Generate member functions for both global and local structs
		// Save the enclosing function context so member function visits don't clobber it
		StringHandle saved_enclosing_function = current_function_name_;
		
		// Check if this is a local struct (declared inside a function)
		bool is_local_struct = current_function_name_.isValid();
		
		// Set struct context so member functions know which struct they belong to
		// NOTE: We don't clear this until the next struct - the string must persist
		// because IrOperands store string_view references to it
		// For nested classes, we need to use the fully qualified name from TypeInfo
		// If current_struct_name_ is valid, this is a nested class, so construct fully qualified name
		StringHandle lookup_name;
		if (current_struct_name_.isValid()) {
			// This is a nested class - construct fully qualified name like "Outer::Inner"
			StringBuilder qualified_name_builder;
			qualified_name_builder.append(StringTable::getStringView(current_struct_name_))
			                     .append("::")
			                     .append(struct_name);
			lookup_name = StringTable::getOrInternStringHandle(qualified_name_builder.commit());
		} else {
			// Top-level class - use simple name
			lookup_name = StringTable::getOrInternStringHandle(struct_name);
		}
		
		auto type_it = gTypesByName.find(lookup_name);
		if (type_it != gTypesByName.end()) {
			current_struct_name_ = type_it->second->name();
		} else {
			current_struct_name_ = lookup_name;
		}
		
		// For local structs, collect member functions for deferred generation
		// For global structs, visit them immediately
		if (is_local_struct) {
			for (const auto& member_func : node.member_functions()) {
				LocalStructMemberInfo info;
				info.struct_name = current_struct_name_;
				info.enclosing_function_name = saved_enclosing_function;
				info.member_function_node = member_func.function_declaration;
				collected_local_struct_members_.push_back(std::move(info));
			}
		} else {
			FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - visiting members immediately, count=", node.member_functions().size());
			for (const auto& member_func : node.member_functions()) {
				// Each member function can be a FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
				FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - processing member function, is_constructor=", member_func.is_constructor);
				try {
					// Call the specific visitor directly instead of visit() to avoid clearing current_function_name_
					const ASTNode& func_decl = member_func.function_declaration;
					if (func_decl.is<FunctionDeclarationNode>()) {
						visitFunctionDeclarationNode(func_decl.as<FunctionDeclarationNode>());
					} else if (func_decl.is<ConstructorDeclarationNode>()) {
						visitConstructorDeclarationNode(func_decl.as<ConstructorDeclarationNode>());
					} else if (func_decl.is<DestructorDeclarationNode>()) {
						visitDestructorDeclarationNode(func_decl.as<DestructorDeclarationNode>());
					}
				} catch (const std::exception& ex) {
					FLASH_LOG(Codegen, Error, "Exception while visiting member function in struct ",
					          struct_name, ": ", ex.what());
					throw;
				} catch (...) {
					FLASH_LOG(Codegen, Error, "Unknown exception while visiting member function in struct ",
					          struct_name);
					throw;
				}
			}
		}  // End of if-else for local vs global struct
		
		// Clear current_function_name_ before visiting nested classes
		// Nested classes should not be treated as local structs even if we're inside
		// a member function context (e.g., after visiting constructors which set current_function_name_)
		// Nested classes are always at class scope, not function scope
		current_function_name_ = StringHandle();
		
		// Save current_struct_name_ before visiting nested classes so each nested class
		// gets the correct parent context (important when there are multiple nested classes)
		StringHandle parent_struct_name = current_struct_name_;
			
			// Visit nested classes recursively
			for (const auto& nested_class_node : node.nested_classes()) {
				if (nested_class_node.is<StructDeclarationNode>()) {
					FLASH_LOG(Codegen, Debug, "  Visiting nested class");
					// Restore parent context before each nested class visit
					current_struct_name_ = parent_struct_name;
					visitStructDeclarationNode(nested_class_node.as<StructDeclarationNode>());
				}
			}

			// Generate global storage for static members
			auto static_member_type_it = gTypesByName.find(node.name());
			if (static_member_type_it != gTypesByName.end()) {
				const TypeInfo* type_info = static_member_type_it->second;
				
				// Skip if we've already processed this TypeInfo pointer
				// (same struct can be registered under multiple keys in gTypesByName)
				if (processed_type_infos_.count(type_info) > 0) {
					// Already processed in generateStaticMemberDeclarations() or earlier visit
				} else {
					processed_type_infos_.insert(type_info);
					
					const StructTypeInfo* struct_info = type_info->getStructInfo();
					if (struct_info) {
						for (const auto& static_member : struct_info->static_members) {
							// Build the qualified name for deduplication using type_info->name()
							// This ensures consistency with generateStaticMemberDeclarations() which uses
							// the type name from gTypesByName iterator (important for template instantiations)
							StringBuilder qualified_name_sb;
							qualified_name_sb.append(StringTable::getStringView(type_info->name())).append("::").append(StringTable::getStringView(static_member.getName()));
							std::string_view qualified_name = qualified_name_sb.commit();
							StringHandle name_handle = StringTable::getOrInternStringHandle(qualified_name);
							
							// Skip if already emitted
							if (emitted_static_members_.count(name_handle) > 0) {
								continue;
							}
							emitted_static_members_.insert(name_handle);

							GlobalVariableDeclOp op;
							op.type = static_member.type;
							op.size_in_bits = static_cast<int>(static_member.size * 8);
							op.var_name = name_handle;  // Phase 3: Now using StringHandle instead of string_view

							// Check if static member has an initializer
							op.is_initialized = static_member.initializer.has_value();
							if (op.is_initialized) {
								// Evaluate the initializer expression
								auto init_operands = visitExpressionNode(static_member.initializer->as<ExpressionNode>());
								// Convert to raw bytes
								if (init_operands.size() >= 3) {
									unsigned long long value = 0;
									if (std::holds_alternative<unsigned long long>(init_operands[2])) {
										value = std::get<unsigned long long>(init_operands[2]);
									} else if (std::holds_alternative<double>(init_operands[2])) {
										double d = std::get<double>(init_operands[2]);
										std::memcpy(&value, &d, sizeof(double));
									}
									size_t byte_count = op.size_in_bits / 8;
									for (size_t i = 0; i < byte_count; ++i) {
										op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
									}
								}
							}
							ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), Token()));
						}
					}
				}
			}
			// Clear current_struct_name_ for top-level structs
		
			if (current_struct_name_.isValid()) {
				std::string_view current_name = StringTable::getStringView(current_struct_name_);
				if (current_name.find("::") == std::string_view::npos) {
					current_struct_name_ = StringHandle();
				}
			}
		// Restore the enclosing function context
		current_function_name_ = saved_enclosing_function;
	}

	void visitEnumDeclarationNode(const EnumDeclarationNode& node) {
		// Enum declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system
		// Enumerators are treated as compile-time constants and don't need runtime code generation
		// For unscoped enums, the enumerators are already added to the symbol table during parsing
	}

	void visitConstructorDeclarationNode(const ConstructorDeclarationNode& node) {
		// If no definition and not explicit, check if implicit
		if (!node.get_definition().has_value()) {
			if (node.is_implicit()) {
				// Implicit constructors might not have a body if trivial, but we must emit the symbol
				// so the linker can find it if referenced.
				// Proceed to generate an empty function body.
			} else {
				return;
			}
		}

		// Reset the temporary variable counter for each new constructor
		// Constructors are always member functions, so reserve TempVar(1) for 'this'
		var_counter = TempVar(2);

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		current_function_name_ = node.name();
		static_local_names_.clear();

		// Create constructor declaration with typed payload
		FunctionDeclOp ctor_decl_op;
		// For nested classes, use current_struct_name_ which contains the fully qualified name
		std::string_view struct_name_for_ctor = current_struct_name_.isValid() ? StringTable::getStringView(current_struct_name_) : StringTable::getStringView(node.struct_name());
		
		// Extract just the last component of the class name for the constructor function name
		// For "Outer::Inner", we want "Inner" as the function name
		std::string_view ctor_function_name = struct_name_for_ctor;
		std::string_view parent_class_name;  // For mangling - all components except the last
		size_t last_colon = struct_name_for_ctor.rfind("::");
		if (last_colon != std::string_view::npos) {
			ctor_function_name = struct_name_for_ctor.substr(last_colon + 2);  // "Inner"
			parent_class_name = struct_name_for_ctor.substr(0, last_colon);     // "Outer"
		} else {
			parent_class_name = struct_name_for_ctor;  // Not nested, use as-is
		}
		
		ctor_decl_op.function_name = StringTable::getOrInternStringHandle(ctor_function_name);  // Constructor name (last component)
		ctor_decl_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_ctor);  // Struct name for member function (fully qualified)
		ctor_decl_op.return_type = Type::Void;  // Constructors don't have a return type
		ctor_decl_op.return_size_in_bits = 0;  // Size is 0 for void
		ctor_decl_op.return_pointer_depth = 0;  // Pointer depth is 0 for void
		ctor_decl_op.linkage = Linkage::CPlusPlus;  // C++ linkage for constructors
		ctor_decl_op.is_variadic = false;  // Constructors are never variadic
		// Constructors defined inside class body are implicitly inline (C++ standard)
		// Mark them as inline so they get weak linkage in the object file
		ctor_decl_op.is_inline = true;

		// Generate mangled name for constructor
		// For template instantiations, use struct_name_for_ctor which has the correct instantiated name
		// (e.g., "Base_char" instead of "Base")
		{
			// Create a dummy TypeSpecifierNode for the return type (constructors return void)
			TypeSpecifierNode return_type(Type::Void, TypeQualifier::None, 0);
			std::vector<std::string_view> empty_namespace_path;
			ctor_decl_op.mangled_name = StringTable::getOrInternStringHandle(NameMangling::generateMangledName(
				ctor_function_name, return_type, node.parameter_nodes(),
				false, struct_name_for_ctor, empty_namespace_path, Linkage::CPlusPlus));
		}
		
		// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
		// We don't add it here to avoid duplication

		// Add parameter types to constructor declaration
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor decl operands");
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			FunctionParam func_param;
			func_param.type = param_type.type();
			func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
			func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
			func_param.name = StringTable::getOrInternStringHandle(param_decl.identifier_token().value());
			func_param.is_reference = param_type.is_reference();
			func_param.is_rvalue_reference = param_type.is_rvalue_reference();
			func_param.cv_qualifier = param_type.cv_qualifier();
			ctor_decl_op.parameters.push_back(func_param);
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(ctor_decl_op), node.name_token()));
		
		symbol_table.enter_scope(ScopeType::Function);

		// Add 'this' pointer to symbol table for member access
		// Look up the struct type to get its type index and size
		// Use struct_name_for_ctor (which is fully qualified) instead of node.struct_name()
		// to handle nested classes correctly (node.struct_name() might be just "Inner" instead of "Outer::Inner")
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
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
				symbol_table.insert("this"sv, this_decl);
			}
		}

		// Add parameters to symbol table
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor symbol table");
			symbol_table.insert(param_decl.identifier_token().value(), param);
		}

		// C++11 Delegating constructor: if present, ONLY call the target constructor
		// No base class or member initialization should happen
		if (node.delegating_initializer().has_value()) {
			const auto& delegating_init = node.delegating_initializer().value();
			
			// Build constructor call: StructName::StructName(this, args...)
			ConstructorCallOp ctor_op;
			ctor_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_ctor);
			ctor_op.object = StringTable::getOrInternStringHandle("this");

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
		// Use struct_name_for_ctor (fully qualified) instead of node.struct_name()
		auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Step 1: Call base class constructors (in declaration order)
				for (const auto& base : struct_info->base_classes) {
					// Check if there's an explicit base initializer
					const BaseInitializer* base_init = nullptr;
					for (const auto& init : node.base_initializers()) {
						if (init.getBaseClassName() == StringTable::getOrInternStringHandle(base.name)) {
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
					ctor_op.struct_name = base_type_info.name();
					ctor_op.object = StringTable::getOrInternStringHandle("this");

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
						// Only call base default constructor if the base class actually has constructors
						// This avoids link errors when inheriting from classes without constructors
						const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
						if (base_struct_info && base_struct_info->hasAnyConstructor()) {
							// Call default constructor with no arguments
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
						}
					}
				}
				
				// Step 1.5: Initialize vptr if this class has virtual functions
				// This must happen after base constructor calls (which set up base vptr)
				// but before member initialization
				if (struct_info->has_vtable) {
					// Use the pre-generated vtable symbol from struct_info
					// The vtable symbol is generated once during buildVTable()
					auto vtable_symbol = StringTable::getOrInternStringHandle(struct_info->vtable_symbol);
					
					// Create a MemberStore instruction to store vtable address to offset 0 (vptr)
					MemberStoreOp vptr_store;
					vptr_store.object = StringTable::getOrInternStringHandle("this");
					vptr_store.member_name = StringTable::getOrInternStringHandle("__vptr");  // Virtual pointer (synthetic member)
					vptr_store.offset = 0;  // vptr is always at offset 0
					vptr_store.struct_type_info = struct_type_info;  // Use TypeInfo pointer
					vptr_store.is_reference = false;
					vptr_store.is_rvalue_reference = false;
					vptr_store.vtable_symbol = vtable_symbol;  // Store vtable symbol as string_view
					
					// The value is a vtable symbol reference
					// Type is pointer (Type::Void with pointer semantics), size is 64 bits (8 bytes)
					// The actual symbol will be loaded using the vtable_symbol field
					vptr_store.value.type = Type::Void;
					vptr_store.value.size_in_bits = 64;
					vptr_store.value.value = static_cast<unsigned long long>(0);  // Placeholder
					
					ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(vptr_store), node.name_token()));
				}
			}
		}

		// Step 2: Generate IR for member initializers (executed before constructor body)
		// Look up the struct type to get member information
		// Use struct_name_for_ctor (fully qualified) instead of node.struct_name()
		struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
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

							// Only call base copy/move constructor if the base class actually has constructors
							// This avoids link errors when inheriting from classes without constructors
							const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
							if (!base_struct_info || !base_struct_info->hasAnyConstructor()) {
								continue;  // Skip if base has no constructors
							}

							// Build constructor call: Base::Base(this, other)
							// For copy constructors, pass 'other' as the copy source (cast to base class reference)
							// For move constructors, pass 'other' as the move source
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = base_type_info.name();
							ctor_op.object = StringTable::getOrInternStringHandle("this");  // 'this' pointer (base subobject is at offset 0 for now)
							// Add 'other' parameter for copy/move constructor
							// IMPORTANT: Use BASE CLASS type_index, not derived class, for proper name mangling
							TypedValue other_arg;
							other_arg.type = Type::Struct;  // Parameter type (struct reference)
							other_arg.size_in_bits = static_cast<int>(base_type_info.struct_info_ ? base_type_info.struct_info_->total_size * 8 : struct_info->total_size * 8);
							other_arg.value = StringTable::getOrInternStringHandle("other");  // Parameter value ('other' object)
							other_arg.type_index = base.type_index;  // Use BASE class type index for proper mangling
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
							member_load.object = StringTable::getOrInternStringHandle("other"sv);  // Load from 'other' parameter
							member_load.member_name = member.getName();
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
							member_store.object = StringTable::getOrInternStringHandle("this"sv);
							member_store.member_name = member.getName();
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
									} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
										member_value = std::get<StringHandle>(init_operands[2]);
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
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
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
						auto explicit_it = explicit_inits.find(std::string(StringTable::getStringView(member.getName())));
						if (explicit_it != explicit_inits.end()) {
							// Special handling for reference members initialized with reference variables/parameters
							// When initializing a reference member (int& ref) with a reference parameter (int& r),
							// we need to use the pointer value that the parameter holds, not dereference it
							bool handled_as_reference_init = false;
							if (member.is_reference || member.is_rvalue_reference) {
								// Check if the initializer is a simple identifier
								const ASTNode& init_expr = explicit_it->second->initializer_expr;
								if (init_expr.is<ExpressionNode>()) {
									const auto& expr_node = init_expr.as<ExpressionNode>();
									if (std::holds_alternative<IdentifierNode>(expr_node)) {
										const auto& id_node = std::get<IdentifierNode>(expr_node);
										auto init_name = StringTable::getOrInternStringHandle(id_node.name());
										
										// Look up the identifier in the symbol table
										std::optional<ASTNode> init_symbol = symbol_table.lookup(init_name);
										if (init_symbol.has_value() && init_symbol->is<DeclarationNode>()) {
											const auto& init_decl = init_symbol->as<DeclarationNode>();
											const auto& init_type = init_decl.type_node().as<TypeSpecifierNode>();
											
											// If the initializer is a reference, use its value directly (it's already a pointer)
											// Don't dereference it - just use the string_view to refer to the variable
											if (init_type.is_reference() || init_type.is_rvalue_reference()) {
												member_value = init_name;
												handled_as_reference_init = true;
											}
										}
									}
								}
							}
							
							if (!handled_as_reference_init) {
								// Use explicit initializer from constructor initializer list
								auto init_operands = visitExpressionNode(explicit_it->second->initializer_expr.as<ExpressionNode>());
								// Extract just the value (third element of init_operands)
								if (std::holds_alternative<TempVar>(init_operands[2])) {
									member_value = std::get<TempVar>(init_operands[2]);
								} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
									member_value = std::get<unsigned long long>(init_operands[2]);
								} else if (std::holds_alternative<double>(init_operands[2])) {
									member_value = std::get<double>(init_operands[2]);
								} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
									member_value = std::get<StringHandle>(init_operands[2]);
								} else {
									member_value = 0ULL;  // fallback
								}
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
								} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
									member_value = std::get<StringHandle>(init_operands[2]);
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
						member_store.object = StringTable::getOrInternStringHandle("this");
						member_store.member_name = member.getName();
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
		const BlockNode& block = node.get_definition().value().as<BlockNode>();
		size_t ctor_stmt_index = 0;
		block.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		// Add implicit return for constructor (constructors don't have explicit return statements)
		ReturnOp ret_op;  // No return value for void
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.name_token()));

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
	}

	void visitDestructorDeclarationNode(const DestructorDeclarationNode& node) {
		if (!node.get_definition().has_value())
			return;

		// Reset the temporary variable counter for each new destructor
		// Destructors are always member functions, so reserve TempVar(1) for 'this'
		var_counter = TempVar(2);

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		current_function_name_ = node.name();
	static_local_names_.clear();

	// Create destructor declaration with typed payload
	FunctionDeclOp dtor_decl_op;
	dtor_decl_op.function_name = StringTable::getOrInternStringHandle(StringBuilder().append("~"sv).append(node.struct_name()));  // Destructor name
	dtor_decl_op.struct_name = node.struct_name();
	dtor_decl_op.return_type = Type::Void;  // Destructors don't have a return type
	dtor_decl_op.return_size_in_bits = 0;  // Size is 0 for void
	dtor_decl_op.return_pointer_depth = 0;  // Pointer depth is 0 for void
	dtor_decl_op.linkage = Linkage::CPlusPlus;  // C++ linkage for destructors
	dtor_decl_op.is_variadic = false;  // Destructors are never variadic

	// Generate mangled name for destructor
	// Use the dedicated mangling function for destructors to ensure correct platform-specific mangling
	// (e.g., MSVC uses ??1ClassName@... format)
	dtor_decl_op.mangled_name = NameMangling::generateMangledNameFromNode(node);

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
				symbol_table.insert("this"sv, this_decl);
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
					dtor_op.struct_name = base_type_info.name();
					dtor_op.object = StringTable::getOrInternStringHandle("this");

					ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), node.name_token()));
				}
			}
		}

		// Add implicit return for destructor (destructors don't have explicit return statements)
		ReturnOp ret_op;  // No return value for void
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.name_token()));

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
	}

	void visitNamespaceDeclarationNode(const NamespaceDeclarationNode& node) {
		// Namespace declarations themselves don't generate IR - they just provide scope
		// Track the current namespace for proper name mangling
		// For anonymous namespaces, push empty string which will be handled specially by mangling
		current_namespace_stack_.push_back(std::string(node.name()));
		
		// Visit all declarations within the namespace
		for (const auto& decl : node.declarations()) {
			visit(decl);
		}
		
		// Pop the namespace from the stack
		current_namespace_stack_.pop_back();
	}

	void visitUsingDirectiveNode(const UsingDirectiveNode& node) {
		// Using directives don't generate IR - they affect name lookup in the symbol table
		// Add the namespace to the current scope's using directives in the local symbol table
		// (not gSymbolTable, which is the parser's symbol table and has different scope management)
		symbol_table.add_using_directive(node.namespace_path());
	}

	void visitUsingDeclarationNode(const UsingDeclarationNode& node) {
		// Using declarations don't generate IR - they import a specific name into the current scope
		// Add the using declaration to the local symbol table (not gSymbolTable)
		symbol_table.add_using_declaration(
			node.identifier_name(),
			node.namespace_path(),
			node.identifier_name()
		);
	}

	void visitNamespaceAliasNode(const NamespaceAliasNode& node) {
		// Namespace aliases don't generate IR - they create an alias for a namespace
		// Add the alias to the local symbol table (not gSymbolTable)
		symbol_table.add_namespace_alias(node.alias_name(), node.target_namespace());
	}

	void visitReturnStatementNode(const ReturnStatementNode& node) {
		if (node.expression()) {
			const auto& expr_opt = node.expression();
			
			// Handle InitializerListNode for braced initializers in return statements
			if (expr_opt->is<InitializerListNode>()) {
				// Create a temporary variable to hold the initialized struct
				TempVar temp_var = var_counter.next();
				
				// Generate initialization code similar to variable declarations
				const InitializerListNode& init_list = expr_opt->as<InitializerListNode>();
				
				// Get struct type information
				Type return_type = current_function_return_type_;
				int return_size = current_function_return_size_;
				
				if (return_type != Type::Struct) {
					FLASH_LOG(Codegen, Error, "InitializerListNode in return statement for non-struct type");
					return;
				}
				
				// Find the struct info
				const StructTypeInfo* struct_info = nullptr;
				
				// Look up the struct by return type index or name
				for (size_t i = 0; i < gTypeInfo.size(); ++i) {
					if (gTypeInfo[i].struct_info_ &&
					    static_cast<int>(gTypeInfo[i].struct_info_->total_size * 8) == return_size) {
						struct_info = gTypeInfo[i].struct_info_.get();
						break;
					}
				}
				
				if (!struct_info) {
					FLASH_LOG(Codegen, Error, "Could not find struct type info for return type");
					return;
				}
				
				// Process initializer list to generate member stores
				const auto& initializers = init_list.initializers();
				std::unordered_map<StringHandle, const ASTNode*> member_values;
				size_t positional_index = 0;
				
				for (size_t i = 0; i < initializers.size(); ++i) {
					if (init_list.is_designated(i)) {
						// Designated initializer - use member name
						StringHandle member_name = init_list.member_name(i);
						member_values[member_name] = &initializers[i];
					} else {
						// Positional initializer - map to member by index
						if (positional_index < struct_info->members.size()) {
							StringHandle member_name = struct_info->members[positional_index].getName();
							member_values[member_name] = &initializers[i];
							positional_index++;
						}
					}
				}
				
				// Generate member stores for each initialized member
				for (const StructMember& member : struct_info->members) {
					StringHandle member_name_handle = member.getName();
					auto it = member_values.find(member_name_handle);
					
					if (it != member_values.end()) {
						// Evaluate the initializer expression
						const ASTNode* init_expr = it->second;
						if (init_expr->is<ExpressionNode>()) {
							auto init_operands = visitExpressionNode(init_expr->as<ExpressionNode>());
							
							if (init_operands.size() >= 3) {
								// Generate member store
								MemberStoreOp store_op;
								store_op.object = temp_var;
								store_op.member_name = member.getName();
								store_op.offset = static_cast<int>(member.offset);
								
								// Create TypedValue from operands
								Type value_type = std::get<Type>(init_operands[0]);
								int value_size = std::get<int>(init_operands[1]);
								IrValue ir_value;
								
								// Extract value from operands
								if (std::holds_alternative<unsigned long long>(init_operands[2])) {
									ir_value = std::get<unsigned long long>(init_operands[2]);
								} else if (std::holds_alternative<TempVar>(init_operands[2])) {
									ir_value = std::get<TempVar>(init_operands[2]);
								} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
									ir_value = std::get<StringHandle>(init_operands[2]);
								} else if (std::holds_alternative<double>(init_operands[2])) {
									ir_value = std::get<double>(init_operands[2]);
								}
								
								store_op.value = { value_type, value_size, ir_value };
								store_op.is_reference = false;
								
								ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_op), node.return_token()));
							}
						}
					}
				}
				
				// Now return the temporary variable
				ReturnOp ret_op;
				ret_op.return_value = temp_var;
				ret_op.return_type = return_type;
				ret_op.return_size = return_size;
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
				return;
			}
			
			// Original handling for ExpressionNode
			assert(expr_opt->is<ExpressionNode>());
			
			// Set flag if we should use RVO (returning struct by value with hidden return param)
			if (current_function_has_hidden_return_param_) {
				in_return_statement_with_rvo_ = true;
			}
			
			// For reference return types, use LValueAddress context to get the address instead of the value
			// This ensures "return *this" returns the address (this pointer), not the dereferenced value
			ExpressionContext return_context = current_function_returns_reference_ 
				? ExpressionContext::LValueAddress 
				: ExpressionContext::Load;
			auto operands = visitExpressionNode(expr_opt->as<ExpressionNode>(), return_context);
			
			// Clear the RVO flag after evaluation
			in_return_statement_with_rvo_ = false;
			
			// Check if this is a void return with a void expression (e.g., return void_func();)
			if (!operands.empty() && operands.size() >= 1) {
				Type expr_type = std::get<Type>(operands[0]);
				
				// If returning a void expression in a void function, just emit void return
				// (the expression was already evaluated for its side effects)
				if (expr_type == Type::Void && current_function_return_type_ == Type::Void) {
					ReturnOp ret_op;  // No return value for void
					ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
					return;
				}
			}
			
			// If the current function has auto return type, deduce it from the return expression
			if (current_function_return_type_ == Type::Auto && !operands.empty() && operands.size() >= 2) {
				Type expr_type = std::get<Type>(operands[0]);
				int expr_size = std::get<int>(operands[1]);
				
				// Build a TypeSpecifierNode for the deduced type
				TypeSpecifierNode deduced_type(expr_type, TypeQualifier::None, expr_size, node.return_token());
				
				// If we have type_index information (for structs), include it
				if (operands.size() >= 4) {
					if (std::holds_alternative<unsigned long long>(operands[3])) {
						TypeIndex type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operands[3]));
						deduced_type = TypeSpecifierNode(expr_type, TypeQualifier::None, expr_size, node.return_token());
						deduced_type.set_type_index(type_index);
					}
				}
				
				// Store the deduced type for this function
				if (current_function_name_.isValid()) {
					deduced_auto_return_types_[std::string(StringTable::getStringView(current_function_name_))] = deduced_type;
				}
				
				// Update current function return type for subsequent return statements
				current_function_return_type_ = expr_type;
				current_function_return_size_ = expr_size;
			}
			
			// Convert to the function's return type if necessary
			// Skip type conversion for reference returns - the expression already has the correct representation
			if (!current_function_returns_reference_ && !operands.empty() && operands.size() >= 2) {
				Type expr_type = std::get<Type>(operands[0]);
				int expr_size = std::get<int>(operands[1]);
		
				// Get the current function's return type
				Type return_type = current_function_return_type_;
				int return_size = current_function_return_size_;
		
				// Convert if types don't match
				if (expr_type != return_type || expr_size != return_size) {
					// Check for user-defined conversion operator
					// If expr is a struct type with a conversion operator to return_type, call it
					if (expr_type == Type::Struct && operands.size() >= 4) {
						TypeIndex expr_type_index = 0;
						if (std::holds_alternative<unsigned long long>(operands[3])) {
							expr_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operands[3]));
						}
						
						if (expr_type_index > 0 && expr_type_index < gTypeInfo.size()) {
							const TypeInfo& source_type_info = gTypeInfo[expr_type_index];
							const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
							
							// Look for a conversion operator to the return type
							const StructMemberFunction* conv_op = findConversionOperator(
								source_struct_info, return_type, 0);
							
							if (conv_op) {
								FLASH_LOG(Codegen, Debug, "Found conversion operator in return statement from ", 
									StringTable::getStringView(source_type_info.name()), 
									" to return type");
								
								// Generate call to the conversion operator
								TempVar result_var = var_counter.next();
								
								// Get the source variable value
								IrValue source_value = std::visit([](auto&& arg) -> IrValue {
									using T = std::decay_t<decltype(arg)>;
									if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
									              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
										return arg;
									} else {
										return 0ULL;
									}
								}, operands[2]);
								
								// Build the mangled name for the conversion operator
								StringHandle struct_name_handle = source_type_info.name();
								std::string_view struct_name = StringTable::getStringView(struct_name_handle);
								
								// Generate the call using CallOp (member function call)
								if (conv_op->function_decl.is<FunctionDeclarationNode>()) {
									const auto& func_decl = conv_op->function_decl.as<FunctionDeclarationNode>();
									std::string_view mangled_name;
									if (func_decl.has_mangled_name()) {
										mangled_name = func_decl.mangled_name();
									} else {
										// Generate mangled name for the conversion operator
										// Use the function's parent struct name, not the source type name,
										// because the conversion operator may be inherited from a base class
										// and we need to call the version defined in the base class.
										std::string_view operator_struct_name = func_decl.parent_struct_name();
										if (operator_struct_name.empty()) {
											operator_struct_name = struct_name;
										}
										mangled_name = generateMangledNameForCall(func_decl, operator_struct_name);
									}
									
									CallOp call_op;
									call_op.result = result_var;
									call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
									call_op.return_type = return_type;
									call_op.return_size_in_bits = return_size;
									call_op.return_type_index = (return_type == Type::Struct) ? current_function_return_type_index_ : 0;
									call_op.is_member_function = true;
									call_op.is_variadic = false;
									
									// For member function calls, first argument is 'this' pointer
									if (std::holds_alternative<StringHandle>(source_value)) {
										// It's a variable - take its address
										TempVar this_ptr = var_counter.next();
										AddressOfOp addr_op;
										addr_op.result = this_ptr;
										addr_op.operand.type = expr_type;
										addr_op.operand.size_in_bits = expr_size;
										addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
										addr_op.operand.value = std::get<StringHandle>(source_value);
										ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
										
										// Add 'this' as first argument
										TypedValue this_arg;
										this_arg.type = expr_type;
										this_arg.size_in_bits = 64;  // Pointer size
										this_arg.value = this_ptr;
										this_arg.type_index = expr_type_index;
										call_op.args.push_back(std::move(this_arg));
									} else if (std::holds_alternative<TempVar>(source_value)) {
										// It's already a temporary
										// ASSUMPTION: For struct types, TempVars at this point
										// represent the address of the object (not the object value itself).
										TypedValue this_arg;
										this_arg.type = expr_type;
										this_arg.size_in_bits = 64;  // Pointer size for 'this'
										this_arg.value = std::get<TempVar>(source_value);
										this_arg.type_index = expr_type_index;
										call_op.args.push_back(std::move(this_arg));
									}
									
									ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), node.return_token()));
									
									// Replace operands with the result of the conversion
									operands.clear();
									operands.emplace_back(return_type);
									operands.emplace_back(return_size);
									operands.emplace_back(result_var);
								}
							} else {
								// No conversion operator found - fall back to generateTypeConversion
								operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
							}
						} else {
							// No valid type_index - fall back to generateTypeConversion
							operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
						}
					} else {
						// Not a struct type - use standard type conversion
						operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
					}
				}
			}
			
			// Create ReturnOp with the return value
			ReturnOp ret_op;
			
			// Check if operands has at least 3 elements before accessing
			if (operands.size() < 3) {
				FLASH_LOG(Codegen, Error, "Return statement: expression evaluation failed or returned insufficient operands");
				return;
			}
			
			// Extract IrValue from operand[2] - it could be various types
			if (std::holds_alternative<unsigned long long>(operands[2])) {
				ret_op.return_value = std::get<unsigned long long>(operands[2]);
			} else if (std::holds_alternative<TempVar>(operands[2])) {
				TempVar return_temp = std::get<TempVar>(operands[2]);
				ret_op.return_value = return_temp;
				
				// C++17 mandatory copy elision: Check if this is a prvalue (e.g., constructor call result)
				// being returned - prvalues used to initialize objects of the same type must have copies elided
				if (isTempVarRVOEligible(return_temp)) {
					FLASH_LOG_FORMAT(Codegen, Debug,
						"RVO opportunity detected: returning prvalue {} (constructor call result)",
						return_temp.name());
					// Note: Actual copy elision would require hidden return parameter support
					// For now, we just log the opportunity
				}
				
				// Mark the temp as a return value for potential NRVO analysis
				markTempVarAsReturnValue(return_temp);
			} else if (std::holds_alternative<StringHandle>(operands[2])) {
				ret_op.return_value = std::get<StringHandle>(operands[2]);
			} else if (std::holds_alternative<double>(operands[2])) {
				ret_op.return_value = std::get<double>(operands[2]);
			}
			ret_op.return_type = std::get<Type>(operands[0]);
			ret_op.return_size = std::get<int>(operands[1]);
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
		}
		else {
			// For void returns, we don't need any operands
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
		}
	}

	void visitBlockNode(const BlockNode& node) {
		// Check if this block contains only VariableDeclarationNodes
		// If so, it's likely from comma-separated declarations and shouldn't create a new scope
		bool only_var_decls = true;
		size_t var_decl_count = 0;
		node.get_statements().visit([&](const ASTNode& statement) {
			if (statement.is<VariableDeclarationNode>()) {
				var_decl_count++;
			} else {
				only_var_decls = false;
			}
		});

		// For blocks that only contain two or more variable declarations, don't enter a new scope
		// This handles comma-separated declarations like: int a = 1, b = 2;
		// which the parser represents as a BlockNode containing multiple VariableDeclarationNodes
		// Single variable declarations in blocks (e.g., { int x = 5; }) should create a scope
		bool enter_scope = !(only_var_decls && var_decl_count > 1);

		if (enter_scope) {
			// Enter a new scope
			symbol_table.enter_scope(ScopeType::Block);
			enterScope();
			ir_.addInstruction(IrOpcode::ScopeBegin, {}, Token());
		}

		// Visit all statements in the block
		node.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		if (enter_scope) {
			// Exit scope and call destructors
			ir_.addInstruction(IrOpcode::ScopeEnd, {}, Token());
			exitScope();
			symbol_table.exit_scope();
		}
	}

	void visitIfStatementNode(const IfStatementNode& node) {
		// Handle C++17 if constexpr - evaluate condition at compile time
		if (node.is_constexpr()) {
			// Evaluate the condition at compile time
			ConstExpr::EvaluationContext ctx(gSymbolTable);
			auto result = ConstExpr::Evaluator::evaluate(node.get_condition(), ctx);
			
			if (!result.success) {
				FLASH_LOG(Codegen, Error, "if constexpr condition is not a constant expression: ", 
				          result.error_message);
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
		size_t current_if = if_counter++;
	
	// Use a single StringBuilder and commit each label before starting the next
	// to avoid buffer overwrites in the shared allocator
	StringBuilder label_sb;
	label_sb.append("if_then_").append(current_if);
	std::string_view then_label = label_sb.commit();
	
	label_sb.append("if_else_").append(current_if);
	std::string_view else_label = label_sb.commit();
	
	label_sb.append("if_end_").append(current_if);
	std::string_view end_label = label_sb.commit();

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
		CondBranchOp cond_branch;
		cond_branch.label_true = StringTable::getOrInternStringHandle(then_label);
		cond_branch.label_false = StringTable::getOrInternStringHandle(node.has_else() ? else_label : end_label);
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Then block
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(then_label)}, Token()));

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
			branch_to_end.target_label = StringTable::getOrInternStringHandle(end_label);
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_end), Token()));
		}

		// Else block (if present)
		if (node.has_else()) {
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(else_label)}, Token()));

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
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(end_label)}, Token()));
	}

	void visitForStatementNode(const ForStatementNode& node) {
		// Enter a new scope for the for loop (C++ standard: for-init-statement creates a scope)
		symbol_table.enter_scope(ScopeType::Block);
		enterScope();
		
		// Generate unique labels for this for loop
		static size_t for_counter = 0;
		size_t current_for = for_counter++;
		
		// Use a single StringBuilder and commit each label before starting the next
		auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("for_start_").append(current_for));
		auto loop_body_label = StringTable::createStringHandle(StringBuilder().append("for_body_").append(current_for));
		auto loop_increment_label = StringTable::createStringHandle(StringBuilder().append("for_increment_").append(current_for));
		auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("for_end_").append(current_for));

		// Execute init statement (if present)
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
		// Always call visit() to let visitBlockNode handle scope creation if needed
		auto body_stmt = node.get_body_statement();
		visit(body_stmt);

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
		
		// Exit the for loop scope
		exitScope();
		symbol_table.exit_scope();
	}

	void visitWhileStatementNode(const WhileStatementNode& node) {
		// Generate unique labels for this while loop
		static size_t while_counter = 0;
		size_t current_while = while_counter++;
		
		// Use a single StringBuilder and commit each label before starting the next
		auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("while_start_").append(current_while));
		auto loop_body_label = StringTable::createStringHandle(StringBuilder().append("while_body_").append(current_while));
		auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("while_end_").append(current_while));
		
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
		// Always call visit() to let visitBlockNode handle scope creation if needed
		auto body_stmt = node.get_body_statement();
		visit(body_stmt);

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
		size_t current_do_while = do_while_counter++;
		
		// Use a single StringBuilder and commit each label before starting the next
		auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("do_while_start_").append(current_do_while));
		auto loop_condition_label = StringTable::createStringHandle(StringBuilder().append("do_while_condition_").append(current_do_while));
		auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("do_while_end_").append(current_do_while));
		
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
		// Always call visit() to let visitBlockNode handle scope creation if needed
		auto body_stmt = node.get_body_statement();
		visit(body_stmt);

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
		StringHandle default_label = StringTable::getOrInternStringHandle(StringBuilder().append("switch_default_").append(switch_counter));
		StringHandle switch_end_label = StringTable::getOrInternStringHandle(StringBuilder().append("switch_end_").append(switch_counter));
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
				case_sb.append("switch_case_").append(switch_counter - 1).append("_").append(case_index);
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
			
			// Create typed BinaryOp for the Equal comparison
			BinaryOp bin_op{
				.lhs = TypedValue{.type = condition_type, .size_in_bits = condition_size, .value = toIrValue(condition_operands[2])},
				.rhs = TypedValue{.type = std::get<Type>(case_value_operands[0]), .size_in_bits = std::get<int>(case_value_operands[1]), .value = toIrValue(case_value_operands[2])},
				.result = cmp_result,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Equal, std::move(bin_op), Token()));

			// Branch to case label if equal, otherwise check next case
			StringBuilder next_check_sb;
			next_check_sb.append("switch_check_").append(switch_counter - 1).append("_").append(check_index + 1);
			std::string_view next_check_label = next_check_sb.commit();

			// For switch statements, we need to jump to case label when condition is true
			// and fall through to next check when false. Since both labels are forward references,
			// we emit: test condition; jz next_check; jmp case_label
			CondBranchOp cond_branch;
			cond_branch.label_true = StringTable::getOrInternStringHandle(next_check_label); // Swap: jump to next if FALSE
			cond_branch.label_false = StringTable::getOrInternStringHandle(case_label);      // This won't be used
			cond_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 1, .value = cmp_result};
			ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

			// Unconditional branch to case label (when condition is true)
			BranchOp branch_to_case;
			branch_to_case.target_label = StringTable::getOrInternStringHandle(case_label);
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_case), Token()));

			// Next check label
			LabelOp next_lbl;
			next_lbl.label_name = StringTable::getOrInternStringHandle(next_check_label);
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
			case_sb.append("switch_case_").append(switch_counter - 1).append("_").append(case_index);
			std::string_view case_label = case_sb.commit();

			// Case label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(case_label)}, Token()));				// Execute case statements
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
		auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("ranged_for_start_").append(ranged_for_counter));
		auto loop_body_label = StringTable::createStringHandle(StringBuilder().append("ranged_for_body_").append(ranged_for_counter));
		auto loop_increment_label = StringTable::createStringHandle(StringBuilder().append("ranged_for_increment_").append(ranged_for_counter));
		auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("ranged_for_end_").append(ranged_for_counter));
		
		ranged_for_counter++;
		
		// Get the loop variable declaration and range expression
		auto loop_var_decl = node.get_loop_variable_decl();
		auto range_expr = node.get_range_expression();

		// C++11+ standard: The range expression is bound to a reference for lifetime extension
		// This ensures temporary objects live for the entire loop duration
		// For now, we only support simple identifiers (not temporaries), so lifetime is already correct
		
		// Check what kind of range expression we have
		if (!range_expr.is<ExpressionNode>()) {
			FLASH_LOG(Codegen, Error, "Range expression must be an expression");
			return;
		}

		auto& expr_variant = range_expr.as<ExpressionNode>();
		if (!std::holds_alternative<IdentifierNode>(expr_variant)) {
			FLASH_LOG(Codegen, Error, "Currently only identifiers are supported as range expressions");
			return;
		}

		const IdentifierNode& range_ident = std::get<IdentifierNode>(expr_variant);
		std::string_view range_name = range_ident.name();

		// Look up the range object in the symbol table
		const std::optional<ASTNode> range_symbol = symbol_table.lookup(range_name);
		if (!range_symbol.has_value()) {
			FLASH_LOG(Codegen, Error, "Range object '", range_name, "' not found in symbol table");
			return;
		}

		// Extract the DeclarationNode from either DeclarationNode or VariableDeclarationNode
		const DeclarationNode* range_decl_ptr = nullptr;
		if (range_symbol->is<DeclarationNode>()) {
			range_decl_ptr = &range_symbol->as<DeclarationNode>();
		} else if (range_symbol->is<VariableDeclarationNode>()) {
			range_decl_ptr = &range_symbol->as<VariableDeclarationNode>().declaration();
		} else {
			FLASH_LOG(Codegen, Error, "Range object '", range_name, "' is not a variable declaration");
			return;
		}

		const DeclarationNode& range_decl = *range_decl_ptr;
		const TypeSpecifierNode& range_type = range_decl.type_node().as<TypeSpecifierNode>();

		// C++ standard: pointers are NOT valid range expressions (no size information)
		// Only arrays and types with begin()/end() are allowed
		if (range_type.pointer_depth() > 0 && !range_decl.is_array()) {
			FLASH_LOG(Codegen, Error, "Cannot use pointer in range-based for loop; use array or type with begin()/end()");
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
			FLASH_LOG(Codegen, Error, "Range expression must be an array or a type with begin()/end() methods");
			return;
		}
	}

	void visitRangedForArray(const RangedForStatementNode& node, std::string_view array_name,
	                         const DeclarationNode& array_decl, StringHandle loop_start_label,
	                         StringHandle loop_body_label, StringHandle loop_increment_label,
	                         StringHandle loop_end_label, size_t counter) {
		auto loop_var_decl = node.get_loop_variable_decl();

		// Unified pointer-based approach: use begin/end pointers for arrays too
		// This is more efficient (no indexing multiplication) and matches what optimizing compilers do
		// For array: auto __begin = &array[0]; auto __end = &array[size]; for (; __begin != __end; ++__begin)

		// Get array size
		auto array_size_node = array_decl.array_size();
		if (!array_size_node.has_value()) {
			FLASH_LOG(Codegen, Error, "Array must have a known size for range-based for loop");
			return;
		}

		// Create begin/end pointer variable names
		StringBuilder sb_begin;
		sb_begin.append("__range_begin_");
		sb_begin.append(counter);
		std::string_view begin_var_name = sb_begin.commit();

		StringBuilder sb_end;
		sb_end.append("__range_end_");
		sb_end.append(counter);
		std::string_view end_var_name = sb_end.commit();

		Token begin_token(Token::Type::Identifier, begin_var_name, 0, 0, 0);
		Token end_token(Token::Type::Identifier, end_var_name, 0, 0, 0);

		// Get the array element type to create pointer type
		const TypeSpecifierNode& array_type = array_decl.type_node().as<TypeSpecifierNode>();
		
		// Calculate the actual element size for pointer arithmetic
		int element_size_bits;
		if (array_type.pointer_depth() > 0) {
			// Array of pointers - element size is pointer size (64 bits)
			element_size_bits = 64;
		} else if (array_type.type() == Type::Struct) {
			// Array of structs - lookup size from type info
			TypeIndex type_index = array_type.type_index();
			if (type_index > 0 && type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					element_size_bits = static_cast<int>(struct_info->total_size * 8);
				} else {
					element_size_bits = static_cast<int>(array_type.size_in_bits());
				}
			} else {
				element_size_bits = static_cast<int>(array_type.size_in_bits());
			}
		} else {
			// Regular array of primitives - use type size
			element_size_bits = static_cast<int>(array_type.size_in_bits());
			if (element_size_bits == 0) {
				element_size_bits = get_type_size_bits(array_type.type());
			}
		}
		
		// Create pointer type for begin/end (element_type*)
		// The size_in_bits should be the element size for correct pointer arithmetic
		auto begin_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			array_type.type(), array_type.type_index(), element_size_bits, Token()
		);
		begin_type_node.as<TypeSpecifierNode>().add_pointer_level();
		auto begin_decl_node = ASTNode::emplace_node<DeclarationNode>(begin_type_node, begin_token);

		auto end_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			array_type.type(), array_type.type_index(), element_size_bits, Token()
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

		// Declare and initialize the loop variable
		// For references (T& x or const T& x), we need the pointer value directly
		// For non-references (T x), we need the dereferenced value (*__begin)
		
		// Check if the loop variable is a reference
		if (!loop_var_decl.is<VariableDeclarationNode>()) {
			FLASH_LOG(Codegen, Error, "loop_var_decl is not a VariableDeclarationNode!");
			return;
		}
		const VariableDeclarationNode& original_var_decl = loop_var_decl.as<VariableDeclarationNode>();
		ASTNode loop_decl_node = original_var_decl.declaration_node();
		const DeclarationNode& loop_decl = loop_decl_node.as<DeclarationNode>();
		const TypeSpecifierNode& loop_type = loop_decl.type_node().as<TypeSpecifierNode>();
		
		ASTNode init_expr;
		// For range-based for loops, __range_begin is a pointer to the element
		// For reference loop variables (T& x), use the pointer value directly (don't dereference)
		// For value loop variables (T x), dereference to get the value
		bool loop_var_is_reference = loop_type.is_reference() || loop_type.is_rvalue_reference();
		
		if (loop_var_is_reference) {
			// Reference: use the iterator pointer value directly (bind to what it points to)
			// Since __range_begin is a pointer, and we want to bind the reference to what it points to,
			// we need to load the pointer value and use it as the reference's pointer
			init_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		} else {
			// Value: dereference the iterator to get the element value
			auto begin_deref_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
			init_expr = ASTNode::emplace_node<ExpressionNode>(
				UnaryOperatorNode(Token(Token::Type::Operator, "*", 0, 0, 0), begin_deref_expr, true)
			);
		}
		
		auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_decl_node, init_expr);

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
	                            const TypeSpecifierNode& range_type, StringHandle loop_start_label,
	                            StringHandle loop_body_label, StringHandle loop_increment_label,
	                            StringHandle loop_end_label, size_t counter) {
		auto loop_var_decl = node.get_loop_variable_decl();

		// Get the struct type info
		if (range_type.type_index() >= gTypeInfo.size()) {
			FLASH_LOG(Codegen, Error, "Invalid type index for range expression");
			return;
		}

		const TypeInfo& type_info = gTypeInfo[range_type.type_index()];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (!struct_info) {
			FLASH_LOG(Codegen, Error, "Range expression is not a struct type");
			return;
		}

		// Check for begin() and end() methods
		const StructMemberFunction* begin_func = struct_info->findMemberFunction("begin"sv);
		const StructMemberFunction* end_func = struct_info->findMemberFunction("end"sv);

		if (!begin_func || !end_func) {
			FLASH_LOG(Codegen, Error, "Range-based for loop requires type to have both begin() and end() methods");
			return;
		}

		// Create iterator variables: auto __begin = range.begin(), __end = range.end()
		StringBuilder sb_begin;
		sb_begin.append("__range_begin_");
		sb_begin.append(counter);
		std::string_view begin_var_name = sb_begin.commit();

		StringBuilder sb_end;
		sb_end.append("__range_end_");
		sb_end.append(counter);
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

		// Declare and initialize the loop variable
		// For references (T& x or const T& x), we need the iterator value directly
		// For non-references (T x), we need the dereferenced value (*__begin)
		
		// Create the loop variable declaration with initialization
		if (!loop_var_decl.is<VariableDeclarationNode>()) {
			assert(false && "loop_var_decl must be a VariableDeclarationNode");
			return;
		}
		const VariableDeclarationNode& original_var_decl = loop_var_decl.as<VariableDeclarationNode>();
		ASTNode loop_decl_node = original_var_decl.declaration_node();
		const DeclarationNode& loop_decl = loop_decl_node.as<DeclarationNode>();
		const TypeSpecifierNode& loop_type = loop_decl.type_node().as<TypeSpecifierNode>();
		
		ASTNode init_expr;
		if (loop_type.is_reference() || loop_type.is_rvalue_reference()) {
			// For reference variables, use the iterator directly (no dereference)
			// The reference will bind to the object pointed to by __begin
			init_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		} else {
			// For non-reference variables, reinterpret iterator as pointer to element type, then dereference
			auto begin_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
			auto loop_ptr_type = ASTNode::emplace_node<TypeSpecifierNode>(
				loop_type.type(), loop_type.type_index(), static_cast<int>(loop_type.size_in_bits()), Token()
			);
			loop_ptr_type.as<TypeSpecifierNode>().add_pointer_level();
			auto cast_expr = ASTNode::emplace_node<ExpressionNode>(
				ReinterpretCastNode(loop_ptr_type, begin_ident_expr, Token(Token::Type::Keyword, "reinterpret_cast", 0, 0, 0))
			);
			init_expr = ASTNode::emplace_node<ExpressionNode>(
				UnaryOperatorNode(Token(Token::Type::Operator, "*", 0, 0, 0), cast_expr, true)
			);
		}
		
		auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_decl_node, init_expr);

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
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(node.label_name())}, node.goto_token()));
	}

	void visitLabelStatementNode(const LabelStatementNode& node) {
		// Generate Label IR instruction with the label name
		std::string label_name(node.label_name());
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(label_name)}, node.label_token()));
	}

	void visitTryStatementNode(const TryStatementNode& node) {
		// Generate try-catch-finally structure
		// For now, we'll generate a simplified version that doesn't actually implement exception handling
		// but allows the code to compile and run

		// Generate unique labels for exception handling using StringBuilder
		static size_t try_counter = 0;
		size_t current_try_id = try_counter++;
		
		// Create and commit each label separately to avoid StringBuilder overlap
		StringBuilder handlers_sb;
		handlers_sb.append("__try_handlers_").append(current_try_id);
		std::string_view handlers_label = handlers_sb.commit();
		
		StringBuilder end_sb;
		end_sb.append("__try_end_").append(current_try_id);
		std::string_view end_label = end_sb.commit();

		// Emit TryBegin marker
		ir_.addInstruction(IrInstruction(IrOpcode::TryBegin, BranchOp{.target_label = StringTable::getOrInternStringHandle(handlers_label)}, node.try_token()));

		// Visit try block
		visit(node.try_block());

		// Emit TryEnd marker
		ir_.addInstruction(IrOpcode::TryEnd, {}, node.try_token());

		// Jump to end after successful try block execution
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));

		// Emit label for exception handlers
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(handlers_label)}, node.try_token()));

		// Visit catch clauses
		for (size_t catch_index = 0; catch_index < node.catch_clauses().size(); ++catch_index) {
			const auto& catch_clause_node = node.catch_clauses()[catch_index];
			const auto& catch_clause = catch_clause_node.as<CatchClauseNode>();
			
			// Generate unique label for this catch handler end using StringBuilder
			StringBuilder catch_end_sb;
			catch_end_sb.append("__catch_end_").append(current_try_id).append("_").append(catch_index);
			std::string_view catch_end_label = catch_end_sb.commit();

			// If this is a typed catch (not catch(...))
			if (!catch_clause.is_catch_all()) {
				const auto& exception_decl = *catch_clause.exception_declaration();
				const auto& decl = exception_decl.as<DeclarationNode>();
				const auto& type_node = decl.type_node().as<TypeSpecifierNode>();

				// Get type information
				TypeIndex type_index = type_node.type_index();
				
				// Allocate a temporary for the caught exception
				TempVar exception_temp = var_counter.next();
				
				// Emit CatchBegin marker with exception type and qualifiers
				CatchBeginOp catch_op;
				catch_op.exception_temp = exception_temp;
				catch_op.type_index = type_index;
				catch_op.exception_type = type_node.type();  // Store the Type enum for built-in types
				catch_op.catch_end_label = catch_end_label;
				catch_op.is_const = type_node.is_const();
				catch_op.is_reference = type_node.is_lvalue_reference();
				catch_op.is_rvalue_reference = type_node.is_rvalue_reference();
				catch_op.is_catch_all = false;  // This is a typed catch, not catch(...)
				ir_.addInstruction(IrInstruction(IrOpcode::CatchBegin, std::move(catch_op), catch_clause.catch_token()));

				// Add the exception variable to the symbol table for the catch block scope
				symbol_table.enter_scope(ScopeType::Block);
				
				// Register the exception parameter in the symbol table
				std::string_view exception_var_name = decl.identifier_token().value();
				if (!exception_var_name.empty()) {
					// Create a variable declaration for the exception parameter
					VariableDeclOp decl_op;
					decl_op.type = type_node.type();
					decl_op.size_in_bits = static_cast<int>(type_node.size_in_bits());
					decl_op.var_name = StringTable::getOrInternStringHandle(exception_var_name);
					
					// Create a TypedValue for the initializer
					TypedValue init_value;
					init_value.type = type_node.type();
					init_value.size_in_bits = static_cast<int>(type_node.size_in_bits());
					init_value.value = exception_temp;
					init_value.is_reference = type_node.is_reference();
					decl_op.initializer = init_value;
					
					decl_op.is_reference = type_node.is_reference();
					decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
					decl_op.is_array = false;
					decl_op.custom_alignment = 0;
					
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), decl.identifier_token()));
					
					// Add to symbol table
					symbol_table.insert(exception_var_name, exception_decl);
				}
			} else {
				// catch(...) - catches all exceptions
				CatchBeginOp catch_op;
				catch_op.exception_temp = TempVar(0);
				catch_op.type_index = TypeIndex(0);
				catch_op.exception_type = Type::Void;  // No specific type for catch(...)
				catch_op.catch_end_label = catch_end_label;
				catch_op.is_const = false;
				catch_op.is_reference = false;
				catch_op.is_rvalue_reference = false;
				catch_op.is_catch_all = true;  // This IS catch(...)
				ir_.addInstruction(IrOpcode::CatchBegin, std::move(catch_op), catch_clause.catch_token());
				symbol_table.enter_scope(ScopeType::Block);
			}

			// Visit catch block body
			visit(catch_clause.body());

			// Emit CatchEnd marker
			ir_.addInstruction(IrOpcode::CatchEnd, {}, catch_clause.catch_token());

			// Exit catch block scope
			symbol_table.exit_scope();

			// Jump to end after catch block
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, catch_clause.catch_token()));

			// Emit catch end label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(catch_end_label)}, catch_clause.catch_token()));
		}

		// Emit end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));
	}

	void visitThrowStatementNode(const ThrowStatementNode& node) {
		if (node.is_rethrow()) {
			// throw; (rethrow current exception)
			ir_.addInstruction(IrOpcode::Rethrow, {}, node.throw_token());
		} else {
			// throw expression;
			const auto& expr = *node.expression();
			
			// Generate code for the expression to throw
			auto expr_operands = visitExpressionNode(expr.as<ExpressionNode>());
			
			// Extract type information from the operands
			// operands format: [type, size, value_or_temp_var] - always 3 elements
			if (expr_operands.size() < 3) {
				FLASH_LOG(Codegen, Error, "Invalid expression operands for throw statement");
				return;
			}
			
			Type expr_type = std::get<Type>(expr_operands[0]);
			size_t type_size = std::get<int>(expr_operands[1]);
			
			// Extract TypeIndex from expression operands (now at position 3 since all operands have 4 elements)
			TypeIndex exception_type_index = 0;
			if (expr_operands.size() >= 4 && std::holds_alternative<unsigned long long>(expr_operands[3])) {
				exception_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(expr_operands[3]));
			}
			
			// Create ThrowOp with typed data
			ThrowOp throw_op;
			throw_op.type_index = exception_type_index;
			throw_op.exception_type = expr_type;  // Store the actual Type enum
			throw_op.size_in_bytes = type_size / 8;  // Convert bits to bytes
			throw_op.is_rvalue = true;  // Default to rvalue for now
			
			// Handle the value - it can be a TempVar, immediate int, or immediate float
			// All these types are compatible with IrValue variant
			if (std::holds_alternative<TempVar>(expr_operands[2])) {
				throw_op.exception_value = std::get<TempVar>(expr_operands[2]);
			} else if (std::holds_alternative<unsigned long long>(expr_operands[2])) {
				throw_op.exception_value = std::get<unsigned long long>(expr_operands[2]);
			} else if (std::holds_alternative<double>(expr_operands[2])) {
				throw_op.exception_value = std::get<double>(expr_operands[2]);
			} else {
				// Unknown operand type - log warning and default to zero value
				FLASH_LOG(Codegen, Warning, "Unknown operand type in throw expression, defaulting to zero");
				throw_op.exception_value = static_cast<unsigned long long>(0);
			}
			
			ir_.addInstruction(IrInstruction(IrOpcode::Throw, std::move(throw_op), node.throw_token()));
		}
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
			// Use StringBuilder to create a persistent string_view
			// (string_view in GlobalVariableDeclOp would dangle if we used local std::string)
			StringBuilder sb;
			if (is_static_local) {
				// Mangle name as: function_name.variable_name
				sb.append(current_function_name_).append(".").append(decl.identifier_token().value());
			} else {
				// For global variables, include namespace path for proper mangling
				if (!current_namespace_stack_.empty()) {
					// Check if we're in an anonymous namespace
					bool in_anonymous_ns = false;
					for (const auto& ns : current_namespace_stack_) {
						if (ns.empty()) {
							in_anonymous_ns = true;
							break;
						}
					}
					
					// For variables in anonymous namespaces with Itanium mangling,
					// we need to generate a unique mangled name
					if (in_anonymous_ns && NameMangling::g_mangling_style == NameMangling::ManglingStyle::Itanium) {
						// Generate proper Itanium mangling for anonymous namespace variable
						sb.append("_ZN");  // Start nested name
						for (const auto& ns : current_namespace_stack_) {
							if (ns.empty()) {
								// Anonymous namespace: use _GLOBAL__N_1
								sb.append("12_GLOBAL__N_1");
							} else {
								sb.append(std::to_string(ns.size())).append(ns);
							}
						}
						// Add variable name
						std::string_view var_id = decl.identifier_token().value();
						sb.append(std::to_string(var_id.size())).append(var_id);
						sb.append("E");  // End nested name
					} else {
						// For MSVC or named namespaces, use namespace::variable format
						for (const auto& ns : current_namespace_stack_) {
							sb.append(ns).append("::");
						}
						sb.append(decl.identifier_token().value());
					}
				} else {
					sb.append(decl.identifier_token().value());
				}
			}
			std::string_view var_name_view = sb.commit();
			// Phase 3: Intern the string using StringTable
			StringHandle var_name = StringTable::getOrInternStringHandle(var_name_view);

			// Store mapping from simple name to mangled name for later lookups
			// This is needed for anonymous namespace variables
			// Phase 4: Using StringHandle for both key and value
			StringHandle simple_name_handle = StringTable::getOrInternStringHandle(decl.identifier_token().value());
			if (var_name_view != decl.identifier_token().value()) {
				global_variable_names_[simple_name_handle] = var_name;
			}

			// Create GlobalVariableDeclOp
			GlobalVariableDeclOp op;
			op.type = type_node.type();
			op.size_in_bits = static_cast<int>(type_node.size_in_bits());
			op.var_name = var_name;  // Phase 3: Now using StringHandle
			op.element_count = 1;  // Default for scalars
			
			// Helper to append a value as raw bytes in little-endian format
			auto appendValueAsBytes = [](std::vector<char>& data, unsigned long long value, size_t byte_count) {
				for (size_t i = 0; i < byte_count; ++i) {
					data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
				}
			};
			
			// Helper to evaluate a constexpr and get the raw value
			auto evalToValue = [&](const ASTNode& expr, Type target_type) -> unsigned long long {
				ConstExpr::EvaluationContext ctx(gSymbolTable);
				auto eval_result = ConstExpr::Evaluator::evaluate(expr, ctx);
				
				if (!eval_result.success) {
					FLASH_LOG(Codegen, Warning, "Non-constant initializer in global variable");
					return 0;
				}
				
				if (target_type == Type::Float) {
					float f = static_cast<float>(eval_result.as_double());
					uint32_t f_bits;
					std::memcpy(&f_bits, &f, sizeof(float));
					return f_bits;
				} else if (target_type == Type::Double || target_type == Type::LongDouble) {
					double d = eval_result.as_double();
					unsigned long long bits;
					std::memcpy(&bits, &d, sizeof(double));
					return bits;
				} else if (std::holds_alternative<double>(eval_result.value)) {
					return static_cast<unsigned long long>(eval_result.as_int());
				} else if (std::holds_alternative<unsigned long long>(eval_result.value)) {
					return std::get<unsigned long long>(eval_result.value);
				} else if (std::holds_alternative<long long>(eval_result.value)) {
					return static_cast<unsigned long long>(std::get<long long>(eval_result.value));
				} else if (std::holds_alternative<bool>(eval_result.value)) {
					return std::get<bool>(eval_result.value) ? 1 : 0;
				}
				return 0;
			};
			
			// Check if this is an array and get element count (product of all dimensions for multidimensional)
			if (decl.is_array() || type_node.is_array()) {
				const auto& dims = decl.array_dimensions();
				if (!dims.empty()) {
					// Calculate total element count as product of all dimensions
					op.element_count = 1;
					for (const auto& dim_expr : dims) {
						ConstExpr::EvaluationContext ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
						if (eval_result.success && eval_result.as_int() > 0) {
							op.element_count *= static_cast<size_t>(eval_result.as_int());
						}
					}
				} else if (type_node.array_size().has_value()) {
					op.element_count = *type_node.array_size();
				}
			}

			// Check if initialized
			size_t element_size = op.size_in_bits / 8;
			if (node.initializer()) {
				const ASTNode& init_node = *node.initializer();
				
				// Handle array initialization with InitializerListNode
				if (init_node.is<InitializerListNode>()) {
					const InitializerListNode& init_list = init_node.as<InitializerListNode>();
					const auto& initializers = init_list.initializers();
					
					op.is_initialized = true;
					op.element_count = initializers.size();
					
					// Build raw bytes for each element
					for (const auto& elem_init : initializers) {
						unsigned long long value = evalToValue(elem_init, type_node.type());
						appendValueAsBytes(op.init_data, value, element_size);
					}
				} else if (init_node.is<ExpressionNode>()) {
					// Single value initialization
					unsigned long long value = evalToValue(init_node, type_node.type());
					op.is_initialized = true;
					appendValueAsBytes(op.init_data, value, element_size);
				} else {
					op.is_initialized = false;
				}
			} else {
				// No explicit initializer provided
				// Check if this is a struct with default member initializers
				if (type_node.type_index() != 0) {
					// This is a user-defined type (struct/class)
					const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->members.empty()) {
						// Check if any members have default initializers
						bool has_default_inits = false;
						for (const auto& member : struct_info->members) {
							if (member.default_initializer.has_value()) {
								has_default_inits = true;
								break;
							}
						}
						
						if (has_default_inits) {
							// Build initial data from default member initializers
							op.is_initialized = true;
							op.init_data.resize(struct_info->total_size, 0);  // Start with zeros
							
							for (const auto& member : struct_info->members) {
								if (member.default_initializer.has_value()) {
									// Evaluate the default initializer
									unsigned long long value = evalToValue(*member.default_initializer, member.type);
									
									// Write the value at the member's offset
									size_t member_size = member.size;
									for (size_t i = 0; i < member_size && (member.offset + i) < op.init_data.size(); ++i) {
										op.init_data[member.offset + i] = static_cast<char>((value >> (i * 8)) & 0xFF);
									}
								}
							}
						} else {
							op.is_initialized = false;
						}
					} else {
						op.is_initialized = false;
					}
				} else {
					op.is_initialized = false;
				}
			}

			ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), decl.identifier_token()));
			// (The parser already added it to the symbol table)
			if (is_static_local) {
				StaticLocalInfo info;
				info.mangled_name = var_name;  // Phase 4: Using StringHandle directly
				info.type = type_node.type();
				info.size_in_bits = static_cast<int>(type_node.size_in_bits());
				// Phase 4: Using StringHandle for key
				StringHandle key = StringTable::getOrInternStringHandle(decl.identifier_token().value());
				static_local_names_[key] = info;
			}

			return;
		}

		// Handle constexpr variables with function call initializers
		// For constexpr, we try to evaluate at compile-time
		if (node.is_constexpr() && node.initializer().has_value()) {
			const ASTNode& init_node = *node.initializer();
			
			// Check if initializer is a function call (including callable object invocation)
			// Lambda calls come through as MemberFunctionCallNode (operator() calls)
			bool is_function_call = false;
			if (init_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = init_node.as<ExpressionNode>();
				is_function_call = std::holds_alternative<FunctionCallNode>(expr) ||
				                   std::holds_alternative<MemberFunctionCallNode>(expr);
			}
			
			if (is_function_call) {
				// Try to evaluate the function call at compile time
				ConstExpr::EvaluationContext ctx(symbol_table);
				auto eval_result = ConstExpr::Evaluator::evaluate(init_node, ctx);
				
				if (eval_result.success) {
					// Insert into symbol table first
					if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
						assert(false && "Expected identifier to be unique");
					}
					
					// Generate variable declaration with compile-time value
					VariableDeclOp decl_op;
					decl_op.type = type_node.type();
					decl_op.size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
					decl_op.var_name = StringTable::getOrInternStringHandle(decl.identifier_token().value());
					decl_op.custom_alignment = static_cast<unsigned long long>(decl.custom_alignment());
					decl_op.is_reference = type_node.is_reference();
					decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
					decl_op.is_array = false;
					
					// Set the compile-time evaluated initializer
					if (std::holds_alternative<long long>(eval_result.value)) {
						decl_op.initializer = TypedValue{type_node.type(), decl_op.size_in_bits, 
							static_cast<unsigned long long>(std::get<long long>(eval_result.value))};
					} else if (std::holds_alternative<unsigned long long>(eval_result.value)) {
						decl_op.initializer = TypedValue{type_node.type(), decl_op.size_in_bits, 
							std::get<unsigned long long>(eval_result.value)};
					} else if (std::holds_alternative<double>(eval_result.value)) {
						double d = std::get<double>(eval_result.value);
						if (type_node.type() == Type::Float) {
							float f = static_cast<float>(d);
							uint32_t bits;
							std::memcpy(&bits, &f, sizeof(float));
							decl_op.initializer = TypedValue{Type::Float, 32, static_cast<unsigned long long>(bits)};
						} else {
							unsigned long long bits;
							std::memcpy(&bits, &d, sizeof(double));
							decl_op.initializer = TypedValue{Type::Double, 64, bits};
						}
					}
					
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), node.declaration().identifier_token()));
					return;  // Done - constexpr variable initialized at compile time
				}
				// If evaluation failed, fall through to runtime evaluation
				// This is allowed - the variable just won't be usable in other constexpr contexts
			}
		}

		// Handle local variable
		// Create variable declaration operands
		// Format: [type, size_in_bits, var_name, custom_alignment, is_ref, is_rvalue_ref, is_array, ...]
		std::vector<IrOperand> operands;
		operands.emplace_back(type_node.type());
		// For pointers, allocate 64 bits (pointer size on x64), not the pointed-to type size
		int size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
		operands.emplace_back(size_in_bits);
		operands.emplace_back(StringTable::getOrInternStringHandle(decl.identifier_token().value()));
		operands.emplace_back(static_cast<unsigned long long>(decl.custom_alignment()));
		operands.emplace_back(type_node.is_reference());
		operands.emplace_back(type_node.is_rvalue_reference());
		operands.emplace_back(decl.is_array());  // Add is_array flag

		// For arrays, calculate total element count (product of all dimensions for multidimensional arrays)
		size_t array_count = 0;
		if (decl.is_array()) {
			const auto& dims = decl.array_dimensions();
			if (!dims.empty()) {
				// Calculate total element count as product of all dimensions
				array_count = 1;
				for (const auto& dim_expr : dims) {
					ConstExpr::EvaluationContext ctx(symbol_table);
					auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
					
					if (eval_result.success) {
						long long dim_size = eval_result.as_int();
						if (dim_size > 0) {
							array_count *= static_cast<size_t>(dim_size);
						} else {
							array_count = 0;
							break;
						}
					} else {
						array_count = 0;
						break;
					}
				}
				
				// Add element type, size, and count as operands
				operands.emplace_back(type_node.type());  // element type
				operands.emplace_back(size_in_bits);      // element size
				operands.emplace_back(static_cast<unsigned long long>(array_count));
			} else if (decl.is_unsized_array() && node.initializer().has_value()) {
				// Unsized array - get size from initializer list
				const ASTNode& init_node = *node.initializer();
				if (init_node.is<InitializerListNode>()) {
					const InitializerListNode& init_list = init_node.as<InitializerListNode>();
					array_count = init_list.initializers().size();
					// Add the inferred size as an operand
					operands.emplace_back(type_node.type());  // element type
					operands.emplace_back(size_in_bits);      // element size
					operands.emplace_back(static_cast<unsigned long long>(array_count));
				}
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
				if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
					assert(false && "Expected identifier to be unique");
				}

				// Add the variable declaration without initializer
				VariableDeclOp decl_op;
				decl_op.type = type_node.type();
				decl_op.size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
				decl_op.var_name = StringTable::getOrInternStringHandle(decl.identifier_token().value());
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
								FLASH_LOG(Codegen, Error, "Cannot instantiate abstract class '", type_info.name(), "'");
								assert(false && "Cannot instantiate abstract class");
							}

							const auto& initializers = init_list.initializers();

							// Check if this is a designated initializer list or aggregate initialization
							// Designated initializers always use direct member initialization
							bool use_direct_member_init = init_list.has_any_designated();
							
							// Check if there's a constructor that matches the number of initializers
							// For aggregate initialization Point{1, 2}, we need a constructor with 2 parameters
							// If no matching constructor exists, use direct member initialization
							// Also consider constructors with default arguments
							bool has_matching_constructor = false;
							const ConstructorDeclarationNode* matching_ctor = nullptr;
							if (!use_direct_member_init && struct_info.hasAnyConstructor()) {
								size_t num_initializers = initializers.size();
								for (const auto& func : struct_info.member_functions) {
									if (func.is_constructor) {
										// Get parameter count from the function declaration
										if (func.function_decl.is<FunctionDeclarationNode>()) {
											const auto& func_decl = func.function_decl.as<FunctionDeclarationNode>();
											size_t param_count = func_decl.parameter_nodes().size();
											if (param_count == num_initializers) {
												has_matching_constructor = true;
												break;
											}
										} else if (func.function_decl.is<ConstructorDeclarationNode>()) {
											const auto& ctor_decl = func.function_decl.as<ConstructorDeclarationNode>();
											const auto& params = ctor_decl.parameter_nodes();
											size_t param_count = params.size();
											
											// Skip copy constructor and move constructor for brace initialization
											// Copy/move constructors should only be used when the initializer is of the same struct type
											if (param_count == 1 && params.size() == 1 && params[0].is<DeclarationNode>()) {
												const auto& param_decl = params[0].as<DeclarationNode>();
												const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
												
												// Skip if this is a copy constructor (reference to same struct type)
												if (param_type.is_reference() && param_type.type() == Type::Struct) {
													// Check if the initializer is actually of this struct type
													bool init_is_struct_of_same_type = false;
													if (num_initializers == 1) {
														const ASTNode& init_expr = initializers[0];
														if (init_expr.is<ExpressionNode>()) {
															const auto& expr = init_expr.as<ExpressionNode>();
															// Get the type of the initializer expression
															if (std::holds_alternative<IdentifierNode>(expr)) {
																const auto& ident = std::get<IdentifierNode>(expr);
																std::optional<ASTNode> symbol = symbol_table.lookup(ident.name());
																if (symbol.has_value()) {
																	if (const DeclarationNode* decl = get_decl_from_symbol(*symbol)) {
																		const TypeSpecifierNode& init_type = decl->type_node().as<TypeSpecifierNode>();
																		if (init_type.type() == Type::Struct && 
																			init_type.type_index() == param_type.type_index()) {
																			init_is_struct_of_same_type = true;
																		}
																	}
																}
															}
														}
													}
													if (!init_is_struct_of_same_type) {
														// Skip copy constructor - initializer is not of the same struct type
														continue;
													}
												}
												
												// Skip if this is a move constructor (rvalue reference to same struct type)
												if (param_type.is_rvalue_reference() && param_type.type() == Type::Struct) {
													// Check if the initializer is actually of this struct type
													bool init_is_struct_of_same_type = false;
													if (num_initializers == 1) {
														const ASTNode& init_expr = initializers[0];
														if (init_expr.is<ExpressionNode>()) {
															const auto& expr = init_expr.as<ExpressionNode>();
															// For move constructor, we only use it for rvalue expressions of same type
															// For now, skip it for brace initialization unless it's explicitly a move
															if (std::holds_alternative<IdentifierNode>(expr)) {
																// Simple identifier - not an rvalue, don't match move constructor
																continue;
															}
														}
													}
													if (!init_is_struct_of_same_type) {
														// Skip move constructor for non-matching types
														continue;
													}
												}
											}
											
											// Exact match
											if (param_count == num_initializers) {
												has_matching_constructor = true;
												matching_ctor = &ctor_decl;
												break;
											}
											
											// Check if constructor has default arguments that cover the gap
											if (param_count > num_initializers) {
												// Check if parameters from num_initializers onwards all have defaults
												bool all_have_defaults = true;
												for (size_t i = num_initializers; i < param_count; ++i) {
													if (params[i].is<DeclarationNode>()) {
														if (!params[i].as<DeclarationNode>().has_default_value()) {
															all_have_defaults = false;
															break;
														}
													} else {
														all_have_defaults = false;
														break;
													}
												}
												if (all_have_defaults) {
													has_matching_constructor = true;
													matching_ctor = &ctor_decl;
													break;
												}
											}
										}
									}
								}
							}

							if (has_matching_constructor) {
								// Generate constructor call with parameters from initializer list
								ConstructorCallOp ctor_op;
								ctor_op.struct_name = type_info.name();
								ctor_op.object = StringTable::getOrInternStringHandle(decl.identifier_token().value());

								// Get constructor parameter types for reference handling
								const auto& ctor_params = matching_ctor ? matching_ctor->parameter_nodes() : std::vector<ASTNode>{};
								
								// Add each initializer as a constructor parameter
								size_t arg_index = 0;
								for (const ASTNode& init_expr : initializers) {
									if (init_expr.is<ExpressionNode>()) {
										// Get the parameter type for this argument (if it exists)
										const TypeSpecifierNode* param_type = nullptr;
										if (arg_index < ctor_params.size() && ctor_params[arg_index].is<DeclarationNode>()) {
											param_type = &ctor_params[arg_index].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
										}
										
										auto init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
										// init_operands = [type, size, value]
										if (init_operands.size() >= 3) {
											TypedValue tv;
											
											// Check if parameter expects a reference and argument is an identifier
											bool is_ident = std::holds_alternative<IdentifierNode>(init_expr.as<ExpressionNode>());
											bool param_is_ref = param_type && (param_type->is_reference() || param_type->is_rvalue_reference());
											
											if (param_is_ref && is_ident) {
												const auto& identifier = std::get<IdentifierNode>(init_expr.as<ExpressionNode>());
												std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
												if (symbol.has_value()) {
													bool is_decl = symbol->is<DeclarationNode>();
													bool is_vardecl = symbol->is<VariableDeclarationNode>();
												}
												
												const DeclarationNode* arg_decl = nullptr;
												if (symbol.has_value() && symbol->is<DeclarationNode>()) {
													arg_decl = &symbol->as<DeclarationNode>();
												} else if (symbol.has_value() && symbol->is<VariableDeclarationNode>()) {
													arg_decl = &symbol->as<VariableDeclarationNode>().declaration();
												}
												
												if (arg_decl) {
													const auto& arg_type = arg_decl->type_node().as<TypeSpecifierNode>();
													
													if (arg_type.is_reference() || arg_type.is_rvalue_reference()) {
														// Argument is already a reference - just pass it through
														tv = toTypedValue(init_operands);
													} else {
														// Argument is a value - take its address
														TempVar addr_var = var_counter.next();
														AddressOfOp addr_op;
														addr_op.result = addr_var;
														addr_op.operand.type = arg_type.type();
														addr_op.operand.size_in_bits = static_cast<int>(arg_type.size_in_bits());
														addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
														addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
														ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
														
														// Create TypedValue with the address
														tv.type = arg_type.type();
														tv.size_in_bits = 64;  // Pointer size
														tv.value = addr_var;
														tv.is_reference = true;  // Mark as reference parameter
														tv.type_index = arg_type.type_index();  // Preserve type_index for struct references
													}
												} else {
													// Not a simple identifier or not found - use as-is
													tv = toTypedValue(init_operands);
												}
											} else {
												// Not a reference parameter or not an identifier - use as-is
												tv = toTypedValue(init_operands);
											}
											
											ctor_op.arguments.push_back(std::move(tv));
										} else {
											assert(false && "Invalid initializer operands - expected [type, size, value]");
										}
									} else {
										assert(false && "Initializer must be an ExpressionNode");
									}
									arg_index++;
								}
								
								// Fill in default arguments for missing parameters
								if (matching_ctor) {
									const auto& params = matching_ctor->parameter_nodes();
									size_t num_explicit_args = ctor_op.arguments.size();
									for (size_t i = num_explicit_args; i < params.size(); ++i) {
										if (params[i].is<DeclarationNode>()) {
											const auto& param_decl = params[i].as<DeclarationNode>();
											if (param_decl.has_default_value()) {
												const ASTNode& default_node = param_decl.default_value();
												if (default_node.is<ExpressionNode>()) {
													auto default_operands = visitExpressionNode(default_node.as<ExpressionNode>());
													if (default_operands.size() >= 3) {
														TypedValue default_arg = toTypedValue(default_operands);
														ctor_op.arguments.push_back(std::move(default_arg));
													}
												}
											}
										}
									}
								}
								
								ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
							} else {
								// No constructor - use direct member initialization
								// Build a map of member names to initializer expressions
								std::unordered_map<StringHandle, const ASTNode*> member_values;
								size_t positional_index = 0;

								for (size_t i = 0; i < initializers.size(); ++i) {
									if (init_list.is_designated(i)) {
										// Designated initializer - use member name
										StringHandle member_name = init_list.member_name(i);
										member_values[member_name] = &initializers[i];
									} else {
										// Positional initializer - map to member by index
										if (positional_index < struct_info.members.size()) {
											StringHandle member_name = struct_info.members[positional_index].getName();
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
									StringHandle member_name_handle = member.getName();
									if (member_values.count(member_name_handle)) {
										const ASTNode& init_expr = *member_values[member_name_handle];
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
											} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
												member_value = std::get<StringHandle>(init_operands[2]);
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
									member_store.object = StringTable::getOrInternStringHandle(decl.identifier_token().value());
									member_store.member_name = member.getName();
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
									std::string(StringTable::getStringView(type_info.name()))
								);
							}
						}
					}
				}
				return; // Early return - we've already added the variable declaration
			} else if (init_node.is<LambdaExpressionNode>()) {
				// Lambda expression initializer (direct)
				const auto& lambda = init_node.as<LambdaExpressionNode>();
				// Pass the target variable name so captures are stored in the right variable
				std::string_view var_name = decl.identifier_token().value();
				generateLambdaExpressionIr(lambda, var_name);
				
				// Check if target type is a function pointer - if so, store __invoke address
				if (type_node.is_function_pointer() && lambda.captures().empty()) {
					TempVar func_addr_var = generateLambdaInvokeFunctionAddress(lambda);
					operands.emplace_back(Type::FunctionPointer);
					operands.emplace_back(64);
					operands.emplace_back(func_addr_var);
				}
				// Lambda expression already emitted VariableDecl, so return early
				if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
					assert(false && "Expected identifier to be unique");
				}
				return;
			} else if (init_node.is<ExpressionNode>() && 
			           std::holds_alternative<LambdaExpressionNode>(init_node.as<ExpressionNode>())) {
				// Lambda expression wrapped in ExpressionNode
				const auto& lambda = std::get<LambdaExpressionNode>(init_node.as<ExpressionNode>());
				// Pass the target variable name so captures are stored in the right variable
				std::string_view var_name = decl.identifier_token().value();
				generateLambdaExpressionIr(lambda, var_name);
				
				// Check if target type is a function pointer - if so, store __invoke address
				if (type_node.is_function_pointer() && lambda.captures().empty()) {
					TempVar func_addr_var = generateLambdaInvokeFunctionAddress(lambda);
					operands.emplace_back(Type::FunctionPointer);
					operands.emplace_back(64);
					operands.emplace_back(func_addr_var);
				}
				// Lambda expression already emitted VariableDecl, so return early
				if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
					assert(false && "Expected identifier to be unique");
				}
				return;
			} else {
				// Regular expression initializer
				// For struct types with copy constructors, check if it's an rvalue (function return)
				// before deciding whether to use constructor call or direct initialization
				// However, if the struct doesn't have a constructor, we need to evaluate the expression
				// IMPORTANT: Pointer types (Base* pb = &b) should process initializer normally
				bool is_struct_with_constructor = false;
				if (type_node.type() == Type::Struct && type_node.pointer_depth() == 0 && type_node.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
					if (type_info.struct_info_ && type_info.struct_info_->hasConstructor()) {
						is_struct_with_constructor = true;
					}
				}
				
				bool is_copy_init_for_struct = (type_node.type() == Type::Struct && 
				                                 type_node.pointer_depth() == 0 &&
				                                 node.initializer() && 
				                                 init_node.is<ExpressionNode>() && 
				                                 !init_node.is<InitializerListNode>() &&
				                                 is_struct_with_constructor);
				
				
				if (!is_copy_init_for_struct) {
					auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
					
					// Check if we need implicit conversion via conversion operator
					// This handles cases like: int i = myStruct; where myStruct has operator int()
					if (init_operands.size() >= 3) {
						Type init_type = std::get<Type>(init_operands[0]);
						int init_size = std::get<int>(init_operands[1]);
						TypeIndex init_type_index = 0;  // Will be set below if type_index is available
						
						// Extract type_index if available (4th element in init_operands)
						if (init_operands.size() >= 4 && std::holds_alternative<unsigned long long>(init_operands[3])) {
							init_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(init_operands[3]));
						}
						
						// Check if source and target types differ and source is a struct
						bool need_conversion = (init_type != type_node.type()) || 
						                       (init_type == Type::Struct && init_type_index != type_node.type_index());
						
						if (need_conversion && init_type == Type::Struct && init_type_index < gTypeInfo.size()) {
							const TypeInfo& source_type_info = gTypeInfo[init_type_index];
							const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
							
							// Look for a conversion operator to the target type
							const StructMemberFunction* conv_op = findConversionOperator(
								source_struct_info, type_node.type(), type_node.type_index());
							
							if (conv_op) {
								FLASH_LOG(Codegen, Debug, "Found conversion operator from ", 
									StringTable::getStringView(source_type_info.name()), 
									" to target type");
								
								// Generate call to the conversion operator
								// The conversion operator is a const member function taking no parameters
								TempVar result_var = var_counter.next();
								
								// Get the source variable value
								IrValue source_value = std::visit([](auto&& arg) -> IrValue {
									using T = std::decay_t<decltype(arg)>;
									if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
									              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
										return arg;
									} else {
										return 0ULL;
									}
								}, init_operands[2]);
								
								// Build the mangled name for the conversion operator
								StringHandle struct_name_handle = source_type_info.name();
								std::string_view struct_name = StringTable::getStringView(struct_name_handle);
								
								// Generate the call using CallOp (member function call)
								if (conv_op->function_decl.is<FunctionDeclarationNode>()) {
									const auto& func_decl = conv_op->function_decl.as<FunctionDeclarationNode>();
									std::string_view mangled_name;
									if (func_decl.has_mangled_name()) {
										mangled_name = func_decl.mangled_name();
									} else {
										// Generate mangled name for the conversion operator
										// Use the function's parent struct name, not the source type name,
										// because the conversion operator may be inherited from a base class
										// and we need to call the version defined in the base class.
										std::string_view operator_struct_name = func_decl.parent_struct_name();
										if (operator_struct_name.empty()) {
											operator_struct_name = struct_name;
										}
										mangled_name = generateMangledNameForCall(func_decl, operator_struct_name);
									}
									
									CallOp call_op;
									call_op.result = result_var;
									call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
									call_op.return_type = type_node.type();
									call_op.return_size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
									call_op.return_type_index = type_node.type_index();
									call_op.is_member_function = true;
									call_op.is_variadic = false;
									
									// For member function calls, first argument is 'this' pointer
									// We need to pass the address of the source object
									if (std::holds_alternative<StringHandle>(source_value)) {
										// It's a variable - take its address
										TempVar this_ptr = var_counter.next();
										AddressOfOp addr_op;
										addr_op.result = this_ptr;
										addr_op.operand.type = init_type;
										addr_op.operand.size_in_bits = init_size;
										addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
										addr_op.operand.value = std::get<StringHandle>(source_value);
										ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
										
										// Add 'this' as first argument
										TypedValue this_arg;
										this_arg.type = init_type;
										this_arg.size_in_bits = 64;  // Pointer size
										this_arg.value = this_ptr;
										this_arg.type_index = init_type_index;
										call_op.args.push_back(std::move(this_arg));
									} else if (std::holds_alternative<TempVar>(source_value)) {
										// It's already a temporary - it might be an address or value
										// For conversion operators, we need the address
										// ASSUMPTION: For struct types, TempVars at this point in variable initialization
										// represent the address of the object (not the object value itself).
										// This is because visitExpressionNode returns addresses for struct identifiers.
										TypedValue this_arg;
										this_arg.type = init_type;
										this_arg.size_in_bits = 64;  // Pointer size for 'this'
										this_arg.value = std::get<TempVar>(source_value);
										this_arg.type_index = init_type_index;
										call_op.args.push_back(std::move(this_arg));
									}
									
									ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), decl.identifier_token()));
									
									// Replace init_operands with the result of the conversion
									init_operands.clear();
									init_operands.emplace_back(type_node.type());
									init_operands.emplace_back(type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits()));
									init_operands.emplace_back(result_var);
								}
							}
						}
					}
					
					operands.insert(operands.end(), init_operands.begin(), init_operands.end());
				} else {
					// For struct with constructor, evaluate the initializer to check if it's an rvalue
					auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
					// Check if this is an rvalue (TempVar) - function return value
					bool is_rvalue = (init_operands.size() >= 3 && std::holds_alternative<TempVar>(init_operands[2]));
					if (is_rvalue) {
						// For rvalues, use direct initialization (no constructor call)
						operands.insert(operands.end(), init_operands.begin(), init_operands.end());
					}
					// For lvalues, skip adding to operands - will use constructor call below
				}
			}
		}

		if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
			assert(false && "Expected identifier to be unique");
		}

		VariableDeclOp decl_op;
		decl_op.type = type_node.type();
		// References and pointers are both 64-bit (pointer size on x64)
		decl_op.size_in_bits = (type_node.pointer_depth() > 0 || type_node.is_reference()) ? 64 : static_cast<int>(type_node.size_in_bits());
		decl_op.var_name = StringTable::getOrInternStringHandle(decl.identifier_token().value());
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
		
		// Track whether the variable was already initialized with an rvalue (function return value)
		// Check if the VariableDecl has an initializer set BEFORE we move decl_op
		bool has_rvalue_initializer = decl_op.initializer.has_value();
		
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), node.declaration().identifier_token()));

		// Handle array initialization with initializer list
		if (decl.is_array() && node.initializer().has_value()) {
			const ASTNode& init_node = *node.initializer();
			if (init_node.is<InitializerListNode>()) {
				const InitializerListNode& init_list = init_node.as<InitializerListNode>();
				const auto& initializers = init_list.initializers();
				
				// Generate store for each element
				for (size_t i = 0; i < initializers.size(); i++) {
					// Evaluate the initializer expression
					auto init_operands = visitExpressionNode(initializers[i].as<ExpressionNode>());
					
					// Generate array element store: arr[i] = value
					ArrayStoreOp store_op;
					store_op.element_type = type_node.type();
					store_op.element_size_in_bits = size_in_bits;
					store_op.array = StringTable::getOrInternStringHandle(decl.identifier_token().value());
					store_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					store_op.value = toTypedValue(init_operands);
					store_op.member_offset = 0;
					store_op.is_pointer_to_array = false;  // Local arrays are actual arrays, not pointers
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(store_op), 
						node.declaration().identifier_token()));
				}
			}
		}

		// If this is a struct type with a constructor, generate a constructor call
		// IMPORTANT: Only for non-pointer struct types. Pointers are just addresses, no constructor needed.
		// IMPORTANT: References also don't need constructor calls - they just bind to existing objects
		if (type_node.type() == Type::Struct && type_node.pointer_depth() == 0 && !type_node.is_reference() && !type_node.is_rvalue_reference()) {
			TypeIndex type_index = type_node.type_index();
			if (type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_index];
				
				// Skip incomplete template instantiations (with "_unknown" in name)
				std::string_view type_name = StringTable::getStringView(type_info.name());
				if (type_name.find("_unknown") != std::string_view::npos) {
					FLASH_LOG(Codegen, Debug, "Skipping constructor call for '", type_name, "' with _unknown in name (incomplete instantiation)");
					// Don't generate constructor calls for incomplete template instantiations
					// Just treat them as plain data (no initialization)
					// The variable declaration was already emitted above
					return;
				}
				
				if (type_info.struct_info_) {
					// Check if this is an abstract class (only for non-pointer types)
					if (type_info.struct_info_->is_abstract && type_node.pointer_levels().empty()) {
						FLASH_LOG(Codegen, Error, "Cannot instantiate abstract class '", type_info.name(), "'");
						assert(false && "Cannot instantiate abstract class");
					}

					if (type_info.struct_info_->hasConstructor()) {
						FLASH_LOG(Codegen, Debug, "Struct ", type_info.name(), " has constructor");
						// Check if we have a copy/move initializer like "Tiny t2 = t;"
						// Skip if the variable was already initialized with an rvalue (function return)
						bool has_copy_init = false;
						bool has_direct_ctor_call = false;
						const ConstructorCallNode* direct_ctor = nullptr;
						
						FLASH_LOG(Codegen, Debug, "has_rvalue_initializer=", has_rvalue_initializer, " node.initializer()=", (bool)node.initializer());
						if (node.initializer() && !has_rvalue_initializer) {
							const ASTNode& init_node = *node.initializer();
							if (init_node.is<ExpressionNode>()) {
								const auto& expr = init_node.as<ExpressionNode>();
								FLASH_LOG(Codegen, Debug, "Checking initializer for ", decl.identifier_token().value());
								// Check if this is a direct constructor call (e.g., S s(x))
								if (std::holds_alternative<ConstructorCallNode>(expr)) {
									has_direct_ctor_call = true;
									direct_ctor = &std::get<ConstructorCallNode>(expr);
									FLASH_LOG(Codegen, Debug, "Found ConstructorCallNode initializer");
								} else if (!init_node.is<InitializerListNode>()) {
									// For copy initialization like "AllSizes b = a;", we need to
									// generate a copy constructor call.
									has_copy_init = true;
								}
							}
						}

						if (has_direct_ctor_call && direct_ctor) {
							// Direct constructor call like S s(x) - process its arguments directly
							FLASH_LOG(Codegen, Debug, "Processing direct constructor call for ", type_info.name());
							// Find the matching constructor to get parameter types for reference handling
							const ConstructorDeclarationNode* matching_ctor = nullptr;
							size_t num_args = 0;
							direct_ctor->arguments().visit([&](ASTNode) { num_args++; });
							
							if (type_info.struct_info_) {
								for (const auto& func : type_info.struct_info_->member_functions) {
									if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
										const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
										const auto& params = ctor_node.parameter_nodes();
										
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
							
							// Create constructor call with the declared variable as the object
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name();
							ctor_op.object = StringTable::getOrInternStringHandle(decl.identifier_token().value());
							
							// Get constructor parameter types for reference handling
							const auto& ctor_params = matching_ctor ? matching_ctor->parameter_nodes() : std::vector<ASTNode>{};
							
							// Process constructor arguments with reference parameter handling
							size_t arg_index = 0;
							direct_ctor->arguments().visit([&](ASTNode argument) {
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
												ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
												
												// Create TypedValue with the address
												tv.type = arg_type.type();
												tv.size_in_bits = 64;  // Pointer size
												tv.value = addr_var;
												tv.is_reference = true;  // Mark as reference parameter
												tv.type_index = arg_type.type_index();  // Preserve type_index for struct references
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
											if (!tv.is_reference) {
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
							
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
						} else if (has_copy_init) {
							// Generate copy constructor call
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name();
							ctor_op.object = StringTable::getOrInternStringHandle(decl.identifier_token().value());

							// Add initializer as copy constructor parameter
							const ASTNode& init_node = *node.initializer();
							auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
							// init_operands = [type, size, value]
							if (init_operands.size() >= 3) {
								TypedValue init_arg = toTypedValue(init_operands);
								ctor_op.arguments.push_back(std::move(init_arg));
							}

							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
						} else if (!has_rvalue_initializer) {
							// No initializer - check if we need to call default constructor
							// Call default constructor if:
							// 1. It's user-defined (not implicit), OR
							// 2. The struct has default member initializers (implicit ctor needs to init them), OR
							// 3. The struct has a vtable (implicit ctor needs to init the vptr)
							const StructMemberFunction* default_ctor = type_info.struct_info_->findDefaultConstructor();
							bool is_implicit_default_ctor = false;
							if (default_ctor && default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
								const auto& ctor_node = default_ctor->function_decl.as<ConstructorDeclarationNode>();
								is_implicit_default_ctor = ctor_node.is_implicit();
							}

							bool needs_default_ctor_call = !is_implicit_default_ctor ||
							                               type_info.struct_info_->hasDefaultMemberInitializers() ||
							                               type_info.struct_info_->has_vtable;

							if (needs_default_ctor_call) {
								// Check if this is an array - need to call constructor for each element
								if (decl.is_array()) {
									// For arrays, we need to call the constructor once for each element
									// Get array size
									size_t array_count = 1;
									auto size_expr = decl.array_size();
									if (size_expr.has_value()) {
										// Evaluate the array size expression using ConstExprEvaluator
										ConstExpr::EvaluationContext ctx(symbol_table);
										auto eval_result = ConstExpr::Evaluator::evaluate(*size_expr, ctx);
										if (eval_result.success) {
											array_count = static_cast<size_t>(eval_result.as_int());
										}
									}
									
									// Generate constructor call for each array element
									for (size_t i = 0; i < array_count; i++) {
										ConstructorCallOp ctor_op;
										ctor_op.struct_name = type_info.name();
										// For arrays, we need to specify the element to construct
										ctor_op.object = StringTable::getOrInternStringHandle(decl.identifier_token().value());
										ctor_op.array_index = i;  // Mark this as an array element constructor call
										
										// If the constructor has parameters with default values, generate the default arguments
										if (default_ctor && default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
											const auto& ctor_node = default_ctor->function_decl.as<ConstructorDeclarationNode>();
											const auto& params = ctor_node.parameter_nodes();
											
											for (const auto& param : params) {
												if (param.is<DeclarationNode>()) {
													const auto& param_decl = param.as<DeclarationNode>();
													if (param_decl.has_default_value()) {
														// Generate IR for the default value expression
														const ASTNode& default_node = param_decl.default_value();
														if (default_node.is<ExpressionNode>()) {
															auto default_operands = visitExpressionNode(default_node.as<ExpressionNode>());
															if (default_operands.size() >= 3) {
																TypedValue default_arg = toTypedValue(default_operands);
																ctor_op.arguments.push_back(std::move(default_arg));
															}
														}
													}
												}
											}
										}
										
										ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
									}
								} else {
									// Single object (non-array) - generate single constructor call
									ConstructorCallOp ctor_op;
									ctor_op.struct_name = type_info.name();
									ctor_op.object = StringTable::getOrInternStringHandle(decl.identifier_token().value());
									
									// If the constructor has parameters with default values, generate the default arguments
									if (default_ctor && default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
										const auto& ctor_node = default_ctor->function_decl.as<ConstructorDeclarationNode>();
										const auto& params = ctor_node.parameter_nodes();
										
										for (const auto& param : params) {
											if (param.is<DeclarationNode>()) {
												const auto& param_decl = param.as<DeclarationNode>();
												if (param_decl.has_default_value()) {
													// Generate IR for the default value expression
													const ASTNode& default_node = param_decl.default_value();
													if (default_node.is<ExpressionNode>()) {
														auto default_operands = visitExpressionNode(default_node.as<ExpressionNode>());
														if (default_operands.size() >= 3) {
															TypedValue default_arg = toTypedValue(default_operands);
															ctor_op.arguments.push_back(std::move(default_arg));
														}
													}
												}
											}
										}
									}
									
									ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
								}
							}
						}
					}
				}
				
				// If this struct has a destructor, register it for automatic cleanup
				if (type_info.struct_info_ && type_info.struct_info_->hasDestructor()) {
					registerVariableWithDestructor(
						std::string(decl.identifier_token().value()),
						std::string(StringTable::getStringView(type_info.name()))
					);
				}
			}
		}
	}

	void visitStructuredBindingNode(const ASTNode& ast_node) {
		const StructuredBindingNode& node = ast_node.as<StructuredBindingNode>();
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Processing structured binding with ", 
		          node.identifiers().size(), " identifiers");
		
		// Step 1: Evaluate the initializer expression and get its type
		const ASTNode& initializer = node.initializer();
		if (!initializer.is<ExpressionNode>()) {
			FLASH_LOG(Codegen, Error, "Structured binding initializer is not an expression");
			return;
		}
		
		auto init_operands = visitExpressionNode(initializer.as<ExpressionNode>());
		if (init_operands.size() < 3) {
			FLASH_LOG(Codegen, Error, "Structured binding initializer produced invalid operands");
			return;
		}
		
		// Extract initializer type information
		Type init_type = std::get<Type>(init_operands[0]);
		int init_size = std::get<int>(init_operands[1]);
		TypeIndex init_type_index = 0;
		
		// Get type_index if available (4th element)
		if (init_operands.size() >= 4 && std::holds_alternative<unsigned long long>(init_operands[3])) {
			init_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(init_operands[3]));
		}
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Initializer type=", (int)init_type, 
		          " type_index=", init_type_index, " ref_qualifier=", (int)node.ref_qualifier());
		
		// Check if this is a reference binding (auto& or auto&&)
		bool is_reference_binding = node.is_lvalue_reference() || node.is_rvalue_reference();
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: is_reference_binding=", is_reference_binding,
		          " is_lvalue_ref=", node.is_lvalue_reference(), " is_rvalue_ref=", node.is_rvalue_reference());
		
		// Step 2: Determine if initializer is an array by checking the symbol table
		bool is_array = false;
		size_t array_size = 0;
		Type array_element_type = init_type;
		int array_element_size = init_size;
		
		// Check if initializer is an identifier (which could be an array variable)
		if (initializer.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = initializer.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr_node)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
				std::optional<ASTNode> symbol = symbol_table.lookup(id_node.name());
				
				if (symbol.has_value()) {
					// Check if it's a DeclarationNode with array information
					if (symbol->is<DeclarationNode>()) {
						const DeclarationNode& decl = symbol->as<DeclarationNode>();
						if (decl.is_array() && decl.array_size().has_value()) {
							// Evaluate array size
							ConstExpr::EvaluationContext ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*decl.array_size(), ctx);
							if (eval_result.success) {
								is_array = true;
								array_size = static_cast<size_t>(eval_result.as_int());
								// Get element type and size from the type specifier
								const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
								array_element_type = type_spec.type();
								array_element_size = static_cast<int>(type_spec.size_in_bits());
								FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Detected array with size ", array_size, 
								          " element_type=", (int)array_element_type, " element_size=", array_element_size);
							}
						}
					}
					// Check if it's a VariableDeclarationNode with array information
					else if (symbol->is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var_decl = symbol->as<VariableDeclarationNode>();
						const DeclarationNode& decl = var_decl.declaration();
						if (decl.is_array() && decl.array_size().has_value()) {
							// Evaluate array size
							ConstExpr::EvaluationContext ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*decl.array_size(), ctx);
							if (eval_result.success) {
								is_array = true;
								array_size = static_cast<size_t>(eval_result.as_int());
								// Get element type and size from the type specifier
								const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
								array_element_type = type_spec.type();
								array_element_size = static_cast<int>(type_spec.size_in_bits());
								FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Detected array with size ", array_size,
								          " element_type=", (int)array_element_type, " element_size=", array_element_size);
							}
						}
					}
				}
			}
		}
		
		// Step 3: Create a hidden temporary variable to hold the initializer
		// Generate unique name for the hidden variable
		TempVar hidden_var = var_counter.next();
		StringBuilder sb;
		sb.append("__structured_binding_e_").append(static_cast<uint64_t>(hidden_var.var_number));
		std::string_view hidden_var_name = sb.commit();
		StringHandle hidden_var_handle = StringTable::createStringHandle(hidden_var_name);
		
		// Declare the hidden variable
		VariableDeclOp hidden_decl_op;
		hidden_decl_op.var_name = hidden_var_handle;
		
		// For arrays, we need to set up the array info properly
		if (is_array) {
			hidden_decl_op.type = array_element_type;
			hidden_decl_op.size_in_bits = array_element_size;
			hidden_decl_op.is_array = true;
			hidden_decl_op.array_element_type = array_element_type;
			hidden_decl_op.array_element_size = array_element_size;
			hidden_decl_op.array_count = array_size;
			// Don't set initializer here for arrays - we'll copy element by element
		} else if (is_reference_binding) {
			// For reference bindings (auto& [a,b] = x), the hidden variable is a reference
			// to the original object, not a copy
			hidden_decl_op.type = init_type;
			hidden_decl_op.size_in_bits = 64;  // Reference is always 64-bit pointer
			hidden_decl_op.is_reference = true;
			hidden_decl_op.is_rvalue_reference = node.is_rvalue_reference();
			
			// Generate addressof for the initializer to get reference
			if (initializer.is<ExpressionNode>()) {
				const ExpressionNode& expr_node = initializer.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(expr_node)) {
					const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
					
					// Generate AddressOf for the identifier
					TempVar addr_temp = var_counter.next();
					AddressOfOp addr_op;
					addr_op.result = addr_temp;
					addr_op.operand.type = init_type;
					addr_op.operand.size_in_bits = init_size;
					addr_op.operand.pointer_depth = 0;
					addr_op.operand.value = StringTable::getOrInternStringHandle(id_node.name());
					ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, addr_op, Token()));
					
					hidden_decl_op.initializer = TypedValue{init_type, 64, addr_temp};
				} else {
					// For other expressions, just use the value and hope for the best
					hidden_decl_op.initializer = toTypedValue(init_operands);
				}
			} else {
				hidden_decl_op.initializer = toTypedValue(init_operands);
			}
		} else {
			hidden_decl_op.type = init_type;
			hidden_decl_op.size_in_bits = init_size;
			hidden_decl_op.initializer = toTypedValue(init_operands);
		}
		
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(hidden_decl_op), Token()));
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Created hidden variable ", hidden_var_name);
		
		// For arrays, copy elements from the source array to the hidden variable
		if (is_array && initializer.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = initializer.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr_node)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
				StringHandle source_array = StringTable::getOrInternStringHandle(id_node.name());
				
				// Copy each element
				for (size_t i = 0; i < array_size; ++i) {
					// Load element from source array
					TempVar element_temp = var_counter.next();
					ArrayAccessOp access_op;
					access_op.result = element_temp;
					access_op.array = source_array;
					access_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					access_op.element_type = array_element_type;
					access_op.element_size_in_bits = array_element_size;
					access_op.is_pointer_to_array = false;
					access_op.member_offset = 0;
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(access_op), Token()));
					
					// Store element to hidden array
					ArrayStoreOp store_op;
					store_op.element_type = array_element_type;
					store_op.element_size_in_bits = array_element_size;
					store_op.array = hidden_var_handle;
					store_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					store_op.value = TypedValue{array_element_type, array_element_size, element_temp};
					store_op.member_offset = 0;
					store_op.is_pointer_to_array = false;
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(store_op), Token()));
				}
			}
		}
		
		// Step 4: Determine decomposition strategy
		if (is_array) {
			// Array decomposition
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Using array decomposition strategy");
			
			// Validate identifier count matches array size
			if (node.identifiers().size() != array_size) {
				FLASH_LOG(Codegen, Error, "Structured binding: number of identifiers (", node.identifiers().size(), 
				          ") does not match array size (", array_size, ")");
				return;
			}
			
			// Create bindings for each array element
			for (size_t i = 0; i < array_size; ++i) {
				StringHandle binding_id = node.identifiers()[i];
				std::string_view binding_name = StringTable::getStringView(binding_id);
				
				FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Creating binding '", binding_name, 
				          "' to array element [", i, "]");
				
				// Create a TypeSpecifierNode for this binding's type
				TypeSpecifierNode binding_type(array_element_type, TypeQualifier::None, 
				                                static_cast<unsigned char>(array_element_size), Token());
				
				// If this is a reference binding (auto& or auto&&), mark the type as a reference
				if (is_reference_binding) {
					if (node.is_lvalue_reference()) {
						binding_type.set_reference(false);  // false = lvalue reference
					} else if (node.is_rvalue_reference()) {
						// For auto&&, bindings to array elements become lvalue references
						binding_type.set_reference(false);  // false = lvalue reference
					}
				}
				
				// Create a synthetic declaration node for the binding
				Token binding_token(Token::Type::Identifier, binding_name, 0, 0, 0);
				ASTNode binding_decl_node = ASTNode::emplace_node<DeclarationNode>(
					ASTNode::emplace_node<TypeSpecifierNode>(binding_type),
					binding_token
				);
				
				// Add to symbol table
				symbol_table.insert(binding_name, binding_decl_node);
				
				// Generate IR for the binding
				if (is_reference_binding) {
					// For reference bindings, create a reference variable that points to the element
					// Compute address of array element: &(hidden_var[i])
					TempVar element_addr = var_counter.next();
					ArrayElementAddressOp addr_op;
					addr_op.result = element_addr;
					addr_op.array = hidden_var_handle;
					addr_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					addr_op.element_type = array_element_type;
					addr_op.element_size_in_bits = array_element_size;
					addr_op.is_pointer_to_array = false;
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(addr_op), binding_token));
					
					// Declare the binding as a reference variable initialized with the address
					VariableDeclOp binding_var_decl;
					binding_var_decl.var_name = binding_id;
					binding_var_decl.type = array_element_type;
					binding_var_decl.size_in_bits = 64;  // References are pointers (64-bit addresses)
					binding_var_decl.is_reference = true;  // Mark as reference
					binding_var_decl.is_rvalue_reference = node.is_rvalue_reference();
					binding_var_decl.initializer = TypedValue{array_element_type, 64, element_addr};
					
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
				} else {
					// For value bindings, load the element value (existing behavior)
					TempVar element_val = var_counter.next();
					ArrayAccessOp load_op;
					load_op.result = element_val;
					load_op.array = hidden_var_handle;
					load_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					load_op.element_type = array_element_type;
					load_op.element_size_in_bits = array_element_size;
					load_op.is_pointer_to_array = false;  // Local array
					load_op.member_offset = 0;
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(load_op), binding_token));
					
					// Now, declare the binding variable with the element value as initializer
					VariableDeclOp binding_var_decl;
					binding_var_decl.var_name = binding_id;
					binding_var_decl.type = array_element_type;
					binding_var_decl.size_in_bits = array_element_size;
					binding_var_decl.initializer = TypedValue{array_element_type, array_element_size, element_val};
					
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
				}
				
				FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Added binding '", binding_name, "' to symbol table");
			}
			
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Successfully created ", array_size, " array bindings");
			return;  // Early return for array case
			
		} else if (init_type != Type::Struct) {
			FLASH_LOG(Codegen, Error, "Structured bindings currently only support struct and array types, got type=", (int)init_type);
			return;
		}
		
		// Step 5: Check for tuple-like decomposition (C++17 protocol)
		// If std::tuple_size<E> is specialized for the type, use tuple-like decomposition
		// Otherwise, fall back to aggregate (struct) decomposition
		if (init_type_index >= gTypeInfo.size()) {
			FLASH_LOG(Codegen, Error, "Invalid type index for structured binding: ", init_type_index);
			return;
		}
		
		const TypeInfo& type_info = gTypeInfo[init_type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		
		if (!struct_info) {
			FLASH_LOG(Codegen, Error, "Type is not a struct for structured binding");
			return;
		}
		
		// Step 5a: Check for tuple-like decomposition protocol (C++17)
		// If std::tuple_size<E> is specialized for the type E, use tuple-like decomposition
		// The protocol requires: std::tuple_size<E>, std::tuple_element<N, E>, and get<N>(e)
		std::string_view type_name_view = StringTable::getStringView(type_info.name());
		
		// Build the expected tuple_size specialization name: "tuple_size_TypeName" or "std::tuple_size_TypeName"
		StringBuilder tuple_size_name_builder;
		tuple_size_name_builder.append("tuple_size_").append(type_name_view);
		std::string_view tuple_size_name = tuple_size_name_builder.commit();
		StringHandle tuple_size_handle = StringTable::getOrInternStringHandle(tuple_size_name);
		
		// Also try with std:: prefix  
		StringBuilder std_tuple_size_name_builder;
		std_tuple_size_name_builder.append("std::tuple_size_").append(type_name_view);
		std::string_view std_tuple_size_name = std_tuple_size_name_builder.commit();
		StringHandle std_tuple_size_handle = StringTable::getOrInternStringHandle(std_tuple_size_name);
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Checking for tuple_size<", type_name_view, 
		          "> as '", tuple_size_name, "' or '", std_tuple_size_name, "'");
		
		// Look up the tuple_size specialization
		auto tuple_size_it = gTypesByName.find(tuple_size_handle);
		if (tuple_size_it == gTypesByName.end()) {
			tuple_size_it = gTypesByName.find(std_tuple_size_handle);
		}
		
		// If tuple_size is specialized for this type, use tuple-like decomposition
		if (tuple_size_it != gTypesByName.end()) {
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Found tuple_size specialization, using tuple-like decomposition");
			
			const TypeInfo* tuple_size_type_info = tuple_size_it->second;
			const StructTypeInfo* tuple_size_struct = tuple_size_type_info->getStructInfo();
			
			// Get the 'value' static member from tuple_size
			size_t tuple_size_value = 0;
			bool found_value = false;
			
			if (tuple_size_struct) {
				for (const auto& static_member : tuple_size_struct->static_members) {
					// Check for 'value' member (can be constexpr or const)
					if (StringTable::getStringView(static_member.name) == "value") {
						// Evaluate the static value
						if (static_member.initializer.has_value()) {
							ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*static_member.initializer, eval_ctx);
							if (eval_result.success) {
								tuple_size_value = static_cast<size_t>(eval_result.as_int());
								found_value = true;
								FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: tuple_size::value = ", tuple_size_value);
							}
						}
						break;
					}
				}
			}
			
			if (!found_value) {
				FLASH_LOG(Codegen, Warning, "visitStructuredBindingNode: Could not get tuple_size::value, falling back to aggregate decomposition");
			} else {
				// Validate that the number of identifiers matches tuple_size::value
				if (node.identifiers().size() != tuple_size_value) {
					FLASH_LOG(Codegen, Error, "Structured binding: number of identifiers (", node.identifiers().size(), 
					          ") does not match tuple_size::value (", tuple_size_value, ")");
					return;
				}
				
				FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: tuple_size detected with ", tuple_size_value, " elements");
				
				// Try to find get<N>() functions for tuple-like decomposition
				bool all_get_found = true;
				std::vector<std::pair<StringHandle, Type>> binding_info;  // (mangled_name, return_type) for each get<N>
				
				// First, look up std::tuple_element<N, E>::type and get<N>() for each binding
				for (size_t i = 0; i < tuple_size_value && all_get_found; ++i) {
					// Build the tuple_element specialization name
					StringBuilder tuple_element_name_builder;
					tuple_element_name_builder.append("tuple_element_").append(static_cast<uint64_t>(i)).append("_").append(type_name_view);
					std::string_view tuple_element_name = tuple_element_name_builder.commit();
					
					// Also try with std:: prefix
					StringBuilder std_tuple_element_name_builder;
					std_tuple_element_name_builder.append("std::tuple_element_").append(static_cast<uint64_t>(i)).append("_").append(type_name_view);
					std::string_view std_tuple_element_name = std_tuple_element_name_builder.commit();
					
					// Look up the type alias
					StringHandle type_alias_handle = StringTable::getOrInternStringHandle(
						StringBuilder().append(tuple_element_name).append("::type").commit());
					StringHandle std_type_alias_handle = StringTable::getOrInternStringHandle(
						StringBuilder().append(std_tuple_element_name).append("::type").commit());
					
					auto type_alias_it = gTypesByName.find(type_alias_handle);
					if (type_alias_it == gTypesByName.end()) {
						type_alias_it = gTypesByName.find(std_type_alias_handle);
					}
					
					Type element_type = Type::Int;  // Default
					int element_size = 32;
					TypeIndex element_type_index = 0;
					
					if (type_alias_it != gTypesByName.end()) {
						const TypeInfo* type_alias_info = type_alias_it->second;
						element_type = type_alias_info->type_;
						element_type_index = type_alias_info->type_index_;
						element_size = type_alias_info->type_size_;
						if (element_size == 0) {
							element_size = get_type_size_bits(element_type);
						}
						FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: tuple_element<", i, ">::type = ", (int)element_type, ", size=", element_size);
					}
					
					// Now look for the get<N>() function
					// First, try template registry with exact index
					TemplateTypeArg index_arg;
					index_arg.base_type = Type::UnsignedLong;
					index_arg.is_value = true;
					index_arg.value = static_cast<int64_t>(i);
					std::vector<TemplateTypeArg> get_template_args = { index_arg };
					
					auto get_spec = gTemplateRegistry.lookupExactSpecialization("get", get_template_args);
					
					if (get_spec.has_value() && get_spec->is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& get_func = get_spec->as<FunctionDeclarationNode>();
						
						// Generate mangled name with template argument
						const DeclarationNode& decl_node = get_func.decl_node();
						const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
						
						std::vector<TypeSpecifierNode> param_types;
						for (const auto& param : get_func.parameter_nodes()) {
							param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
						}
						
						std::vector<int64_t> template_args = { static_cast<int64_t>(i) };
						auto mangled = NameMangling::generateMangledNameWithTemplateArgs(
							"get", return_type, param_types, template_args, 
							get_func.is_variadic(), "", current_namespace_stack_);
						
						StringHandle mangled_handle = StringTable::getOrInternStringHandle(mangled.view());
						binding_info.push_back({mangled_handle, element_type});
						
						FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Found get<", i, "> with mangled name: ", mangled.view());
					} else {
						// Try symbol table lookup for explicit specializations
						extern SymbolTable gSymbolTable;
						auto get_overloads = gSymbolTable.lookup_all("get");
						
						bool found_this_get = false;
						size_t func_index = 0;
						
						for (const auto& overload : get_overloads) {
							if (!overload.is<FunctionDeclarationNode>()) continue;
							
							const FunctionDeclarationNode& get_func = overload.as<FunctionDeclarationNode>();
							const DeclarationNode& decl_node = get_func.decl_node();
							const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
							
							// Check if this overload's return type matches our element type
							// or if it's the i-th function declaration (by order)
							bool type_matches = (return_type.type() == element_type);
							if (element_type == Type::Struct) {
								type_matches = type_matches && (return_type.type_index() == element_type_index);
							}
							
							if (type_matches || func_index == i) {
								// Generate mangled name with template argument for this specialization
								std::vector<TypeSpecifierNode> param_types;
								for (const auto& param : get_func.parameter_nodes()) {
									param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
								}
								
								std::vector<int64_t> template_args = { static_cast<int64_t>(i) };
								auto mangled = NameMangling::generateMangledNameWithTemplateArgs(
									"get", return_type, param_types, template_args,
									get_func.is_variadic(), "", current_namespace_stack_);
								
								StringHandle mangled_handle = StringTable::getOrInternStringHandle(mangled.view());
								binding_info.push_back({mangled_handle, element_type});
								
								FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Found get<", i, "> (symbol table) with mangled name: ", mangled.view());
								found_this_get = true;
								break;
							}
							func_index++;
						}
						
						if (!found_this_get) {
							FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: get<", i, "> not found, falling back to aggregate");
							all_get_found = false;
						}
					}
				}
				
				// If we found all get<N>() functions, generate the tuple-like decomposition
				if (all_get_found && binding_info.size() == tuple_size_value) {
					FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: All get<> functions found, using tuple-like protocol");
					
					// Generate calls to get<N>(e) for each binding
					for (size_t i = 0; i < tuple_size_value; ++i) {
						StringHandle binding_id = node.identifiers()[i];
						std::string_view binding_name = StringTable::getStringView(binding_id);
						
						auto [get_mangled_name, element_type] = binding_info[i];
						
						// Look up element size from tuple_element type alias
						int element_size = get_type_size_bits(element_type);
						TypeIndex element_type_index = 0;
						
						// Generate call to get<N>(hidden_var)
						TempVar result_temp = var_counter.next();
						
						CallOp call_op;
						call_op.result = result_temp;
						call_op.return_type = element_type;
						call_op.return_size_in_bits = element_size;
						call_op.return_type_index = element_type_index;
						call_op.function_name = get_mangled_name;
						call_op.is_member_function = false;
						call_op.uses_return_slot = false;
						
						// Pass the hidden variable as argument
						TypedValue arg;
						arg.type = init_type;
						arg.size_in_bits = init_size;
						arg.value = hidden_var_handle;
						arg.type_index = init_type_index;
						arg.is_reference = true;  // Pass by const reference
						call_op.args.push_back(arg);
						
						Token binding_token(Token::Type::Identifier, binding_name, 0, 0, 0);
						ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binding_token));
						
						// Create the binding variable
						VariableDeclOp binding_var_decl;
						binding_var_decl.var_name = binding_id;
						binding_var_decl.type = element_type;
						binding_var_decl.size_in_bits = element_size;
						binding_var_decl.initializer = TypedValue{element_type, element_size, result_temp, false, false, element_type_index};
						
						ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
						
						// Create synthetic declaration for symbol table
						TypeSpecifierNode binding_type(element_type, TypeQualifier::None,
						                               static_cast<unsigned char>(element_size > 255 ? 255 : element_size), Token());
						binding_type.set_type_index(element_type_index);
						
						ASTNode binding_decl_node = ASTNode::emplace_node<DeclarationNode>(
							ASTNode::emplace_node<TypeSpecifierNode>(binding_type),
							binding_token
						);
						symbol_table.insert(binding_name, binding_decl_node);
						
						FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Created tuple binding '", binding_name, 
						          "' via get<", i, ">");
					}
					
					FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Successfully created ", tuple_size_value, " bindings using tuple-like protocol");
					return;  // Done - don't fall through to aggregate decomposition
				}
				
				// Fall through to aggregate decomposition
				FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Falling through to aggregate decomposition");
			}
		}
		
		// Step 6: Aggregate (struct) decomposition
		aggregate_decomposition:
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Using aggregate decomposition");
		
		// Step 6a: Validate that we have the correct number of identifiers
		// Count non-static public members (all members in FlashCpp are non-static by default)
		size_t public_member_count = 0;
		for (const auto& member : struct_info->members) {
			if (member.access == AccessSpecifier::Public) {
				public_member_count++;
			}
		}
		
		if (node.identifiers().size() != public_member_count) {
			FLASH_LOG(Codegen, Error, "Structured binding: number of identifiers (", node.identifiers().size(), 
			          ") does not match number of public members (", public_member_count, ")");
			return;
		}
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Decomposing struct with ", 
		          public_member_count, " public members");
		
		// Step 7: Create bindings for each identifier
		// For each binding, we create a variable that's initialized with a member access expression
		size_t binding_idx = 0;
		for (const auto& member : struct_info->members) {
			if (member.access != AccessSpecifier::Public) {
				continue;  // Skip non-public members
			}
			
			if (binding_idx >= node.identifiers().size()) {
				break;  // Safety check
			}
			
			StringHandle binding_id = node.identifiers()[binding_idx];
			std::string_view binding_name = StringTable::getStringView(binding_id);
			
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Creating binding '", binding_name, 
			          "' to member '", StringTable::getStringView(member.name), "'");
			
			// Create a TypeSpecifierNode for this binding's type
			// The binding has the same type as the struct member
			// For size_in_bits, clamp to 255 since TypeSpecifierNode uses unsigned char
			// For struct types, type_index is what matters, not size_in_bits
			size_t member_size_bits_full = member.size * 8;
			unsigned char member_size_bits = (member_size_bits_full > 255) ? 255 : static_cast<unsigned char>(member_size_bits_full);
			TypeSpecifierNode binding_type(member.type, TypeQualifier::None, member_size_bits, Token());
			binding_type.set_type_index(member.type_index);
			
			// If this is a reference binding (auto& or auto&&), mark the type as a reference
			if (is_reference_binding) {
				if (node.is_lvalue_reference()) {
					binding_type.set_reference(false);  // false = lvalue reference
				} else if (node.is_rvalue_reference()) {
					// For auto&&, the binding type depends on value category
					// Since we're binding to members of the hidden variable, they're lvalues
					// So auto&& bindings to struct members become lvalue references
					binding_type.set_reference(false);  // false = lvalue reference
				}
			}
			
			// Create a synthetic declaration node for the binding
			// This allows the binding to be looked up in the symbol table
			Token binding_token(Token::Type::Identifier, binding_name, 0, 0, 0);
			ASTNode binding_decl_node = ASTNode::emplace_node<DeclarationNode>(
				ASTNode::emplace_node<TypeSpecifierNode>(binding_type),
				binding_token
			);
			
			// Add to symbol table
			symbol_table.insert(binding_name, binding_decl_node);
			
			// Generate IR for the binding
			if (is_reference_binding) {
				// For reference bindings, create a reference variable that points to the member
				// We need to get the address of the member, not load its value
				
				// Compute address of member: &(hidden_var.member)
				TempVar member_addr = var_counter.next();
				ComputeAddressOp addr_op;
				addr_op.result = member_addr;
				addr_op.base = hidden_var_handle;
				addr_op.total_member_offset = static_cast<int>(member.offset);
				addr_op.result_type = member.type;
				addr_op.result_size_bits = 64;  // Address is 64-bit pointer
				
				ir_.addInstruction(IrInstruction(IrOpcode::ComputeAddress, std::move(addr_op), binding_token));
				
				// Declare the binding as a reference variable initialized with the address
				VariableDeclOp binding_var_decl;
				binding_var_decl.var_name = binding_id;
				binding_var_decl.type = member.type;
				binding_var_decl.size_in_bits = 64;  // References are pointers (64-bit addresses)
				binding_var_decl.is_reference = true;  // Mark as reference
				binding_var_decl.is_rvalue_reference = node.is_rvalue_reference();
				binding_var_decl.initializer = TypedValue{member.type, 64, member_addr, false, false, member.type_index};
				
				ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
			} else {
				// For value bindings, load the member value (existing behavior)
				// First, generate a member access to load the value
				TempVar member_val = var_counter.next();
				MemberLoadOp load_op;
				load_op.result.type = member.type;
				load_op.result.size_in_bits = member_size_bits;
				load_op.result.value = member_val;
				load_op.result.type_index = member.type_index;
				load_op.object = hidden_var_handle;
				load_op.member_name = member.name;
				load_op.offset = static_cast<int>(member.offset);
				load_op.struct_type_info = &type_info;
				load_op.is_reference = member.is_reference;
				load_op.is_rvalue_reference = member.is_rvalue_reference;
				load_op.is_pointer_to_member = false;
				
				ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), binding_token));
				
				// Now, declare the binding variable with the member value as initializer
				VariableDeclOp binding_var_decl;
				binding_var_decl.var_name = binding_id;
				binding_var_decl.type = member.type;
				binding_var_decl.size_in_bits = member_size_bits;
				binding_var_decl.initializer = TypedValue{member.type, static_cast<int>(member_size_bits), member_val, false, false, member.type_index};
				
				ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
			}
			
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Added binding '", binding_name, "' to symbol table");
			
			binding_idx++;
		}
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Successfully created ", binding_idx, " bindings");
	}

	std::vector<IrOperand> visitExpressionNode(const ExpressionNode& exprNode, 
	                                            ExpressionContext context = ExpressionContext::Load) {
		if (std::holds_alternative<IdentifierNode>(exprNode)) {
			const auto& expr = std::get<IdentifierNode>(exprNode);
			return generateIdentifierIr(expr, context);
		}
		else if (std::holds_alternative<QualifiedIdentifierNode>(exprNode)) {
			const auto& expr = std::get<QualifiedIdentifierNode>(exprNode);
			return generateQualifiedIdentifierIr(expr);
		}
		else if (std::holds_alternative<BoolLiteralNode>(exprNode)) {
			const auto& expr = std::get<BoolLiteralNode>(exprNode);
			// Convert boolean to integer for IR (true=1, false=0)
			// Return format: [type, size_in_bits, value, 0ULL]
			return { Type::Bool, 8, expr.value() ? 1ULL : 0ULL, 0ULL };
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
			return generateUnaryOperatorIr(expr, context);
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
			return generateArraySubscriptIr(expr, context);
		}
		else if (std::holds_alternative<MemberAccessNode>(exprNode)) {
			const auto& expr = std::get<MemberAccessNode>(exprNode);
			return generateMemberAccessIr(expr, context);
		}
		else if (std::holds_alternative<SizeofExprNode>(exprNode)) {
			const auto& sizeof_node = std::get<SizeofExprNode>(exprNode);
			
			// Try to evaluate as a constant expression first
			auto const_result = tryEvaluateAsConstExpr(sizeof_node);
			if (!const_result.empty()) {
				return const_result;
			}
			
			// Fall back to IR generation if constant evaluation failed
			return generateSizeofIr(sizeof_node);
		}
		else if (std::holds_alternative<SizeofPackNode>(exprNode)) {
			const auto& expr = std::get<SizeofPackNode>(exprNode);
			// sizeof... should have been replaced with a constant during template instantiation
			// If we reach here, it means sizeof... wasn't properly substituted
			// This is an error - sizeof... can only appear in template contexts
			FLASH_LOG(Codegen, Error, "sizeof... operator found during code generation - should have been substituted during template instantiation");
			return {};
		}
		else if (std::holds_alternative<AlignofExprNode>(exprNode)) {
			const auto& alignof_node = std::get<AlignofExprNode>(exprNode);
			
			// Try to evaluate as a constant expression first
			auto const_result = tryEvaluateAsConstExpr(alignof_node);
			if (!const_result.empty()) {
				return const_result;
			}
			
			// Fall back to IR generation if constant evaluation failed
			return generateAlignofIr(alignof_node);
		}
		else if (std::holds_alternative<NoexceptExprNode>(exprNode)) {
			const auto& noexcept_node = std::get<NoexceptExprNode>(exprNode);
			// noexcept(expr) returns true if expr doesn't throw, false otherwise
			// Analyze the expression to determine if it can throw
			bool is_noexcept = true;  // Default assumption
			
			if (noexcept_node.expr().is<ExpressionNode>()) {
				is_noexcept = isExpressionNoexcept(noexcept_node.expr().as<ExpressionNode>());
			}
			
			// Return a compile-time constant boolean
			return { Type::Bool, 8, is_noexcept ? 1ULL : 0ULL, 0ULL };
		}
		else if (std::holds_alternative<OffsetofExprNode>(exprNode)) {
			const auto& expr = std::get<OffsetofExprNode>(exprNode);
			return generateOffsetofIr(expr);
		}
		else if (std::holds_alternative<TypeTraitExprNode>(exprNode)) {
			const auto& expr = std::get<TypeTraitExprNode>(exprNode);
			return generateTypeTraitIr(expr);
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
		else if (std::holds_alternative<ConstCastNode>(exprNode)) {
			const auto& expr = std::get<ConstCastNode>(exprNode);
			return generateConstCastIr(expr);
		}
		else if (std::holds_alternative<ReinterpretCastNode>(exprNode)) {
			const auto& expr = std::get<ReinterpretCastNode>(exprNode);
			return generateReinterpretCastIr(expr);
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
			FLASH_LOG(Codegen, Error, "Fold expression found during code generation - should have been expanded during template instantiation");
			return {};
		}
		else if (std::holds_alternative<PseudoDestructorCallNode>(exprNode)) {
			// Explicit destructor call: obj.~Type() or ptr->~Type()
			// For class types, this calls the destructor
			// For non-class types (like int), this is a no-op
			const auto& dtor = std::get<PseudoDestructorCallNode>(exprNode);
			std::string_view type_name = dtor.has_qualified_name() 
				? std::string_view(dtor.qualified_type_name()) 
				: dtor.type_name();
			FLASH_LOG(Codegen, Debug, "Generating explicit destructor call for type: ", type_name);
			
			// Get the object expression
			ASTNode object_node = dtor.object();
			
			// Try to determine if this is a struct type that needs destructor call
			std::string_view object_name;
			const DeclarationNode* object_decl = nullptr;
			TypeSpecifierNode object_type(Type::Void, TypeQualifier::None, 0);
			
			if (object_node.is<ExpressionNode>()) {
				const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
				
				if (std::holds_alternative<IdentifierNode>(object_expr)) {
					const IdentifierNode& object_ident = std::get<IdentifierNode>(object_expr);
					object_name = object_ident.name();
					
					// Look up the object in symbol table
					const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
					if (symbol.has_value()) {
						object_decl = get_decl_from_symbol(*symbol);
						if (object_decl) {
							object_type = object_decl->type_node().as<TypeSpecifierNode>();
							
							// Handle arrow access (ptr->~Type)
							if (dtor.is_arrow_access() && object_type.pointer_levels().size() > 0) {
								object_type.remove_pointer_level();
							}
						}
					}
				}
			}
			
			// Only generate destructor call for struct types
			if (is_struct_type(object_type.type())) {
				size_t struct_type_index = object_type.type_index();
				if (struct_type_index > 0 && struct_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[struct_type_index];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					
					// Check if struct has a destructor
					if (struct_info && struct_info->hasDestructor()) {
						FLASH_LOG(Codegen, Debug, "Generating IR for destructor call on struct: ", 
						         StringTable::getStringView(struct_info->getName()));
						
						// Generate destructor call IR
						DestructorCallOp dtor_op;
						dtor_op.struct_name = struct_info->getName();
						dtor_op.object = StringTable::getOrInternStringHandle(object_name);
						ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), dtor.type_name_token()));
					} else {
						FLASH_LOG(Codegen, Debug, "Struct ", type_name, " has no destructor, skipping call");
					}
				}
			} else {
				// For non-class types (int, etc.), explicit destructor call is a no-op
				FLASH_LOG(Codegen, Debug, "Non-class type ", type_name, " - destructor call is no-op");
			}
			
			// Destructor calls return void
			return {};
		}
		else {
			assert(false && "Not implemented yet");
		}
		return {};
	}

	// Helper function to calculate size_bits for local variables with proper fallback handling
	// Consolidates logic for handling arrays, pointers, and regular variables
	int calculateIdentifierSizeBits(const TypeSpecifierNode& type_node, bool is_array, std::string_view identifier_name) {
		bool is_array_type = is_array || type_node.is_array();
		int size_bits;
		
		if (is_array_type || type_node.pointer_depth() > 0) {
			// For arrays and pointers, the identifier itself is a pointer (64 bits on x64)
			// The element/pointee size is stored separately and used for pointer arithmetic
			size_bits = 64;  // Pointer size on x64 architecture
		} else {
			// For regular variables, return the variable size
			size_bits = static_cast<int>(type_node.size_in_bits());
			// Fallback: if size_bits is 0, calculate from type (parser bug workaround)
			if (size_bits == 0) {
				FLASH_LOG(Codegen, Warning, "Parser returned size_bits=0 for identifier '", identifier_name, 
				         "' (type=", static_cast<int>(type_node.type()), ") - using fallback calculation");
				size_bits = get_type_size_bits(type_node.type());
			}
		}
		
		return size_bits;
	}

	std::vector<IrOperand> generateIdentifierIr(const IdentifierNode& identifierNode, 
	                                             ExpressionContext context = ExpressionContext::Load) {
		// Check if this is a captured variable in a lambda
		StringHandle var_name_str = StringTable::getOrInternStringHandle(identifierNode.name());
		if (current_lambda_context_.isActive() &&
		    current_lambda_context_.captures.find(var_name_str) != current_lambda_context_.captures.end()) {
			// This is a captured variable - generate member access (this->x)
			// Look up the closure struct type
			auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					// Find the member
					const StructMember* member = struct_info->findMemberRecursive(var_name_str);
					if (member) {
						// Check if this is a by-reference capture
						auto kind_it = current_lambda_context_.capture_kinds.find(var_name_str);
						bool is_reference = (kind_it != current_lambda_context_.capture_kinds.end() &&
						                     kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);

						if (is_reference) {
							// By-reference capture: member is a pointer, need to dereference
							// First, load the pointer from the closure
							TempVar ptr_temp = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = ptr_temp;
							member_load.result.type = member->type;  // Base type (e.g., Int)
							member_load.result.size_in_bits = 64;  // pointer size in bits
							member_load.object = StringTable::getOrInternStringHandle("this");
							member_load.member_name = member->getName();
							member_load.offset = static_cast<int>(member->offset);
							member_load.is_reference = member->is_reference;
							member_load.is_rvalue_reference = member->is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

							// The ptr_temp now contains the address of the captured variable
							// We need to dereference it using PointerDereference
							auto type_it = current_lambda_context_.capture_types.find(var_name_str);
							if (type_it != current_lambda_context_.capture_types.end()) {
								const TypeSpecifierNode& orig_type = type_it->second;

								// Generate Dereference to load the value
								TempVar result_temp = var_counter.next();
								std::vector<IrOperand> deref_operands;
								DereferenceOp deref_op;
								deref_op.result = result_temp;
								deref_op.pointer.type = orig_type.type();
								deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
								deref_op.pointer.value = ptr_temp;
								ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));
								
								// Mark as lvalue with Indirect metadata for unified assignment handler
								// This represents dereferencing a pointer: *ptr
								LValueInfo lvalue_info(
									LValueInfo::Kind::Indirect,
									ptr_temp,  // The pointer temp var
									0  // offset is 0 for dereference
								);
								setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));

								TypeIndex type_index = (orig_type.type() == Type::Struct) ? orig_type.type_index() : 0;
								return { orig_type.type(), static_cast<int>(orig_type.size_in_bits()), result_temp, static_cast<unsigned long long>(type_index) };
							}

							// Fallback: return the pointer temp
							return { member->type, 64, ptr_temp, 0ULL };
						} else {
							// By-value capture: direct member access
							TempVar result_temp = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = result_temp;
							member_load.result.type = member->type;
							member_load.result.size_in_bits = static_cast<int>(member->size * 8);
							member_load.object = StringTable::getOrInternStringHandle("this");  // implicit this pointer
							member_load.member_name = member->getName();
							member_load.offset = static_cast<int>(member->offset);
							member_load.is_reference = member->is_reference;
							member_load.is_rvalue_reference = member->is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
							
							// For mutable lambdas, set LValue metadata so assignments write back to the member
							if (current_lambda_context_.is_mutable) {
								// Use 'this' as the base object (StringHandle version)
								// The assignment handler will emit MemberStore using this info
								LValueInfo lvalue_info(
									LValueInfo::Kind::Member,
									StringTable::getOrInternStringHandle("this"),  // object name (this pointer)
									static_cast<int>(member->offset)
								);
								lvalue_info.member_name = member->getName();
								lvalue_info.is_pointer_to_member = true;  // 'this' is a pointer, need to dereference
								setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
							}
							
							TypeIndex type_index = (member->type == Type::Struct) ? member->type_index : 0;
							return { member->type, static_cast<int>(member->size * 8), result_temp, static_cast<unsigned long long>(type_index) };
						}
					}
				}
			}
		}

		// If we're inside a [*this] lambda, prefer resolving to members of the copied object
		if (isInCopyThisLambda() && current_lambda_context_.enclosing_struct_type_index > 0) {
			const TypeInfo* enclosing_type_info = nullptr;
			for (const auto& ti : gTypeInfo) {
				if (ti.type_index_ == current_lambda_context_.enclosing_struct_type_index) {
					enclosing_type_info = &ti;
					break;
				}
			}
			if (enclosing_type_info && enclosing_type_info->getStructInfo()) {
				const StructTypeInfo* enclosing_struct = enclosing_type_info->getStructInfo();
				const StructMember* member = enclosing_struct->findMemberRecursive(var_name_str);
				if (member) {
					if (auto copy_this_temp = emitLoadCopyThis(Token())) {
						TempVar result_temp = var_counter.next();
						MemberLoadOp member_load;
						member_load.result.value = result_temp;
						member_load.result.type = member->type;
						member_load.result.size_in_bits = static_cast<int>(member->size * 8);
						member_load.object = *copy_this_temp;
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(member->offset);
						member_load.is_reference = member->is_reference;
						member_load.is_rvalue_reference = member->is_rvalue_reference;
						member_load.struct_type_info = nullptr;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
						
						LValueInfo lvalue_info(
							LValueInfo::Kind::Member,
							*copy_this_temp,
							static_cast<int>(member->offset)
						);
						lvalue_info.member_name = member->getName();
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
						
						TypeIndex type_index = (member->type == Type::Struct) ? member->type_index : 0;
						return { member->type, static_cast<int>(member->size * 8), result_temp, static_cast<unsigned long long>(type_index) };
					}
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

			// Generate GlobalLoad with mangled name
			TempVar result_temp = var_counter.next();
			GlobalLoadOp op;
			op.result.type = info.type;
			op.result.size_in_bits = info.size_in_bits;
			op.result.value = result_temp;
			op.global_name = info.mangled_name;  // Use mangled name
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

			// Return the temp variable that will hold the loaded value
			return { info.type, info.size_in_bits, result_temp, 0ULL };
		}

		// Try local symbol table (for local variables, parameters, etc.) BEFORE checking member variables
		// This ensures constructor parameters shadow member variables in initializer expressions
		std::optional<ASTNode> symbol = symbol_table.lookup(identifierNode.name());
		bool is_global = false;

		// If not found locally, try global symbol table (for enum values, global variables, namespace-scoped variables, etc.)
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(identifierNode.name());
			is_global = symbol.has_value();  // If found in global table, it's a global
			
			// If still not found, check using directives from local scope in the global symbol table
			// This handles cases like: using namespace X; int y = X_var;
			// where X_var is defined in namespace X
			if (!symbol.has_value()) {
				auto using_directives = symbol_table.get_current_using_directives();
				for (const auto& ns_path : using_directives) {
					symbol = global_symbol_table_->lookup_qualified(ns_path, identifierNode.name());
					if (symbol.has_value()) {
						is_global = true;
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
			auto type_it = gTypesByName.find(current_struct_name_);
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
						member_load.object = StringTable::getOrInternStringHandle("this");  // implicit this pointer
						member_load.member_name = member->getName();
						member_load.offset = static_cast<int>(member->offset);
						member_load.is_reference = member->is_reference;
						member_load.is_rvalue_reference = member->is_rvalue_reference;
						member_load.struct_type_info = nullptr;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
						
						// Mark as lvalue with member metadata for unified assignment handler
						LValueInfo lvalue_info(
							LValueInfo::Kind::Member,
							StringTable::getOrInternStringHandle("this"),
							static_cast<int>(member->offset)
						);
						lvalue_info.member_name = member->getName();
						setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
						
						TypeIndex type_index = (member->type == Type::Struct) ? member->type_index : 0;
						return { member->type, static_cast<int>(member->size * 8), result_temp, static_cast<unsigned long long>(type_index) };
					}
					
					// Check if this identifier is a static member
					const StructStaticMember* static_member = struct_info->findStaticMember(var_name_str);
					if (static_member) {
						// This is a static member access - generate GlobalLoad IR
						// Static members are stored as globals with qualified names
						// Note: Namespaces are already included in current_struct_name_ via mangling
						auto qualified_name = StringTable::getOrInternStringHandle(StringBuilder().append(current_struct_name_).append("::"sv).append(var_name_str));
						
						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.type = static_member->type;
						op.result.size_in_bits = static_cast<int>(static_member->size * 8);
						op.result.value = result_temp;
						op.global_name = qualified_name;
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));
						
						TypeIndex type_index = (static_member->type == Type::Struct) ? static_member->type_index : 0;
						return { static_member->type, static_cast<int>(static_member->size * 8), result_temp, static_cast<unsigned long long>(type_index) };
					}
				}
			}
		}
		if (!symbol.has_value()) {
			FLASH_LOG(Codegen, Error, "Symbol '", identifierNode.name(), "' not found in symbol table during code generation");
			FLASH_LOG(Codegen, Error, "  Current function: ", current_function_name_);
			FLASH_LOG(Codegen, Error, "  Current struct: ", current_struct_name_);
			assert(false && "Expected symbol to exist");
			return {};
		}

		if (symbol->is<DeclarationNode>()) {
			const auto& decl_node = symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

			// Check if this is an enum value (enumerator constant)
			// IMPORTANT: References and pointers to enum are VARIABLES, not enumerator constants
			// Only non-reference, non-pointer enum-typed identifiers CAN BE enumerators
			// We must verify the identifier actually exists as an enumerator before treating it as a constant
			if (type_node.type() == Type::Enum && !type_node.is_reference() && type_node.pointer_depth() == 0) {
				// Check if this identifier is actually an enumerator (not just a variable of enum type)
				size_t enum_type_index = type_node.type_index();
				if (enum_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[enum_type_index];
					const EnumTypeInfo* enum_info = type_info.getEnumInfo();
					if (enum_info) {
						// Use findEnumerator to check if this identifier is actually an enumerator
						const Enumerator* enumerator = enum_info->findEnumerator(StringTable::getOrInternStringHandle(identifierNode.name()));
						if (enumerator) {
							// This IS an enumerator constant - return its value using the underlying type
							return { enum_info->underlying_type, static_cast<int>(enum_info->underlying_size),
							         static_cast<unsigned long long>(enumerator->value) };
						}
						// If not found as an enumerator, it's a variable of enum type - fall through to variable handling
					}
				}
			}

			// Check if this is a global variable
			if (is_global) {
				// Generate GlobalLoad IR instruction
				TempVar result_temp = var_counter.next();
				// For arrays, result is a pointer (64-bit address)
				bool is_array_type = decl_node.is_array() || type_node.is_array();
				int size_bits = (type_node.pointer_depth() > 0 || is_array_type) ? 64 : static_cast<int>(type_node.size_in_bits());
				GlobalLoadOp op;
				op.result.type = type_node.type();
				op.result.size_in_bits = size_bits;
				op.result.value = result_temp;
				
				// Check if this global has a mangled name (e.g., anonymous namespace variable)
				// Phase 4: Using StringHandle for lookup
				StringHandle simple_name_handle = StringTable::getOrInternStringHandle(identifierNode.name());
				auto it = global_variable_names_.find(simple_name_handle);
				if (it != global_variable_names_.end()) {
					op.global_name = it->second;  // Use mangled StringHandle
				} else {
					op.global_name = StringTable::getOrInternStringHandle(identifierNode.name());  // Use simple name as StringHandle
				}
				
				op.is_array = is_array_type;  // Arrays need LEA to get address
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Return the temp variable that will hold the loaded value
				// For pointers and arrays, return 64 bits (pointer size)
				// Include type_index for struct types
				TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
				return { type_node.type(), size_bits, result_temp, static_cast<unsigned long long>(type_index) };
			}

			// Check if this is a reference parameter - if so, we need to dereference it
			// Reference parameters (both lvalue & and rvalue &&) hold an address, and we need to load the value from that address
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
					constexpr int POINTER_SIZE_BITS = 64;  // x64 pointer size
					return { type_node.type(), POINTER_SIZE_BITS, StringTable::getOrInternStringHandle(identifierNode.name()), 0ULL };
				}
				
				// For LValueAddress context (e.g., LHS of assignment, function call with reference parameter)
				// Return the reference variable name directly as a pointer (64 bits) - don't dereference
				// This is critical for passing reference variables to reference parameters
				if (context == ExpressionContext::LValueAddress) {
					// For auto types, default to int (32 bits)
					Type pointee_type = type_node.type();
					int pointee_size = static_cast<int>(type_node.size_in_bits());
					if (pointee_type == Type::Auto || pointee_size == 0) {
						pointee_type = Type::Int;
						pointee_size = 32;
					}
					
					TypeIndex type_index = (pointee_type == Type::Struct) ? type_node.type_index() : 0;
					// Return reference name directly with POINTEE type and size (not pointer size)
					// This is important for handleLValueAssignment which uses lhs_operands[0] and [1] as pointee type/size
					// The reference variable will hold the pointer, but we return the pointee info
					return { pointee_type, pointee_size, StringTable::getOrInternStringHandle(identifierNode.name()), static_cast<unsigned long long>(type_index) };
				}
				
				// For non-array references in Load context, we need to dereference to get the value
				TempVar result_temp = var_counter.next();
				DereferenceOp deref_op;
				deref_op.result = result_temp;
				
				// For auto types, default to int (32 bits) since the mangling also defaults to int
				// This matches the behavior in NameMangling.h which falls through to 'H' (int)
				Type pointee_type = type_node.type();
				int pointee_size = static_cast<int>(type_node.size_in_bits());
				if (pointee_type == Type::Auto || pointee_size == 0) {
					pointee_type = Type::Int;
					pointee_size = 32;
				}
				
				// For enum references, treat dereferenced value as underlying type
				// This allows enum variables to work in arithmetic/bitwise operations
				if (pointee_type == Type::Enum && type_node.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
					const EnumTypeInfo* enum_info = type_info.getEnumInfo();
					if (enum_info) {
						pointee_type = enum_info->underlying_type;
						pointee_size = static_cast<int>(enum_info->underlying_size);
					}
				}
				
				deref_op.pointer.type = pointee_type;
				deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
				deref_op.pointer.pointer_depth = type_node.pointer_depth() > 0 ? type_node.pointer_depth() : 1;  // References are like pointers
				deref_op.pointer.value = StringTable::getOrInternStringHandle(identifierNode.name());  // The reference parameter holds the address
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));
				
				TypeIndex type_index = (pointee_type == Type::Struct || type_node.type() == Type::Enum) ? type_node.type_index() : 0;
				return { pointee_type, pointee_size, result_temp, static_cast<unsigned long long>(type_index) };
			}
			
			// Regular local variable
			// Use helper function to calculate size_bits with proper fallback handling
			int size_bits = calculateIdentifierSizeBits(type_node, decl_node.is_array(), identifierNode.name());
			
			// For enum variables (not enumerators), return the underlying integer type
			// This allows enum variables to work in arithmetic/bitwise operations
			Type return_type = type_node.type();
			if (type_node.type() == Type::Enum && type_node.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
				const EnumTypeInfo* enum_info = type_info.getEnumInfo();
				if (enum_info) {
					return_type = enum_info->underlying_type;
					size_bits = static_cast<int>(enum_info->underlying_size);
				}
			}
			
			// For the 4th element: 
			// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
			// - For enum types, return type_index to preserve type information
			// - For non-struct/enum pointer types, return pointer_depth
			// - Otherwise return 0
			unsigned long long fourth_element = 0ULL;
			if (type_node.type() == Type::Struct || type_node.type() == Type::Enum) {
				fourth_element = static_cast<unsigned long long>(type_node.type_index());
			} else if (type_node.pointer_depth() > 0) {
				fourth_element = static_cast<unsigned long long>(type_node.pointer_depth());
			}
			return { return_type, size_bits, StringTable::getOrInternStringHandle(identifierNode.name()), fourth_element };
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
				// For arrays, result is a pointer (64-bit address)
				bool is_array_type = decl_node.is_array() || type_node.is_array();
				int size_bits = is_array_type ? 64 : static_cast<int>(type_node.size_in_bits());
				GlobalLoadOp op;
				op.result.type = type_node.type();
				op.result.size_in_bits = size_bits;
				op.result.value = result_temp;
				
				// Check if this global has a mangled name (e.g., anonymous namespace variable)
				// Phase 4: Using StringHandle for lookup
				StringHandle simple_name_handle = StringTable::getOrInternStringHandle(identifierNode.name());
				auto it = global_variable_names_.find(simple_name_handle);
				if (it != global_variable_names_.end()) {
					op.global_name = it->second;  // Use mangled StringHandle
				} else {
					op.global_name = StringTable::getOrInternStringHandle(identifierNode.name());  // Use simple name as StringHandle
				}
				
				op.is_array = is_array_type;  // Arrays need LEA to get address
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Return the temp variable that will hold the loaded value
				// Include type_index for struct types
				TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
				return { type_node.type(), size_bits, result_temp, static_cast<unsigned long long>(type_index) };
			} else {
				// This is a local variable
				
				// Check if this is a reference variable - if so, we need to dereference it
				// Reference variables (both lvalue & and rvalue &&) hold an address, and we need to load the value from that address
				// EXCEPT for array references, where the reference IS the array pointer
				if (type_node.is_reference()) {
					// For references to arrays (e.g., int (&arr)[3]), the reference variable
					// already holds the array address directly. We don't dereference it.
					// Just return it as a pointer (64 bits on x64 architecture).
					if (type_node.is_array()) {
						// Return the array reference as a 64-bit pointer
						constexpr int POINTER_SIZE_BITS = 64;  // x64 pointer size
						return { type_node.type(), POINTER_SIZE_BITS, StringTable::getOrInternStringHandle(identifierNode.name()), 0ULL };
					}
					
					// For LValueAddress context (assignment LHS), we need to treat the reference variable
					// as an indirect lvalue (pointer that needs dereferencing for stores)
					if (context == ExpressionContext::LValueAddress) {
						// For auto types, default to int (32 bits)
						Type pointee_type = type_node.type();
						int pointee_size = static_cast<int>(type_node.size_in_bits());
						if (pointee_type == Type::Auto || pointee_size == 0) {
							pointee_type = Type::Int;
							pointee_size = 32;
						}
						
						// The reference variable holds a pointer address
						// We need to load it into a temp and mark it with Indirect LValue metadata
						TempVar addr_temp = var_counter.next();
						StringHandle var_handle = StringTable::getOrInternStringHandle(identifierNode.name());
						
						// Use AssignmentOp to copy the pointer value to a temp
						AssignmentOp assign_op;
						assign_op.result = addr_temp;
						assign_op.lhs = TypedValue{pointee_type, 64, addr_temp};  // 64-bit pointer dest
						assign_op.rhs = TypedValue{pointee_type, 64, var_handle};  // 64-bit pointer source
						assign_op.is_pointer_store = false;
						assign_op.dereference_rhs_references = false;  // Don't dereference - just copy the pointer!
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
						
						// Mark the temp with Indirect LValue metadata
						// This tells the assignment handler to use DereferenceStore
						LValueInfo lvalue_info(
							LValueInfo::Kind::Indirect,
							addr_temp,  // The pointer temp var
							0  // offset is 0 for dereference
						);
						setTempVarMetadata(addr_temp, TempVarMetadata::makeLValue(lvalue_info));
						
						TypeIndex type_index = (pointee_type == Type::Struct) ? type_node.type_index() : 0;
						return { pointee_type, pointee_size, addr_temp, static_cast<unsigned long long>(type_index) };
					}
					
					// For Load context (reading the value), dereference to get the value
					TempVar result_temp = var_counter.next();
					DereferenceOp deref_op;
					deref_op.result = result_temp;
					
					// For auto types, default to int (32 bits) since the mangling also defaults to int
					// This matches the behavior in NameMangling.h which falls through to 'H' (int)
					Type pointee_type = type_node.type();
					int pointee_size = static_cast<int>(type_node.size_in_bits());
					if (pointee_type == Type::Auto || pointee_size == 0) {
						pointee_type = Type::Int;
						pointee_size = 32;
					}
					
					deref_op.pointer.type = pointee_type;
					deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
					deref_op.pointer.pointer_depth = type_node.pointer_depth() > 0 ? type_node.pointer_depth() : 1;  // References are like pointers
					deref_op.pointer.value = StringTable::getOrInternStringHandle(identifierNode.name());  // The reference variable holds the address
					ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));
					
					TypeIndex type_index = (pointee_type == Type::Struct) ? type_node.type_index() : 0;
					return { pointee_type, pointee_size, result_temp, static_cast<unsigned long long>(type_index) };
				}
				
				// Regular local variable (not a reference) - return variable name
				// Use helper function to calculate size_bits with proper fallback handling
				int size_bits = calculateIdentifierSizeBits(type_node, decl_node.is_array(), identifierNode.name());
				
				// For the 4th element: 
				// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
				// - For non-struct pointer types, return pointer_depth
				// - Otherwise return 0
				unsigned long long fourth_element = (type_node.type() == Type::Struct)
					? static_cast<unsigned long long>(type_node.type_index())
					: ((type_node.pointer_depth() > 0) ? static_cast<unsigned long long>(type_node.pointer_depth()) : 0ULL);
				return { type_node.type(), size_bits, StringTable::getOrInternStringHandle(identifierNode.name()), fourth_element };
			}
		}
		
		// Check if it's a FunctionDeclarationNode (function name used as value)
		if (symbol->is<FunctionDeclarationNode>()) {
			// This is a function name being used as a value (e.g., fp = add)
			// Generate FunctionAddress IR instruction
			const auto& func_decl = symbol->as<FunctionDeclarationNode>();
			
			// Compute mangled name from the function declaration
			const auto& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
			std::vector<TypeSpecifierNode> param_types;
			for (const auto& param : func_decl.parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
				}
			}
			std::string_view mangled = generateMangledNameForCall(
				identifierNode.name(), return_type, param_types, func_decl.is_variadic(), "");
			
			TempVar func_addr_var = var_counter.next();
			FunctionAddressOp op;
			op.result.type = Type::FunctionPointer;
			op.result.size_in_bits = 64;
			op.result.value = func_addr_var;
			op.function_name = StringTable::getOrInternStringHandle(identifierNode.name());
			op.mangled_name = StringTable::getOrInternStringHandle(mangled);
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionAddress, std::move(op), Token()));

			// Return the function address as a pointer (64 bits)
			return { Type::FunctionPointer, 64, func_addr_var, 0ULL };
		}

		// Check if it's a TemplateVariableDeclarationNode (variable template)
		if (symbol->is<TemplateVariableDeclarationNode>()) {
			// Variable template without instantiation - should not reach codegen
			// The parser should have instantiated it already
			assert(false && "Uninstantiated variable template in codegen");
			return {};
		}

		// If we get here, the symbol is not a known type
		FLASH_LOG(Codegen, Error, "Unknown symbol type for identifier '", identifierNode.name(), "'");
		assert(false && "Identifier is not a DeclarationNode");
		return {};
	}

	std::vector<IrOperand> generateQualifiedIdentifierIr(const QualifiedIdentifierNode& qualifiedIdNode) {
		// Check if this is a scoped enum value (e.g., Direction::North)
		const auto& namespaces = qualifiedIdNode.namespaces();
		if (namespaces.size() >= 1) {
			// The struct/enum name is the last namespace component
			std::string struct_or_enum_name(namespaces.back());
			
			// Could be EnumName::EnumeratorName
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_or_enum_name));
			if (type_it != gTypesByName.end() && type_it->second->isEnum()) {
				const EnumTypeInfo* enum_info = type_it->second->getEnumInfo();
				if (enum_info && enum_info->is_scoped) {
					// This is a scoped enum - look up the enumerator value
					long long enum_value = enum_info->getEnumeratorValue(StringTable::getOrInternStringHandle(qualifiedIdNode.name()));
					// Return the enum value as a constant
					return { enum_info->underlying_type, static_cast<int>(enum_info->underlying_size),
					         static_cast<unsigned long long>(enum_value) };
				}
			}

			// Check if this is a static member access (e.g., StructName::static_member or ns::StructName::static_member)
			auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_or_enum_name));
			FLASH_LOG(Codegen, Debug, "generateQualifiedIdentifierIr: struct_or_enum_name='", struct_or_enum_name, "', found=", (struct_type_it != gTypesByName.end()));
			
			// If not found directly, try with default template argument suffix "_void"
			// This handles cases like has_type<T>::value where T has a default = void argument
			if (struct_type_it == gTypesByName.end()) {
				std::string_view struct_name_with_void = StringBuilder().append(struct_or_enum_name).append("_void"sv).commit();
				struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_with_void));
				if (struct_type_it != gTypesByName.end()) {
					FLASH_LOG(Codegen, Debug, "Found struct with _void suffix: ", struct_name_with_void);
				}
			}
			
			if (struct_type_it != gTypesByName.end() && struct_type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				// If struct_info is null, this might be a type alias - resolve it via type_index
				if (!struct_info && struct_type_it->second->type_index_ < gTypeInfo.size()) {
					const TypeInfo* resolved_type = &gTypeInfo[struct_type_it->second->type_index_];
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
						std::string_view owner_name = StringTable::getStringView(owner_struct->getName());
						if (owner_name.find("_unknown") != std::string_view::npos) {
							FLASH_LOG(Codegen, Error, "Cannot access static member '", qualifiedIdNode.name(), 
							          "' from incomplete template instantiation '", owner_name, "'");
							// Return a placeholder value instead of generating GlobalLoad
							// This prevents linker errors from undefined references to incomplete instantiations
							return { Type::Bool, 8, 0ULL, 0ULL };
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
									if (base.type_index < gTypeInfo.size()) {
										const TypeInfo& base_type = gTypeInfo[base.type_index];
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
									return { Type::Bool, 8, static_cast<unsigned long long>(value), 0ULL };
								}
								
								// Follow the full type alias chain (e.g., true_type -> bool_constant -> integral_constant)
								std::unordered_set<TypeIndex> visited;
								while (resolved_type && 
								       resolved_type->type_index_ < gTypeInfo.size() && 
								       resolved_type->type_index_ != 0 &&
								       !visited.contains(resolved_type->type_index_)) {
									visited.insert(resolved_type->type_index_);
									const TypeInfo* target_type = &gTypeInfo[resolved_type->type_index_];
									
									if (target_type && target_type->isStruct() && target_type->getStructInfo()) {
										// Use the target struct's name
										qualified_struct_name = target_type->name();
										FLASH_LOG(Codegen, Debug, "Resolved type alias to: ", qualified_struct_name);
										
										// If target is also an alias, continue following
										if (target_type->type_index_ != 0 && target_type->type_index_ != resolved_type->type_index_) {
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
						TempVar result_temp = var_counter.next();
						GlobalLoadOp op;
						op.result.type = static_member->type;
						op.result.size_in_bits = static_cast<int>(static_member->size * 8);
						op.result.value = result_temp;
						// Use qualified name as the global symbol name: StructName::static_member
						op.global_name = StringTable::getOrInternStringHandle(StringBuilder().append(qualified_struct_name).append("::"sv).append(qualifiedIdNode.name()));
						ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

						// Return the temp variable that will hold the loaded value
						TypeIndex type_index = (static_member->type == Type::Struct) ? static_member->type_index : 0;
						return { static_member->type, static_cast<int>(static_member->size * 8), result_temp, static_cast<unsigned long long>(type_index) };
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
			return { Type::Int, 32,  StringTable::getOrInternStringHandle(qualifiedIdNode.name()), 0ULL };
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
				op.global_name = StringTable::getOrInternStringHandle(qualifiedIdNode.name());  // Use the identifier name
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

				// Return the temp variable that will hold the loaded value
				TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
				return { type_node.type(), static_cast<int>(type_node.size_in_bits()), result_temp, static_cast<unsigned long long>(type_index) };
			} else {
				// Local variable - just return the name
				TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
				return { type_node.type(), static_cast<int>(type_node.size_in_bits()),  StringTable::getOrInternStringHandle(qualifiedIdNode.name()), static_cast<unsigned long long>(type_index) };
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
			op.global_name = StringTable::getOrInternStringHandle(qualifiedIdNode.name());  // Use the identifier name
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(op), Token()));

			// Return the temp variable that will hold the loaded value
			// For pointers, return 64 bits (pointer size)
			TypeIndex type_index = (type_node.type() == Type::Struct) ? type_node.type_index() : 0;
			return { type_node.type(), size_bits, result_temp, static_cast<unsigned long long>(type_index) };
		}

		if (found_symbol->is<FunctionDeclarationNode>()) {
			// This is a function - just return the name for function calls
			// The actual function call handling is done elsewhere
			return { Type::Function, 64, StringTable::getOrInternStringHandle(qualifiedIdNode.name()), 0ULL };
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
			return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<double>(numericLiteralNode.value()), 0ULL };
		} else {
			// For integer literals, the value is stored as unsigned long long
			return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<unsigned long long>(numericLiteralNode.value()), 0ULL };
		}
	}

	std::vector<IrOperand> generateTypeConversion(const std::vector<IrOperand>& operands, Type fromType, Type toType, const Token& source_token) {
		// Get the actual size from the operands (they already contain the correct size)
		// operands format: [type, size, value]
		int fromSize = (operands.size() >= 2) ? std::get<int>(operands[1]) : get_type_size_bits(fromType);
		
		// For struct types (Struct or UserDefined), use the size from operands, not get_type_size_bits
		int toSize;
		if (is_struct_type(toType)) {
			// Preserve the original size for struct types
			toSize = fromSize;
		} else {
			toSize = get_type_size_bits(toType);
		}
		
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
				return { toType, toSize, value, 0ULL };
			} else if (std::holds_alternative<int>(operands[2])) {
				int value = std::get<int>(operands[2]);
				// Convert to unsigned long long for consistency
				return { toType, toSize, static_cast<unsigned long long>(value) };
			} else if (std::holds_alternative<double>(operands[2])) {
				double value = std::get<double>(operands[2]);
				return { toType, toSize, value, 0ULL };
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
		return { toType, toSize, resultVar, 0ULL };
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
		return { Type::Char, 64, result_var, 0ULL };
	}

	// ============================================================================
	// Address Expression Analysis for One-Pass Address Calculation
	// ============================================================================
	
	// Helper function to extract DeclarationNode from a symbol (handles both DeclarationNode and VariableDeclarationNode)
	static const DeclarationNode* getDeclarationFromSymbol(const std::optional<ASTNode>& symbol) {
		if (!symbol.has_value()) {
			return nullptr;
		}
		if (symbol->is<DeclarationNode>()) {
			return &symbol->as<DeclarationNode>();
		} else if (symbol->is<VariableDeclarationNode>()) {
			return &symbol->as<VariableDeclarationNode>().declaration();
		}
		return nullptr;
	}
	
	// Structure to hold the components of an address expression
	struct AddressComponents {
		std::variant<StringHandle, TempVar> base;           // Base variable or temp
		std::vector<ComputeAddressOp::ArrayIndex> array_indices;  // Array indices
		int total_member_offset = 0;                        // Accumulated member offsets
		Type final_type = Type::Void;                       // Type of final result
		int final_size_bits = 0;                            // Size in bits
		int pointer_depth = 0;                              // Pointer depth of final result
	};

	// Analyze an expression for address calculation components
	// Returns std::nullopt if the expression is not suitable for one-pass address calculation
	std::optional<AddressComponents> analyzeAddressExpression(
		const ExpressionNode& expr, 
		int accumulated_offset = 0) 
	{
		// Handle Identifier (base case)
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& identifier = std::get<IdentifierNode>(expr);
			StringHandle identifier_handle = StringTable::getOrInternStringHandle(identifier.name());
			
			// Look up the identifier
			std::optional<ASTNode> symbol = symbol_table.lookup(identifier_handle);
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(identifier_handle);
			}
			if (!symbol.has_value()) {
				return std::nullopt;  // Can't find identifier
			}
			
			// Get type info
			const TypeSpecifierNode* type_node = nullptr;
			if (symbol->is<DeclarationNode>()) {
				type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			} else if (symbol->is<VariableDeclarationNode>()) {
				type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
			} else {
				return std::nullopt;
			}
			
			AddressComponents result;
			result.base = identifier_handle;
			result.total_member_offset = accumulated_offset;
			result.final_type = type_node->type();
			result.final_size_bits = static_cast<int>(type_node->size_in_bits());
			return result;
		}
		
		// Handle MemberAccess (obj.member)
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			const MemberAccessNode& memberAccess = std::get<MemberAccessNode>(expr);
			const ASTNode& object_node = memberAccess.object();
			
			if (!object_node.is<ExpressionNode>()) {
				return std::nullopt;
			}
			
			const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
			
			// Get object type to lookup member
			auto object_operands = visitExpressionNode(obj_expr, ExpressionContext::LValueAddress);
			if (object_operands.size() < 4) {
				return std::nullopt;
			}
			
			Type object_type = std::get<Type>(object_operands[0]);
			TypeIndex type_index = 0;
			if (std::holds_alternative<unsigned long long>(object_operands[3])) {
				type_index = static_cast<TypeIndex>(std::get<unsigned long long>(object_operands[3]));
			}
			
			// Look up member information
			if (type_index == 0 || type_index >= gTypeInfo.size() || object_type != Type::Struct) {
				return std::nullopt;
			}
			
			const StructTypeInfo* struct_info = gTypeInfo[type_index].getStructInfo();
			if (!struct_info) {
				return std::nullopt;
			}
			
			std::string_view member_name = memberAccess.member_name();
			StringHandle member_handle = StringTable::getOrInternStringHandle(std::string(member_name));
			const StructMember* member = struct_info->findMemberRecursive(member_handle);
			
			if (!member) {
				return std::nullopt;
			}
			
			// Recurse with accumulated offset
			int new_offset = accumulated_offset + static_cast<int>(member->offset);
			auto base_components = analyzeAddressExpression(obj_expr, new_offset);
			
			if (!base_components.has_value()) {
				return std::nullopt;
			}
			
			// Update type to member type
			base_components->final_type = member->type;
			base_components->final_size_bits = static_cast<int>(member->size * 8);
			
			return base_components;
		}
		
		// Handle ArraySubscript (arr[index])
		if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			const ArraySubscriptNode& arraySubscript = std::get<ArraySubscriptNode>(expr);
			
			// For multidimensional arrays (nested ArraySubscriptNode), return nullopt
			// to let the specialized handling in generateUnaryOperatorIr compute the flat index correctly
			const ExpressionNode& array_expr_inner = arraySubscript.array_expr().as<ExpressionNode>();
			if (std::holds_alternative<ArraySubscriptNode>(array_expr_inner)) {
				return std::nullopt;  // Fall through to multidimensional array handling
			}
			
			// Get the array and index operands
			auto array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
			auto index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());
			
			if (array_operands.size() < 3 || index_operands.size() < 3) {
				return std::nullopt;
			}
			
			Type element_type = std::get<Type>(array_operands[0]);
			int element_size_bits = std::get<int>(array_operands[1]);
			int element_pointer_depth = 0;  // Track pointer depth for pointer array elements
			
			// Calculate actual element size from array declaration
			if (std::holds_alternative<StringHandle>(array_operands[2])) {
				StringHandle array_name = std::get<StringHandle>(array_operands[2]);
				std::optional<ASTNode> symbol = symbol_table.lookup(array_name);
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(array_name);
				}
				
				const DeclarationNode* decl_ptr = getDeclarationFromSymbol(symbol);
				if (decl_ptr && (decl_ptr->is_array() || decl_ptr->type_node().as<TypeSpecifierNode>().is_array())) {
					const TypeSpecifierNode& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
					if (type_node.pointer_depth() > 0) {
						element_size_bits = 64;
						element_pointer_depth = type_node.pointer_depth();  // Track pointer depth
					} else if (type_node.type() == Type::Struct) {
						TypeIndex type_index_from_decl = type_node.type_index();
						if (type_index_from_decl > 0 && type_index_from_decl < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index_from_decl];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info) {
								element_size_bits = static_cast<int>(struct_info->total_size * 8);
							}
						}
					} else {
						element_size_bits = static_cast<int>(type_node.size_in_bits());
						if (element_size_bits == 0) {
							element_size_bits = get_type_size_bits(type_node.type());
						}
					}
				}
			} else if (std::holds_alternative<TempVar>(array_operands[2])) {
				// Array from expression (e.g., member access: obj.arr_member[idx])
				// array_operands[1] contains total array size, we need element size
				// For primitive types, use the type's size directly
				if (element_type == Type::Struct) {
					// For struct arrays, element_size_bits is already correct from member info
					// (it contains the struct size, not the total array size)
				} else {
					// For primitive type arrays, get the element size from the type
					element_size_bits = get_type_size_bits(element_type);
				}
				// Try to get pointer depth from array_operands[3] if available
				if (array_operands.size() >= 4 && std::holds_alternative<unsigned long long>(array_operands[3])) {
					element_pointer_depth = static_cast<int>(std::get<unsigned long long>(array_operands[3]));
				}
			}
			
			// Recurse on the array expression (could be nested: arr[i][j])
			auto base_components = analyzeAddressExpression(arraySubscript.array_expr().as<ExpressionNode>(), accumulated_offset);
			
			if (!base_components.has_value()) {
				return std::nullopt;
			}
			
			// Add this array index
			ComputeAddressOp::ArrayIndex arr_idx;
			arr_idx.element_size_bits = element_size_bits;
			
			// Capture index type information for proper sign extension
			arr_idx.index_type = std::get<Type>(index_operands[0]);
			arr_idx.index_size_bits = std::get<int>(index_operands[1]);
			
			// Set index value
			if (std::holds_alternative<unsigned long long>(index_operands[2])) {
				arr_idx.index = std::get<unsigned long long>(index_operands[2]);
			} else if (std::holds_alternative<TempVar>(index_operands[2])) {
				arr_idx.index = std::get<TempVar>(index_operands[2]);
			} else if (std::holds_alternative<StringHandle>(index_operands[2])) {
				arr_idx.index = std::get<StringHandle>(index_operands[2]);
			} else {
				return std::nullopt;
			}
			
			base_components->array_indices.push_back(arr_idx);
			base_components->final_type = element_type;
			base_components->final_size_bits = element_size_bits;
			base_components->pointer_depth = element_pointer_depth;  // Set pointer depth for the element
			
			return base_components;
		}
		
		// Unsupported expression type
		return std::nullopt;
	}

	std::vector<IrOperand> generateUnaryOperatorIr(const UnaryOperatorNode& unaryOperatorNode, 
	                                                 ExpressionContext context = ExpressionContext::Load) {
		std::vector<IrOperand> irOperands;

		// OPERATOR OVERLOAD RESOLUTION
		// For full standard compliance, operator& should call overloaded operator& if it exists.
		// __builtin_addressof (marked with is_builtin_addressof flag) always bypasses overloads.
		if (!unaryOperatorNode.is_builtin_addressof() && unaryOperatorNode.op() == "&" && 
		    unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			
			// For now, only handle simple identifiers
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& ident = std::get<IdentifierNode>(operandExpr);
				StringHandle identifier_handle = StringTable::getOrInternStringHandle(ident.name());
				
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier_handle);
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier_handle);
				}
				
				if (symbol.has_value()) {
					const TypeSpecifierNode* type_node = nullptr;
					if (symbol->is<DeclarationNode>()) {
						type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
					} else if (symbol->is<VariableDeclarationNode>()) {
						type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
					}
					
					if (type_node && type_node->type() == Type::Struct && type_node->pointer_depth() == 0) {
						// Check for operator& overload
						auto overload_result = findUnaryOperatorOverload(type_node->type_index(), "&");
						
						if (overload_result.has_overload) {
							// Found an overload! Generate a member function call instead of built-in address-of
							FLASH_LOG_FORMAT(Codegen, Debug, "Resolving operator& overload for type index {}", 
							         type_node->type_index());
							
							const StructMemberFunction& member_func = *overload_result.member_overload;
							const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
							
							// Get struct name for mangling
							std::string_view struct_name = StringTable::getStringView(gTypeInfo[type_node->type_index()].name());
							
							// Get the return type from the function declaration
							const TypeSpecifierNode& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
							
							// Generate mangled name using the proper mangling infrastructure
							// This handles both Itanium (Linux) and MSVC (Windows) name mangling
							std::string_view operator_func_name = "operator&";
							std::vector<TypeSpecifierNode> empty_params; // No explicit parameters (only implicit 'this')
							std::vector<std::string_view> empty_namespace;
							auto mangled_name = NameMangling::generateMangledName(
								operator_func_name,
								return_type,
								empty_params,
								false, // not variadic
								struct_name,
								empty_namespace,
								Linkage::CPlusPlus
							);
							
							// Generate the call
							TempVar ret_var = var_counter.next();
							
							// Create CallOp
							CallOp call_op;
							call_op.result = ret_var;
							call_op.return_type = return_type.type();
							// For pointer return types, use 64-bit size (pointer size on x64)
							if (return_type.pointer_depth() > 0) {
								call_op.return_size_in_bits = 64;
							} else {
								call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
								if (call_op.return_size_in_bits == 0) {
									call_op.return_size_in_bits = get_type_size_bits(return_type.type());
								}
							}
							call_op.function_name = mangled_name;  // MangledName implicitly converts to StringHandle
							call_op.is_variadic = false;
							call_op.uses_return_slot = false;
							call_op.is_member_function = true;  // This is a member function call
							
							// Add 'this' pointer as first argument
							call_op.args.push_back(TypedValue{
								.type = type_node->type(),
								.size_in_bits = 64,  // Pointer size
								.value = IrValue(identifier_handle)
							});
							
							// Add the function call instruction
							ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), unaryOperatorNode.get_token()));
							
							// Return the result
							unsigned long long fourth_element = static_cast<unsigned long long>(return_type.pointer_depth());
							if (fourth_element == 0 && return_type.type() == Type::Struct) {
								fourth_element = static_cast<unsigned long long>(return_type.type_index());
							}
							
							return {return_type.type(), call_op.return_size_in_bits, ret_var, fourth_element};
						}
					}
				}
			}
		}

		auto tryBuildIdentifierOperand = [&](const IdentifierNode& identifier, std::vector<IrOperand>& out) -> bool {
			// Phase 4: Using StringHandle for lookup
			StringHandle identifier_handle = StringTable::getOrInternStringHandle(identifier.name());

			// Static local variables are stored as globals with mangled names
			auto static_local_it = static_local_names_.find(identifier_handle);
			if (static_local_it != static_local_names_.end()) {
				out.clear();
				out.emplace_back(static_local_it->second.type);
				out.emplace_back(static_cast<int>(static_local_it->second.size_in_bits));
				out.emplace_back(static_local_it->second.mangled_name);
				out.emplace_back(0ULL); // pointer depth - assume 0 for static locals for now
				return true;
			}

			std::optional<ASTNode> symbol = symbol_table.lookup(identifier_handle);
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(identifier_handle);
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
			out.emplace_back(identifier_handle);
			// For the 4th element: 
			// - For struct types, ALWAYS return type_index (even if it's a pointer to struct)
			// - For non-struct pointer types, return pointer_depth
			// - Otherwise return 0
			unsigned long long fourth_element = (type_node->type() == Type::Struct)
				? static_cast<unsigned long long>(type_node->type_index())
				: ((type_node->pointer_depth() > 0) ? static_cast<unsigned long long>(type_node->pointer_depth()) : 0ULL);
			out.emplace_back(fourth_element);
			return true;
		};

		// Special handling for &arr[index] - generate address directly without loading value
		if (unaryOperatorNode.op() == "&" && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			
			// Try new one-pass address analysis first
			auto addr_components = analyzeAddressExpression(operandExpr);
			if (addr_components.has_value()) {
				// Successfully analyzed - generate ComputeAddress IR
				TempVar result_var = var_counter.next();
				
				ComputeAddressOp compute_addr_op;
				compute_addr_op.result = result_var;
				compute_addr_op.base = addr_components->base;
				compute_addr_op.array_indices = std::move(addr_components->array_indices);
				compute_addr_op.total_member_offset = addr_components->total_member_offset;
				compute_addr_op.result_type = addr_components->final_type;
				compute_addr_op.result_size_bits = addr_components->final_size_bits;
				
				ir_.addInstruction(IrInstruction(IrOpcode::ComputeAddress, std::move(compute_addr_op), unaryOperatorNode.get_token()));
				
				// Return pointer to result (64-bit pointer)
				// The 4th element is pointer_depth + 1 (we're taking address, so depth increases)
				return { addr_components->final_type, 64, result_var, static_cast<unsigned long long>(addr_components->pointer_depth + 1) };
			}
			
			// Fall back to legacy implementation if analysis failed
			
			// Handle &arr[index].member (member access on array element)
			if (std::holds_alternative<MemberAccessNode>(operandExpr)) {
				const MemberAccessNode& memberAccess = std::get<MemberAccessNode>(operandExpr);
				const ASTNode& object_node = memberAccess.object();
				
				// Check if the object is an array subscript
				if (object_node.is<ExpressionNode>()) {
					const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
					if (std::holds_alternative<ArraySubscriptNode>(obj_expr)) {
						const ArraySubscriptNode& arraySubscript = std::get<ArraySubscriptNode>(obj_expr);
						
						// Get the array and index operands
						auto array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
						auto index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());
						
						// Check that we have valid operands
						if (array_operands.size() >= 3 && index_operands.size() >= 3) {
							Type element_type = std::get<Type>(array_operands[0]);
							int element_size_bits = std::get<int>(array_operands[1]);
							
							// For arrays, array_operands[1] is the pointer size (64), not element size
							// We need to calculate the actual element size from the array declaration
							if (std::holds_alternative<StringHandle>(array_operands[2])) {
								StringHandle array_name = std::get<StringHandle>(array_operands[2]);
								std::optional<ASTNode> symbol = symbol_table.lookup(array_name);
								if (!symbol.has_value() && global_symbol_table_) {
									symbol = global_symbol_table_->lookup(array_name);
								}
								if (symbol.has_value()) {
									const DeclarationNode* decl_ptr = nullptr;
									if (symbol->is<DeclarationNode>()) {
										decl_ptr = &symbol->as<DeclarationNode>();
									} else if (symbol->is<VariableDeclarationNode>()) {
										decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
									}
									
									if (decl_ptr && (decl_ptr->is_array() || decl_ptr->type_node().as<TypeSpecifierNode>().is_array())) {
										// This is an array - calculate element size
										const TypeSpecifierNode& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
										if (type_node.pointer_depth() > 0) {
											// Array of pointers
											element_size_bits = 64;
										} else if (type_node.type() == Type::Struct) {
											// Array of structs
											TypeIndex type_index_from_decl = type_node.type_index();
											if (type_index_from_decl > 0 && type_index_from_decl < gTypeInfo.size()) {
												const TypeInfo& type_info = gTypeInfo[type_index_from_decl];
												const StructTypeInfo* struct_info = type_info.getStructInfo();
												if (struct_info) {
													element_size_bits = static_cast<int>(struct_info->total_size * 8);
												}
											}
										} else {
											// Regular array - use type size
											element_size_bits = static_cast<int>(type_node.size_in_bits());
											if (element_size_bits == 0) {
												element_size_bits = get_type_size_bits(type_node.type());
											}
										}
									}
								}
							}
							
							// Get the struct type index (4th element of array_operands contains type_index for struct types)
							TypeIndex type_index = 0;
							if (array_operands.size() >= 4 && std::holds_alternative<unsigned long long>(array_operands[3])) {
								type_index = static_cast<TypeIndex>(std::get<unsigned long long>(array_operands[3]));
							}
							
							// Look up member information
							if (type_index > 0 && type_index < gTypeInfo.size() && element_type == Type::Struct) {
								const StructTypeInfo* struct_info = gTypeInfo[type_index].getStructInfo();
								if (struct_info) {
									std::string_view member_name = memberAccess.member_name();
									StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
									const StructMember* member = struct_info->findMemberRecursive(member_handle);
									
									if (member) {
										// First, get the address of the array element
										TempVar elem_addr_var = var_counter.next();
										ArrayElementAddressOp elem_addr_payload;
										elem_addr_payload.result = elem_addr_var;
										elem_addr_payload.element_type = element_type;
										elem_addr_payload.element_size_in_bits = element_size_bits;
										
										// Set array (either variable name or temp)
										if (std::holds_alternative<StringHandle>(array_operands[2])) {
											elem_addr_payload.array = std::get<StringHandle>(array_operands[2]);
										} else if (std::holds_alternative<TempVar>(array_operands[2])) {
											elem_addr_payload.array = std::get<TempVar>(array_operands[2]);
										}
										
										// Set index as TypedValue
										elem_addr_payload.index = toTypedValue(std::span<const IrOperand>(&index_operands[0], 3));
										
										ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(elem_addr_payload), arraySubscript.bracket_token()));
										
										// Now compute the member address by adding the member offset
										// We need to add the offset to the pointer value
										// Treat the pointer as a 64-bit integer for arithmetic purposes
										constexpr int POINTER_SIZE_BITS = 64;
										TempVar member_addr_var = var_counter.next();
										BinaryOp add_offset;
										add_offset.lhs = { Type::UnsignedLongLong, POINTER_SIZE_BITS, elem_addr_var };  // pointer treated as integer
										add_offset.rhs = { Type::UnsignedLongLong, POINTER_SIZE_BITS, static_cast<unsigned long long>(member->offset) };
										add_offset.result = member_addr_var;
										
										ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_offset), memberAccess.member_token()));
										
										// Return pointer to member (64-bit pointer, 0 for no additional type info)
										return { member->type, POINTER_SIZE_BITS, member_addr_var, 0ULL };
									}
								}
							}
						}
					}
				}
				
				// Handle general case: &obj.member (where obj is NOT an array subscript)
				// This generates the member address directly without loading the value
				if (!object_node.is<ExpressionNode>() || 
				    (object_node.is<ExpressionNode>() && !std::holds_alternative<ArraySubscriptNode>(object_node.as<ExpressionNode>()))) {
					
					// Get the object expression (identifier, pointer dereference, etc.)
					auto object_operands = visitExpressionNode(object_node.as<ExpressionNode>(), ExpressionContext::LValueAddress);
					
					if (object_operands.size() >= 3) {
						Type object_type = std::get<Type>(object_operands[0]);
						
						// Get the struct type index
						TypeIndex type_index = 0;
						if (object_operands.size() >= 4 && std::holds_alternative<unsigned long long>(object_operands[3])) {
							type_index = static_cast<TypeIndex>(std::get<unsigned long long>(object_operands[3]));
						}
						
						// Look up member information
						if (type_index > 0 && type_index < gTypeInfo.size() && object_type == Type::Struct) {
							const StructTypeInfo* struct_info = gTypeInfo[type_index].getStructInfo();
							if (struct_info) {
								std::string_view member_name = memberAccess.member_name();
								StringHandle member_handle = StringTable::getOrInternStringHandle(std::string(member_name));
								const StructMember* member = struct_info->findMemberRecursive(member_handle);
								
								if (member) {
									TempVar result_var = var_counter.next();
									constexpr int POINTER_SIZE_BITS = 64;
									
									// For simple identifiers, generate a MemberAddressOp or use AddressOf with member context
									// For now, use a simpler approach: emit AddressOf, then Add offset in generated code
									// But mark the intermediate as NOT a reference to avoid dereferencing
									
									if (std::holds_alternative<StringHandle>(object_operands[2])) {
										StringHandle obj_name = std::get<StringHandle>(object_operands[2]);
										
										// Create a custom AddressOf-like operation that computes obj_addr + member_offset directly
										// We'll use ArrayElementAddress with index 0 and treat it as a base address calc
										// Actually, let's just emit the calculation inline without using intermediate temps
										
										// Generate IR to compute the member address
										// We need a MemberAddressOp or similar
										// For now, let's use the existing approach but avoid marking as reference
										
										// Option: Generate AddressOfMemberOp
										AddressOfMemberOp addr_member_op;
										addr_member_op.result = result_var;
										addr_member_op.base_object = obj_name;
										addr_member_op.member_offset = static_cast<int>(member->offset);
										addr_member_op.member_type = member->type;
										addr_member_op.member_size_in_bits = static_cast<int>(member->size * 8);
										
										ir_.addInstruction(IrInstruction(IrOpcode::AddressOfMember, std::move(addr_member_op), memberAccess.member_token()));
										
										// Return pointer to member
										return { member->type, POINTER_SIZE_BITS, result_var, 0ULL };
									}
								}
							}
						}
					}
				}
			}
			
			// Handle &arr[index] (without member access) - includes multidimensional arrays
			if (std::holds_alternative<ArraySubscriptNode>(operandExpr)) {
				const ArraySubscriptNode& arraySubscript = std::get<ArraySubscriptNode>(operandExpr);
				
				// Check if this is a multidimensional array access (nested ArraySubscriptNode)
				const ExpressionNode& array_expr = arraySubscript.array_expr().as<ExpressionNode>();
				if (std::holds_alternative<ArraySubscriptNode>(array_expr)) {
					// This is a multidimensional array access like &arr[i][j]
					auto multi_dim = collectMultiDimArrayIndices(arraySubscript);
					
					if (multi_dim.is_valid && multi_dim.base_decl) {
						// Compute flat index using the same logic as generateArraySubscriptIr
						const auto& dims = multi_dim.base_decl->array_dimensions();
						std::vector<size_t> strides;
						strides.reserve(dims.size());
						
						// Calculate strides (same as in generateArraySubscriptIr)
						bool valid_dimensions = true;
						for (size_t i = 0; i < dims.size(); ++i) {
							size_t stride = 1;
							for (size_t j = i + 1; j < dims.size(); ++j) {
								ConstExpr::EvaluationContext ctx(symbol_table);
								auto eval_result = ConstExpr::Evaluator::evaluate(dims[j], ctx);
								if (eval_result.success && eval_result.as_int() > 0) {
									stride *= static_cast<size_t>(eval_result.as_int());
								} else {
									// Invalid dimension - fall through to single-dimension handling
									valid_dimensions = false;
									break;
								}
							}
							if (!valid_dimensions) break;
							strides.push_back(stride);
						}
						
						if (!valid_dimensions) {
							// Fall through to single-dimensional array handling
							goto single_dim_handling;
						}
						
						// Get element type and size
						const TypeSpecifierNode& type_node = multi_dim.base_decl->type_node().as<TypeSpecifierNode>();
						Type element_type = type_node.type();
						int element_size_bits = static_cast<int>(type_node.size_in_bits());
						if (element_size_bits == 0) {
							element_size_bits = get_type_size_bits(element_type);
						}
						TypeIndex element_type_index = type_node.type_index();
						
						// Compute flat index: for arr[i][j] on arr[M][N], index = i*N + j
						TempVar flat_index = var_counter.next();
						bool first_term = true;
						
						for (size_t k = 0; k < multi_dim.indices.size(); ++k) {
							auto idx_operands = visitExpressionNode(multi_dim.indices[k].as<ExpressionNode>());
							
							if (strides[k] == 1) {
								if (first_term) {
									// flat_index = indices[k]
									AssignmentOp assign_op;
									assign_op.result = flat_index;
									assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
									assign_op.rhs = toTypedValue(idx_operands);
									ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
									first_term = false;
								} else {
									// flat_index += indices[k]
									TempVar new_flat = var_counter.next();
									BinaryOp add_op;
									add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
									add_op.rhs = toTypedValue(idx_operands);
									add_op.result = IrValue{new_flat};
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
									flat_index = new_flat;
								}
							} else {
								// temp = indices[k] * strides[k]
								TempVar temp_prod = var_counter.next();
								BinaryOp mul_op;
								mul_op.lhs = toTypedValue(idx_operands);
								mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[k])};
								mul_op.result = IrValue{temp_prod};
								ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
								
								if (first_term) {
									flat_index = temp_prod;
									first_term = false;
								} else {
									// flat_index += temp
									TempVar new_flat = var_counter.next();
									BinaryOp add_op;
									add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
									add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, temp_prod};
									add_op.result = IrValue{new_flat};
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
									flat_index = new_flat;
								}
							}
						}
						
						// Now generate ArrayElementAddress with the flat index
						TempVar addr_var = var_counter.next();
						ArrayElementAddressOp payload;
						payload.result = addr_var;
						payload.element_type = element_type;
						payload.element_size_in_bits = element_size_bits;
						payload.array = StringTable::getOrInternStringHandle(multi_dim.base_array_name);
						payload.index.type = Type::UnsignedLongLong;
						payload.index.size_in_bits = 64;
						payload.index.value = flat_index;
						
						ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(payload), arraySubscript.bracket_token()));
						
						return { element_type, 64, addr_var, static_cast<unsigned long long>(element_type_index) };
					}
				}
				
				// Fall through to single-dimensional array handling
				single_dim_handling:
				// Get the array and index operands
				auto array_operands = visitExpressionNode(arraySubscript.array_expr().as<ExpressionNode>());
				auto index_operands = visitExpressionNode(arraySubscript.index_expr().as<ExpressionNode>());
				
				Type element_type = std::get<Type>(array_operands[0]);
				int element_size_bits = std::get<int>(array_operands[1]);
				
				// For arrays, array_operands[1] is the pointer size (64), not element size
				// We need to calculate the actual element size from the array declaration
				if (std::holds_alternative<StringHandle>(array_operands[2])) {
					StringHandle array_name = std::get<StringHandle>(array_operands[2]);
					std::optional<ASTNode> symbol = symbol_table.lookup(array_name);
					if (!symbol.has_value() && global_symbol_table_) {
						symbol = global_symbol_table_->lookup(array_name);
					}
					if (symbol.has_value()) {
						const DeclarationNode* decl_ptr = nullptr;
						if (symbol->is<DeclarationNode>()) {
							decl_ptr = &symbol->as<DeclarationNode>();
						} else if (symbol->is<VariableDeclarationNode>()) {
							decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
						}
						
						if (decl_ptr && (decl_ptr->is_array() || decl_ptr->type_node().as<TypeSpecifierNode>().is_array())) {
							// This is an array - calculate element size
							const TypeSpecifierNode& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
							if (type_node.pointer_depth() > 0) {
								// Array of pointers
								element_size_bits = 64;
							} else if (type_node.type() == Type::Struct) {
								// Array of structs
								TypeIndex type_index = type_node.type_index();
								if (type_index > 0 && type_index < gTypeInfo.size()) {
									const TypeInfo& type_info = gTypeInfo[type_index];
									const StructTypeInfo* struct_info = type_info.getStructInfo();
									if (struct_info) {
										element_size_bits = static_cast<int>(struct_info->total_size * 8);
									}
								}
							} else {
								// Regular array - use type size
								element_size_bits = static_cast<int>(type_node.size_in_bits());
								if (element_size_bits == 0) {
									element_size_bits = get_type_size_bits(type_node.type());
								}
							}
						}
					}
				}
				
				// Create temporary for the address
				TempVar addr_var = var_counter.next();
				
				// Create typed payload for ArrayElementAddress
				ArrayElementAddressOp payload;
				payload.result = addr_var;
				payload.element_type = element_type;
				payload.element_size_in_bits = element_size_bits;
				
				// Set array (either variable name or temp)
				if (std::holds_alternative<StringHandle>(array_operands[2])) {
					payload.array = std::get<StringHandle>(array_operands[2]);
				} else if (std::holds_alternative<TempVar>(array_operands[2])) {
					payload.array = std::get<TempVar>(array_operands[2]);
				}
				
				// Set index as TypedValue
				payload.index = toTypedValue(std::span<const IrOperand>(&index_operands[0], 3)); 
				
				ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(payload), arraySubscript.bracket_token()));
				
				// Return pointer to element (64-bit pointer)
				return { element_type, 64, addr_var, 0ULL };
			}
		}
		
		// Helper lambda to generate member increment/decrement IR
		// Returns the result operands, or empty if not applicable
		auto generateMemberIncDec = [&](StringHandle object_name, 
		                                 const StructMember* member, bool is_reference_capture,
		                                 const Token& token) -> std::vector<IrOperand> {
			int member_size_bits = static_cast<int>(member->size * 8);
			TempVar result_var = var_counter.next();
			StringHandle member_name = member->getName();
			
			if (is_reference_capture) {
				// By-reference: load pointer, dereference, inc/dec, store back through pointer
				TempVar ptr_temp = var_counter.next();
				MemberLoadOp member_load;
				member_load.result.value = ptr_temp;
				member_load.result.type = member->type;
				member_load.result.size_in_bits = 64;  // pointer
				member_load.object = object_name;
				member_load.member_name = member_name;
				member_load.offset = static_cast<int>(member->offset);
				member_load.is_reference = true;
				member_load.is_rvalue_reference = false;
				member_load.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), token));
				
				// Load current value through pointer
				TempVar current_val = var_counter.next();
				DereferenceOp deref_op;
				deref_op.result = current_val;
				deref_op.pointer.type = member->type;
				deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
				deref_op.pointer.pointer_depth = 1;  // TODO: Verify pointer depth
				deref_op.pointer.value = ptr_temp;
				ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), token));
				
				bool is_prefix = unaryOperatorNode.is_prefix();
				BinaryOp add_op{
					.lhs = { member->type, member_size_bits, current_val },
					.rhs = { Type::Int, 32, 1ULL },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(
					unaryOperatorNode.op() == "++" ? IrOpcode::Add : IrOpcode::Subtract, 
					std::move(add_op), token));
				
				// Store back through pointer
				DereferenceStoreOp store_op;
				store_op.pointer.type = member->type;
				store_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
				store_op.pointer.pointer_depth = 1;  // Single pointer dereference
				store_op.pointer.value = ptr_temp;
				store_op.value = { member->type, member_size_bits, result_var };
				ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));
				
				TempVar return_val = is_prefix ? result_var : current_val;
				return { member->type, member_size_bits, return_val, 0ULL };
			} else {
				// By-value: load member, inc/dec, store back to member
				TempVar current_val = var_counter.next();
				MemberLoadOp member_load;
				member_load.result.value = current_val;
				member_load.result.type = member->type;
				member_load.result.size_in_bits = member_size_bits;
				member_load.object = object_name;
				member_load.member_name = member_name;
				member_load.offset = static_cast<int>(member->offset);
				member_load.is_reference = false;
				member_load.is_rvalue_reference = false;
				member_load.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), token));
				
				bool is_prefix = unaryOperatorNode.is_prefix();
				BinaryOp add_op{
					.lhs = { member->type, member_size_bits, current_val },
					.rhs = { Type::Int, 32, 1ULL },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(
					unaryOperatorNode.op() == "++" ? IrOpcode::Add : IrOpcode::Subtract, 
					std::move(add_op), token));
				
				// Store back to member
				MemberStoreOp store_op;
				store_op.object = object_name;
				store_op.member_name = member_name;
				store_op.offset = static_cast<int>(member->offset);
				store_op.value = { member->type, member_size_bits, result_var };
				store_op.is_reference = false;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_op), token));
				
				TempVar return_val = is_prefix ? result_var : current_val;
				return { member->type, member_size_bits, return_val, 0ULL };
			}
		};
		
		// Check if this is an increment/decrement on a captured variable in a lambda
		if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--") && 
		    current_lambda_context_.isActive() && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
				StringHandle var_name_str = StringTable::getOrInternStringHandle(identifier.name());
				
				// Check if this is a captured variable
				if (current_lambda_context_.captures.find(var_name_str) != current_lambda_context_.captures.end()) {
					// Look up the closure struct type
					auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
					if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
						const StructTypeInfo* struct_info = type_it->second->getStructInfo();
						const StructMember* member = struct_info->findMemberRecursive(var_name_str);
						if (member) {
							auto kind_it = current_lambda_context_.capture_kinds.find(var_name_str);
							bool is_reference = (kind_it != current_lambda_context_.capture_kinds.end() &&
							                     kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);
							return generateMemberIncDec(StringTable::getOrInternStringHandle("this"sv), member, is_reference, 
							                            unaryOperatorNode.get_token());
						}
					}
				}
			}
		}
		
		// Check if this is an increment/decrement on a struct member (e.g., ++inst.v)
		if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--") && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<MemberAccessNode>(operandExpr)) {
				const MemberAccessNode& member_access = std::get<MemberAccessNode>(operandExpr);
				auto member_name = StringTable::getOrInternStringHandle(member_access.member_name());
				
				// Get the object being accessed
				ASTNode object_node = member_access.object();
				if (object_node.is<ExpressionNode>()) {
					const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(obj_expr)) {
						const IdentifierNode& object_ident = std::get<IdentifierNode>(obj_expr);
						auto object_name = StringTable::getOrInternStringHandle(object_ident.name());
						
						// Look up the struct in symbol table
						std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
						if (!symbol.has_value() && global_symbol_table_) {
							symbol = global_symbol_table_->lookup(object_name);
						}
						
						if (symbol.has_value()) {
							const DeclarationNode* object_decl = get_decl_from_symbol(*symbol);
							if (object_decl) {
								const TypeSpecifierNode& object_type = object_decl->type_node().as<TypeSpecifierNode>();
								if (is_struct_type(object_type.type())) {
									TypeIndex type_index = object_type.type_index();
									if (type_index < gTypeInfo.size()) {
										const StructTypeInfo* struct_info = gTypeInfo[type_index].getStructInfo();
										if (struct_info) {
											const StructMember* member = struct_info->findMemberRecursive(member_name);
											if (member) {
												return generateMemberIncDec(object_name, member, false,
												                            member_access.member_token());
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
		
		std::vector<IrOperand> operandIrOperands;
		bool operandHandledAsIdentifier = false;
		// For ++, --, and & operators on identifiers, use tryBuildIdentifierOperand
		// This ensures we get the variable name (or static local's mangled name) directly
		// rather than generating a load that would lose the variable identity
		if ((unaryOperatorNode.op() == "++" || unaryOperatorNode.op() == "--" || unaryOperatorNode.op() == "&") && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
				operandHandledAsIdentifier = tryBuildIdentifierOperand(identifier, operandIrOperands);
			}
		}

		// Special case: unary plus on lambda triggers decay to function pointer
		// Check if operand is a lambda expression before visiting it
		if (unaryOperatorNode.op() == "+" && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<LambdaExpressionNode>(operandExpr)) {
				const LambdaExpressionNode& lambda = std::get<LambdaExpressionNode>(operandExpr);
				
				// For non-capturing lambdas, unary plus triggers conversion to function pointer
				// This returns the address of the lambda's __invoke static function
				if (lambda.captures().empty()) {
					// Generate the lambda functions (operator(), __invoke, etc.)
					generateLambdaExpressionIr(lambda);
					
					// Return the address of the __invoke function
					TempVar func_addr_var = generateLambdaInvokeFunctionAddress(lambda);
					return { Type::FunctionPointer, 64, func_addr_var, 0ULL };
				}
				// For capturing lambdas, fall through to normal handling
				// (they cannot decay to function pointers)
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
			// Logical NOT always returns bool8
			return { Type::Bool, 8, result_var, 0ULL };
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
			// Increment operator (prefix or postfix)
			
			// Check if this is a pointer increment (requires pointer arithmetic)
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
						} else {
							FLASH_LOG(Codegen, Error, "Could not type for identifier ", identifier.name());
							assert(false && "Invalid type node");
						}
						
						if (type_node->pointer_depth() > 0) {
							is_pointer = true;
							// Calculate element size for pointer arithmetic
							if (type_node->pointer_depth() > 1) {
								element_size = 8;  // Multi-level pointer: element is a pointer
							} else {
								// Single-level pointer: element size is sizeof(base_type)
								element_size = getSizeInBytes(type_node->type(), type_node->type_index(), type_node->size_in_bits());
							}
						}
					}
				}
			}
			
			// Use pointer-aware increment/decrement opcode
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			
			// Store element size for pointer arithmetic in the IR
			if (is_pointer) {
				// For pointers, we use a BinaryOp to add element_size instead
				// Use UnsignedLongLong for pointer arithmetic (pointers are 64-bit addresses)
			if (unaryOperatorNode.is_prefix()) {
				// ++ptr becomes: ptr = ptr + element_size
				BinaryOp add_op{
					.lhs = { Type::UnsignedLongLong, 64, std::holds_alternative<StringHandle>(operandIrOperands[2]) ? std::get<StringHandle>(operandIrOperands[2]) : IrValue{} },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));					// Store back to the pointer variable
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						AssignmentOp assign_op;
						assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
						assign_op.lhs = { Type::UnsignedLongLong, 64, std::get<StringHandle>(operandIrOperands[2]) };
						assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					}
					// Return pointer value (64-bit)
					return { operandType, 64, result_var, 0ULL };
				} else {
					// ptr++ (postfix): save old value, increment, return old value
					TempVar old_value = var_counter.next();
					
					// Save old value
					AssignmentOp save_op;
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						save_op.result = old_value;
						save_op.lhs = { Type::UnsignedLongLong, 64, old_value };
						save_op.rhs = toTypedValue(operandIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(save_op), Token()));
					}
					
				// ptr = ptr + element_size
				BinaryOp add_op{
					.lhs = { Type::UnsignedLongLong, 64, std::holds_alternative<StringHandle>(operandIrOperands[2]) ? std::get<StringHandle>(operandIrOperands[2]) : IrValue{} },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));					// Store back to the pointer variable
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						AssignmentOp assign_op;
						assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
						assign_op.lhs = { Type::UnsignedLongLong, 64, std::get<StringHandle>(operandIrOperands[2]) };
						assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					}
					// Return old pointer value
					return { operandType, 64, old_value, 0ULL };
				}
			} else {
				// Regular integer increment
				if (unaryOperatorNode.is_prefix()) {
					// Prefix increment: ++x
					ir_.addInstruction(IrInstruction(IrOpcode::PreIncrement, unary_op, Token()));
				} else {
					// Postfix increment: x++
					ir_.addInstruction(IrInstruction(IrOpcode::PostIncrement, unary_op, Token()));
				}
			}
		}
		else if (unaryOperatorNode.op() == "--") {
			// Decrement operator (prefix or postfix)
			
			// Check if this is a pointer decrement (requires pointer arithmetic)
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
							// Calculate element size for pointer arithmetic
							if (type_node->pointer_depth() > 1) {
								element_size = 8;  // Multi-level pointer: element is a pointer
							} else {
								// Single-level pointer: element size is sizeof(base_type)
								element_size = getSizeInBytes(type_node->type(), type_node->type_index(), type_node->size_in_bits());
							}
						}
					}
				}
			}
			
			// Use pointer-aware decrement opcode
			UnaryOp unary_op{
				.value = toTypedValue(operandIrOperands),
				.result = result_var
			};
			
			// Store element size for pointer arithmetic in the IR
			if (is_pointer) {
				// For pointers, we use a BinaryOp to subtract element_size instead
				// Use UnsignedLongLong for pointer arithmetic (pointers are 64-bit addresses)
			if (unaryOperatorNode.is_prefix()) {
				// --ptr becomes: ptr = ptr - element_size
				BinaryOp sub_op{
					.lhs = { Type::UnsignedLongLong, 64, std::holds_alternative<StringHandle>(operandIrOperands[2]) ? std::get<StringHandle>(operandIrOperands[2]) : IrValue{} },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), Token()));					// Store back to the pointer variable
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						AssignmentOp assign_op;
						assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
						assign_op.lhs = { Type::UnsignedLongLong, 64, std::get<StringHandle>(operandIrOperands[2]) };
						assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					}
					// Return pointer value (64-bit)
					return { operandType, 64, result_var, 0ULL };
				} else {
					// ptr-- (postfix): save old value, decrement, return old value
					TempVar old_value = var_counter.next();
					
					// Save old value
					AssignmentOp save_op;
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						save_op.result = old_value;
						save_op.lhs = { Type::UnsignedLongLong, 64, old_value };
						save_op.rhs = toTypedValue(operandIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(save_op), Token()));
					}
					
				// ptr = ptr - element_size
				BinaryOp sub_op{
					.lhs = { Type::UnsignedLongLong, 64, std::holds_alternative<StringHandle>(operandIrOperands[2]) ? std::get<StringHandle>(operandIrOperands[2]) : IrValue{} },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), Token()));					// Store back to the pointer variable
					if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
						AssignmentOp assign_op;
						assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
						assign_op.lhs = { Type::UnsignedLongLong, 64, std::get<StringHandle>(operandIrOperands[2]) };
						assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
					}
					// Return old pointer value
					return { operandType, 64, old_value, 0ULL };
				}
			} else {
				// Regular integer decrement
				if (unaryOperatorNode.is_prefix()) {
					// Prefix decrement: --x
					ir_.addInstruction(IrInstruction(IrOpcode::PreDecrement, unary_op, Token()));
				} else {
					// Postfix decrement: x--
					ir_.addInstruction(IrInstruction(IrOpcode::PostDecrement, unary_op, Token()));
				}
			}
		}
		else if (unaryOperatorNode.op() == "&") {
			// Address-of operator: &x
			// Get the current pointer depth from operandIrOperands
			unsigned long long operand_ptr_depth = 0;
			if (operandIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
				operand_ptr_depth = std::get<unsigned long long>(operandIrOperands[3]);
			}
			
			// Create typed payload with TypedValue
			AddressOfOp op;
			op.result = result_var;
			
			// Populate TypedValue with full type information
			op.operand.type = operandType;
			op.operand.size_in_bits = std::get<int>(operandIrOperands[1]);
			op.operand.pointer_depth = static_cast<int>(operand_ptr_depth);
			
			// Get the operand value - it's at index 2 in operandIrOperands
			if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
				op.operand.value = std::get<StringHandle>(operandIrOperands[2]);
			} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
				op.operand.value = std::get<TempVar>(operandIrOperands[2]);
			} else {
				assert(false && "AddressOf operand must be StringHandle or TempVar");
			}
			
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, op, Token()));
			// Return 64-bit pointer with incremented pointer depth
			return { operandType, 64, result_var, operand_ptr_depth + 1 };
		}
		else if (unaryOperatorNode.op() == "*") {
			// Dereference operator: *x
			// When dereferencing a pointer, the result size depends on the pointer depth:
			// - For single pointer (int*), result is the base type size (e.g., 32 for int)
			// - For multi-level pointer (int**), result is still a pointer (64 bits)
			
			// For LValueAddress context (e.g., assignment LHS like *ptr = value),
			// we need to return operands with lvalue metadata so handleLValueAssignment
			// can detect this is a dereference store.
			if (context == ExpressionContext::LValueAddress) {
				// Get the element size (what we're storing to)
				int element_size = 64; // Default to pointer size
				int pointer_depth = 0;
				
				// Get pointer depth
				if (operandIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
					pointer_depth = static_cast<int>(std::get<unsigned long long>(operandIrOperands[3]));
				} else if (unaryOperatorNode.get_operand().is<ExpressionNode>()) {
					const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(operandExpr)) {
						const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
						auto symbol = symbol_table.lookup(identifier.name());
						const DeclarationNode* decl = getDeclarationFromSymbol(symbol);
						if (decl) {
							pointer_depth = decl->type_node().as<TypeSpecifierNode>().pointer_depth();
						}
					}
				}
				
				// Calculate element size after dereference
				if (pointer_depth <= 1) {
					element_size = get_type_size_bits(operandType);
					if (element_size == 0) {
						element_size = 64;  // Default to pointer size for unknown types
					}
				}
				
				// Create a TempVar with Indirect lvalue metadata
				// This allows handleLValueAssignment to recognize this as a dereference store
				TempVar lvalue_temp = var_counter.next();
				
				// Extract the pointer base (StringHandle or TempVar)
				std::variant<StringHandle, TempVar> base;
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					base = std::get<StringHandle>(operandIrOperands[2]);
				} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
					base = std::get<TempVar>(operandIrOperands[2]);
				} else {
					// Fall back to old behavior if we can't extract base
					// This can happen with complex expressions that don't have a simple base
					FLASH_LOG(Codegen, Debug, "Dereference LValueAddress fallback: operand is not StringHandle or TempVar");
					return operandIrOperands;
				}
				
				// Set lvalue metadata with Indirect kind (dereference)
				LValueInfo lvalue_info(
					LValueInfo::Kind::Indirect,
					base,
					0  // offset is 0 for simple dereference
				);
				setTempVarMetadata(lvalue_temp, TempVarMetadata::makeLValue(lvalue_info));
				
				// Return with TempVar that has the lvalue metadata
				unsigned long long result_ptr_depth = (pointer_depth > 0) ? (pointer_depth - 1) : 0;
				return { operandType, element_size, lvalue_temp, result_ptr_depth };
			}
			
			int element_size = 64; // Default to pointer size
			int pointer_depth = 0;
			
			// First, try to get pointer depth from operandIrOperands (for TempVar results from previous operations)
			if (operandIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
				pointer_depth = static_cast<int>(std::get<unsigned long long>(operandIrOperands[3]));
			}
			// Otherwise, look up the pointer operand to determine its pointer depth from symbol table
			else if (unaryOperatorNode.get_operand().is<ExpressionNode>()) {
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
						if (type_node) {
							pointer_depth = type_node->pointer_depth();
						}
					}
				}
			}
			
			// After dereferencing, pointer_depth decreases by 1
			// If still > 0, result is a pointer (64 bits)
			// If == 0, result is the base type
			if (pointer_depth <= 1) {
				// Single-level pointer or less: result is base type size
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
			}
			// else: multi-level pointer, element_size stays 64 (pointer)
		
			// Create typed payload with TypedValue
			DereferenceOp op;
			op.result = result_var;
			
			// Populate TypedValue with full type information
			op.pointer.type = operandType;
			// Use element_size as pointee size so IRConverter can load correct width
			op.pointer.size_in_bits = element_size;
			op.pointer.pointer_depth = pointer_depth;
			
			// Get the pointer value - it's at index 2 in operandIrOperands
			if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
				op.pointer.value = std::get<StringHandle>(operandIrOperands[2]);
			} else if (std::holds_alternative<TempVar>(operandIrOperands[2])) {
				op.pointer.value = std::get<TempVar>(operandIrOperands[2]);
			} else {
				assert(false && "Dereference pointer must be StringHandle or TempVar");
			}
		
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, op, Token()));
			
			// Mark dereference result as lvalue (Option 2: Value Category Tracking)
			// *ptr is an lvalue - it designates the dereferenced object
			// Extract StringHandle or TempVar from pointer.value (IrValue)
			std::variant<StringHandle, TempVar> base;
			if (std::holds_alternative<StringHandle>(op.pointer.value)) {
				base = std::get<StringHandle>(op.pointer.value);
			} else if (std::holds_alternative<TempVar>(op.pointer.value)) {
				base = std::get<TempVar>(op.pointer.value);
			}
			LValueInfo lvalue_info(
				LValueInfo::Kind::Indirect,
				base,
				0  // offset is 0 for simple dereference
			);
			setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));
		
			// Return the dereferenced value with the decremented pointer depth
			unsigned long long result_ptr_depth = (pointer_depth > 0) ? (pointer_depth - 1) : 0;
			return { operandType, element_size, result_var, result_ptr_depth };
		}
		else {
			assert(false && "Unary operator not implemented yet");
		}

		// Return the result
		return { operandType, std::get<int>(operandIrOperands[1]), result_var, 0ULL };
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
		auto true_label = StringTable::createStringHandle(StringBuilder().append("ternary_true_").append(ternary_counter));
		auto false_label = StringTable::createStringHandle(StringBuilder().append("ternary_false_").append(ternary_counter));
		auto end_label = StringTable::createStringHandle(StringBuilder().append("ternary_end_").append(ternary_counter));
		ternary_counter++;

		// Evaluate the condition
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
		return { result_type, result_size, result_var, 0ULL };
	}

	std::vector<IrOperand> generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		const auto& op = binaryOperatorNode.op();
		static const std::unordered_set<std::string_view> compound_assignment_ops = {
			"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
		};

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

		// Special handling for assignment to array subscript or member access
		// Use LValueAddress context to avoid redundant Load instructions
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			
			// Check if LHS is an array subscript or member access (lvalue expressions)
			if (std::holds_alternative<ArraySubscriptNode>(lhs_expr) || 
			    std::holds_alternative<MemberAccessNode>(lhs_expr)) {
				
				// Evaluate LHS with LValueAddress context (no Load instruction)
				auto lhsIrOperands = visitExpressionNode(lhs_expr, ExpressionContext::LValueAddress);
				
				// Safety check: if LHS evaluation failed or returned invalid size, fall through to legacy code
				bool use_unified_handler = !lhsIrOperands.empty();
				if (use_unified_handler && lhsIrOperands.size() >= 2) {
					int lhs_size = std::get<int>(lhsIrOperands[1]);
					if (lhs_size <= 0 || lhs_size > 1024) {
						FLASH_LOG(Codegen, Info, "Unified handler skipped: invalid size (", lhs_size, ")");
						use_unified_handler = false;  // Invalid size, use legacy code
					}
				} else {
					FLASH_LOG(Codegen, Info, "Unified handler skipped: empty or insufficient operands");
					use_unified_handler = false;
				}
				
				if (use_unified_handler) {
					// Evaluate RHS normally (Load context)
					auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
					
					// Try to handle assignment using unified lvalue metadata handler
					if (handleLValueAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token())) {
						// Assignment was handled successfully via metadata
						FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for array/member assignment");
						return rhsIrOperands;
					}
					
					// If metadata handler didn't work, fall through to legacy code
					// This shouldn't happen with proper metadata, but provides a safety net
					FLASH_LOG(Codegen, Info, "Unified handler returned false, falling through to legacy code");
				}
				// If use_unified_handler is false, fall through to legacy handlers below
			}
		}

		// Special handling for assignment to member variables in member functions
		// Now that implicit member access is marked with lvalue metadata, use unified handler
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>() && current_struct_name_.isValid()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
				std::string_view lhs_name = lhs_ident.name();

				// Check if this is a member variable of the current struct
				auto type_it = gTypesByName.find(current_struct_name_);
				if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
					const StructTypeInfo* struct_info = type_it->second->getStructInfo();
					if (struct_info) {
						const StructMember* member = struct_info->findMemberRecursive(StringTable::getOrInternStringHandle(std::string(lhs_name)));
						if (member) {
							// This is an assignment to a member variable: member = value
							// Handle via unified handler (identifiers are now marked as lvalues)
							auto lhsIrOperands = visitExpressionNode(lhs_expr);
							auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
							
							// Handle assignment using unified lvalue metadata handler
							if (handleLValueAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token())) {
								// Assignment was handled successfully via metadata
								FLASH_LOG(Codegen, Debug, "Unified handler SUCCESS for implicit member assignment (", lhs_name, ")");
								return rhsIrOperands;
							}
							
							// This shouldn't happen with proper metadata, but log for debugging
							FLASH_LOG(Codegen, Error, "Unified handler unexpectedly failed for implicit member assignment: ", lhs_name);
							return { Type::Int, 32, TempVar{0} };
						}
					}
				}
			}
		}

		// Special handling for assignment to captured-by-reference variable inside lambda
		// Now that captured-by-reference identifiers are marked with lvalue metadata, use unified handler
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>() && current_lambda_context_.isActive()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
				std::string_view lhs_name = lhs_ident.name();
				StringHandle lhs_name_str = StringTable::getOrInternStringHandle(lhs_name);

				// Check if this is a captured-by-reference variable
				auto capture_it = current_lambda_context_.captures.find(lhs_name_str);
				if (capture_it != current_lambda_context_.captures.end()) {
					auto kind_it = current_lambda_context_.capture_kinds.find(lhs_name_str);
					if (kind_it != current_lambda_context_.capture_kinds.end() &&
					    kind_it->second == LambdaCaptureNode::CaptureKind::ByReference) {
						// This is assignment to a captured-by-reference variable
						// Handle via unified handler (identifiers are now marked as lvalues)
						auto lhsIrOperands = visitExpressionNode(lhs_expr);
						auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
						
						// Handle assignment using unified lvalue metadata handler
						if (handleLValueAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token())) {
							// Assignment was handled successfully via metadata
							FLASH_LOG(Codegen, Debug, "Unified handler SUCCESS for captured-by-reference assignment (", lhs_name, ")");
							return rhsIrOperands;
						}
						
						// This shouldn't happen with proper metadata, but log for debugging
						FLASH_LOG(Codegen, Error, "Unified handler unexpectedly failed for captured-by-reference assignment: ", lhs_name);
						return { Type::Int, 32, TempVar{0} };
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
						assign_op.lhs.value = StringTable::getOrInternStringHandle(lhs_name);
						assign_op.rhs = toTypedValue(rhsIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
						
						// Return the result
						return { lhs_type.type(), static_cast<int>(lhs_type.size_in_bits()), result_var, 0ULL };
					}
				}
			}
		}

		// Special handling for global variable assignment
		if (op == "=" && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
				const IdentifierNode& lhs_ident = std::get<IdentifierNode>(lhs_expr);
				std::string_view lhs_name = lhs_ident.name();

				// Check if this is a global variable (not found in local symbol table, but found in global)
				const std::optional<ASTNode> local_symbol = symbol_table.lookup(lhs_name);
				bool is_global = false;
				
				if (!local_symbol.has_value() && global_symbol_table_) {
					// Not found locally - check global symbol table
					const std::optional<ASTNode> global_symbol = global_symbol_table_->lookup(lhs_name);
					if (global_symbol.has_value() && global_symbol->is<VariableDeclarationNode>()) {
						is_global = true;
					}
				}
				
				if (is_global) {
					// This is a global variable assignment - generate GlobalStore instruction
					// Generate IR for the RHS
					auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

					// Generate GlobalStore IR: global_store @global_name, %value
					std::vector<IrOperand> store_operands;
					store_operands.emplace_back(StringTable::getOrInternStringHandle(lhs_name));  // global name
					
					// Extract the value from RHS (rhsIrOperands[2])
					if (std::holds_alternative<TempVar>(rhsIrOperands[2])) {
						store_operands.emplace_back(std::get<TempVar>(rhsIrOperands[2]));
					} else if (std::holds_alternative<unsigned long long>(rhsIrOperands[2]) || std::holds_alternative<double>(rhsIrOperands[2])) {
						// For constant values, we need to create a temp var and assign to it first
						TempVar temp = var_counter.next();
						AssignmentOp assign_op;
						assign_op.result = temp;
						assign_op.lhs.type = std::get<Type>(rhsIrOperands[0]);
						assign_op.lhs.size_in_bits = std::get<int>(rhsIrOperands[1]);
						assign_op.lhs.value = temp;
						assign_op.rhs = toTypedValue(rhsIrOperands);
						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
						store_operands.emplace_back(temp);
					} else {
						store_operands.emplace_back(std::get<TempVar>(rhsIrOperands[2]));
					}

					ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_operands), binaryOperatorNode.get_token());

					// Return the RHS value as the result (assignment expression returns the assigned value)
					return rhsIrOperands;
				}
			}
		}

		// Special handling for compound assignment to array subscript or member access
		// Use LValueAddress context for the LHS, similar to regular assignment
		// Helper lambda to check if operator is a compound assignment
		if (compound_assignment_ops.count(op) > 0 &&
		    binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
			const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
			
			// Check if LHS is an array subscript or member access (lvalue expressions)
			if (std::holds_alternative<ArraySubscriptNode>(lhs_expr) || 
			    std::holds_alternative<MemberAccessNode>(lhs_expr)) {
				
				// Evaluate LHS with LValueAddress context (no Load instruction)
				auto lhsIrOperands = visitExpressionNode(lhs_expr, ExpressionContext::LValueAddress);
				
				// Safety check
				bool use_unified_handler = !lhsIrOperands.empty();
				if (use_unified_handler && lhsIrOperands.size() >= 2) {
					int lhs_size = std::get<int>(lhsIrOperands[1]);
					if (lhs_size <= 0 || lhs_size > 1024) {
						FLASH_LOG(Codegen, Info, "Compound assignment unified handler skipped: invalid size (", lhs_size, ")");
						use_unified_handler = false;
					}
				} else {
					FLASH_LOG(Codegen, Info, "Compound assignment unified handler skipped: empty or insufficient operands");
					use_unified_handler = false;
				}
				
				if (use_unified_handler) {
					// Evaluate RHS normally (Load context)
					auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());
					
					// For compound assignments, we need to:
					// 1. Load the current value from the lvalue
					// 2. Perform the operation (add, subtract, etc.)
					// 3. Store the result back to the lvalue
					
					// Try to handle compound assignment using lvalue metadata
					if (handleLValueCompoundAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token(), op)) {
						// Compound assignment was handled successfully via metadata
						FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for array/member compound assignment");
						// Return the LHS operands which contain the result type/size info
						// The actual result value is stored in the lvalue, so we return lvalue info
						return lhsIrOperands;
					}
					
					// If metadata handler didn't work, fall through to legacy code
					FLASH_LOG(Codegen, Info, "Compound assignment unified handler returned false, falling through to legacy code");
				}
			}
		}

		// Generate IR for the left-hand side and right-hand side of the operation
		// For assignment (=), use LValueAddress context for LHS to avoid dereferencing reference parameters
		ExpressionContext lhs_context = (op == "=") ? ExpressionContext::LValueAddress : ExpressionContext::Load;
		auto lhsIrOperands = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>(), lhs_context);
		auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

		// Try unified metadata-based handler for compound assignments on identifiers
		// This ensures implicit member accesses (including [*this] lambdas) use the correct base object
		if (compound_assignment_ops.count(op) > 0 &&
		    handleLValueCompoundAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token(), op)) {
			FLASH_LOG(Codegen, Info, "Unified handler SUCCESS for compound assignment");
			return lhsIrOperands;
		}

		// Try unified lvalue-based assignment handler (uses value category metadata)
		// This handles assignments like *ptr = value using lvalue metadata
		if (op == "=" && handleLValueAssignment(lhsIrOperands, rhsIrOperands, binaryOperatorNode.get_token())) {
			// Assignment was handled via lvalue metadata, return RHS as result
			return rhsIrOperands;
		}

		// Get the types and sizes of the operands
		Type lhsType = std::get<Type>(lhsIrOperands[0]);
		Type rhsType = std::get<Type>(rhsIrOperands[0]);
		int lhsSize = std::get<int>(lhsIrOperands[1]);
		int rhsSize = std::get<int>(rhsIrOperands[1]);

		// Check for binary operator overloads on struct types
		// Binary operators like +, -, *, etc. can be overloaded as member functions
		// This should be checked before trying to generate built-in arithmetic operations
		if (lhsType == Type::Struct && lhsIrOperands.size() >= 4) {
			// Get the type index of the left operand
			TypeIndex lhs_type_index = 0;
			if (std::holds_alternative<unsigned long long>(lhsIrOperands[3])) {
				lhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(lhsIrOperands[3]));
			}
			
			// Get the type index of the right operand (if it's a struct)
			TypeIndex rhs_type_index = 0;
			if (rhsType == Type::Struct && rhsIrOperands.size() >= 4) {
				if (std::holds_alternative<unsigned long long>(rhsIrOperands[3])) {
					rhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(rhsIrOperands[3]));
				}
			}
			
			// List of binary operators that can be overloaded
			// Skip assignment operators (=, +=, -=, etc.) as they are handled separately
			static const std::unordered_set<std::string_view> overloadable_binary_ops = {
				"+", "-", "*", "/", "%",           // Arithmetic
				"==", "!=", "<", ">", "<=", ">=",  // Comparison
				"&&", "||",                        // Logical
				"&", "|", "^",                     // Bitwise
				"<<", ">>",                        // Shift
				",",                               // Comma (already handled above)
				"<=>",                             // Spaceship (handled below)
			};
			
			if (overloadable_binary_ops.count(op) > 0 && lhs_type_index > 0) {
				// Check for operator overload
				auto overload_result = findBinaryOperatorOverload(lhs_type_index, rhs_type_index, op);
				
				if (overload_result.has_overload) {
					// Found an overload! Generate a member function call instead of built-in operation
					FLASH_LOG_FORMAT(Codegen, Debug, "Resolving binary operator{} overload for type index {}", 
					         op, lhs_type_index);
					
					const StructMemberFunction& member_func = *overload_result.member_overload;
					const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
					
					// Get struct name for mangling
					std::string_view struct_name = StringTable::getStringView(gTypeInfo[lhs_type_index].name());
					
					// Get the return type from the function declaration
					const TypeSpecifierNode& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
					
					// Get the parameter types for mangling
					std::vector<TypeSpecifierNode> param_types;
					for (const auto& param_node : func_decl.parameter_nodes()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
							param_types.push_back(param_type);
						}
					}
					
					// Generate mangled name for the operator
					std::string operator_func_name = "operator";
					operator_func_name += op;
					std::vector<std::string_view> empty_namespace;
					auto mangled_name = NameMangling::generateMangledName(
						operator_func_name,
						return_type,
						param_types,
						false, // not variadic
						struct_name,
						empty_namespace,
						Linkage::CPlusPlus
					);
					
					// Generate the call to the operator overload
					// For member function: a.operator+(b) where 'a' is 'this' and 'b' is the parameter
					TempVar result_var = var_counter.next();
					
					// Take address of LHS to pass as 'this' pointer
					// The LHS operand contains a struct value - extract it properly
					std::variant<StringHandle, TempVar> lhs_value;
					if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
						lhs_value = std::get<StringHandle>(lhsIrOperands[2]);
					} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
						lhs_value = std::get<TempVar>(lhsIrOperands[2]);
					} else {
						// Can't take address of non-lvalue
						FLASH_LOG(Codegen, Error, "Cannot take address of binary operator LHS - not an lvalue");
						return {};
					}
					
					TempVar lhs_addr = var_counter.next();
					AddressOfOp addr_op;
					addr_op.result = lhs_addr;
					addr_op.operand.type = lhsType;
					addr_op.operand.size_in_bits = lhsSize;
					addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
					// Convert std::variant<StringHandle, TempVar> to IrValue
					if (std::holds_alternative<StringHandle>(lhs_value)) {
						addr_op.operand.value = std::get<StringHandle>(lhs_value);
					} else {
						addr_op.operand.value = std::get<TempVar>(lhs_value);
					}
					ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), binaryOperatorNode.get_token()));
					
					// Create the call operation
					CallOp call_op;
					call_op.result = result_var;
					call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
					call_op.return_type = return_type.type();
					call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
					call_op.return_type_index = return_type.type_index();
					call_op.is_member_function = true;  // This is a member function call
					
					// Detect if returning struct by value (needs hidden return parameter for RVO)
					bool returns_struct_by_value = (return_type.type() == Type::Struct && return_type.pointer_depth() == 0 && !return_type.is_reference());
					if (returns_struct_by_value) {
						call_op.uses_return_slot = true;
					}
					
					// Add 'this' pointer as first argument
					TypedValue this_arg;
					this_arg.type = lhsType;
					this_arg.size_in_bits = 64;  // 'this' is always a pointer (64-bit)
					this_arg.value = lhs_addr;
					call_op.args.push_back(this_arg);
					
					// Add RHS as the second argument
					// Check if the parameter is a reference - if so, we need to pass the address
					if (!param_types.empty() && param_types[0].is_reference()) {
						// Parameter is a reference - we need to pass the address of RHS
						std::variant<StringHandle, TempVar> rhs_value;
						if (std::holds_alternative<StringHandle>(rhsIrOperands[2])) {
							rhs_value = std::get<StringHandle>(rhsIrOperands[2]);
						} else if (std::holds_alternative<TempVar>(rhsIrOperands[2])) {
							rhs_value = std::get<TempVar>(rhsIrOperands[2]);
						} else {
							// Can't take address of non-lvalue
							FLASH_LOG(Codegen, Error, "Cannot take address of binary operator RHS - not an lvalue");
							return {};
						}
						
						TempVar rhs_addr = var_counter.next();
						AddressOfOp rhs_addr_op;
						rhs_addr_op.result = rhs_addr;
						rhs_addr_op.operand.type = rhsType;
						rhs_addr_op.operand.size_in_bits = rhsSize;
						rhs_addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
						// Convert std::variant<StringHandle, TempVar> to IrValue
						if (std::holds_alternative<StringHandle>(rhs_value)) {
							rhs_addr_op.operand.value = std::get<StringHandle>(rhs_value);
						} else {
							rhs_addr_op.operand.value = std::get<TempVar>(rhs_value);
						}
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(rhs_addr_op), binaryOperatorNode.get_token()));
						
						// Create TypedValue with the address
						TypedValue rhs_arg;
						rhs_arg.type = rhsType;
						rhs_arg.size_in_bits = 64;  // Reference is a pointer (64-bit)
						rhs_arg.value = rhs_addr;
						call_op.args.push_back(rhs_arg);
					} else {
						// Parameter is not a reference - pass the value directly
						call_op.args.push_back(toTypedValue(rhsIrOperands));
					}
					
					ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binaryOperatorNode.get_token()));
					
					// Return the result
					return {return_type.type(), static_cast<int>(return_type.size_in_bits()), result_var, 
					        return_type.type_index()};
				}
			}
		}

		// Special handling for spaceship operator <=> on struct types
		// This should be converted to a member function call: lhs.operator<=>(rhs)
		if (op == "<=>") {
			// Check if LHS is a struct type
			if (lhsType == Type::Struct && binaryOperatorNode.get_lhs().is<ExpressionNode>()) {
				const ExpressionNode& lhs_expr = binaryOperatorNode.get_lhs().as<ExpressionNode>();
				
				// Get the LHS value - can be an identifier, member access, or other expression
				std::variant<StringHandle, TempVar> lhs_value;
				TypeIndex lhs_type_index = 0;
				
				if (std::holds_alternative<IdentifierNode>(lhs_expr)) {
					// Simple identifier case: p1 <=> p2
					const auto& lhs_id = std::get<IdentifierNode>(lhs_expr);
					std::string_view lhs_name = lhs_id.name();
					lhs_value = StringTable::getOrInternStringHandle(lhs_name);
					
					// Get the struct type info from symbol table
					auto symbol = symbol_table.lookup(lhs_name);
					if (symbol && symbol->is<VariableDeclarationNode>()) {
						const auto& var_decl = symbol->as<VariableDeclarationNode>();
						const auto& decl = var_decl.declaration();
						const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
						lhs_type_index = type_node.type_index();
					} else if (symbol && symbol->is<DeclarationNode>()) {
						const auto& decl = symbol->as<DeclarationNode>();
						const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
						lhs_type_index = type_node.type_index();
					} else {
						// Can't find the variable declaration
						return {};
					}
				} else if (std::holds_alternative<MemberAccessNode>(lhs_expr)) {
					// Member access case: p.member <=> q.member
					const auto& member_access = std::get<MemberAccessNode>(lhs_expr);
					
					// Generate IR for the member access expression
					std::vector<IrOperand> member_ir = generateMemberAccessIr(member_access);
					if (member_ir.empty() || member_ir.size() < 4) {
						return {};
					}
					
					// Extract the result temp var and type index
					lhs_value = std::get<TempVar>(member_ir[2]);
					lhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(member_ir[3]));
				} else {
					// Other expression types - use already-generated lhsIrOperands
					// The lhsIrOperands were already generated earlier in this function
					if (lhsIrOperands.size() >= 3 && std::holds_alternative<TempVar>(lhsIrOperands[2])) {
						lhs_value = std::get<TempVar>(lhsIrOperands[2]);
					} else {
						// Complex expression that doesn't produce a temp var
						return {};
					}
					
					// Try to get type index from lhsIrOperands if available
					if (lhsIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(lhsIrOperands[3])) {
						lhs_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(lhsIrOperands[3]));
					} else {
						// Can't determine type index for complex expression
						return {};
					}
				}
				
				// Look up the operator<=> function in the struct
				if (lhs_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[lhs_type_index];
					if (type_info.struct_info_) {
						const StructTypeInfo& struct_info = *type_info.struct_info_;
						
						// Find operator<=> in member functions
						const StructMemberFunction* spaceship_op = nullptr;
						for (const auto& func : struct_info.member_functions) {
							if (func.is_operator_overload && func.operator_symbol == "<=>") {
								spaceship_op = &func;
								break;
							}
						}
						
						if (spaceship_op && spaceship_op->function_decl.is<FunctionDeclarationNode>()) {
							const auto& func_decl = spaceship_op->function_decl.as<FunctionDeclarationNode>();
							
							// Generate a member function call: lhs.operator<=>(rhs)
							TempVar result_var = var_counter.next();
							
							// Get return type from the function declaration
							const auto& return_type_node = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
							Type return_type = return_type_node.type();
							int return_size = static_cast<int>(return_type_node.size_in_bits());
							
							// Generate mangled name for the operator<=> call
							std::vector<TypeSpecifierNode> param_types;
							for (const auto& param_node : func_decl.parameter_nodes()) {
								if (param_node.is<DeclarationNode>()) {
									const auto& param_decl = param_node.as<DeclarationNode>();
									const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
									param_types.push_back(param_type);
								}
							}
							
							std::string_view mangled_name = generateMangledNameForCall(
								"operator<=>",
								return_type_node,
								param_types,
								false, // not variadic
								StringTable::getStringView(type_info.name())
							);
							
							// Create the call operation
							CallOp call_op;
							call_op.result = result_var;
							call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
							call_op.return_type = return_type;
							call_op.return_size_in_bits = return_size;
							call_op.is_member_function = true;
							call_op.is_variadic = func_decl.is_variadic();
							
							// Add the LHS object as the first argument (this pointer)
							// For member functions, the this pointer is passed by name or temp var
							TypedValue lhs_arg;
							lhs_arg.type = lhsType;
							lhs_arg.size_in_bits = lhsSize;
							// Convert lhs_value (which can be string_view or TempVar) to IrValue
							if (std::holds_alternative<StringHandle>(lhs_value)) {
								lhs_arg.value = IrValue(std::get<StringHandle>(lhs_value));
							} else {
								lhs_arg.value = IrValue(std::get<TempVar>(lhs_value));
							}
							call_op.args.push_back(lhs_arg);
						
							// Add the RHS as the second argument
							// Check if parameter expects a reference
							TypedValue rhs_arg = toTypedValue(rhsIrOperands);
							if (param_types.size() > 0) {
								// Check if first parameter is a reference
								const TypeSpecifierNode& param_type = param_types[0];
								if (param_type.is_reference() || param_type.is_rvalue_reference()) {
									rhs_arg.is_reference = true;
								}
							}
							call_op.args.push_back(rhs_arg);
						
							ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binaryOperatorNode.get_token()));
							
							// Return the result
							return { return_type, return_size, result_var, 0ULL };
						}
					}
				}
			}
			
			// If we get here, operator<=> is not defined or not found
			// Fall through to error handling
		}

		// Try to get pointer depth for pointer arithmetic
		int lhs_pointer_depth = 0;
		const TypeSpecifierNode* lhs_type_node = nullptr;
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
					lhs_type_node = &type_node;
				} else if (symbol && symbol->is<DeclarationNode>()) {
					const auto& decl = symbol->as<DeclarationNode>();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					lhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
					lhs_type_node = &type_node;
				}
			}
		}

		// Try to get pointer depth for RHS as well (for ptr - ptr case)
		int rhs_pointer_depth = 0;
		if (binaryOperatorNode.get_rhs().is<ExpressionNode>()) {
			const ExpressionNode& rhs_expr = binaryOperatorNode.get_rhs().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(rhs_expr)) {
				const auto& rhs_id = std::get<IdentifierNode>(rhs_expr);
				auto symbol = symbol_table.lookup(rhs_id.name());
				if (symbol && symbol->is<VariableDeclarationNode>()) {
					const auto& var_decl = symbol->as<VariableDeclarationNode>();
					const auto& decl = var_decl.declaration();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					rhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
				} else if (symbol && symbol->is<DeclarationNode>()) {
					const auto& decl = symbol->as<DeclarationNode>();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					rhs_pointer_depth = static_cast<int>(type_node.pointer_depth());
				}
			}
		}

		// Special handling for pointer subtraction (ptr - ptr)
		// Result is ptrdiff_t (number of elements between pointers)
		if (op == "-" && lhs_pointer_depth > 0 && rhs_pointer_depth > 0 && lhs_type_node) {
			// Both sides are pointers - this is pointer difference
			// C++ standard: (ptr1 - ptr2) / sizeof(*ptr1) gives element count
			// Result type is ptrdiff_t (signed long, 64-bit on x64)
			
			// Step 1: Subtract the pointers (gives byte difference)
			TempVar byte_diff = var_counter.next();
			BinaryOp sub_op{
				.lhs = { lhsType, 64, toIrValue(lhsIrOperands[2]) },
				.rhs = { rhsType, 64, toIrValue(rhsIrOperands[2]) },
				.result = byte_diff,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), binaryOperatorNode.get_token()));
			
			// Step 2: Determine element size using existing getSizeInBytes function
			size_t element_size;
			if (lhs_pointer_depth > 1) {
				element_size = 8;  // Multi-level pointer: element is a pointer
			} else {
				// Single-level pointer: element size is sizeof(base_type)
				element_size = getSizeInBytes(lhs_type_node->type(), lhs_type_node->type_index(), lhs_type_node->size_in_bits());
			}
			
			// Step 3: Divide byte difference by element size to get element count
			TempVar result_var = var_counter.next();
			BinaryOp div_op{
				.lhs = { Type::Long, 64, byte_diff },
				.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
				.result = result_var,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Divide, std::move(div_op), binaryOperatorNode.get_token()));
			
			// Return result as Long (ptrdiff_t) with 64-bit size
			return { Type::Long, 64, result_var, 0ULL };
		}

		// Special handling for pointer arithmetic (ptr + int or ptr - int)
		// Only apply if LHS is actually a pointer (has pointer_depth > 0)
		// NOT for regular 64-bit integers like long, even though they are also 64 bits
		if ((op == "+" || op == "-") && lhsSize == 64 && lhs_pointer_depth > 0 && is_integer_type(rhsType) && lhs_type_node) {
			// Left side is a pointer (64-bit with pointer_depth > 0), right side is integer
			// Result should be a pointer (64-bit)
			// Need to scale the offset by sizeof(pointed-to-type)
		
			// Determine element size using existing getSizeInBytes function
			size_t element_size;
			if (lhs_pointer_depth > 1) {
				// Multi-level pointer: element is a pointer, so 8 bytes
				element_size = 8;
			} else {
				// Single-level pointer: element size is sizeof(base_type)
				element_size = getSizeInBytes(lhs_type_node->type(), lhs_type_node->type_index(), lhs_type_node->size_in_bits());
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
			return { lhsType, 64, result_var, 0ULL };
		}
	
		// Check for logical operations BEFORE type promotions
		// Logical operations should preserve boolean types without promotion
		if (op == "&&" || op == "||") {
			TempVar result_var = var_counter.next();
			BinaryOp bin_op{
				.lhs = { Type::Bool, 8, toIrValue(lhsIrOperands[2]) },
				.rhs = { Type::Bool, 8, toIrValue(rhsIrOperands[2]) },
				.result = result_var,
			};
			IrOpcode opcode = (op == "&&") ? IrOpcode::LogicalAnd : IrOpcode::LogicalOr;
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
			return { Type::Bool, 8, result_var, 0ULL };  // Logical operations return bool8
		}

		// Special handling for pointer compound assignment (ptr += int or ptr -= int)
		// MUST be before type promotions to avoid truncating the pointer
		if ((op == "+=" || op == "-=") && lhsSize == 64 && lhs_pointer_depth > 0 && is_integer_type(rhsType) && lhs_type_node) {
			// Left side is a pointer (64-bit), right side is integer
			// Need to scale the offset by sizeof(pointed-to-type)
			FLASH_LOG_FORMAT(Codegen, Debug, "[PTR_ARITH_DEBUG] Compound assignment: lhsSize={}, pointer_depth={}, rhsType={}", lhsSize, lhs_pointer_depth, static_cast<int>(rhsType));
			
			// Determine element size using existing getSizeInBytes function
			size_t element_size;
			if (lhs_pointer_depth > 1) {
				element_size = 8;  // Multi-level pointer
			} else {
				// Single-level pointer: element size is sizeof(base_type)
				element_size = getSizeInBytes(lhs_type_node->type(), lhs_type_node->type_index(), lhs_type_node->size_in_bits());
			}
			
			// Scale the offset: offset_scaled = offset * element_size
			TempVar scaled_offset = var_counter.next();
			BinaryOp scale_op{
				.lhs = toTypedValue(rhsIrOperands),
				.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
				.result = scaled_offset,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(scale_op), binaryOperatorNode.get_token()));
			
			// ptr = ptr + scaled_offset (or ptr - scaled_offset)
			TempVar result_var = var_counter.next();
			BinaryOp ptr_arith_op{
				.lhs = { lhsType, lhsSize, toIrValue(lhsIrOperands[2]) },
				.rhs = { Type::Int, 32, scaled_offset },
				.result = result_var,
			};
			
			IrOpcode ptr_opcode = (op == "+=") ? IrOpcode::Add : IrOpcode::Subtract;
			ir_.addInstruction(IrInstruction(ptr_opcode, std::move(ptr_arith_op), binaryOperatorNode.get_token()));
			
			// Store result back to LHS (must be a variable)
			if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
				AssignmentOp assign_op;
				assign_op.result = std::get<StringHandle>(lhsIrOperands[2]);
				assign_op.lhs = { lhsType, lhsSize, std::get<StringHandle>(lhsIrOperands[2]) };
				
				// Check if LHS is a reference variable
				StringHandle lhs_handle = std::get<StringHandle>(lhsIrOperands[2]);
				std::string_view lhs_name = StringTable::getStringView(lhs_handle);
				assign_op.lhs.is_reference = isVariableReference(lhs_name);
				
				assign_op.rhs = { lhsType, lhsSize, result_var };
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
				AssignmentOp assign_op;
				assign_op.result = std::get<TempVar>(lhsIrOperands[2]);
				assign_op.lhs = { lhsType, lhsSize, std::get<TempVar>(lhsIrOperands[2]) };
				
				// Check if LHS TempVar corresponds to a reference variable
				TempVar lhs_temp = std::get<TempVar>(lhsIrOperands[2]);
				std::string_view temp_name = lhs_temp.name();
				// Remove '%' prefix if present
				if (!temp_name.empty() && temp_name[0] == '%') {
					temp_name = temp_name.substr(1);
				}
				assign_op.lhs.is_reference = isVariableReference(temp_name);
				
				assign_op.rhs = { lhsType, lhsSize, result_var };
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			}
			
			// Return the pointer result
			return { lhsType, lhsSize, result_var, 0ULL };
		}

		// Apply integer promotions and find common type
		// BUT: Skip type promotion for pointer assignments (ptr = ptr_expr)
		// Pointers should not be converted to common types
		if (op == "=" && lhsSize == 64 && lhs_pointer_depth > 0) {
			// This is a pointer assignment - no type conversions needed
			// Just assign the RHS to the LHS directly
			FLASH_LOG_FORMAT(Codegen, Debug, "[PTR_ARITH_DEBUG] Pointer assignment: lhsSize={}, pointer_depth={}", lhsSize, lhs_pointer_depth);
			
			// Get the assignment target (must be a variable)
			if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
				TempVar result_var = var_counter.next();
				AssignmentOp assign_op;
				assign_op.result = std::get<StringHandle>(lhsIrOperands[2]);
				assign_op.lhs = { lhsType, lhsSize, std::get<StringHandle>(lhsIrOperands[2]) };
				
				// Check if LHS is a reference variable
				StringHandle lhs_handle = std::get<StringHandle>(lhsIrOperands[2]);
				std::string_view lhs_name = StringTable::getStringView(lhs_handle);
				assign_op.lhs.is_reference = isVariableReference(lhs_name);
				
				assign_op.rhs = toTypedValue(rhsIrOperands);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
				// Return the assigned value
				return { lhsType, lhsSize, std::get<StringHandle>(lhsIrOperands[2]), 0ULL };
			} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
				TempVar result_var = var_counter.next();
				AssignmentOp assign_op;
				assign_op.result = std::get<TempVar>(lhsIrOperands[2]);
				assign_op.lhs = { lhsType, lhsSize, std::get<TempVar>(lhsIrOperands[2]) };
				
				// Check if LHS TempVar corresponds to a reference variable
				TempVar lhs_temp = std::get<TempVar>(lhsIrOperands[2]);
				std::string_view temp_name = lhs_temp.name();
				// Remove '%' prefix if present
				if (!temp_name.empty() && temp_name[0] == '%') {
					temp_name = temp_name.substr(1);
				}
				assign_op.lhs.is_reference = isVariableReference(temp_name);
				
				assign_op.rhs = toTypedValue(rhsIrOperands);
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
				// Return the assigned value
				return { lhsType, lhsSize, std::get<TempVar>(lhsIrOperands[2]), 0ULL };
			}
		}
		
		// Special handling for assignment: convert RHS to LHS type instead of finding common type
		// For assignment, we don't want to promote the LHS
		if (op == "=") {
			// Convert RHS to LHS type if they differ
			if (rhsType != lhsType) {
				rhsIrOperands = generateTypeConversion(rhsIrOperands, rhsType, lhsType, binaryOperatorNode.get_token());
			}
			// Now both are the same type, create assignment
			AssignmentOp assign_op;
			// Extract the LHS value directly (it's either StringHandle or TempVar)
			if (std::holds_alternative<StringHandle>(lhsIrOperands[2])) {
				assign_op.result = std::get<StringHandle>(lhsIrOperands[2]);
			} else if (std::holds_alternative<TempVar>(lhsIrOperands[2])) {
				assign_op.result = std::get<TempVar>(lhsIrOperands[2]);
			} else {
				// LHS is an immediate value - this shouldn't happen for valid assignments
				assert(false && "Assignment LHS cannot be an immediate value");
				return {};
			}
			assign_op.lhs = toTypedValue(lhsIrOperands);
			assign_op.rhs = toTypedValue(rhsIrOperands);
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), binaryOperatorNode.get_token()));
			// Assignment expression returns the LHS (the assigned-to value)
			return lhsIrOperands;
		}
		
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
		
		// Mark arithmetic/comparison result as prvalue (Option 2: Value Category Tracking)
		// Binary operations produce temporary values (prvalues) with no persistent identity
		setTempVarMetadata(result_var, TempVarMetadata::makePRValue());

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
		// Comparison operations (typed)
		// For pointer comparisons, override types to use 64-bit unsigned integers
		// Helper lambda to apply pointer comparison type override
		auto applyPointerComparisonOverride = [&](BinaryOp& bin_op, IrOpcode& opcode) {
			if (lhs_pointer_depth > 0 && rhs_pointer_depth > 0) {
				bin_op.lhs.type = Type::UnsignedLongLong;
				bin_op.lhs.size_in_bits = 64;
				bin_op.rhs.type = Type::UnsignedLongLong;
				bin_op.rhs.size_in_bits = 64;
				
				// For ordered comparisons, ensure we use unsigned comparison for pointers
				if (opcode == IrOpcode::LessThan) opcode = IrOpcode::UnsignedLessThan;
				else if (opcode == IrOpcode::LessEqual) opcode = IrOpcode::UnsignedLessEqual;
				else if (opcode == IrOpcode::GreaterThan) opcode = IrOpcode::UnsignedGreaterThan;
				else if (opcode == IrOpcode::GreaterEqual) opcode = IrOpcode::UnsignedGreaterEqual;
			}
		};
		
		if (op == "==" && !is_floating_point_op) {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			opcode = IrOpcode::Equal;
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "!=" && !is_floating_point_op) {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			opcode = IrOpcode::NotEqual;
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "<" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedLessThan : IrOpcode::LessThan;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == "<=" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedLessEqual : IrOpcode::LessEqual;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == ">" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedGreaterThan : IrOpcode::GreaterThan;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			applyPointerComparisonOverride(bin_op, opcode);
			ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		else if (op == ">=" && !is_floating_point_op) {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedGreaterEqual : IrOpcode::GreaterEqual;
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = result_var,
			};
			applyPointerComparisonOverride(bin_op, opcode);
		ir_.addInstruction(IrInstruction(opcode, std::move(bin_op), binaryOperatorNode.get_token()));
		}
		// Compound assignment operations (typed)
		// For compound assignments, result is stored back in LHS variable
		// NOTE: Pointer compound assignments (ptr += int, ptr -= int) are handled earlier,
		// before type promotions, to avoid truncating the pointer
		else if (op == "+=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),  // Store result in LHS variable
			};
			ir_.addInstruction(IrInstruction(IrOpcode::AddAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "-=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::SubAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "*=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::MulAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "/=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::DivAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "%=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ModAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "&=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::AndAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "|=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::OrAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "^=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::XorAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == "<<=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ShlAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (op == ">>=") {
			BinaryOp bin_op{
				.lhs = toTypedValue(lhsIrOperands),
				.rhs = toTypedValue(rhsIrOperands),
				.result = toIrValue(lhsIrOperands[2]),
			};
			ir_.addInstruction(IrInstruction(IrOpcode::ShrAssign, std::move(bin_op), binaryOperatorNode.get_token()));
			return lhsIrOperands;  // Compound assignment returns the LHS
		}
		else if (is_floating_point_op) { // Floating-point operations
			// Float operations use typed BinaryOp
			if (op == "+" || op == "-" || op == "*" || op == "/") {
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
				return { commonType, get_type_size_bits(commonType), result_var, 0ULL };
			}

			// Float comparison operations use typed BinaryOp
			else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
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

				// Float comparisons return boolean (bool8)
				return { Type::Bool, 8, result_var, 0ULL };
			}
			else {
				// Unsupported floating-point operator
				assert(false && "Unsupported floating-point binary operator");
				return {};
			}
		}
	
		// For comparison operations, return boolean type (8 bits - bool size in C++)
		// For other operations, return the common type
		if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
			return { Type::Bool, 8, result_var, 0ULL };
		} else {
			// Return the result variable with its type and size
			// Note: Assignment is handled earlier and returns before reaching this point
			return { commonType, get_type_size_bits(commonType), result_var, 0ULL };
		}
	}

	// Helper function to generate Microsoft Visual C++ mangled name for function calls
	// Delegates to NameMangling::generateMangledName to keep all mangling logic in one place
	std::string_view generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<TypeSpecifierNode>& param_types, bool is_variadic = false, std::string_view struct_name = "", const std::vector<std::string>& namespace_path = {}) {
		return NameMangling::generateMangledName(name, return_type, param_types, is_variadic, struct_name, namespace_path).view();
	}

	// Overload that accepts parameter nodes directly to avoid creating a temporary vector
	std::string_view generateMangledNameForCall(std::string_view name, const TypeSpecifierNode& return_type, const std::vector<ASTNode>& param_nodes, bool is_variadic = false, std::string_view struct_name = "", const std::vector<std::string>& namespace_path = {}) {
		return NameMangling::generateMangledName(name, return_type, param_nodes, is_variadic, struct_name, namespace_path).view();
	}

	// Overload that accepts a FunctionDeclarationNode directly
	// This extracts the function name, return type, parameters, and other info from the node
	// If struct_name_override is provided, it takes precedence over node.parent_struct_name()
	std::string_view generateMangledNameForCall(const FunctionDeclarationNode& func_node, std::string_view struct_name_override = "", const std::vector<std::string>& namespace_path = {}) {
		const DeclarationNode& decl_node = func_node.decl_node();
		const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
		std::string_view func_name = decl_node.identifier_token().value();
		
		std::string_view struct_name = !struct_name_override.empty() ? struct_name_override
			: (func_node.is_member_function() ? func_node.parent_struct_name() : std::string_view{});
		
		// Pass linkage from the function node to ensure extern "C" functions aren't mangled
		return NameMangling::generateMangledName(func_name, return_type, func_node.parameter_nodes(),
			func_node.is_variadic(), struct_name, namespace_path, func_node.linkage()).view();
	}
	
	// Helper function to handle compiler intrinsics
	// Returns true if the function is an intrinsic and has been handled, false otherwise
	std::optional<std::vector<IrOperand>> tryGenerateIntrinsicIr(std::string_view func_name, const FunctionCallNode& functionCallNode) {
		// Lookup table for intrinsic handlers using if-else chain
		// More maintainable than multiple nested if statements
		
		// Variadic argument intrinsics
		if (func_name == "__builtin_va_start" || func_name == "__va_start") {
			return generateVaStartIntrinsic(functionCallNode);
		}
		if (func_name == "__builtin_va_arg") {
			return generateVaArgIntrinsic(functionCallNode);
		}
		
		// Integer abs intrinsics
		if (func_name == "__builtin_labs" || func_name == "__builtin_llabs") {
			return generateBuiltinAbsIntIntrinsic(functionCallNode);
		}
		
		// Floating point abs intrinsics
		if (func_name == "__builtin_fabs" || func_name == "__builtin_fabsf" || func_name == "__builtin_fabsl") {
			return generateBuiltinAbsFloatIntrinsic(functionCallNode, func_name);
		}
		
		// Optimization hint intrinsics
		if (func_name == "__builtin_unreachable") {
			return generateBuiltinUnreachableIntrinsic(functionCallNode);
		}
		if (func_name == "__builtin_assume") {
			return generateBuiltinAssumeIntrinsic(functionCallNode);
		}
		if (func_name == "__builtin_expect") {
			return generateBuiltinExpectIntrinsic(functionCallNode);
		}
		if (func_name == "__builtin_launder") {
			return generateBuiltinLaunderIntrinsic(functionCallNode);
		}
		
		return std::nullopt;  // Not an intrinsic
	}
	
	// Generate inline IR for __builtin_labs / __builtin_llabs
	// Uses branchless abs: abs(x) = (x XOR sign_mask) - sign_mask where sign_mask = x >> 63
	std::vector<IrOperand> generateBuiltinAbsIntIntrinsic(const FunctionCallNode& functionCallNode) {
		if (functionCallNode.arguments().size() != 1) {
			FLASH_LOG(Codegen, Error, "__builtin_labs/__builtin_llabs requires exactly 1 argument");
			return {Type::Long, 64, 0ULL, 0ULL};
		}
		
		// Get the argument
		ASTNode arg = functionCallNode.arguments()[0];
		auto arg_ir = visitExpressionNode(arg.as<ExpressionNode>());
		
		// Extract argument details
		Type arg_type = std::get<Type>(arg_ir[0]);
		int arg_size = std::get<int>(arg_ir[1]);
		TypedValue arg_value = toTypedValue(arg_ir);
		
		// Step 1: Arithmetic shift right by 63 to get sign mask (all 1s if negative, all 0s if positive)
		TempVar sign_mask = var_counter.next();
		BinaryOp shift_op{
			.lhs = arg_value,
			.rhs = TypedValue{Type::Int, 32, 63ULL},
			.result = sign_mask
		};
		ir_.addInstruction(IrInstruction(IrOpcode::ShiftRight, std::move(shift_op), functionCallNode.called_from()));
		
		// Step 2: XOR with sign mask
		TempVar xor_result = var_counter.next();
		BinaryOp xor_op{
			.lhs = arg_value,
			.rhs = TypedValue{arg_type, arg_size, sign_mask},
			.result = xor_result
		};
		ir_.addInstruction(IrInstruction(IrOpcode::BitwiseXor, std::move(xor_op), functionCallNode.called_from()));
		
		// Step 3: Subtract sign mask
		TempVar abs_result = var_counter.next();
		BinaryOp sub_op{
			.lhs = TypedValue{arg_type, arg_size, xor_result},
			.rhs = TypedValue{arg_type, arg_size, sign_mask},
			.result = abs_result
		};
		ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(sub_op), functionCallNode.called_from()));
		
		return {arg_type, arg_size, abs_result, 0ULL};
	}
	
	// Generate inline IR for __builtin_fabs / __builtin_fabsf / __builtin_fabsl
	// Uses bitwise AND to clear the sign bit
	std::vector<IrOperand> generateBuiltinAbsFloatIntrinsic(const FunctionCallNode& functionCallNode, std::string_view func_name) {
		if (functionCallNode.arguments().size() != 1) {
			FLASH_LOG(Codegen, Error, func_name, " requires exactly 1 argument");
			return {Type::Double, 64, 0ULL, 0ULL};
		}
		
		// Get the argument
		ASTNode arg = functionCallNode.arguments()[0];
		auto arg_ir = visitExpressionNode(arg.as<ExpressionNode>());
		
		// Extract argument details
		Type arg_type = std::get<Type>(arg_ir[0]);
		int arg_size = std::get<int>(arg_ir[1]);
		TypedValue arg_value = toTypedValue(arg_ir);
		
		// For floating point abs, clear the sign bit using bitwise AND
		// Float (32-bit): AND with 0x7FFFFFFF
		// Double (64-bit): AND with 0x7FFFFFFFFFFFFFFF
		unsigned long long mask = (arg_size == 32) ? 0x7FFFFFFFULL : 0x7FFFFFFFFFFFFFFFULL;
		
		TempVar abs_result = var_counter.next();
		BinaryOp and_op{
			.lhs = arg_value,
			.rhs = TypedValue{Type::UnsignedLongLong, arg_size, mask},
			.result = abs_result
		};
		ir_.addInstruction(IrInstruction(IrOpcode::BitwiseAnd, std::move(and_op), functionCallNode.called_from()));
		
		return {arg_type, arg_size, abs_result, 0ULL};
	}
	
	// Generate IR for __builtin_va_arg intrinsic
	// __builtin_va_arg(va_list, type) - reads the current value and advances the appropriate offset
	std::vector<IrOperand> generateVaArgIntrinsic(const FunctionCallNode& functionCallNode) {
		// __builtin_va_arg takes 2 arguments: va_list variable and type
		// After preprocessing: __builtin_va_arg(args, int) - parser sees this as function call with 2 args
		if (functionCallNode.arguments().size() != 2) {
			FLASH_LOG(Codegen, Error, "__builtin_va_arg requires exactly 2 arguments (va_list and type)");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Get the first argument (va_list variable)
		ASTNode arg0 = functionCallNode.arguments()[0];
		auto va_list_ir = visitExpressionNode(arg0.as<ExpressionNode>());
		
		// Get the second argument (type identifier or type specifier) 
		ASTNode arg1 = functionCallNode.arguments()[1];
		
		// Extract type information from the second argument
		Type requested_type = Type::Int;
		int requested_size = 32;
		bool is_float_type = false;
		
		// The second argument can be either an IdentifierNode (from old macro) or TypeSpecifierNode (from new parser)
		// TypeSpecifierNode is stored directly in ASTNode, not in ExpressionNode
		if (arg1.is<TypeSpecifierNode>()) {
			// New parser path: TypeSpecifierNode passed directly
			const auto& type_spec = arg1.as<TypeSpecifierNode>();
			requested_type = type_spec.type();
			requested_size = static_cast<int>(type_spec.size_in_bits());
			is_float_type = (requested_type == Type::Float || requested_type == Type::Double);
		} else if (arg1.is<ExpressionNode>() && std::holds_alternative<IdentifierNode>(arg1.as<ExpressionNode>())) {
			// Old path: IdentifierNode with type name
			std::string_view type_name = std::get<IdentifierNode>(arg1.as<ExpressionNode>()).name();
			
			// Map type names to Type enum
			if (type_name == "int") {
				requested_type = Type::Int;
				requested_size = 32;
			} else if (type_name == "double") {
				requested_type = Type::Double;
				requested_size = 64;
				is_float_type = true;
			} else if (type_name == "float") {
				requested_type = Type::Float;
				requested_size = 32;
				is_float_type = true;
			} else if (type_name == "long") {
				requested_type = Type::Long;
				requested_size = 64;
			} else if (type_name == "char") {
				requested_type = Type::Char;
				requested_size = 8;
			} else {
				// Default to int
				requested_type = Type::Int;
				requested_size = 32;
			}
		}
		
		// va_list_ir[2] contains the variable/temp identifier
		std::variant<StringHandle, TempVar> va_list_var;
		if (std::holds_alternative<TempVar>(va_list_ir[2])) {
			va_list_var = std::get<TempVar>(va_list_ir[2]);
		} else if (std::holds_alternative<StringHandle>(va_list_ir[2])) {
			va_list_var = std::get<StringHandle>(va_list_ir[2]);
		} else {
			FLASH_LOG(Codegen, Error, "__builtin_va_arg first argument must be a variable");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		if (context_->isItaniumMangling()) {
			// Linux/System V AMD64 ABI: Use va_list structure
			// va_list points to a structure with:
			//   unsigned int gp_offset;      (offset 0)
			//   unsigned int fp_offset;      (offset 4)
			//   void *overflow_arg_area;     (offset 8)
			//   void *reg_save_area;         (offset 16)
			
			// The va_list variable is a char* that points to the va_list structure.
			// We need to load this pointer value into a TempVar.
			TempVar va_list_struct_ptr;
			if (std::holds_alternative<TempVar>(va_list_var)) {
				// va_list is already a TempVar - use it directly
				va_list_struct_ptr = std::get<TempVar>(va_list_var);
			} else {
				// va_list is a variable name - load its value (which is a pointer) into a TempVar
				va_list_struct_ptr = var_counter.next();
				StringHandle var_name_handle = std::get<StringHandle>(va_list_var);
				
				// Use Assignment to load the pointer value from the variable
				AssignmentOp load_pointer;
				load_pointer.result = va_list_struct_ptr;
				load_pointer.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				load_pointer.rhs = TypedValue{Type::UnsignedLongLong, 64, var_name_handle};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_pointer), functionCallNode.called_from()));
			}
			
			// Step 2: Compute address of the appropriate offset field (gp_offset for ints, fp_offset for floats)
			// Step 3: Load current offset value (32-bit unsigned) from the offset field
			TempVar current_offset = var_counter.next();
			DereferenceOp load_offset;
			load_offset.result = current_offset;
			load_offset.pointer.type = Type::Int;  // Reading a 32-bit offset
			load_offset.pointer.size_in_bits = 64;  // Pointer is always 64 bits
			load_offset.pointer.pointer_depth = 1;
			
			if (is_float_type) {
				// fp_offset is at offset 4 - compute va_list_struct_ptr + 4
				TempVar fp_offset_addr = var_counter.next();
				BinaryOp fp_offset_calc;
				fp_offset_calc.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				fp_offset_calc.rhs = TypedValue{Type::UnsignedLongLong, 64, 4ULL};
				fp_offset_calc.result = fp_offset_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_offset_calc), functionCallNode.called_from()));
				
				// Materialize the address before using it
				TempVar materialized_fp_addr = var_counter.next();
				AssignmentOp materialize;
				materialize.result = materialized_fp_addr;
				materialize.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_fp_addr};
				materialize.rhs = TypedValue{Type::UnsignedLongLong, 64, fp_offset_addr};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize), functionCallNode.called_from()));
				
				// Read 32-bit fp_offset value from [va_list_struct + 4]
				load_offset.pointer.value = materialized_fp_addr;
			} else {
				// gp_offset is at offset 0 - read directly from va_list_struct_ptr
				// Read 32-bit gp_offset value from [va_list_struct + 0]
				load_offset.pointer.value = va_list_struct_ptr;
			}
			
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_offset), functionCallNode.called_from()));
			
			// Step 4: Load reg_save_area pointer (at offset 16)
			TempVar reg_save_area_field_addr = var_counter.next();
			BinaryOp reg_save_addr;
			reg_save_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
			reg_save_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, 16ULL};
			reg_save_addr.result = reg_save_area_field_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(reg_save_addr), functionCallNode.called_from()));
			
			// Materialize the address before using it
			TempVar materialized_reg_save_addr = var_counter.next();
			AssignmentOp materialize_reg;
			materialize_reg.result = materialized_reg_save_addr;
			materialize_reg.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_reg_save_addr};
			materialize_reg.rhs = TypedValue{Type::UnsignedLongLong, 64, reg_save_area_field_addr};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_reg), functionCallNode.called_from()));
			
			TempVar reg_save_area_ptr = var_counter.next();
			DereferenceOp load_reg_save_ptr;
			load_reg_save_ptr.result = reg_save_area_ptr;
			load_reg_save_ptr.pointer.type = Type::UnsignedLongLong;
			load_reg_save_ptr.pointer.size_in_bits = 64;  // Pointer is always 64 bits
			load_reg_save_ptr.pointer.pointer_depth = 1;  // TODO: Verify pointer depth
			load_reg_save_ptr.pointer.value = materialized_reg_save_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(load_reg_save_ptr), functionCallNode.called_from()));
			
			// Step 5: Compute address: reg_save_area + current_offset
			TempVar arg_addr = var_counter.next();
			BinaryOp compute_addr;
			compute_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, reg_save_area_ptr};
			// Need to convert offset from uint32 to uint64 for addition
			TempVar offset_64 = var_counter.next();
			AssignmentOp convert_offset;
			convert_offset.result = offset_64;
			convert_offset.lhs = TypedValue{Type::UnsignedLongLong, 64, offset_64};
			convert_offset.rhs = TypedValue{Type::UnsignedInt, 32, current_offset};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(convert_offset), functionCallNode.called_from()));
			
			compute_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, offset_64};
			compute_addr.result = arg_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(compute_addr), functionCallNode.called_from()));
			
			// Step 6: Read the value at arg_addr
			TempVar value = var_counter.next();
			DereferenceOp read_value;
			read_value.result = value;
			read_value.pointer.type = requested_type;
			read_value.pointer.size_in_bits = 64;  // Pointer is always 64 bits
			read_value.pointer.pointer_depth = 1;  // TODO: Verify pointer depth
			read_value.pointer.value = arg_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(read_value), functionCallNode.called_from()));
			
			// Step 7: Increment the offset by 8 bytes (or 16 for floats in XMM regs) and store back
			// Instead of creating a separate TempVar for new_offset, compute and store directly
			
			// First, compute new_offset = current_offset + increment
			TempVar new_offset = var_counter.next();
			BinaryOp increment_offset;
			increment_offset.lhs = TypedValue{Type::UnsignedInt, 32, current_offset};
			increment_offset.rhs = TypedValue{Type::UnsignedInt, 32, is_float_type ? 16ULL : 8ULL};
			increment_offset.result = new_offset;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(increment_offset), functionCallNode.called_from()));
			
			// Step 8: Store updated offset back to the appropriate field in the structure
			// Use Assignment to ensure new_offset is materialized before DereferenceStore reads it
			TempVar materialized_offset = var_counter.next();
			AssignmentOp materialize;
			materialize.result = materialized_offset;
			materialize.lhs = TypedValue{Type::UnsignedInt, 32, materialized_offset};
			materialize.rhs = TypedValue{Type::UnsignedInt, 32, new_offset};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize), functionCallNode.called_from()));
			
			DereferenceStoreOp store_offset;
			store_offset.pointer.type = Type::UnsignedInt;
			store_offset.pointer.size_in_bits = 64;  // Pointer is always 64 bits
			store_offset.pointer.pointer_depth = 1;
			if (is_float_type) {
				// Store to fp_offset field at offset 4
				TempVar fp_offset_store_addr = var_counter.next();
				BinaryOp fp_store_addr_calc;
				fp_store_addr_calc.lhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_ptr};
				fp_store_addr_calc.rhs = TypedValue{Type::UnsignedLongLong, 64, 4ULL};
				fp_store_addr_calc.result = fp_offset_store_addr;
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(fp_store_addr_calc), functionCallNode.called_from()));
				
				// Materialize the address before using it in DereferenceStore
				TempVar materialized_addr = var_counter.next();
				AssignmentOp materialize_addr;
				materialize_addr.result = materialized_addr;
				materialize_addr.lhs = TypedValue{Type::UnsignedLongLong, 64, materialized_addr};
				materialize_addr.rhs = TypedValue{Type::UnsignedLongLong, 64, fp_offset_store_addr};
				ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(materialize_addr), functionCallNode.called_from()));
				
				store_offset.pointer.value = materialized_addr;
			} else {
				// Store to gp_offset field at offset 0
				store_offset.pointer.value = va_list_struct_ptr;
			}
			store_offset.value = TypedValue{Type::UnsignedInt, 32, materialized_offset};
			ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_offset), functionCallNode.called_from()));
			
			return {requested_type, requested_size, value};
			
		} else {
			// Windows/MSVC ABI: Simple pointer-based approach
			// va_list is a char*, advance by 8 bytes
			
			// Step 1: Dereference va_list to get the current pointer value
			TempVar current_ptr = var_counter.next();
			DereferenceOp deref_ptr_op;
			deref_ptr_op.result = current_ptr;
			deref_ptr_op.pointer.type = Type::UnsignedLongLong;
			deref_ptr_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
			deref_ptr_op.pointer.pointer_depth = 1;  // TODO: Verify pointer depth
			// Convert std::variant<StringHandle, TempVar> to IrValue
			if (std::holds_alternative<StringHandle>(va_list_var)) {
				deref_ptr_op.pointer.value = std::get<StringHandle>(va_list_var);
			} else {
				deref_ptr_op.pointer.value = std::get<TempVar>(va_list_var);
			}
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_ptr_op), functionCallNode.called_from()));
			
			// Step 2: Read the value at the current pointer
			TempVar value = var_counter.next();
			DereferenceOp deref_value_op;
			deref_value_op.result = value;
			deref_value_op.pointer.type = requested_type;
			deref_value_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
			deref_value_op.pointer.pointer_depth = 1;  // TODO: Verify pointer depth
			deref_value_op.pointer.value = current_ptr;
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_value_op), functionCallNode.called_from()));
			
			// Step 3: Advance va_list by 8 bytes
			TempVar next_ptr = var_counter.next();
			BinaryOp add_op;
			add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, current_ptr};
			add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, 8ULL};
			add_op.result = next_ptr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), functionCallNode.called_from()));
			
			// Step 4: Store the updated pointer back to va_list
			AssignmentOp assign_op;
			assign_op.result = var_counter.next();  // unused but required
			if (std::holds_alternative<TempVar>(va_list_var)) {
				assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<TempVar>(va_list_var)};
			} else {
				assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
			}
			assign_op.rhs = TypedValue{Type::UnsignedLongLong, 64, next_ptr};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), functionCallNode.called_from()));
			
			return {requested_type, requested_size, value};
		}
	}
	
	// Generate IR for __builtin_va_start intrinsic
	std::vector<IrOperand> generateVaStartIntrinsic(const FunctionCallNode& functionCallNode) {
		// __builtin_va_start takes 2 arguments: va_list (not pointer!), and last fixed parameter
		if (functionCallNode.arguments().size() != 2) {
			FLASH_LOG(Codegen, Error, "__builtin_va_start requires exactly 2 arguments");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Get the first argument (va_list variable)
		ASTNode arg0 = functionCallNode.arguments()[0];
		auto arg0_ir = visitExpressionNode(arg0.as<ExpressionNode>());
		
		// Get the second argument (last fixed parameter)
		ASTNode arg1 = functionCallNode.arguments()[1];
		auto arg1_ir = visitExpressionNode(arg1.as<ExpressionNode>());
		
		// The second argument should be an identifier (the parameter name)
		std::string_view last_param_name;
		if (std::holds_alternative<IdentifierNode>(arg1.as<ExpressionNode>())) {
			last_param_name = std::get<IdentifierNode>(arg1.as<ExpressionNode>()).name();
		} else {
			FLASH_LOG(Codegen, Error, "__builtin_va_start second argument must be a parameter name");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Platform-specific varargs implementation:
		// - Windows (MSVC mangling): variadic args on stack, use &last_param + 8
		// - Linux (Itanium mangling): variadic args in registers, initialize va_list structure
		
		if (context_->isItaniumMangling()) {
			// Linux/System V AMD64 ABI: Use va_list structure
			// The structure has already been initialized in the function prologue by IRConverter.
			// We just need to assign the address of the va_list structure to the user's va_list variable.
			
			// Get address of the va_list structure
			TempVar va_list_struct_addr = var_counter.next();
			AddressOfOp struct_addr_op;
			struct_addr_op.result = va_list_struct_addr;
			struct_addr_op.operand.type = Type::Char;
			struct_addr_op.operand.size_in_bits = 8;
			struct_addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
			struct_addr_op.operand.value = StringTable::getOrInternStringHandle("__varargs_va_list_struct__"sv);
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(struct_addr_op), functionCallNode.called_from()));
			
			// Finally, assign the address of the va_list structure to the user's va_list variable (char* pointer)
			// Get the va_list variable from arg0_ir[2]
			std::variant<StringHandle, TempVar> va_list_var;
			if (std::holds_alternative<TempVar>(arg0_ir[2])) {
				va_list_var = std::get<TempVar>(arg0_ir[2]);
			} else if (std::holds_alternative<StringHandle>(arg0_ir[2])) {
				va_list_var = std::get<StringHandle>(arg0_ir[2]);
			} else {
				FLASH_LOG(Codegen, Error, "__builtin_va_start first argument must be a variable or temp");
				return {Type::Void, 0, 0ULL, 0ULL};
			}
			
			AssignmentOp final_assign;
			if (std::holds_alternative<StringHandle>(va_list_var)) {
				final_assign.result = std::get<StringHandle>(va_list_var);
				final_assign.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
			} else {
				final_assign.result = std::get<TempVar>(va_list_var);
				final_assign.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<TempVar>(va_list_var)};
			}
			final_assign.rhs = TypedValue{Type::UnsignedLongLong, 64, va_list_struct_addr};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(final_assign), functionCallNode.called_from()));
			
		} else {
			// Windows/MSVC ABI: Compute &last_param + 8
			TempVar last_param_addr = var_counter.next();
			
			// Generate AddressOf IR for the last parameter
			AddressOfOp addr_op;
			addr_op.result = last_param_addr;
			// Get the type of the last parameter from the symbol table
			auto param_symbol = symbol_table.lookup(last_param_name);
			if (!param_symbol.has_value()) {
				FLASH_LOG(Codegen, Error, "Parameter '", last_param_name, "' not found in __builtin_va_start");
				return {Type::Void, 0, 0ULL, 0ULL};
			}
			const DeclarationNode& param_decl = param_symbol->as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
			
			addr_op.operand.type = param_type.type();
			addr_op.operand.size_in_bits = static_cast<int>(param_type.size_in_bits());
			addr_op.operand.pointer_depth = param_type.pointer_depth();
			addr_op.operand.value = StringTable::getOrInternStringHandle(last_param_name);
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), functionCallNode.called_from()));
			
			// Add 8 bytes (64 bits) to get to the next parameter slot
			TempVar va_start_addr = var_counter.next();
			BinaryOp add_op;
			add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, last_param_addr};
			add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, 8ULL};
			add_op.result = va_start_addr;
			ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), functionCallNode.called_from()));
			
			// Assign to va_list variable
			std::variant<StringHandle, TempVar> va_list_var;
			if (std::holds_alternative<TempVar>(arg0_ir[2])) {
				va_list_var = std::get<TempVar>(arg0_ir[2]);
			} else if (std::holds_alternative<StringHandle>(arg0_ir[2])) {
				va_list_var = std::get<StringHandle>(arg0_ir[2]);
			} else {
				FLASH_LOG(Codegen, Error, "__builtin_va_start first argument must be a variable or temp");
				return {Type::Void, 0, 0ULL, 0ULL};
			}
			
			AssignmentOp assign_op;
			if (std::holds_alternative<StringHandle>(va_list_var)) {
				assign_op.result = std::get<StringHandle>(va_list_var);
				assign_op.lhs = TypedValue{Type::UnsignedLongLong, 64, std::get<StringHandle>(va_list_var)};
			} else {
			}
			assign_op.rhs = TypedValue{Type::UnsignedLongLong, 64, va_start_addr};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), functionCallNode.called_from()));
		}
		
		// __builtin_va_start returns void
		return {Type::Void, 0, 0ULL, 0ULL};
	}
	
	// Generate IR for __builtin_unreachable intrinsic
	// This is an optimization hint that tells the compiler a code path is unreachable
	// Standard usage: after switch default: cases, or after functions that don't return
	// Implementation: We generate no actual code - this is purely an optimization hint
	// In a more advanced compiler, this would enable dead code elimination and assumptions
	std::vector<IrOperand> generateBuiltinUnreachableIntrinsic(const FunctionCallNode& functionCallNode) {
		// Verify no arguments (some compilers allow it, we'll be strict)
		if (functionCallNode.arguments().size() != 0) {
			FLASH_LOG(Codegen, Warning, "__builtin_unreachable should not have arguments (ignoring)");
		}
		
		// For now, we just return void and don't generate any IR
		// A more sophisticated implementation could:
		// 1. Mark the current basic block as unreachable for optimization
		// 2. Allow following code to be eliminated as dead code
		// 3. Use this information for branch prediction
		
		FLASH_LOG(Codegen, Debug, "__builtin_unreachable encountered - marking code path as unreachable");
		
		// Return void (this intrinsic doesn't produce a value)
		return {Type::Void, 0, 0ULL, 0ULL};
	}
	
	// Generate IR for __builtin_assume intrinsic
	// This is an optimization hint that tells the compiler to assume a condition is true
	// Syntax: __builtin_assume(condition)
	// Implementation: We evaluate the condition but don't use the result
	// In a more advanced compiler, this would enable optimizations based on the assumption
	std::vector<IrOperand> generateBuiltinAssumeIntrinsic(const FunctionCallNode& functionCallNode) {
		if (functionCallNode.arguments().size() != 1) {
			FLASH_LOG(Codegen, Error, "__builtin_assume requires exactly 1 argument (condition)");
			return {Type::Void, 0, 0ULL, 0ULL};
		}
		
		// Evaluate the condition expression (but we don't use the result)
		// In a real implementation, we'd use this to inform the optimizer
		ASTNode condition = functionCallNode.arguments()[0];
		auto condition_ir = visitExpressionNode(condition.as<ExpressionNode>());
		
		// For now, we just evaluate the expression and ignore it
		// A more sophisticated implementation could:
		// 1. Track assumptions for later optimization passes
		// 2. Use assumptions for constant folding
		// 3. Enable more aggressive optimizations in conditional branches
		
		FLASH_LOG(Codegen, Debug, "__builtin_assume encountered - assumption recorded (not yet used for optimization)");
		
		// Return void (this intrinsic doesn't produce a value)
		return {Type::Void, 0, 0ULL, 0ULL};
	}
	
	// Generate IR for __builtin_expect intrinsic
	// This is a branch prediction hint: __builtin_expect(expr, expected_value)
	// Returns expr, but hints that expr will likely equal expected_value
	// Common usage: if (__builtin_expect(rare_condition, 0)) { /* unlikely path */ }
	std::vector<IrOperand> generateBuiltinExpectIntrinsic(const FunctionCallNode& functionCallNode) {
		if (functionCallNode.arguments().size() != 2) {
			FLASH_LOG(Codegen, Error, "__builtin_expect requires exactly 2 arguments (expr, expected_value)");
			// Return a default value matching typical usage (long type)
			return {Type::LongLong, 64, 0ULL, 0ULL};
		}
		
		// Evaluate the first argument (the expression)
		ASTNode expr = functionCallNode.arguments()[0];
		auto expr_ir = visitExpressionNode(expr.as<ExpressionNode>());
		
		// Evaluate the second argument (the expected value) but don't use it for now
		ASTNode expected = functionCallNode.arguments()[1];
		auto expected_ir = visitExpressionNode(expected.as<ExpressionNode>());
		
		// For now, we just return the expression value unchanged
		// A more sophisticated implementation could:
		// 1. Pass branch prediction hints to the code generator
		// 2. Reorder basic blocks to favor the expected path
		// 3. Use profile-guided optimization data
		
		FLASH_LOG(Codegen, Debug, "__builtin_expect encountered - branch prediction hint recorded (not yet used)");
		
		// Return the first argument (the expression value)
		return expr_ir;
	}
	
	// Generate IR for __builtin_launder intrinsic
	// This prevents the compiler from assuming anything about what a pointer points to
	// Syntax: __builtin_launder(ptr)
	// Essential for implementing std::launder and placement new operations
	// Returns the pointer unchanged, but creates an optimization barrier
	std::vector<IrOperand> generateBuiltinLaunderIntrinsic(const FunctionCallNode& functionCallNode) {
		if (functionCallNode.arguments().size() != 1) {
			FLASH_LOG(Codegen, Error, "__builtin_launder requires exactly 1 argument (pointer)");
			return {Type::UnsignedLongLong, 64, 0ULL, 0ULL};
		}
		
		// Evaluate the pointer argument
		ASTNode ptr_arg = functionCallNode.arguments()[0];
		auto ptr_ir = visitExpressionNode(ptr_arg.as<ExpressionNode>());
		
		// Extract pointer details
		Type ptr_type = std::get<Type>(ptr_ir[0]);
		int ptr_size = std::get<int>(ptr_ir[1]);
		
		// For now, we just return the pointer unchanged
		// In a real implementation, __builtin_launder would:
		// 1. Create an optimization barrier so compiler can't assume anything about pointee
		// 2. Prevent const/restrict/alias analysis from making invalid assumptions
		// 3. Essential after placement new to get a pointer to the new object
		//
		// Example use case:
		//   struct S { const int x; };
		//   alignas(S) char buffer[sizeof(S)];
		//   new (buffer) S{42};  // placement new
		//   S* ptr = std::launder(reinterpret_cast<S*>(buffer));  // safe access
		
		FLASH_LOG(Codegen, Debug, "__builtin_launder encountered - optimization barrier created");
		
		// Return the pointer unchanged (but optimization barrier is implied)
		return ptr_ir;
	}
	
	std::vector<IrOperand> generateFunctionCallIr(const FunctionCallNode& functionCallNode) {
		std::vector<IrOperand> irOperands;

		const auto& decl_node = functionCallNode.function_declaration();
		std::string_view func_name_view = decl_node.identifier_token().value();

		// Check for compiler intrinsics and handle them specially
		auto intrinsic_result = tryGenerateIntrinsicIr(func_name_view, functionCallNode);
		if (intrinsic_result.has_value()) {
			return intrinsic_result.value();
		}

		// Check if this function is marked as inline_always (pure expression template instantiations)
		// These functions should always be inlined and never generate calls
		// Look up the function to check its inline_always flag
		extern SymbolTable gSymbolTable;
		auto all_overloads = gSymbolTable.lookup_all(func_name_view);
		
		for (const auto& overload : all_overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode* overload_func_decl = &overload.as<FunctionDeclarationNode>();
				const DeclarationNode* overload_decl = &overload_func_decl->decl_node();
				
				// Check if this is the matching overload
				if (overload_decl == &decl_node) {
					// Found the matching function - check if it should be inlined
					if (overload_func_decl->is_inline_always() && functionCallNode.arguments().size() == 1) {
						// Check if function returns a reference - if so, we need special handling
						const TypeSpecifierNode& return_type_spec = overload_decl->type_node().as<TypeSpecifierNode>();
						bool returns_reference = return_type_spec.is_reference() || return_type_spec.is_rvalue_reference();
						
						auto arg_node = functionCallNode.arguments()[0];
						if (arg_node.is<ExpressionNode>()) {
							FLASH_LOG(Codegen, Debug, "Inlining pure expression function (inline_always): ", func_name_view);
							
							if (returns_reference) {
								// For functions returning references (like std::move, std::forward),
								// we need to generate an addressof the argument, not just return it
								const ExpressionNode& arg_expr = arg_node.as<ExpressionNode>();
								
								// Check if the argument is an identifier (common case for move(x))
								if (std::holds_alternative<IdentifierNode>(arg_expr)) {
									const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);
									
									// Generate addressof for the identifier
									TempVar result_var = var_counter.next();
									AddressOfOp op;
									op.result = result_var;
									
									// Get type info from the identifier
									StringHandle id_handle = StringTable::getOrInternStringHandle(ident.name());
									std::optional<ASTNode> symbol = symbol_table.lookup(id_handle);
									if (!symbol.has_value() && global_symbol_table_) {
										symbol = global_symbol_table_->lookup(id_handle);
									}
									
									Type operand_type = Type::Int;  // Default
									int operand_size = 32;
									if (symbol.has_value()) {
										if (symbol->is<DeclarationNode>()) {
											const TypeSpecifierNode& type = symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
											operand_type = type.type();
											operand_size = static_cast<int>(type.size_in_bits());
											if (operand_size == 0) operand_size = get_type_size_bits(operand_type);
										} else if (symbol->is<VariableDeclarationNode>()) {
											const TypeSpecifierNode& type = symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
											operand_type = type.type();
											operand_size = static_cast<int>(type.size_in_bits());
											if (operand_size == 0) operand_size = get_type_size_bits(operand_type);
										}
									}
									
									op.operand.type = operand_type;
									op.operand.size_in_bits = operand_size;
									op.operand.pointer_depth = 0;
									op.operand.value = id_handle;
									
									ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, op, Token()));
									
									// Return pointer type (64-bit address) with pointer depth 1
									return { operand_type, 64, result_var, 1ULL };
								}
								// For non-identifier expressions, fall through to generate a regular call
								// (we can't inline complex expressions that need reference semantics)
							} else {
								// Non-reference return - can inline directly by returning argument
								auto arg_ir = visitExpressionNode(arg_node.as<ExpressionNode>());
								return arg_ir;
							}
						}
					}
					break;  // Found the matching function, stop searching
				}
			}
		}

		// Check if this is a function pointer call
		// Look up the identifier in the symbol table to see if it's a function pointer variable
		const std::optional<ASTNode> func_symbol = symbol_table.lookup(func_name_view);
		const DeclarationNode* func_ptr_decl = nullptr;
		
		// Check for DeclarationNode directly
		if (func_symbol.has_value() && func_symbol->is<DeclarationNode>()) {
			func_ptr_decl = &func_symbol->as<DeclarationNode>();
		}
		// Also check for VariableDeclarationNode (from comma-separated declarations)
		else if (func_symbol.has_value() && func_symbol->is<VariableDeclarationNode>()) {
			func_ptr_decl = &func_symbol->as<VariableDeclarationNode>().declaration();
		}
		
		if (func_ptr_decl) {
			const auto& func_type = func_ptr_decl->type_node().as<TypeSpecifierNode>();

			// Check if this is a function pointer or auto type (which could be a callable)
			// auto&& parameters in recursive lambdas need to be treated as callables
			if (func_type.is_function_pointer()) {
				// This is an indirect call through a function pointer
				// Generate IndirectCall IR: [result_var, func_ptr_var, arg1, arg2, ...]
				TempVar ret_var = var_counter.next();
				
				// Mark function return value as prvalue (Option 2: Value Category Tracking)
				setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());
				
				// Generate IR for function arguments
				std::vector<TypedValue> arguments;
				functionCallNode.arguments().visit([&](ASTNode argument) {
					auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
					// Extract type, size, and value from the expression result
					Type arg_type = std::get<Type>(argumentIrOperands[0]);
					int arg_size = std::get<int>(argumentIrOperands[1]);
					IrValue arg_value = std::visit([](auto&& arg) -> IrValue {
						using T = std::decay_t<decltype(arg)>;
						if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, std::string_view> ||
						              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
							return arg;
						} else {
							return 0ULL;
						}
					}, argumentIrOperands[2]);
					arguments.push_back(TypedValue{arg_type, arg_size, arg_value});
				});

				// Add the indirect call instruction
				IndirectCallOp op{
					.result = ret_var,
					.function_pointer = StringTable::getOrInternStringHandle(func_name_view),
					.arguments = std::move(arguments)
				};
				ir_.addInstruction(IrOpcode::IndirectCall, std::move(op), functionCallNode.called_from());

				// Return the result variable with the return type from the function signature
				if (func_type.has_function_signature()) {
					const auto& sig = func_type.function_signature();
					return { sig.return_type, 64, ret_var, 0ULL };  // 64 bits for return value
				} else {
					// For auto types or missing signature, default to int
					return { Type::Int, 32, ret_var, 0ULL };
				}
			}
			
			// Handle auto-typed callable (e.g., recursive lambda pattern: self(self, n-1))
			// When an auto&& parameter is called like a function, it's a callable object
			// We need to generate a member function call to its operator()
			if (func_type.type() == Type::Auto) {
				// This is likely a recursive lambda call pattern where 'self' is a lambda passed as auto&&
				// We need to find the lambda's closure type and call its operator()
				
				// Look up the deduced type for this auto parameter
				// First, check if we're inside a lambda context
				if (current_lambda_context_.isActive()) {
					// We're inside a lambda - this could be a recursive call through an auto&& parameter
					// The pattern is: auto factorial = [](auto&& self, int n) { ... self(self, n-1); }
					
					// Get the current lambda's closure type name to construct the operator() call
					std::string_view closure_type_name = StringTable::getStringView(current_lambda_context_.closure_type);
					
					// Generate a member function call to operator()
					TempVar ret_var = var_counter.next();
					setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());
					
					// Build the call operands
					CallOp call_op;
					call_op.result = ret_var;
					call_op.return_type = Type::Int;  // Default, will be refined
					call_op.return_size_in_bits = 32;
					call_op.is_variadic = false;
					
					// Add the object (self) as the first argument (this pointer)
					call_op.args.push_back(TypedValue{
						.type = Type::Struct,
						.size_in_bits = 64,  // Pointer size
						.value = IrValue(StringTable::getOrInternStringHandle(func_name_view))
					});
					
					// Generate IR for the remaining arguments and collect types for mangling
					std::vector<TypeSpecifierNode> arg_types;
					
					// Look up the closure type to get the proper type_index
					TypeIndex closure_type_index = 0;
					auto it = gTypesByName.find(current_lambda_context_.closure_type);
					if (it != gTypesByName.end()) {
						closure_type_index = it->second->type_index_;
					}
					
					functionCallNode.arguments().visit([&](ASTNode argument) {
						// Check if this argument is the same as the callee (recursive lambda pattern)
						// In that case, we should pass the reference directly without dereferencing
						bool is_self_arg = false;
						const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(arg_expr)) {
							const auto& id = std::get<IdentifierNode>(arg_expr);
							if (id.name() == func_name_view) {
								is_self_arg = true;
							}
						}
						
						if (is_self_arg) {
							// For the self argument in recursive lambda calls, pass the reference directly
							// Don't call visitExpressionNode which would dereference it
							call_op.args.push_back(TypedValue{
								.type = Type::Struct,
								.size_in_bits = 64,  // Reference/pointer size
								.value = IrValue(StringTable::getOrInternStringHandle(func_name_view))
							});
							
							// Type for mangling is rvalue reference to closure type
							TypeSpecifierNode self_type(Type::Struct, closure_type_index, 8, Token());
							self_type.set_reference(true);
							arg_types.push_back(self_type);
						} else {
							// Normal argument - visit the expression
							auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
							Type arg_type = std::get<Type>(argumentIrOperands[0]);
							int arg_size = std::get<int>(argumentIrOperands[1]);
							IrValue arg_value = std::visit([](auto&& arg) -> IrValue {
								using T = std::decay_t<decltype(arg)>;
								if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, std::string_view> ||
								              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
									return arg;
								} else {
									return 0ULL;
								}
							}, argumentIrOperands[2]);
							call_op.args.push_back(TypedValue{arg_type, arg_size, arg_value});
							
							// Type for mangling
							TypeSpecifierNode type_node(arg_type, 0, arg_size, Token());
							arg_types.push_back(type_node);
						}
					});
					
					// Generate mangled name for operator() call
					TypeSpecifierNode return_type_node(Type::Int, 0, 32, Token());
					std::string_view mangled_name = generateMangledNameForCall(
						"operator()",
						return_type_node,
						arg_types,
						false,
						closure_type_name
					);
					call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
					
					ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), functionCallNode.called_from()));
					
					return { Type::Int, 32, ret_var, 0ULL };
				}
			}
		}

		// Get the function declaration to extract parameter types for mangling
		std::string_view function_name = func_name_view;
		const FunctionDeclarationNode* matched_func_decl = nullptr;
		
		// Check if FunctionCallNode has a pre-computed mangled name (for namespace-scoped functions)
		// If so, use it directly and skip the lookup logic
		if (functionCallNode.has_mangled_name()) {
			function_name = functionCallNode.mangled_name();
			FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name from FunctionCallNode: {}", function_name);
			// We don't need to find matched_func_decl since we already have the mangled name
			// The mangled name is sufficient for generating the call instruction
		}

		// Only do symbol table lookup if we don't have a pre-computed mangled name
		if (!functionCallNode.has_mangled_name()) {
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
			FLASH_LOG_FORMAT(Codegen, Debug, "Looking for function: {}, all_overloads size: {}, gSymbolTable_overloads size: {}", 
				func_name_view, all_overloads.size(), gSymbolTable_overloads.size());
			for (const auto& overload : all_overloads) {
				if (overload.is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode* overload_func_decl = &overload.as<FunctionDeclarationNode>();
					const DeclarationNode* overload_decl = &overload_func_decl->decl_node();
					FLASH_LOG_FORMAT(Codegen, Debug, "  Checking overload at {}, looking for {}", 
						(void*)overload_decl, (void*)&decl_node);
					if (overload_decl == &decl_node) {
						// Found the matching overload
						matched_func_decl = overload_func_decl;

						// Use pre-computed mangled name if available, otherwise generate it
						if (matched_func_decl->has_mangled_name()) {
							function_name = matched_func_decl->mangled_name();
							FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name: {}", function_name);
						} else if (matched_func_decl->linkage() != Linkage::C) {
							function_name = generateMangledNameForCall(*matched_func_decl, "", current_namespace_stack_);
							FLASH_LOG_FORMAT(Codegen, Debug, "Generated mangled name (no pre-computed): {}", function_name);
						}
						break;
					}
				}
			}
	
			// Fallback: if pointer comparison failed (e.g., for template instantiations),
			// try to find the function by checking if there's only one overload with this name
			if (!matched_func_decl && all_overloads.size() == 1 && all_overloads[0].is<FunctionDeclarationNode>()) {
				matched_func_decl = &all_overloads[0].as<FunctionDeclarationNode>();
		
				// Use pre-computed mangled name if available, otherwise generate it
				if (matched_func_decl->has_mangled_name()) {
					function_name = matched_func_decl->mangled_name();
					FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name (fallback 1): {}", function_name);
				} else if (matched_func_decl->linkage() != Linkage::C) {
					function_name = generateMangledNameForCall(*matched_func_decl, "", current_namespace_stack_);
				}
			}

			// Additional fallback: check gSymbolTable directly (for member functions added during delayed parsing)
			if (!matched_func_decl && gSymbolTable_overloads.size() == 1 && gSymbolTable_overloads[0].is<FunctionDeclarationNode>()) {
				matched_func_decl = &gSymbolTable_overloads[0].as<FunctionDeclarationNode>();
		
				// Use pre-computed mangled name if available, otherwise generate it
				if (matched_func_decl->has_mangled_name()) {
					function_name = matched_func_decl->mangled_name();
					FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name (fallback 2): {}", function_name);
				} else if (matched_func_decl->linkage() != Linkage::C) {
					function_name = generateMangledNameForCall(*matched_func_decl, "", current_namespace_stack_);
				}
			}

			// Final fallback: if we're in a member function, check the current struct's member functions
			if (!matched_func_decl && current_struct_name_.isValid()) {
				auto type_it = gTypesByName.find(current_struct_name_);
				if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
					const StructTypeInfo* struct_info = type_it->second->getStructInfo();
					if (struct_info) {
						for (const auto& member_func : struct_info->member_functions) {
							if (member_func.function_decl.is<FunctionDeclarationNode>()) {
								const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
								if (func_decl.decl_node().identifier_token().value() == func_name_view) {
									// Found matching member function
									matched_func_decl = &func_decl;
								
									// Use pre-computed mangled name if available, otherwise generate it
									if (matched_func_decl->has_mangled_name()) {
										function_name = matched_func_decl->mangled_name();
									} else if (matched_func_decl->linkage() != Linkage::C) {
										function_name = generateMangledNameForCall(*matched_func_decl, StringTable::getStringView(current_struct_name_));
									}
									break;
								}
							}
						}
					}
				
					// If not found in current struct, check base classes
					if (!matched_func_decl && struct_info) {
						// Search through base classes recursively
						std::function<void(const StructTypeInfo*)> searchBaseClasses = [&](const StructTypeInfo* current_struct) {
							for (const auto& base_spec : current_struct->base_classes) {
								// Look up base class in gTypeInfo
								if (base_spec.type_index < gTypeInfo.size()) {
									const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
									if (base_type_info.isStruct()) {
										const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
										if (base_struct_info) {
											// Check member functions in base class
											for (const auto& member_func : base_struct_info->member_functions) {
												if (member_func.function_decl.is<FunctionDeclarationNode>()) {
													const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
													if (func_decl.decl_node().identifier_token().value() == func_name_view) {
														// Found matching member function in base class
														matched_func_decl = &func_decl;
													
														// Use pre-computed mangled name if available
														if (matched_func_decl->has_mangled_name()) {
															function_name = matched_func_decl->mangled_name();
														} else if (matched_func_decl->linkage() != Linkage::C) {
															// Generate mangled name with base class name
															function_name = generateMangledNameForCall(*matched_func_decl, StringTable::getStringView(base_struct_info->getName()));
														}
														return; // Stop searching once found
													}
												}
											}
											// Recursively search base classes of this base class
											if (!matched_func_decl) {
												searchBaseClasses(base_struct_info);
											}
										}
									}
								}
							}
						};
						searchBaseClasses(struct_info);
					}
				}
			}
		} // End of symbol table lookup (only if no pre-computed mangled name)
	
		// Always add the return variable and function name (mangled for overload resolution)
		FLASH_LOG_FORMAT(Codegen, Debug, "Final function_name for call: '{}'", function_name);
		TempVar ret_var = var_counter.next();
		
		// Mark function return value as prvalue (Option 2: Value Category Tracking)
		// Function returns (by value) produce temporaries with no persistent identity
		setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());
		
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(StringTable::getOrInternStringHandle(function_name));

		// Process arguments - match them with parameter types
		size_t arg_index = 0;
		const auto& func_decl_node = functionCallNode.function_declaration();
		
		// Get parameters from the function declaration
		std::vector<ASTNode> param_nodes;
		if (matched_func_decl) {
			param_nodes = matched_func_decl->parameter_nodes();
		} else {
			// Try to get from the function declaration stored in FunctionCallNode
			// Look up the function in symbol table to get full declaration with parameters
			auto func_symbol = symbol_table.lookup(func_decl_node.identifier_token().value());
			if (!func_symbol.has_value() && global_symbol_table_) {
				func_symbol = global_symbol_table_->lookup(func_decl_node.identifier_token().value());
			}
			if (func_symbol.has_value() && func_symbol->is<FunctionDeclarationNode>()) {
				const auto& func_decl = func_symbol->as<FunctionDeclarationNode>();
				param_nodes = func_decl.parameter_nodes();
			}
		}
		
		functionCallNode.arguments().visit([&](ASTNode argument) {
			// Get the parameter type for this argument (if it exists)
			const TypeSpecifierNode* param_type = nullptr;
			if (arg_index < param_nodes.size() && param_nodes[arg_index].is<DeclarationNode>()) {
				param_type = &param_nodes[arg_index].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			}
			
			// Special case: if argument is a reference identifier being passed to a reference parameter,
			// handle it directly without visiting the expression. This prevents the Load context from
			// generating a Dereference operation (which would give us the value, not the address).
			// For reference-to-reference passing, we just want to pass the variable name directly,
			// and let the IRConverter use MOV to load the address stored in the reference.
			if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference()) &&
			    std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier.name());
				}
				if (symbol.has_value()) {
					const DeclarationNode* decl_ptr = nullptr;
					if (symbol->is<DeclarationNode>()) {
						decl_ptr = &symbol->as<DeclarationNode>();
					} else if (symbol->is<VariableDeclarationNode>()) {
						decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
					}
					if (decl_ptr) {
						const auto& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
						if (type_node.is_reference() || type_node.is_rvalue_reference()) {
							// Argument is a reference variable being passed to a reference parameter
							// Pass the identifier name directly - the IRConverter will use MOV to
							// load the address stored in the reference variable
							irOperands.emplace_back(type_node.type());
							irOperands.emplace_back(64);  // References are stored as 64-bit pointers
							irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
							arg_index++;
							return;  // Skip the rest of the processing
						}
					}
				}
			}
			
			// Determine expression context for the argument
			// Default to Load context, which reads values
			ExpressionContext arg_context = ExpressionContext::Load;
			
			// If the parameter expects a reference, use LValueAddress context to avoid dereferencing
			// This is needed for non-reference arguments being passed to reference parameters
			if (param_type && param_type->is_reference()) {
				arg_context = ExpressionContext::LValueAddress;
			}
			
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>(), arg_context);
			arg_index++;
			
			// Check if we need to call a conversion operator for this argument
			// This handles cases like: func(myStruct) where func expects int and myStruct has operator int()
			if (param_type && argumentIrOperands.size() >= 3) {
				Type arg_type = std::get<Type>(argumentIrOperands[0]);
				int arg_size = std::get<int>(argumentIrOperands[1]);
				Type param_base_type = param_type->type();
				
				// Check if argument is struct type and parameter expects different type
				if (arg_type == Type::Struct && arg_type != param_base_type && param_type->pointer_depth() == 0) {
					TypeIndex arg_type_index = 0;
					if (argumentIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(argumentIrOperands[3])) {
						arg_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(argumentIrOperands[3]));
					}
					
					if (arg_type_index > 0 && arg_type_index < gTypeInfo.size()) {
						const TypeInfo& source_type_info = gTypeInfo[arg_type_index];
						const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
						
						// Look for a conversion operator to the parameter type
						const StructMemberFunction* conv_op = findConversionOperator(
							source_struct_info, param_base_type, param_type->type_index());
						
						if (conv_op) {
							FLASH_LOG(Codegen, Debug, "Found conversion operator for function argument from ",
								StringTable::getStringView(source_type_info.name()),
								" to parameter type");
							
							// Generate call to the conversion operator
							TempVar result_var = var_counter.next();
							
							// Get the source value
							IrValue source_value = std::visit([](auto&& arg) -> IrValue {
								using T = std::decay_t<decltype(arg)>;
								if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
								              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
									return arg;
								} else {
									return 0ULL;
								}
							}, argumentIrOperands[2]);
							
							// Generate the call to conversion operator
							if (conv_op->function_decl.is<FunctionDeclarationNode>()) {
								const auto& func_decl = conv_op->function_decl.as<FunctionDeclarationNode>();
								std::string_view mangled_name;
								if (func_decl.has_mangled_name()) {
									mangled_name = func_decl.mangled_name();
								} else {
									StringHandle struct_name_handle = source_type_info.name();
									std::string_view struct_name = StringTable::getStringView(struct_name_handle);
									// Use the function's parent struct name, not the source type name,
									// because the conversion operator may be inherited from a base class
									// and we need to call the version defined in the base class.
									std::string_view operator_struct_name = func_decl.parent_struct_name();
									if (operator_struct_name.empty()) {
										operator_struct_name = struct_name;
									}
									mangled_name = generateMangledNameForCall(func_decl, operator_struct_name);
								}
								
								CallOp call_op;
								call_op.result = result_var;
								call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
								call_op.return_type = param_base_type;
								call_op.return_size_in_bits = param_type->pointer_depth() > 0 ? 64 : static_cast<int>(param_type->size_in_bits());
								call_op.return_type_index = param_type->type_index();
								call_op.is_member_function = true;
								call_op.is_variadic = false;
								
								// For member function calls, first argument is 'this' pointer
								if (std::holds_alternative<StringHandle>(source_value)) {
									// It's a variable - take its address
									TempVar this_ptr = var_counter.next();
									AddressOfOp addr_op;
									addr_op.result = this_ptr;
									addr_op.operand.type = arg_type;
									addr_op.operand.size_in_bits = arg_size;
									addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
									addr_op.operand.value = std::get<StringHandle>(source_value);
									ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
									
									// Add 'this' as first argument
									TypedValue this_arg;
									this_arg.type = arg_type;
									this_arg.size_in_bits = 64;  // Pointer size
									this_arg.value = this_ptr;
									this_arg.type_index = arg_type_index;
									call_op.args.push_back(std::move(this_arg));
								} else if (std::holds_alternative<TempVar>(source_value)) {
									// It's already a temporary
									TypedValue this_arg;
									this_arg.type = arg_type;
									this_arg.size_in_bits = 64;  // Pointer size for 'this'
									this_arg.value = std::get<TempVar>(source_value);
									this_arg.type_index = arg_type_index;
									call_op.args.push_back(std::move(this_arg));
								}
								
								ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), Token()));
								
								// Replace argumentIrOperands with the result of the conversion
								argumentIrOperands.clear();
								argumentIrOperands.emplace_back(param_base_type);
								argumentIrOperands.emplace_back(param_type->pointer_depth() > 0 ? 64 : static_cast<int>(param_type->size_in_bits()));
								argumentIrOperands.emplace_back(result_var);
							}
						}
					}
				}
			}
			
			// Check if visitExpressionNode returned a TempVar - this means the value was computed
			// (e.g., global load, expression result, etc.) and we should use the TempVar directly
			bool use_computed_result = (argumentIrOperands.size() >= 3 && 
			                            std::holds_alternative<TempVar>(argumentIrOperands[2]));
			
			// For identifiers that returned local variable references (string_view), handle specially
			if (!use_computed_result && std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier.name());
				}
				if (!symbol.has_value()) {
					FLASH_LOG(Codegen, Error, "Symbol '", identifier.name(), "' not found for function argument");
					FLASH_LOG(Codegen, Error, "  Current function: ", current_function_name_);
					throw std::runtime_error("Missing symbol for function argument");
				}

				const DeclarationNode* decl_ptr = nullptr;
				if (symbol->is<DeclarationNode>()) {
					decl_ptr = &symbol->as<DeclarationNode>();
				} else if (symbol->is<VariableDeclarationNode>()) {
					decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
				}

				if (!decl_ptr) {
					FLASH_LOG(Codegen, Error, "Function argument '", identifier.name(), "' is not a DeclarationNode");
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
					addr_op.operand.type = type_node.type();
					addr_op.operand.size_in_bits = static_cast<int>(type_node.size_in_bits());
					addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
					addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
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
						// References are stored as pointers (64 bits), not the pointee size
						irOperands.emplace_back(type_node.type());
						irOperands.emplace_back(64);  // Pointer size, not pointee size
						irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
					} else {
						// Argument is a value - take its address
						TempVar addr_var = var_counter.next();

						AddressOfOp addr_op;
						addr_op.result = addr_var;
						addr_op.operand.type = type_node.type();
						addr_op.operand.size_in_bits = static_cast<int>(type_node.size_in_bits());
						addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
						addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
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
					deref_op.pointer.type = type_node.type();
					deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
					deref_op.pointer.pointer_depth = 1;  // TODO: Verify pointer depth
					deref_op.pointer.value = StringTable::getOrInternStringHandle(identifier.name());
					ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));
					
					// Pass the dereferenced value
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
					irOperands.emplace_back(deref_var);
				} else {
					// Regular variable - pass by value
					// For pointer types, size is always 64 bits regardless of pointee type
					int arg_size = (type_node.pointer_depth() > 0) ? 64 : static_cast<int>(type_node.size_in_bits());
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(arg_size);
					irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
				}
			} else {
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
						addr_op.operand.type = literal_type;
						addr_op.operand.size_in_bits = literal_size;
						addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
						addr_op.operand.value = temp_var;
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
						
						// Pass the address
						irOperands.emplace_back(literal_type);
						irOperands.emplace_back(64);  // Pointer size
						irOperands.emplace_back(addr_var);
					} else {
						// Not a literal (expression result in a TempVar) - check if it needs address taken
						if (argumentIrOperands.size() >= 3 && std::holds_alternative<TempVar>(argumentIrOperands[2])) {
							Type expr_type = std::get<Type>(argumentIrOperands[0]);
							int expr_size = std::get<int>(argumentIrOperands[1]);
							TempVar expr_var = std::get<TempVar>(argumentIrOperands[2]);
							
							// If expr_size is 64 bits and it's a struct type, it's likely already an address
							// (e.g., from a cast to rvalue reference like static_cast<Widget&&>(w1))
							// In this case, just pass it through without taking another address
							bool is_already_address = (expr_size == 64 && expr_type == Type::Struct);
							
							if (is_already_address) {
								// Already an address - pass through
								irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
							} else {
								// Need to take address of the value
								TempVar addr_var = var_counter.next();
								AddressOfOp addr_op;
								addr_op.result = addr_var;
								addr_op.operand.type = expr_type;
								addr_op.operand.size_in_bits = expr_size;
								// pointer_depth is 0 because we're taking the address of a value (not a pointer)
								// The TempVar holds a direct value (e.g., constructed object), not a pointer
								addr_op.operand.pointer_depth = 0;
								addr_op.operand.value = expr_var;
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
								
								irOperands.emplace_back(expr_type);
								irOperands.emplace_back(64);  // Pointer size
								irOperands.emplace_back(addr_var);
							}
						} else {
							// Fallback - just pass through
							irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
						}
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
		call_op.function_name = StringTable::getOrInternStringHandle(function_name);
		
		// Get return type information
		const auto& return_type = decl_node.type_node().as<TypeSpecifierNode>();
		call_op.return_type = return_type.type();
		// For pointers and references, use 64-bit size (pointer size on x64)
		// References are represented as addresses at the IR level
		call_op.return_size_in_bits = (return_type.pointer_depth() > 0 || return_type.is_reference()) 
			? 64 
			: static_cast<int>(return_type.size_in_bits());
		call_op.return_type_index = return_type.type_index();
		call_op.is_member_function = false;
		
		// Detect if calling a function that returns struct by value (needs hidden return parameter for RVO)
		bool returns_struct_by_value = (return_type.type() == Type::Struct && return_type.pointer_depth() == 0);
		if (returns_struct_by_value) {
			call_op.uses_return_slot = true;
			call_op.return_slot = ret_var;  // The result temp var serves as the return slot
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Function call {} returns struct by value - using return slot (temp_{})",
				function_name, ret_var.var_number);
		}
		
		// Set is_variadic based on function declaration (if available)
		if (matched_func_decl) {
			call_op.is_variadic = matched_func_decl->is_variadic();
		}
		
		// Convert operands to TypedValue arguments (skip first 2: result and function_name)
		// Operands come in groups of 3 (type, size, value) or 4 (type, size, value, type_index)
		// toTypedValue handles both cases
		size_t arg_idx = 0;
		for (size_t i = 2; i < irOperands.size(); ) {
			// Peek ahead to determine operand group size
			// If there are at least 4 more operands and the 4th is an integer, assume it's type_index
			size_t group_size = 3;
			if (i + 3 < irOperands.size() && std::holds_alternative<unsigned long long>(irOperands[i + 3])) {
				// Check if this looks like a type_index by seeing if i+4 would be start of next group
				// or end of operands
				bool next_is_type = (i + 4 >= irOperands.size() || std::holds_alternative<Type>(irOperands[i + 4]));
				if (next_is_type) {
					group_size = 4;
				}
			}
			
			TypedValue arg = toTypedValue(std::span<const IrOperand>(&irOperands[i], group_size));
			
			// Check if this parameter is a reference type
			if (matched_func_decl && arg_idx < param_nodes.size() && param_nodes[arg_idx].is<DeclarationNode>()) {
				const TypeSpecifierNode& param_type = param_nodes[arg_idx].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
				if (param_type.is_reference() || param_type.is_rvalue_reference()) {
					arg.is_reference = true;
				}
			}
			
			call_op.args.push_back(arg);
			i += group_size;
			arg_idx++;
		}

		// Add the function call instruction with typed payload
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), functionCallNode.called_from()));

		// Return the result variable with its type and size
		// For references, return 64-bit size (address size)
		int result_size = (return_type.pointer_depth() > 0 || return_type.is_reference())
			? 64
			: static_cast<int>(return_type.size_in_bits());
		// Return type_index for struct types so structured bindings can decompose the result
		unsigned long long type_index_result = (return_type.type() == Type::Struct) 
			? static_cast<unsigned long long>(return_type.type_index())
			: 0ULL;
		return { return_type.type(), result_size, ret_var, type_index_result };
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

			// CRITICAL: First, collect the lambda for generation!
			// This ensures operator() and __invoke functions will be generated.
			// Without this, the lambda is never added to collected_lambdas_ and
			// its functions are never generated, causing linker errors.
			generateLambdaExpressionIr(lambda);
			
			// Check if this is a generic lambda (has auto parameters)
			bool is_generic = false;
			std::vector<size_t> auto_param_indices;
			size_t param_idx = 0;
			for (const auto& param_node : lambda.parameters()) {
				if (param_node.is<DeclarationNode>()) {
					const auto& param_decl = param_node.as<DeclarationNode>();
					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					if (param_type.type() == Type::Auto) {
						is_generic = true;
						auto_param_indices.push_back(param_idx);
					}
				}
				param_idx++;
			}

			// For non-capturing lambdas, we can optimize by calling __invoke directly
			// (a static function that doesn't need a 'this' pointer).
			// For capturing lambdas, we must call operator() with the closure object.
			if (lambda.captures().empty()) {
				// Non-capturing lambda: call __invoke directly
				StringHandle closure_type_name = lambda.generate_lambda_name();
				StringHandle invoke_name = StringTable::getOrInternStringHandle(StringBuilder().append(closure_type_name).append("_invoke"sv));

				// Generate a direct function call to __invoke
				TempVar ret_var = var_counter.next();

				// Create CallOp structure (matching the pattern in generateFunctionCallIr)
				CallOp call_op;
				call_op.result = ret_var;
				
				// Build TypeSpecifierNode for return type (needed for mangling)
				TypeSpecifierNode return_type_node(Type::Int, 0, 32, memberFunctionCallNode.called_from());
				if (lambda.return_type().has_value()) {
					const auto& ret_type = lambda.return_type()->as<TypeSpecifierNode>();
					return_type_node = ret_type;
					call_op.return_type = ret_type.type();
					call_op.return_size_in_bits = static_cast<int>(ret_type.size_in_bits());
				} else {
					// Default to int if no explicit return type
					call_op.return_type = Type::Int;
					call_op.return_size_in_bits = 32;
				}
				
				// Build TypeSpecifierNodes for parameters (needed for mangling)
				// For generic lambdas, we need to deduce auto parameters from arguments
				std::vector<TypeSpecifierNode> param_types;
				std::vector<TypeSpecifierNode> deduced_param_types;  // For generic lambdas
				
				if (is_generic) {
					// First, collect argument types
					std::vector<TypeSpecifierNode> arg_types;
					memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
						const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(arg_expr)) {
							const auto& identifier = std::get<IdentifierNode>(arg_expr);
							const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
							if (symbol.has_value()) {
								const DeclarationNode* decl = get_decl_from_symbol(*symbol);
								if (decl) {
									TypeSpecifierNode type_node = decl->type_node().as<TypeSpecifierNode>();
									// Resolve auto type from lambda initializer if available
									if (type_node.type() == Type::Auto) {
										if (auto deduced = deduceLambdaClosureType(*symbol, decl->identifier_token())) {
											type_node = *deduced;
										}
									}
									arg_types.push_back(type_node);
								} else {
									// Default to int
									arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
								}
							} else {
								arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
							}
					} else if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
						arg_types.push_back(TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8));
						} else if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
							const auto& literal = std::get<NumericLiteralNode>(arg_expr);
							arg_types.push_back(TypeSpecifierNode(literal.type(), TypeQualifier::None, 
								static_cast<unsigned char>(literal.sizeInBits())));
						} else {
							// For complex expressions, evaluate and get type
							auto operands = visitExpressionNode(arg_expr);
							Type type = std::get<Type>(operands[0]);
							int size = std::get<int>(operands[1]);
							arg_types.push_back(TypeSpecifierNode(type, TypeQualifier::None, static_cast<unsigned char>(size)));
						}
					});
					
					// Now build param_types with deduced types for auto parameters
					size_t arg_idx = 0;
					for (const auto& param_node : lambda.parameters()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
							
							if (param_type.type() == Type::Auto && arg_idx < arg_types.size()) {
								// Deduce type from argument, preserving reference flags from auto&& parameter
								TypeSpecifierNode deduced_type = arg_types[arg_idx];
								// Copy reference flags from auto parameter (e.g., auto&& -> T&&)
								if (param_type.is_rvalue_reference()) {
									deduced_type.set_reference(true);  // rvalue reference (&&)
								} else if (param_type.is_reference()) {
									deduced_type.set_reference(false);  // lvalue reference (&)
								}
								deduced_param_types.push_back(deduced_type);
								param_types.push_back(deduced_type);
							} else {
								param_types.push_back(param_type);
							}
						}
						arg_idx++;
					}
					
					// Build instantiation key and request instantiation
					std::string instantiation_key = std::to_string(lambda.lambda_id());
					for (const auto& deduced : deduced_param_types) {
						instantiation_key += "_" + std::to_string(static_cast<int>(deduced.type())) + 
						                     "_" + std::to_string(deduced.size_in_bits());
					}
					
					// Check if we've already scheduled this instantiation
					if (generated_generic_lambda_instantiations_.find(instantiation_key) == 
					    generated_generic_lambda_instantiations_.end()) {
						// Schedule this instantiation
						GenericLambdaInstantiation inst;
						inst.lambda_id = lambda.lambda_id();
						inst.instantiation_key = StringTable::getOrInternStringHandle(instantiation_key);
						for (size_t i = 0; i < auto_param_indices.size() && i < deduced_param_types.size(); ++i) {
							inst.deduced_types.push_back({auto_param_indices[i], deduced_param_types[i]});
						}
						pending_generic_lambda_instantiations_.push_back(std::move(inst));
						generated_generic_lambda_instantiations_.insert(instantiation_key);
						
						// Also store deduced types in the LambdaInfo for generation
						// Find the LambdaInfo for this lambda
						for (auto& lambda_info : collected_lambdas_) {
							if (lambda_info.lambda_id == lambda.lambda_id()) {
								// Store deduced types (full TypeSpecifierNode to preserve struct info and reference flags)
								for (size_t i = 0; i < auto_param_indices.size() && i < deduced_param_types.size(); ++i) {
									lambda_info.setDeducedType(
										auto_param_indices[i],
										deduced_param_types[i]
									);
								}
								break;
							}
						}
					}
				} else {
					// Non-generic: use parameter types directly
					for (const auto& param_node : lambda.parameters()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
							param_types.push_back(param_type);
						}
					}
				}
				
				// Generate mangled name for __invoke (matching how it's defined in generateLambdaInvokeFunction)
				std::string_view mangled = generateMangledNameForCall(
					StringTable::getStringView(invoke_name),
					return_type_node,
					param_types,
					false,  // not variadic
					""  // not a member function
				);
				
				call_op.function_name = StringTable::getOrInternStringHandle(mangled);
				call_op.is_member_function = false;
				call_op.is_variadic = false;  // Lambdas cannot be variadic in C++20


				// Add arguments
				memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
					const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
					auto argumentIrOperands = visitExpressionNode(arg_expr);
					if (std::holds_alternative<IdentifierNode>(arg_expr)) {
						const auto& identifier = std::get<IdentifierNode>(arg_expr);
						const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
						// Convert to TypedValue
						TypedValue arg;
						arg.type = type_node.type();
						arg.size_in_bits = static_cast<int>(type_node.size_in_bits());
						arg.value = StringTable::getOrInternStringHandle(identifier.name());
						call_op.args.push_back(arg);
					} else {
						// Convert argumentIrOperands to TypedValue
						TypedValue arg = toTypedValue(argumentIrOperands);
						call_op.args.push_back(arg);
					}
				});

				// Add the function call instruction with typed payload
				ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberFunctionCallNode.called_from()));

				// Return the result with actual return type from lambda
				return { call_op.return_type, call_op.return_size_in_bits, ret_var, 0ULL };
			}
			// For capturing lambdas, fall through to the regular member function call path
			// The closure object was already created by generateLambdaExpressionIr
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
			std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
			// Also check global symbol table if not found locally
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(object_name);
			}
			if (symbol.has_value()) {
				// Use helper to get DeclarationNode from either DeclarationNode or VariableDeclarationNode
				object_decl = get_decl_from_symbol(*symbol);
				if (object_decl) {
					object_type = object_decl->type_node().as<TypeSpecifierNode>();
					
					// If the type is 'auto', deduce the actual closure type from lambda initializer
					if (object_type.type() == Type::Auto) {
						if (auto deduced = deduceLambdaClosureType(*symbol, object_decl->identifier_token())) {
							object_type = *deduced;
						}
					}
				}
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
						if (symbol.has_value()) {
							const DeclarationNode* ptr_decl = get_decl_from_symbol(*symbol);
							if (ptr_decl) {
								object_decl = ptr_decl;
								// Get the pointer type and remove one level of indirection
								TypeSpecifierNode ptr_type = ptr_decl->type_node().as<TypeSpecifierNode>();
								if (ptr_type.pointer_levels().size() > 0) {
									object_type = ptr_type;
									object_type.remove_pointer_level();
								}
							}
						}
					}
				}
			}
		} else if (std::holds_alternative<MemberAccessNode>(object_expr)) {
			// Handle member access (from this->operation transformation for function pointer calls)
			const MemberAccessNode& member_access = std::get<MemberAccessNode>(object_expr);
			
			// Get the base object (should be "this")
			const ASTNode& base_node = member_access.object();
			if (base_node.is<ExpressionNode>()) {
				const ExpressionNode& base_expr = base_node.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(base_expr)) {
					const IdentifierNode& base_ident = std::get<IdentifierNode>(base_expr);
					std::string_view base_name = base_ident.name();
					
					// Look up the base object (e.g., "this")
					const std::optional<ASTNode> symbol = symbol_table.lookup(base_name);
					if (symbol.has_value()) {
						const DeclarationNode* base_decl = get_decl_from_symbol(*symbol);
						if (base_decl) {
							TypeSpecifierNode base_type_spec = base_decl->type_node().as<TypeSpecifierNode>();
							
							// If this is a pointer (like "this"), dereference it
							if (base_type_spec.pointer_levels().size() > 0) {
								base_type_spec.remove_pointer_level();
							}
							
							// Now base_type_spec should be the struct type
							if (base_type_spec.type() == Type::Struct) {
								object_type = base_type_spec;
								object_name = base_name;  // Use the base name for the call
							}
						}
					}
				}
			}
		}

		// For immediate lambda invocation, object_decl can be nullptr
		// In that case, we still need object_type to be set correctly

		// Special case: Handle namespace-qualified function calls that were incorrectly parsed as member function calls
		// This can happen when std::function() is parsed and the object is a namespace identifier
		if (std::holds_alternative<QualifiedIdentifierNode>(object_expr)) {
			// This is a namespace-qualified function call, not a member function call
			// Treat it as a regular function call instead
			return convertMemberCallToFunctionCall(memberFunctionCallNode);
		}
		
		// Verify this is a struct type BEFORE checking other cases
		// If object_type is not a struct, this might be a misparsed namespace-qualified function call
		if (object_type.type() != Type::Struct) {
			// The object is not a struct - this might be a namespace identifier or other non-struct type
			// Treat this as a regular function call instead of a member function call
			return convertMemberCallToFunctionCall(memberFunctionCallNode);
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
				StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
				for (const auto& member_func : struct_info->member_functions) {
					if (member_func.getName() == func_name_handle) {
						called_member_func = &member_func;
						if (member_func.is_virtual) {
							is_virtual_call = true;
							vtable_index = member_func.vtable_index;
						}
						break;
					}
				}
				
				// If not found in the current class, search base classes
				const StructTypeInfo* declaring_struct = struct_info;
				if (!called_member_func && !struct_info->base_classes.empty()) {
					auto searchBaseClasses = [&](auto&& self, const StructTypeInfo* current_struct) -> void {
						for (const auto& base_spec : current_struct->base_classes) {
							if (base_spec.type_index < gTypeInfo.size()) {
								const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
								if (base_type_info.isStruct()) {
									const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
									if (base_struct_info) {
										// Check member functions in base class
										for (const auto& member_func : base_struct_info->member_functions) {
											if (member_func.getName() == func_name_handle) {
												called_member_func = &member_func;
												declaring_struct = base_struct_info;  // Update to use base class name
												if (member_func.is_virtual) {
													is_virtual_call = true;
													vtable_index = member_func.vtable_index;
												}
												return; // Stop searching once found
											}
										}
										// Recursively search base classes of this base class
										if (!called_member_func) {
											self(self, base_struct_info);
										}
									}
								}
							}
						}
					};
					searchBaseClasses(searchBaseClasses, struct_info);
				}
				
				// Use declaring_struct instead of struct_info for mangled name generation
				// This ensures we use the correct class name where the function is declared
				struct_info = declaring_struct;
				
				// If not found as member function, check if it's a function pointer data member
				if (!called_member_func) {
					for (const auto& member : struct_info->members) {
						if (member.getName() == func_name_handle && member.type == Type::FunctionPointer) {
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
								member_load.object = StringTable::getOrInternStringHandle(object_name);
							}
							
							member_load.member_name = StringTable::getOrInternStringHandle(func_name);  // Member name
							member_load.offset = static_cast<int>(member.offset);  // Member offset
							member_load.is_reference = member.is_reference;
							member_load.is_rvalue_reference = member.is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
							
							// Now add the indirect call with the function pointer temp var
							irOperands.emplace_back(func_ptr_temp);
							
							// Add arguments
							std::vector<TypedValue> arguments;
							memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
								auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
								// Extract type, size, and value from the expression result
								Type arg_type = std::get<Type>(argumentIrOperands[0]);
								int arg_size = std::get<int>(argumentIrOperands[1]);
								IrValue arg_value = std::visit([](auto&& arg) -> IrValue {
									using T = std::decay_t<decltype(arg)>;
									if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, std::string_view> ||
												  std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
										return arg;
									} else {
										return 0ULL;
									}
								}, argumentIrOperands[2]);
								arguments.push_back(TypedValue{arg_type, arg_size, arg_value});
							});
						
							IndirectCallOp op{
								.result = ret_var,
								.function_pointer = func_ptr_temp,
								.arguments = std::move(arguments)
							};
							ir_.addInstruction(IrInstruction(IrOpcode::IndirectCall, std::move(op), memberFunctionCallNode.called_from()));
							
							// Return with function pointer's return type
							// TODO: Need to get the actual return type from the function signature stored in the member's TypeSpecifierNode
							// For now, assume int return type (common case)
							return { Type::Int, 32, ret_var, 0ULL };
						}
					}
				}
			}
		}

		// Check if this is a member function template that needs instantiation
		if (struct_info) {
			std::string_view func_name = func_decl_node.identifier_token().value();
			StringBuilder qualified_name_sb;
			qualified_name_sb.append(StringTable::getStringView(struct_info->getName())).append("::").append(func_name);
			std::string qualified_template_name(qualified_name_sb.commit());
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
						FLASH_LOG(Codegen, Debug, "Argument is not an ExpressionNode");
						return;
					}
					FLASH_LOG(Codegen, Trace, "Argument is an ExpressionNode");
					
					const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
					
					// DEBUG removed
					
					// Get type of argument - for literals, use the literal type
					if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
						arg_types.push_back(Type::Bool);
					} else if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
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
						// Check requires clause constraint before instantiation
						bool should_instantiate = true;
						if (template_func.has_requires_clause()) {
							const RequiresClauseNode& requires_clause = 
								template_func.requires_clause()->as<RequiresClauseNode>();
							
							// Get template parameter names for evaluation
							std::vector<std::string_view> eval_param_names;
							for (const auto& tparam_node : template_func.template_parameters()) {
								if (tparam_node.is<TemplateParameterNode>()) {
									eval_param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
								}
							}
							
							// Convert arg_types to TemplateTypeArg for evaluation
							std::vector<TemplateTypeArg> type_args;
							for (const auto& arg_type : arg_types) {
								TemplateTypeArg type_arg;
								type_arg.base_type = arg_type;
								type_arg.type_index = 0;
								type_args.push_back(type_arg);
							}
							
							// Evaluate the constraint with the template arguments
							auto constraint_result = evaluateConstraint(
								requires_clause.constraint_expr(), type_args, eval_param_names);
							
							if (!constraint_result.satisfied) {
								// Constraint not satisfied - report detailed error
								// Build template arguments string
								std::string args_str;
								for (size_t i = 0; i < arg_types.size(); ++i) {
									if (i > 0) args_str += ", ";
									args_str += std::string(TemplateRegistry::typeToString(arg_types[i]));
								}
								
								FLASH_LOG(Codegen, Error, "constraint not satisfied for template function '", func_name, "'");
								FLASH_LOG(Codegen, Error, "  ", constraint_result.error_message);
								if (!constraint_result.failed_requirement.empty()) {
									FLASH_LOG(Codegen, Error, "  failed requirement: ", constraint_result.failed_requirement);
								}
								if (!constraint_result.suggestion.empty()) {
									FLASH_LOG(Codegen, Error, "  suggestion: ", constraint_result.suggestion);
								}
								FLASH_LOG(Codegen, Error, "  template arguments: ", args_str);
								
								// Don't create instantiation - constraint failed
								should_instantiate = false;
							}
						}
						
						// Create new instantiation only if constraint was satisfied (or no constraint)
						if (should_instantiate) {
							gTemplateRegistry.registerInstantiation(inst_key, template_func.function_declaration());
						}
						
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
							inst_info.mangled_name = StringTable::getOrInternStringHandle(mangled_func_name);
							inst_info.struct_name = struct_info->getName();
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
			std::string_view current_function = getCurrentFunctionName();
			if (!checkMemberFunctionAccess(called_member_func, struct_info, current_context, current_function)) {
				std::string_view access_str = (called_member_func->access == AccessSpecifier::Private) ? "private"sv : "protected"sv;
				std::string context_str = current_context ? (std::string(" from '") + std::string(StringTable::getStringView(current_context->getName())) + "'") : "";
				FLASH_LOG(Codegen, Error, "Cannot access ", access_str, " member function '", called_member_func->getName(), 
				          "' of '", struct_info->getName(), "'", context_str);
				assert(false && "Access control violation");
				return { Type::Int, 32, TempVar{0} };
			}
		}

		TempVar ret_var = var_counter.next();

		if (is_virtual_call && vtable_index >= 0) {
			// Generate virtual function call using VirtualCallOp
			VirtualCallOp vcall_op;
			// Get return type from the actual member function (if found) instead of the placeholder declaration
			// The placeholder may not have correct pointer depth information for the return type
			const auto& return_type = (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) 
				? called_member_func->function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>()
				: func_decl_node.type_node().as<TypeSpecifierNode>();
			vcall_op.result.type = return_type.type();
			// For pointer return types, use 64 bits (pointer size), otherwise use the type's size
			// Also handle reference return types as pointers (64 bits)
			FLASH_LOG(Codegen, Debug, "VirtualCall return_type: ptr_depth=", return_type.pointer_depth(),
			          " is_ptr=", return_type.is_pointer(),
			          " is_ref=", return_type.is_reference(),
			          " is_rref=", return_type.is_rvalue_reference(),
			          " size_bits=", return_type.size_in_bits());
			if (return_type.pointer_depth() > 0 || return_type.is_pointer() || return_type.is_reference() || return_type.is_rvalue_reference()) {
				vcall_op.result.size_in_bits = 64;
			} else {
				vcall_op.result.size_in_bits = static_cast<int>(return_type.size_in_bits());
			}
			FLASH_LOG(Codegen, Debug, "VirtualCall result.size_in_bits=", vcall_op.result.size_in_bits);
			vcall_op.result.value = ret_var;
			vcall_op.object_type = object_type.type();
			vcall_op.object_size = static_cast<int>(object_type.size_in_bits());
			vcall_op.object = StringTable::getOrInternStringHandle(object_name);
			vcall_op.vtable_index = vtable_index;
			// Set is_pointer_access based on whether the object is accessed through a pointer (ptr->method)
			// or through a reference (ref.method()). References are implemented as pointers internally,
			// so they need the same treatment as pointer access for virtual dispatch.
			vcall_op.is_pointer_access = (object_type.pointer_depth() > 0) || object_type.is_reference() || object_type.is_rvalue_reference();

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
					tv.value = StringTable::getOrInternStringHandle(identifier.name());
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
			
			// Vector to hold deduced parameter types (populated for generic lambdas)
			std::vector<TypeSpecifierNode> param_types;
			
			// Check if this is an instantiated template function
			std::string_view func_name = func_decl_node.identifier_token().value();
			StringHandle function_name;
			
			// Check if this is a member function - use struct_info to determine
			if (struct_info) {
				// For nested classes, we need the fully qualified name from TypeInfo
				auto struct_name = struct_info->getName();
				auto type_it = gTypesByName.find(struct_name);
				if (type_it != gTypesByName.end()) {
					struct_name = type_it->second->name();
				}
				auto qualified_template_name = StringTable::getOrInternStringHandle(StringBuilder().append(struct_name).append("::"sv).append(func_name));
				
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
						if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
							template_args.push_back(TemplateArgument::makeType(Type::Bool));
						} else if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
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
					function_name = StringTable::getOrInternStringHandle(StringBuilder().append(struct_name).append("::"sv).append(mangled_func_name));
				} else {
					// Regular member function (not a template) - generate proper mangled name
					// Use the function declaration from struct_info if available (has correct parameters)
					const FunctionDeclarationNode* func_for_mangling = &func_decl;
					if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
						func_for_mangling = &called_member_func->function_decl.as<FunctionDeclarationNode>();
					}
					
					// Get return type and parameter types from the function declaration
					const auto& return_type_node = func_for_mangling->decl_node().type_node().as<TypeSpecifierNode>();
					
					// Check if this is a generic lambda call (lambda with auto parameters)
					bool is_generic_lambda = StringTable::getStringView(struct_name).substr(0, 9) == "__lambda_"sv;
					if (is_generic_lambda) {
						// For generic lambdas, we need to deduce auto parameter types from arguments
						// Collect argument types first
						std::vector<TypeSpecifierNode> arg_types;
						memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
							const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
							if (std::holds_alternative<IdentifierNode>(arg_expr)) {
								const auto& identifier = std::get<IdentifierNode>(arg_expr);
								const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
								if (symbol.has_value()) {
									const DeclarationNode* decl = get_decl_from_symbol(*symbol);
									if (decl) {
										TypeSpecifierNode type_node = decl->type_node().as<TypeSpecifierNode>();
										// Resolve auto type from lambda initializer if available
										if (type_node.type() == Type::Auto) {
											if (auto deduced = deduceLambdaClosureType(*symbol, decl->identifier_token())) {
												type_node = *deduced;
											}
										}
										arg_types.push_back(type_node);
									} else {
										arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
									}
								} else {
									arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
								}
					} else if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
						arg_types.push_back(TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8));
							} else if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
								const auto& literal = std::get<NumericLiteralNode>(arg_expr);
								arg_types.push_back(TypeSpecifierNode(literal.type(), TypeQualifier::None, 
									static_cast<unsigned char>(literal.sizeInBits())));
							} else {
								// Default to int for complex expressions
								arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
							}
						});
						
						// Now build param_types with deduced types for auto parameters
						size_t arg_idx = 0;
						for (const auto& param_node : func_for_mangling->parameter_nodes()) {
							if (param_node.is<DeclarationNode>()) {
								const auto& param_decl = param_node.as<DeclarationNode>();
								const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
								
								if (param_type.type() == Type::Auto && arg_idx < arg_types.size()) {
									// Deduce type from argument, preserving reference flags from auto&& parameter
									TypeSpecifierNode deduced_type = arg_types[arg_idx];
									if (param_type.is_rvalue_reference()) {
										deduced_type.set_reference(true);  // rvalue reference (&&)
									} else if (param_type.is_reference()) {
										deduced_type.set_reference(false);  // lvalue reference (&)
									}
									param_types.push_back(deduced_type);
									
									// Also store the deduced type in LambdaInfo for use by generateLambdaOperatorCallFunction
									for (auto& lambda_info : collected_lambdas_) {
										if (lambda_info.closure_type_name == struct_name) {
											lambda_info.setDeducedType(arg_idx, deduced_type);
											break;
										}
									}
								} else {
									param_types.push_back(param_type);
								}
							}
							arg_idx++;
						}
					} else {
						// Non-lambda: use parameter types directly from declaration
						for (const auto& param_node : func_for_mangling->parameter_nodes()) {
							if (param_node.is<DeclarationNode>()) {
								const auto& param_decl = param_node.as<DeclarationNode>();
								const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
								param_types.push_back(param_type);
							}
						}
					}
					
					// Generate proper mangled name including parameter types
					std::string_view mangled = generateMangledNameForCall(
						func_name,
						return_type_node,
						param_types,
						func_for_mangling->is_variadic(),
						StringTable::getStringView(struct_name)
					);
					function_name = StringTable::getOrInternStringHandle(mangled);
				}
			} else {
				// Non-member function or fallback
				function_name = StringTable::getOrInternStringHandle(func_name);
			}
			
			// Create CallOp structure
			CallOp call_op;
			call_op.result = ret_var;
			call_op.function_name = function_name;
			
			// Get return type information from the actual member function declaration
			// Use called_member_func if available (has the substituted template types)
			// Otherwise fall back to func_decl or func_decl_node
			const TypeSpecifierNode* return_type_ptr = nullptr;
			if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
				return_type_ptr = &called_member_func->function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>();
			} else {
				return_type_ptr = &func_decl_node.type_node().as<TypeSpecifierNode>();
			}
			const auto& return_type = *return_type_ptr;
			call_op.return_type = return_type.type();
			call_op.return_size_in_bits = (return_type.pointer_depth() > 0) ? 64 : static_cast<int>(return_type.size_in_bits());
			call_op.is_member_function = true;
			
			// Get the actual function declaration to check if it's variadic
			const FunctionDeclarationNode* actual_func_decl_for_variadic = nullptr;
			if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
				actual_func_decl_for_variadic = &called_member_func->function_decl.as<FunctionDeclarationNode>();
			} else {
				actual_func_decl_for_variadic = &func_decl;
			}
			call_op.is_variadic = actual_func_decl_for_variadic->is_variadic();
			
			// Detect if calling a member function that returns struct by value (needs hidden return parameter for RVO)
			bool returns_struct_by_value = (return_type.type() == Type::Struct && return_type.pointer_depth() == 0 && !return_type.is_reference());
			if (returns_struct_by_value) {
				call_op.uses_return_slot = true;
				call_op.return_slot = ret_var;  // The result temp var serves as the return slot
				call_op.return_type_index = return_type.type_index();
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Member function call {} returns struct by value - using return slot (temp_{})",
					StringTable::getStringView(function_name), ret_var.var_number);
			}
			
			// Add the object as the first argument (this pointer)
			// The 'this' pointer is always 64 bits (pointer size on x64), regardless of struct size
			// This is critical for empty structs (size 0) which still need a valid address
			IrValue this_arg_value;
			bool object_is_pointer_like = object_type.pointer_depth() > 0 || object_type.is_reference() || object_type.is_rvalue_reference();
			if (object_is_pointer_like) {
				// For pointer/reference objects, pass through directly
				this_arg_value = IrValue(StringTable::getOrInternStringHandle(object_name));
			} else {
				// For object values, take the address so member functions receive a pointer to the object
				TempVar this_addr = var_counter.next();
				AddressOfOp addr_op;
				addr_op.result = this_addr;
				addr_op.operand.type = object_type.type();
				addr_op.operand.size_in_bits = static_cast<int>(object_type.size_in_bits());
				addr_op.operand.pointer_depth = static_cast<int>(object_type.pointer_depth());
				addr_op.operand.value = StringTable::getOrInternStringHandle(object_name);
				ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), memberFunctionCallNode.called_from()));
				this_arg_value = IrValue(this_addr);
			}
			call_op.args.push_back(TypedValue{
				.type = object_type.type(),
				.size_in_bits = 64,  // Pointer size - always 64 bits on x64 architecture
				.value = this_arg_value
			});

			// Generate IR for function arguments and add to CallOp
			size_t arg_index = 0;
		
			// Get the actual function declaration with parameters from struct_info if available
			const FunctionDeclarationNode* actual_func_decl = nullptr;
			if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
				actual_func_decl = &called_member_func->function_decl.as<FunctionDeclarationNode>();
			} else {
				actual_func_decl = &func_decl;
			}
		
			memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
				auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			
				// Get the parameter type from the function declaration to check if it's a reference
				// For generic lambdas, use the deduced types from param_types instead of the original auto types
				const TypeSpecifierNode* param_type = nullptr;
				std::optional<TypeSpecifierNode> deduced_param_type;
				if (arg_index < param_types.size()) {
					// Use deduced type from param_types (handles generic lambdas correctly)
					deduced_param_type = param_types[arg_index];
					param_type = &(*deduced_param_type);
				} else if (arg_index < actual_func_decl->parameter_nodes().size()) {
					const ASTNode& param_node = actual_func_decl->parameter_nodes()[arg_index];
					if (param_node.is<DeclarationNode>()) {
						const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
						param_type = &param_decl.type_node().as<TypeSpecifierNode>();
					} else if (param_node.is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var_decl = param_node.as<VariableDeclarationNode>();
						const DeclarationNode& param_decl = var_decl.declaration();
						param_type = &param_decl.type_node().as<TypeSpecifierNode>();
					}
				}
			
				// For variables, we need to add the type and size
				if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
					const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
					const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					
					// Check if this is a function being passed as a function pointer argument
					if (symbol.has_value() && symbol->is<FunctionDeclarationNode>()) {
						// Function being passed as function pointer - just pass its name
						call_op.args.push_back(TypedValue{
							.type = Type::FunctionPointer,
							.size_in_bits = 64,  // Pointer size
							.value = IrValue(StringTable::getOrInternStringHandle(identifier.name()))
						});
					} else if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
				
						// Check if parameter expects a reference
						if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
							// Parameter expects a reference - pass the address of the argument
							if (type_node.is_reference() || type_node.is_rvalue_reference()) {
								// Argument is already a reference - just pass it through
								call_op.args.push_back(TypedValue{
									.type = type_node.type(),
									.size_in_bits = static_cast<int>(type_node.size_in_bits()),
									.value = IrValue(StringTable::getOrInternStringHandle(identifier.name())),
									.is_reference = true
								});
							} else {
								// Argument is a value - take its address
								TempVar addr_var = var_counter.next();
						
								AddressOfOp addr_op;
								addr_op.result = addr_var;
								addr_op.operand.type = type_node.type();
								addr_op.operand.size_in_bits = static_cast<int>(type_node.size_in_bits());
								addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
								addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
						
								// Pass the address with pointer size
								call_op.args.push_back(TypedValue{
									.type = type_node.type(),
									.size_in_bits = 64,  // Pointer size
									.value = IrValue(addr_var),
									.is_reference = true
								});
							}
						} else {
							// Regular pass by value
							call_op.args.push_back(TypedValue{
								.type = type_node.type(),
								.size_in_bits = static_cast<int>(type_node.size_in_bits()),
								.value = IrValue(StringTable::getOrInternStringHandle(identifier.name()))
							});
						}
					} else if (symbol.has_value() && symbol->is<VariableDeclarationNode>()) {
						// Handle VariableDeclarationNode (local variables)
						const auto& var_decl = symbol->as<VariableDeclarationNode>();
						const auto& decl_node = var_decl.declaration();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
				
						// Check if parameter expects a reference
						if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
							// Parameter expects a reference - pass the address of the argument
							if (type_node.is_reference() || type_node.is_rvalue_reference()) {
								// Argument is already a reference - just pass it through
								call_op.args.push_back(TypedValue{
									.type = type_node.type(),
									.size_in_bits = static_cast<int>(type_node.size_in_bits()),
									.value = IrValue(StringTable::getOrInternStringHandle(identifier.name())),
									.is_reference = true
								});
							} else {
								// Argument is a value - take its address
								TempVar addr_var = var_counter.next();
						
								AddressOfOp addr_op;
								addr_op.result = addr_var;
								addr_op.operand.type = type_node.type();
								addr_op.operand.size_in_bits = static_cast<int>(type_node.size_in_bits());
								addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
								addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
						
								// Pass the address with pointer size
								call_op.args.push_back(TypedValue{
									.type = type_node.type(),
									.size_in_bits = 64,  // Pointer size
									.value = IrValue(addr_var),
									.is_reference = true
								});
							}
						} else {
							// Regular pass by value
							call_op.args.push_back(TypedValue{
								.type = type_node.type(),
								.size_in_bits = static_cast<int>(type_node.size_in_bits()),
								.value = IrValue(StringTable::getOrInternStringHandle(identifier.name()))
							});
						}
					} else {
						// Unknown symbol type - use toTypedValue fallback
						call_op.args.push_back(toTypedValue(std::span<const IrOperand>(argumentIrOperands.data(), argumentIrOperands.size())));
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
							addr_op.operand.type = literal_type;
							addr_op.operand.size_in_bits = literal_size;
							addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
							addr_op.operand.value = temp_var;
							ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
							
							// Pass the address
							call_op.args.push_back(TypedValue{
								.type = literal_type,
								.size_in_bits = 64,  // Pointer size
								.value = IrValue(addr_var),
								.is_reference = true
							});
						} else {
							// Not a literal (expression result in a TempVar) - take its address
							if (argumentIrOperands.size() >= 3 && std::holds_alternative<TempVar>(argumentIrOperands[2])) {
								Type expr_type = std::get<Type>(argumentIrOperands[0]);
								int expr_size = std::get<int>(argumentIrOperands[1]);
								TempVar expr_var = std::get<TempVar>(argumentIrOperands[2]);
								
								TempVar addr_var = var_counter.next();
								AddressOfOp addr_op;
								addr_op.result = addr_var;
								addr_op.operand.type = expr_type;
								addr_op.operand.size_in_bits = expr_size;
								addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
								addr_op.operand.value = expr_var;
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
								
								call_op.args.push_back(TypedValue{
									.type = expr_type,
									.size_in_bits = 64,  // Pointer size
									.value = IrValue(addr_var),
									.is_reference = true
								});
							} else {
								// Fallback - just pass through
								call_op.args.push_back(toTypedValue(std::span<const IrOperand>(argumentIrOperands.data(), argumentIrOperands.size())));
							}
						}
					} else {
						// Parameter doesn't expect a reference - pass through as-is
						call_op.args.push_back(toTypedValue(std::span<const IrOperand>(argumentIrOperands.data(), argumentIrOperands.size())));
					}
				}
			
				arg_index++;
			});
			
			// Add the function call instruction with typed payload
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberFunctionCallNode.called_from()));
		}

		// Return the result variable with its type and size
		// If we found the actual member function from the struct, use its return type
		// Otherwise fall back to the placeholder function declaration
		const auto& return_type = (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) 
			? called_member_func->function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>()
			: func_decl_node.type_node().as<TypeSpecifierNode>();
		
		// For pointer/reference return types, use 64 bits (pointer size on x64)
		// Otherwise, use the type's natural size
		int return_size_bits = (return_type.pointer_depth() > 0 || return_type.is_reference() || return_type.is_rvalue_reference())
			? 64
			: static_cast<int>(return_type.size_in_bits());
		
		return { return_type.type(), return_size_bits, ret_var, static_cast<unsigned long long>(return_type.type_index()) };
	}

	// Helper struct for multidimensional array access
	struct MultiDimArrayAccess {
		std::string_view base_array_name;
		std::vector<ASTNode> indices;  // Indices from outermost to innermost
		const DeclarationNode* base_decl = nullptr;
		bool is_valid = false;
	};

	// Helper function to collect all indices from a chain of ArraySubscriptNodes
	// For arr[i][j][k], returns {base="arr", indices=[i, j, k]}
	MultiDimArrayAccess collectMultiDimArrayIndices(const ArraySubscriptNode& subscript) {
		MultiDimArrayAccess result;
		std::vector<ASTNode> indices_reversed;
		const ExpressionNode* current = &subscript.array_expr().as<ExpressionNode>();
		
		// Collect the outermost index first (the one in the current subscript)
		indices_reversed.push_back(subscript.index_expr());
		
		// Walk down the chain of ArraySubscriptNodes
		while (std::holds_alternative<ArraySubscriptNode>(*current)) {
			const ArraySubscriptNode& inner = std::get<ArraySubscriptNode>(*current);
			indices_reversed.push_back(inner.index_expr());
			current = &inner.array_expr().as<ExpressionNode>();
		}
		
		// The base should be an identifier
		if (std::holds_alternative<IdentifierNode>(*current)) {
			const IdentifierNode& base_ident = std::get<IdentifierNode>(*current);
			result.base_array_name = base_ident.name();
			
			// Look up the declaration
			std::optional<ASTNode> symbol = symbol_table.lookup(result.base_array_name);
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(result.base_array_name);
			}
			if (symbol.has_value()) {
				if (symbol->is<DeclarationNode>()) {
					result.base_decl = &symbol->as<DeclarationNode>();
				} else if (symbol->is<VariableDeclarationNode>()) {
					result.base_decl = &symbol->as<VariableDeclarationNode>().declaration();
				}
			}
			
			// Reverse the indices so they're in order from outermost to innermost
			// For arr[i][j], we collected [j, i], now reverse to [i, j]
			result.indices.reserve(indices_reversed.size());
			for (auto it = indices_reversed.rbegin(); it != indices_reversed.rend(); ++it) {
				result.indices.push_back(*it);
			}
			
			result.is_valid = (result.base_decl != nullptr) && 
			                  (result.base_decl->array_dimension_count() == result.indices.size()) &&
			                  (result.indices.size() > 1);  // Only valid for multidimensional
		}
		
		return result;
	}

	std::vector<IrOperand> generateArraySubscriptIr(const ArraySubscriptNode& arraySubscriptNode,
	                                                 ExpressionContext context = ExpressionContext::Load) {
		// Generate IR for array[index] expression
		// This computes the address: base_address + (index * element_size)

		// Check for multidimensional array access pattern (arr[i][j])
		// If the array expression is itself an ArraySubscriptNode, we have a multidimensional access
		const ExpressionNode& array_expr = arraySubscriptNode.array_expr().as<ExpressionNode>();
		if (std::holds_alternative<ArraySubscriptNode>(array_expr)) {
			// This could be a multidimensional array access
			auto multi_dim = collectMultiDimArrayIndices(arraySubscriptNode);
			
			if (multi_dim.is_valid && multi_dim.base_decl) {
				// We have a valid multidimensional array access
				// For arr[M][N][P] accessed as arr[i][j][k], compute flat_index = i*N*P + j*P + k
				
				const auto& type_node = multi_dim.base_decl->type_node().as<TypeSpecifierNode>();
				Type element_type = type_node.type();
				int element_size_bits = static_cast<int>(type_node.size_in_bits());
				size_t element_type_index = (element_type == Type::Struct) ? type_node.type_index() : 0;
				
				// Get element size for struct types
				if (element_size_bits == 0 && element_type == Type::Struct && element_type_index > 0) {
					const TypeInfo& type_info = gTypeInfo[element_type_index];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						element_size_bits = static_cast<int>(struct_info->total_size * 8);
					}
				}
				
				// Get all dimension sizes
				std::vector<size_t> dim_sizes;
				const auto& dims = multi_dim.base_decl->array_dimensions();
				for (const auto& dim_expr : dims) {
					ConstExpr::EvaluationContext ctx(symbol_table);
					auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
					if (eval_result.success && eval_result.as_int() > 0) {
						dim_sizes.push_back(static_cast<size_t>(eval_result.as_int()));
					} else {
						// Can't evaluate dimension at compile time, fall back to regular handling
						break;
					}
				}
				
				if (dim_sizes.size() == multi_dim.indices.size()) {
					// All dimensions evaluated successfully, compute flat index
					// For arr[D0][D1][D2] accessed as arr[i0][i1][i2]:
					// flat_index = i0 * (D1*D2) + i1 * D2 + i2
					
					// First, compute strides: stride[k] = product of dimensions after k
					std::vector<size_t> strides(dim_sizes.size());
					strides.back() = 1;
					for (int k = static_cast<int>(dim_sizes.size()) - 2; k >= 0; --k) {
						strides[k] = strides[k + 1] * dim_sizes[k + 1];
					}
					
					// Generate code to compute flat index
					// Start with the first index times its stride
					auto idx0_operands = visitExpressionNode(multi_dim.indices[0].as<ExpressionNode>());
					TempVar flat_index = var_counter.next();
					
					if (strides[0] == 1) {
						// Simple case: stride is 1, just copy the index
						// Use Add with 0 to effectively copy
						BinaryOp add_op;
						add_op.lhs = toTypedValue(idx0_operands);
						add_op.rhs = TypedValue{Type::Int, 32, 0ULL};
						add_op.result = IrValue{flat_index};
						ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
					} else {
						// flat_index = indices[0] * strides[0]
						BinaryOp mul_op;
						mul_op.lhs = toTypedValue(idx0_operands);
						mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[0])};
						mul_op.result = IrValue{flat_index};
						ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
					}
					
					// Add remaining indices: flat_index += indices[k] * strides[k]
					for (size_t k = 1; k < multi_dim.indices.size(); ++k) {
						auto idx_operands = visitExpressionNode(multi_dim.indices[k].as<ExpressionNode>());
						
						if (strides[k] == 1) {
							// flat_index += indices[k]
							TempVar new_flat = var_counter.next();
							BinaryOp add_op;
							add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
							add_op.rhs = toTypedValue(idx_operands);
							add_op.result = IrValue{new_flat};
							ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
							flat_index = new_flat;
						} else {
							// temp = indices[k] * strides[k]
							TempVar temp_prod = var_counter.next();
							BinaryOp mul_op;
							mul_op.lhs = toTypedValue(idx_operands);
							mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[k])};
							mul_op.result = IrValue{temp_prod};
							ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
							
							// flat_index += temp
							TempVar new_flat = var_counter.next();
							BinaryOp add_op;
							add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
							add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, temp_prod};
							add_op.result = IrValue{new_flat};
							ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
							flat_index = new_flat;
						}
					}
					
					// Now generate the array access using the flat index
					TempVar result_var = var_counter.next();
					
					// Mark array element access as lvalue using metadata system
					LValueInfo lvalue_info(
						LValueInfo::Kind::ArrayElement,
						StringTable::getOrInternStringHandle(multi_dim.base_array_name),
						0  // offset computed dynamically by index
					);
					lvalue_info.array_index = IrValue{flat_index};
					lvalue_info.is_pointer_to_array = false;  // This is a real array, not a pointer
					setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));
					
					// Create ArrayAccessOp with the flat index
					ArrayAccessOp payload;
					payload.result = result_var;
					payload.element_type = element_type;
					payload.element_size_in_bits = element_size_bits;
					payload.member_offset = 0;
					payload.is_pointer_to_array = false;
					payload.array = StringTable::getOrInternStringHandle(multi_dim.base_array_name);
					payload.index.type = Type::UnsignedLongLong;
					payload.index.size_in_bits = 64;
					payload.index.value = flat_index;
					
					if (context == ExpressionContext::LValueAddress) {
						// Don't emit ArrayAccess instruction (no load)
						return { element_type, element_size_bits, result_var, static_cast<unsigned long long>(element_type_index) };
					}
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));
					
					return { element_type, element_size_bits, result_var, static_cast<unsigned long long>(element_type_index) };
				}
			}
		}

		// Check if the array expression is a member access (e.g., obj.array[index])
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

						if (is_struct_type(type_node.type())) {
							TypeIndex struct_type_index = type_node.type_index();
							if (struct_type_index < gTypeInfo.size()) {
								const TypeInfo& struct_type_info = gTypeInfo[struct_type_index];
								const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
								
								if (struct_info) {
									const StructMember* member = struct_info->findMemberRecursive(StringTable::getOrInternStringHandle(std::string(member_name)));
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
										int base_element_size = get_type_size_bits(element_type);  // Use existing helper
										
										if (base_element_size > 0 && element_size_bits > base_element_size) {
											// It's an array
											element_size_bits = base_element_size;
										}

										// Create a temporary variable for the result
										TempVar result_var = var_counter.next();
										
										// Mark array element access as lvalue (Option 2: Value Category Tracking)
										StringHandle qualified_name = StringTable::getOrInternStringHandle(
											StringBuilder().append(object_name).append(".").append(member_name));
										LValueInfo lvalue_info(
											LValueInfo::Kind::ArrayElement,
											qualified_name,
											static_cast<int64_t>(member->offset)  // member offset in struct
										);
										// Store index information for unified assignment handler
										lvalue_info.array_index = toIrValue(index_operands[2]);
										lvalue_info.is_pointer_to_array = false;  // Member arrays are actual arrays, not pointers
										setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

										// Create typed payload for ArrayAccess with qualified member name
										ArrayAccessOp payload;
										payload.result = result_var;
										payload.element_type = element_type;
										payload.element_size_in_bits = element_size_bits;
										payload.array = StringTable::getOrInternStringHandle(StringBuilder().append(object_name).append(".").append(member_name));
										payload.member_offset = static_cast<int64_t>(member->offset);
										payload.is_pointer_to_array = false;  // Member arrays are actual arrays, not pointers
										
										// Set index as TypedValue
										payload.index.type = std::get<Type>(index_operands[0]);
										payload.index.size_in_bits = std::get<int>(index_operands[1]);
										if (std::holds_alternative<unsigned long long>(index_operands[2])) {
											payload.index.value = std::get<unsigned long long>(index_operands[2]);
										} else if (std::holds_alternative<TempVar>(index_operands[2])) {
											payload.index.value = std::get<TempVar>(index_operands[2]);
										} else if (std::holds_alternative<StringHandle>(index_operands[2])) {
											payload.index.value = std::get<StringHandle>(index_operands[2]);
										}

										// When context is LValueAddress, skip the load and return address/metadata only
										if (context == ExpressionContext::LValueAddress) {
											// Don't emit ArrayAccess instruction (no load)
											// Just return the metadata with the result temp var
											return { element_type, element_size_bits, result_var, 0ULL };
										}

										// Create instruction with typed payload (Load context - default)
										ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));

										// Return the result with the element type
										return { element_type, element_size_bits, result_var, 0ULL };
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

		// Check if this is a pointer type (e.g., int* arr)
		// If so, we need to get the base type size, not the pointer size (64)
		// Look up the identifier to get the actual type info
		bool is_pointer_to_array = false;
		size_t element_type_index = 0;  // Track type_index for struct elements
		int element_pointer_depth = 0;  // Track pointer depth for pointer array elements
		const ExpressionNode& arr_expr = arraySubscriptNode.array_expr().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(arr_expr)) {
			const IdentifierNode& arr_ident = std::get<IdentifierNode>(arr_expr);
			std::optional<ASTNode> symbol = symbol_table.lookup(arr_ident.name());
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(arr_ident.name());
			}
			if (symbol.has_value()) {
				const DeclarationNode* decl_ptr = nullptr;
				if (symbol->is<DeclarationNode>()) {
					decl_ptr = &symbol->as<DeclarationNode>();
				} else if (symbol->is<VariableDeclarationNode>()) {
					decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
				}
				
				if (decl_ptr) {
					const auto& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
					
					// Capture type_index for struct types (important for member access on array elements)
					if (type_node.type() == Type::Struct) {
						element_type_index = type_node.type_index();
					}
					
					// For array types, ALWAYS get the element size from type_node, not from array_operands
					// array_operands[1] contains 64 (pointer size) for arrays, not the element size
					if (decl_ptr->is_array() || type_node.is_array()) {
						// Check if this is an array of pointers (e.g., int* ptrs[3])
						// In this case, the element size should be the pointer size (64 bits), not the base type size
						if (type_node.pointer_depth() > 0) {
							// Array of pointers: element size is always 64 bits (pointer size)
							element_size_bits = 64;
							// Track pointer depth for the array element (e.g., for int* arr[3], element has pointer_depth=1)
							element_pointer_depth = type_node.pointer_depth();
						} else {
							// Get the element size from type_node
							element_size_bits = static_cast<int>(type_node.size_in_bits());
							// If still 0, compute from type info for struct types
							if (element_size_bits == 0 && type_node.type() == Type::Struct && element_type_index > 0) {
								const TypeInfo& type_info = gTypeInfo[element_type_index];
								const StructTypeInfo* struct_info = type_info.getStructInfo();
								if (struct_info) {
									element_size_bits = static_cast<int>(struct_info->total_size * 8);
								}
							}
						}
					}
					// For pointer types (not arrays), get the pointee size
					else if (type_node.pointer_depth() > 0) {
						// Get the base type size (what the pointer points to)
						element_size_bits = static_cast<int>(type_node.size_in_bits());
						is_pointer_to_array = true;  // This is a pointer, not an actual array
					}
				}
			}
		}
		
		// Fix element size for array members accessed through TempVar (e.g., vls.values[i])
		// When array comes from member_access, element_size_bits is the TOTAL array size (e.g., 640 bits for int[20])
		// We need to derive the actual element size from the element type
		if (std::holds_alternative<TempVar>(array_operands[2]) && !is_pointer_to_array) {
			// Check if element_size_bits is much larger than expected for element_type
			int base_element_size = get_type_size_bits(element_type);
			if (base_element_size > 0 && element_size_bits > base_element_size) {
				// This is likely an array where we got the total size instead of element size
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Array subscript on TempVar: fixing element_size from {} bits (total) to {} bits (element)",
					element_size_bits, base_element_size);
				element_size_bits = base_element_size;
			}
		}

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();
		
		// If the array expression resolved to a TempVar that actually refers to a member,
		// recover the qualified name and offset from its lvalue metadata so we don't lose
		// struct/offset information (important for member arrays).
		std::variant<StringHandle, TempVar> base_variant;
		int base_member_offset = 0;
		bool base_is_pointer_to_member = false;
		// Fast-path: if the array expression is a member access, rebuild qualified name directly
		if (std::holds_alternative<MemberAccessNode>(array_expr)) {
			const auto& member_access = std::get<MemberAccessNode>(array_expr);
			if (member_access.object().is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = member_access.object().as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const auto& object_ident = std::get<IdentifierNode>(obj_expr);
					std::string_view object_name = object_ident.name();
					auto symbol = symbol_table.lookup(object_name);
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
						if (is_struct_type(type_node.type()) && type_node.type_index() < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
							if (const StructTypeInfo* struct_info = type_info.getStructInfo()) {
								if (const StructMember* member = struct_info->findMemberRecursive(StringTable::getOrInternStringHandle(std::string(member_access.member_name())))) {
									base_variant = StringTable::getOrInternStringHandle(
										StringBuilder().append(object_name).append(".").append(member_access.member_name()));
									base_member_offset = static_cast<int>(member->offset);
									// Member access via '.' is not a pointer access for locals
								}
							}
						}
					}
				}
			}
			// If object isn't a simple identifier (e.g., arr[i].member), fall back to using the
			// computed operands to keep a valid base (TempVar or StringHandle) instead of
			// leaving an empty StringHandle that leads to invalid offsets.
			if (base_variant.valueless_by_exception()) {
				if (std::holds_alternative<TempVar>(array_operands[2])) {
					base_variant = std::get<TempVar>(array_operands[2]);
				} else if (std::holds_alternative<StringHandle>(array_operands[2])) {
					base_variant = std::get<StringHandle>(array_operands[2]);
				}
			}
		}
		// Simple identifier array (non-member)
		else if (std::holds_alternative<IdentifierNode>(array_expr)) {
			const auto& ident = std::get<IdentifierNode>(array_expr);
			base_variant = StringTable::getOrInternStringHandle(ident.name());
		}
		if (std::holds_alternative<TempVar>(array_operands[2])) {
			TempVar base_temp = std::get<TempVar>(array_operands[2]);
			if (auto base_lv = getTempVarLValueInfo(base_temp)) {
				if (base_lv->kind == LValueInfo::Kind::Member && base_lv->member_name.has_value()) {
					// Build qualified name: object.member
					if (std::holds_alternative<StringHandle>(base_lv->base)) {
						auto obj_name = std::get<StringHandle>(base_lv->base);
						base_variant = StringTable::getOrInternStringHandle(
							StringBuilder().append(StringTable::getStringView(obj_name))
							                .append(".")
							                .append(StringTable::getStringView(base_lv->member_name.value())));
						base_member_offset = base_lv->offset;
						base_is_pointer_to_member = base_lv->is_pointer_to_member;
					}
				}
			}
		}
		if (!std::holds_alternative<StringHandle>(base_variant)) {
			if (std::holds_alternative<StringHandle>(array_operands[2])) {
				base_variant = std::get<StringHandle>(array_operands[2]);
			}
		}
		// Prefer keeping TempVar base when available to preserve stack offsets for nested accesses
		if (!std::holds_alternative<TempVar>(base_variant) && std::holds_alternative<TempVar>(array_operands[2])) {
			base_variant = std::get<TempVar>(array_operands[2]);
		}
		
		// Mark array element access as lvalue (Option 2: Value Category Tracking)
		// arr[i] is an lvalue - it designates an object with a stable address
		LValueInfo lvalue_info(
			LValueInfo::Kind::ArrayElement,
			base_variant,
			base_member_offset  // offset for member arrays (otherwise 0)
		);
		// Store index information for unified assignment handler
		// Support both constant and variable indices
		lvalue_info.array_index = toIrValue(index_operands[2]);
		FLASH_LOG(Codegen, Debug, "Array index stored in metadata (supports constants and variables)");
		lvalue_info.is_pointer_to_array = is_pointer_to_array || base_is_pointer_to_member;
		setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

		// Create typed payload for ArrayAccess
		ArrayAccessOp payload;
		payload.result = result_var;
		payload.element_type = element_type;
		payload.element_size_in_bits = element_size_bits;
		payload.member_offset = 0;  // Not a member array
		payload.is_pointer_to_array = is_pointer_to_array;
		
		// Set array (either variable name or temp)
		if (std::holds_alternative<StringHandle>(array_operands[2])) {
			payload.array = std::get<StringHandle>(array_operands[2]);
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
		} else if (std::holds_alternative<StringHandle>(index_operands[2])) {
			payload.index.value = std::get<StringHandle>(index_operands[2]);
		}

		// When context is LValueAddress, skip the load and return address/metadata only
		if (context == ExpressionContext::LValueAddress) {
			// Don't emit ArrayAccess instruction (no load)
			// Just return the metadata with the result temp var
			// The metadata contains all information needed for store operations
			// For the 4th element: 
			// - For struct types, return type_index
			// - For pointer array elements, return pointer_depth
			// - Otherwise return 0
			unsigned long long fourth_element = (element_type == Type::Struct)
				? static_cast<unsigned long long>(element_type_index)
				: ((element_pointer_depth > 0) ? static_cast<unsigned long long>(element_pointer_depth) : 0ULL);
			return { element_type, element_size_bits, result_var, fourth_element };
		}

		// Create instruction with typed payload (Load context - default)
		ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));

		// Return [element_type, element_size_bits, result_var, struct_type_index or pointer_depth]
		// For the 4th element: 
		// - For struct types, return type_index
		// - For pointer array elements, return pointer_depth
		// - Otherwise return 0
		unsigned long long fourth_element = (element_type == Type::Struct)
			? static_cast<unsigned long long>(element_type_index)
			: ((element_pointer_depth > 0) ? static_cast<unsigned long long>(element_pointer_depth) : 0ULL);
		return { element_type, element_size_bits, result_var, fourth_element };
	}

	// Helper function to validate and setup identifier-based member access
	// Returns true on success, false on error
	bool validateAndSetupIdentifierMemberAccess(
		std::string_view object_name,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		bool& is_pointer_dereference) {
		
		// Look up the object in the symbol table (local first, then global)
		std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
		
		// If not found locally, try global symbol table (for global struct variables)
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(object_name);
		}
		
		// If not found in symbol tables, check if it's a type name (for static member access like ClassName::member)
		if (!symbol.has_value()) {
			FLASH_LOG(Codegen, Debug, "validateAndSetupIdentifierMemberAccess: object_name='", object_name, "' not in symbol table, checking gTypesByName");
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(object_name));
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				// This is a type name - set up for static member access
				FLASH_LOG(Codegen, Debug, "Found type '", object_name, "' in gTypesByName with type_index=", type_it->second->type_index_);
				base_object = StringTable::getOrInternStringHandle(object_name);
				base_type = Type::Struct;
				base_type_index = type_it->second->type_index_;
				is_pointer_dereference = false;  // Type names don't need dereferencing
				return true;
			}
			
			FLASH_LOG(Codegen, Error, "object '", object_name, "' not found in symbol table or type registry");
			return false;
		}

		// Use helper to get DeclarationNode from either DeclarationNode or VariableDeclarationNode
		const DeclarationNode* object_decl_ptr = get_decl_from_symbol(*symbol);
		if (!object_decl_ptr) {
			FLASH_LOG(Codegen, Error, "object '", object_name, "' is not a declaration");
			return false;
		}
		const DeclarationNode& object_decl = *object_decl_ptr;
		const TypeSpecifierNode& object_type = object_decl.type_node().as<TypeSpecifierNode>();

		// Verify this is a struct type (or a pointer/reference to a struct type)
		// References and pointers are automatically dereferenced for member access
		// Note: Type can be either Struct or UserDefined (for user-defined types like Point)
		// For pointers, the type might be Void with pointer_depth > 0 and type_index pointing to struct
		bool is_valid_for_member_access = is_struct_type(object_type.type()) || 
		                                  (object_type.pointer_depth() > 0 && object_type.type_index() > 0);
		if (!is_valid_for_member_access) {
			FLASH_LOG(Codegen, Error, "member access '.' on non-struct type '", object_name, "'");
			return false;
		}

		base_object = StringTable::getOrInternStringHandle(object_name);
		base_type = object_type.type();
		base_type_index = object_type.type_index();
		
		// Check if this is a pointer to struct (e.g., P* pp)
		// In this case, member access like pp->member should be treated as pointer dereference
		if (object_type.pointer_depth() > 0) {
			is_pointer_dereference = true;
		}
		
		return true;
	}

	std::vector<IrOperand> generateMemberAccessIr(const MemberAccessNode& memberAccessNode,
	                                               ExpressionContext context = ExpressionContext::Load) {
		std::vector<IrOperand> irOperands;

		// Get the object being accessed
		ASTNode object_node = memberAccessNode.object();
		std::string_view member_name = memberAccessNode.member_name();
		bool is_arrow = memberAccessNode.is_arrow();

		// Variables to hold the base object info
		std::variant<StringHandle, TempVar> base_object;
		Type base_type = Type::Void;
		size_t base_type_index = 0;
		bool is_pointer_dereference = false;  // Track if we're accessing through pointer (ptr->member)
		bool base_setup_complete = false;

		// OPERATOR-> OVERLOAD RESOLUTION
		// If this is arrow access (obj->member), check if the object has operator->() overload
		if (is_arrow && object_node.is<ExpressionNode>()) {
			const ExpressionNode& expr = object_node.as<ExpressionNode>();
			
			// For now, only handle simple identifiers  
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& ident = std::get<IdentifierNode>(expr);
				StringHandle identifier_handle = StringTable::getOrInternStringHandle(ident.name());
				
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier_handle);
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier_handle);
				}
				
				if (symbol.has_value()) {
					const TypeSpecifierNode* type_node = nullptr;
					if (symbol->is<DeclarationNode>()) {
						type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
					} else if (symbol->is<VariableDeclarationNode>()) {
						type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
					}
					
					// Check if it's a struct with operator-> overload
					if (type_node && type_node->type() == Type::Struct && type_node->pointer_depth() == 0) {
						auto overload_result = findUnaryOperatorOverload(type_node->type_index(), "->");
						
						if (overload_result.has_overload) {
							// Found an overload! Call operator->() to get pointer, then access member
							FLASH_LOG_FORMAT(Codegen, Debug, "Resolving operator-> overload for type index {}", 
							         type_node->type_index());
							
							const StructMemberFunction& member_func = *overload_result.member_overload;
							const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
							
							// Get struct name for mangling
							std::string_view struct_name = StringTable::getStringView(gTypeInfo[type_node->type_index()].name());
							
							// Get the return type from the function declaration (should be a pointer)
							const TypeSpecifierNode& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
							
							// Generate mangled name for operator->
							std::string_view operator_func_name = "operator->";
							std::vector<TypeSpecifierNode> empty_params;
							std::vector<std::string_view> empty_namespace;
							auto mangled_name = NameMangling::generateMangledName(
								operator_func_name,
								return_type,
								empty_params,
								false,
								struct_name,
								empty_namespace,
								Linkage::CPlusPlus
							);
							
							// Generate the call to operator->()
							TempVar ptr_result = var_counter.next();
							
							CallOp call_op;
							call_op.result = ptr_result;
							call_op.return_type = return_type.type();
							call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
							if (call_op.return_size_in_bits == 0) {
								call_op.return_size_in_bits = get_type_size_bits(return_type.type());
							}
							call_op.function_name = mangled_name;
							call_op.is_variadic = false;
							call_op.uses_return_slot = false;
							call_op.is_member_function = true;
							
							// Add 'this' pointer as first argument
							call_op.args.push_back(TypedValue{
								.type = type_node->type(),
								.size_in_bits = 64,  // Pointer size
								.value = IrValue(identifier_handle)
							});
							
							// Add the function call instruction
							ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberAccessNode.member_token()));
							
							// Now we have a pointer in ptr_result
							// operator-> should return a pointer, so we treat ptr_result as pointing to the actual object
							// Set up base_object info for the dereferenced pointer
							if (return_type.pointer_depth() > 0) {
								base_object = ptr_result;
								base_type = return_type.type();
								base_type_index = return_type.type_index();
								is_pointer_dereference = true;  // We'll dereference this pointer to access the member
								base_setup_complete = true;
							}
						}
					}
				}
			}
		}

		// If we haven't already set up base_object from operator-> overload, do normal processing
		if (!base_setup_complete && object_node.is<ExpressionNode>()) {
			const ExpressionNode& expr = object_node.as<ExpressionNode>();

			// Case 1: Simple identifier (e.g., obj.member or ptr->member where ptr is raw pointer)
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& object_ident = std::get<IdentifierNode>(expr);
				std::string_view object_name = object_ident.name();
				
				bool handled = false;
				
				// Special handling for 'this' in lambdas with [*this] or [this] capture
				if (object_name == "this") {
					// First try [*this] capture - returns copy of the object
					if (auto copy_this_temp = emitLoadCopyThis(memberAccessNode.member_token())) {
						base_object = *copy_this_temp;
						base_type = Type::Struct;
						base_type_index = current_lambda_context_.enclosing_struct_type_index;
						handled = true;
					}
					// Then try [this] capture - returns pointer to the object
					else if (auto this_ptr_temp = emitLoadThisPointer(memberAccessNode.member_token())) {
						base_object = *this_ptr_temp;
						base_type = Type::Struct;
						base_type_index = current_lambda_context_.enclosing_struct_type_index;
						is_pointer_dereference = true;  // Need to dereference the pointer
						handled = true;
					}
				}
				
				if (!handled) {
					if (!validateAndSetupIdentifierMemberAccess(object_name, base_object, base_type, base_type_index, is_pointer_dereference)) {
						return {};
					}
					// Note: validateAndSetupIdentifierMemberAccess already sets is_pointer_dereference
					// for pointer types, so no additional handling needed for is_arrow
				}
			}
			// Case 2: Nested member access (e.g., obj.inner.member)
			else if (std::holds_alternative<MemberAccessNode>(expr)) {
				const MemberAccessNode& nested_access = std::get<MemberAccessNode>(expr);

				// Recursively generate IR for the nested member access
				// Pass context to avoid loading intermediate struct values when they're part of lvalue expressions
				std::vector<IrOperand> nested_result = generateMemberAccessIr(nested_access, context);
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
					FLASH_LOG(Codegen, Error, "nested member access on non-struct type");
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
					FLASH_LOG(Codegen, Error, "member access on non-dereference unary operator");
					return {};
				}

				// Get the pointer operand
				const ASTNode& operand_node = unary_op.get_operand();
				if (!operand_node.is<ExpressionNode>()) {
					FLASH_LOG(Codegen, Error, "dereference operand is not an expression");
					return {};
				}
				const ExpressionNode& operand_expr = operand_node.as<ExpressionNode>();
				
				// Special handling for 'this' in lambdas with [this] or [*this] capture
				// Check this first before evaluating the expression
				bool is_lambda_this = false;
				if (std::holds_alternative<IdentifierNode>(operand_expr)) {
					const IdentifierNode& ptr_ident = std::get<IdentifierNode>(operand_expr);
					std::string_view ptr_name = ptr_ident.name();
					
					if (ptr_name == "this" && current_lambda_context_.isActive() && 
					    current_lambda_context_.captures.find(StringTable::getOrInternStringHandle("this"sv)) != current_lambda_context_.captures.end()) {
						is_lambda_this = true;
						// We're in a lambda that captured [this] or [*this]
						// Check which kind of capture it is
						auto capture_kind_it = current_lambda_context_.capture_kinds.find(StringTable::getOrInternStringHandle("this"sv));
						if (capture_kind_it != current_lambda_context_.capture_kinds.end() && 
						    capture_kind_it->second == LambdaCaptureNode::CaptureKind::CopyThis) {
							// [*this] capture: load from the copied object in __copy_this
							int copy_this_offset = getClosureMemberOffset("__copy_this");
							
							TempVar copy_this_ref = var_counter.next();
							MemberLoadOp load_copy_this;
							load_copy_this.result.value = copy_this_ref;
							load_copy_this.result.type = Type::Struct;
							load_copy_this.result.size_in_bits = 64;  // Pointer size
							load_copy_this.object = StringTable::getOrInternStringHandle("this"sv);  // Lambda's this (the closure)
							load_copy_this.member_name = StringTable::getOrInternStringHandle("__copy_this");
							load_copy_this.offset = copy_this_offset;
							load_copy_this.is_reference = false;
							load_copy_this.is_rvalue_reference = false;
							load_copy_this.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_copy_this), memberAccessNode.member_token()));
							
							// Use this as the base (it's a struct value, not a pointer)
							base_object = copy_this_ref;
							base_type = Type::Struct;
							base_type_index = current_lambda_context_.enclosing_struct_type_index;
						} else {
							// [this] capture: load the pointer from __this
							int this_member_offset = getClosureMemberOffset("__this");
							
							TempVar this_ptr = var_counter.next();
							MemberLoadOp load_this;
							load_this.result.value = this_ptr;
							load_this.result.type = Type::Void;
							load_this.result.size_in_bits = 64;
							load_this.object = StringTable::getOrInternStringHandle("this"sv);  // Lambda's this (the closure)
							load_this.member_name = StringTable::getOrInternStringHandle("__this");
							load_this.offset = this_member_offset;
							load_this.is_reference = false;
							load_this.is_rvalue_reference = false;
							load_this.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_this), memberAccessNode.member_token()));
							
							// Use this loaded pointer as the base
							base_object = this_ptr;
							base_type = Type::Struct;
							base_type_index = current_lambda_context_.enclosing_struct_type_index;
						}
					}
				}
				
				if (!is_lambda_this) {
					// Normal pointer handling - evaluate the pointer expression
					// This supports any expression that evaluates to a pointer:
					// - Simple identifiers (ptr)
					// - Function calls (getPtr())
					// - Nested member access (obj.ptr_member)
					// etc.
					auto pointer_operands = visitExpressionNode(operand_expr);
					if (pointer_operands.empty() || pointer_operands.size() < 3) {
						FLASH_LOG(Codegen, Error, "Failed to evaluate pointer expression for member access");
						return {};
					}
					
					// Extract type information from the evaluated expression
					Type pointer_type = std::get<Type>(pointer_operands[0]);
					
					// Get the type_index if available (for struct pointers)
					size_t pointer_type_index = 0;
					if (pointer_operands.size() >= 4 && std::holds_alternative<unsigned long long>(pointer_operands[3])) {
						pointer_type_index = static_cast<size_t>(std::get<unsigned long long>(pointer_operands[3]));
					}
					
					// The pointer value can be a string_view (identifier name) or TempVar (expression result)
					if (std::holds_alternative<StringHandle>(pointer_operands[2])) {
						base_object = std::get<StringHandle>(pointer_operands[2]);
					} else if (std::holds_alternative<TempVar>(pointer_operands[2])) {
						base_object = std::get<TempVar>(pointer_operands[2]);
					} else {
						FLASH_LOG(Codegen, Error, "Pointer expression result has unsupported value type");
						return {};
					}
					
					base_type = pointer_type;
					base_type_index = pointer_type_index;
					is_pointer_dereference = true;  // Mark that we're accessing through a pointer
				}
			}
			// Case 4: Array subscript (e.g., arr[i].member)
			else if (std::holds_alternative<ArraySubscriptNode>(expr)) {
				const ArraySubscriptNode& array_sub = std::get<ArraySubscriptNode>(expr);
				
				// Generate IR for the array subscript expression
				// This will evaluate arr[i] and return the element
				// array_operands = [element_type, element_size_bits, temp_var, struct_type_index (optional)]
				auto array_operands = generateArraySubscriptIr(array_sub);
				if (array_operands.empty() || array_operands.size() < 3) {
					FLASH_LOG(Codegen, Error, "Failed to evaluate array subscript for member access");
					return {};
				}
				
				// Extract type information from the evaluated array element
				// array_operands = [type, size_bits, temp_var, type_index (optional)]
				Type element_type = std::get<Type>(array_operands[0]);
				
				// Get the type_index if available (for struct elements)
				size_t element_type_index = 0;
				if (array_operands.size() >= 4 && std::holds_alternative<unsigned long long>(array_operands[3])) {
					element_type_index = static_cast<size_t>(std::get<unsigned long long>(array_operands[3]));
				}
				
				// The array element should be stored in a temp var
				if (std::holds_alternative<TempVar>(array_operands[2])) {
					base_object = std::get<TempVar>(array_operands[2]);
				} else {
					FLASH_LOG(Codegen, Error, "Array subscript result has unsupported value type");
					return {};
				}
				
				base_type = element_type;
				base_type_index = element_type_index;
			}
			else {
				FLASH_LOG(Codegen, Error, "member access on unsupported expression type");
				return {};
			}
		}
		else if (object_node.is<IdentifierNode>()) {
			const IdentifierNode& object_ident = object_node.as<IdentifierNode>();
			std::string_view object_name = object_ident.name();
			
			bool handled = false;
			
			// Special handling for 'this' in lambdas with [*this] capture
			if (object_name == "this") {
				if (auto copy_this_temp = emitLoadCopyThis(memberAccessNode.member_token())) {
					base_object = *copy_this_temp;
					base_type = Type::Struct;
					base_type_index = current_lambda_context_.enclosing_struct_type_index;
					handled = true;
				}
			}
			
			if (!handled) {
				if (!validateAndSetupIdentifierMemberAccess(object_name, base_object, base_type, base_type_index, is_pointer_dereference)) {
					return {};
				}
			}
		}
		else if (!base_setup_complete) {
			std::cerr << "error: member access on unsupported object type\n";
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
			if (std::holds_alternative<StringHandle>(base_object)) {
				std::cerr << "  Object name: " << std::get<StringHandle>(base_object) << "\n";
			}
			std::cerr << "  Available struct types in gTypeInfo:\n";
			for (const auto& ti : gTypeInfo) {
				if (ti.type_ == Type::Struct && ti.getStructInfo()) {
					std::cerr << "    - " << ti.name() << " (type_index=" << ti.type_index_ << ")\n";
				}
			}
			std::cerr << "  Available types in gTypesByName:\n";
			for (const auto& [name, ti] : gTypesByName) {
				if (ti->type_ == Type::Struct) {
					std::cerr << "    - " << name << " (type_index=" << ti->type_index_ << ")\n";
				}
			}
			std::cerr << "error: struct type info not found\n";
			return {};
		}

		const StructTypeInfo* struct_info = type_info->getStructInfo();
		
		// FIRST check if this is a static member (can be accessed via instance in C++)
		auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(StringTable::getOrInternStringHandle(member_name));
		if (static_member) {
			// This is a static member! Access it via GlobalLoad instead of MemberLoad
			// Static members are accessed using qualified names (OwnerClassName::memberName)
			// Use the owner_struct name, not the current struct, to get the correct qualified name
			StringBuilder qualified_name_sb;
			qualified_name_sb.append(StringTable::getStringView(owner_struct->getName()));
			qualified_name_sb.append("::"sv);
			qualified_name_sb.append(member_name);
			std::string_view qualified_name = qualified_name_sb.commit();
			
			FLASH_LOG(Codegen, Debug, "Static member access: ", member_name, " in struct ", type_info->name(), " owned by ", owner_struct->getName(), " -> qualified_name: ", qualified_name);
			
			// Create a temporary variable for the result
			TempVar result_var = var_counter.next();
			
			// Build GlobalLoadOp for the static member
			GlobalLoadOp global_load;
			global_load.result.value = result_var;
			global_load.result.type = static_member->type;
			global_load.result.size_in_bits = static_cast<int>(static_member->size * 8);
			global_load.global_name = StringTable::getOrInternStringHandle(qualified_name);
			
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(global_load), Token()));
			
			// Return operands in the same format as regular members
			if (static_member->type == Type::Struct) {
				// Format: [type, size_bits, temp_var, type_index]
				return { static_member->type, static_cast<int>(static_member->size * 8), result_var, static_cast<unsigned long long>(static_member->type_index) };
			} else {
				// Format: [type, size_bits, temp_var]
				return { static_member->type, static_cast<int>(static_member->size * 8), result_var };
			}
		}
		
		// Use recursive lookup to find instance members in base classes as well
		const StructMember* member = struct_info->findMemberRecursive(StringTable::getOrInternStringHandle(member_name));

		if (!member) {
			std::cerr << "error: member '" << member_name << "' not found in struct '" << type_info->name() << "'\n";
			std::cerr << "  available members:\n";
			for (const auto& m : struct_info->members) {
				std::cerr << "    - " << StringTable::getStringView(m.getName()) << "\n";
			}
			return {};
		}

		// Check access control
		const StructTypeInfo* current_context = getCurrentStructContext();
		std::string_view current_function = getCurrentFunctionName();
		if (!checkMemberAccess(member, struct_info, current_context, nullptr, current_function)) {
			std::cerr << "Error: Cannot access ";
			if (member->access == AccessSpecifier::Private) {
				std::cerr << "private";
			} else if (member->access == AccessSpecifier::Protected) {
				std::cerr << "protected";
			}
			std::cerr << " member '" << member_name << "' of '" << StringTable::getStringView(struct_info->getName()) << "'";
			if (current_context) {
				std::cerr << " from '" << StringTable::getStringView(current_context->getName()) << "'";
			}
			std::cerr << "\n";
			return {};
		}

		// Check if base_object is a TempVar with lvalue metadata
		// If so, we can unwrap it to get the ultimate base and combine offsets
		// This optimization is ONLY applied in LValueAddress context (for stores)
		// In Load context, we keep the chain of member_access instructions
		int accumulated_offset = static_cast<int>(member->offset);
		std::variant<StringHandle, TempVar> ultimate_base = base_object;
		StringHandle ultimate_member_name = StringTable::getOrInternStringHandle(member_name);
		bool did_unwrap = false;
		
		if (context == ExpressionContext::LValueAddress && std::holds_alternative<TempVar>(base_object)) {
			TempVar base_temp = std::get<TempVar>(base_object);
			auto base_lvalue_info = getTempVarLValueInfo(base_temp);
			
			if (base_lvalue_info.has_value() && base_lvalue_info->kind == LValueInfo::Kind::Member) {
				// The base is itself a member access
				// Combine the offsets and use the ultimate base (LValueAddress context only)
				accumulated_offset += base_lvalue_info->offset;
				ultimate_base = base_lvalue_info->base;
				is_pointer_dereference = base_lvalue_info->is_pointer_to_member;
				// When unwrapping nested member access, use the first-level member name
				// For example: obj.inner.value -> use "inner" (member of obj), not "value"
				if (base_lvalue_info->member_name.has_value()) {
					ultimate_member_name = base_lvalue_info->member_name.value();
				}
				did_unwrap = true;
			}
		}

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();
		
		// Mark member access as lvalue (Option 2: Value Category Tracking)
		// obj.member is an lvalue - it designates a specific object member
		LValueInfo lvalue_info(
			LValueInfo::Kind::Member,
			did_unwrap ? ultimate_base : base_object,
			did_unwrap ? accumulated_offset : static_cast<int>(member->offset)
		);
		// Store member name for unified assignment handler
		lvalue_info.member_name = ultimate_member_name;
		lvalue_info.is_pointer_to_member = is_pointer_dereference;  // Mark if accessing through pointer
		setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

		// Build MemberLoadOp
		MemberLoadOp member_load;
		member_load.result.value = result_var;
		member_load.result.type = member->type;
		member_load.result.size_in_bits = static_cast<int>(member->size * 8);  // Convert bytes to bits

		// Add the base object
		// In LValueAddress context with unwrapping: use ultimate_base
		// In Load context: use immediate base_object
		if (did_unwrap) {
			if (std::holds_alternative<StringHandle>(ultimate_base)) {
				member_load.object = std::get<StringHandle>(ultimate_base);
			} else {
				member_load.object = std::get<TempVar>(ultimate_base);
			}
			member_load.member_name = ultimate_member_name;
			member_load.offset = accumulated_offset;
		} else {
			if (std::holds_alternative<StringHandle>(base_object)) {
				member_load.object = std::get<StringHandle>(base_object);
			} else {
				member_load.object = std::get<TempVar>(base_object);
			}
			member_load.member_name = StringTable::getOrInternStringHandle(member_name);
			member_load.offset = static_cast<int>(member->offset);
		}
	
		// Add reference metadata (required for proper handling of reference members)
		member_load.is_reference = member->is_reference;
		member_load.is_rvalue_reference = member->is_rvalue_reference;
		member_load.struct_type_info = nullptr;
		member_load.is_pointer_to_member = is_pointer_dereference;  // Mark if accessing through pointer

		// When context is LValueAddress, skip the load and return address/metadata only
		if (context == ExpressionContext::LValueAddress) {
			// Don't emit MemberAccess instruction (no load)
			// Just return the metadata with the result temp var
			// The metadata contains all information needed for store operations
			if (member->type == Type::Struct) {
				// Format: [type, size_bits, temp_var, type_index]
				return { member->type, static_cast<int>(member->size * 8), result_var, static_cast<unsigned long long>(member->type_index) };
			} else {
				// Format: [type, size_bits, temp_var]
				return { member->type, static_cast<int>(member->size * 8), result_var };
			}
		}

		// Add the member access instruction (Load context - default)
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

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

	// Helper function to calculate array size from a DeclarationNode
	// Returns the total size in bytes, or 0 if the array size cannot be determined
	std::optional<size_t> calculateArraySize(const DeclarationNode& decl) {
		if (!decl.is_array()) {
			return std::nullopt;
		}

		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
		size_t element_size = type_spec.size_in_bits() / 8;
		
		// For struct types, get size from gTypeInfo instead of size_in_bits()
		if (element_size == 0 && type_spec.type() == Type::Struct) {
			size_t type_index = type_spec.type_index();
			if (type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					element_size = struct_info->total_size;
				}
			}
		}
		
		if (element_size == 0) {
			return std::nullopt;
		}
		
		// Get array size - support multidimensional arrays
		const auto& dims = decl.array_dimensions();
		if (dims.empty()) {
			return std::nullopt;
		}

		// Evaluate all dimension size expressions and compute total element count
		size_t array_count = 1;
		ConstExpr::EvaluationContext ctx(symbol_table);
		
		for (const auto& dim_expr : dims) {
			auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
			if (!eval_result.success) {
				return std::nullopt;
			}
			
			long long dim_size = eval_result.as_int();
			if (dim_size <= 0) {
				return std::nullopt;
			}
			
			// Check for potential overflow in multiplication
			size_t dim_size_u = static_cast<size_t>(dim_size);
			if (array_count > SIZE_MAX / dim_size_u) {
				FLASH_LOG(Codegen, Warning, "Array dimension count calculation would overflow");
				return std::nullopt;
			}
			array_count *= dim_size_u;
		}
		
		// Check for potential overflow in multiplication with element size
		if (array_count > SIZE_MAX / element_size) {
			FLASH_LOG(Codegen, Warning, "Array size calculation would overflow: ", array_count, " * ", element_size);
			return std::nullopt;
		}
		
		return element_size * array_count;
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

			// Workaround for parser limitation: when sizeof(arr) is parsed where arr is an
			// array variable, the parser may incorrectly parse it as a type.
			// If size_in_bits is 0, try looking up the identifier in the symbol table.
			if (type_spec.size_in_bits() == 0 && type_spec.token().type() == Token::Type::Identifier) {
				std::string_view identifier = type_spec.token().value();
				
				// Look up the identifier in the symbol table
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier);
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier);
				}
				
				if (symbol.has_value()) {
					const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					if (decl) {
						auto array_size = calculateArraySize(*decl);
						if (array_size.has_value()) {
							// Return sizeof result for array
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(*array_size) };
						}
					}
				}
			}

			// Handle array types: sizeof(int[10]) 
			if (type_spec.is_array()) {
				size_t element_size = type_spec.size_in_bits() / 8;
				size_t array_count = 0;
				
				if (type_spec.array_size().has_value()) {
					array_count = *type_spec.array_size();
				}
				
				if (array_count > 0) {
					size_in_bytes = element_size * array_count;
				} else {
					size_in_bytes = element_size; // Fallback: just element size
				}
			}
			// Handle struct types
			else if (type == Type::Struct) {
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

			// Special handling for identifiers: sizeof(x) where x is a variable
			// This path handles cases where the parser correctly identifies x as an expression
			const ExpressionNode& expr = expr_node.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
				
				// Look up the identifier in the symbol table
				std::optional<ASTNode> symbol = symbol_table.lookup(id_node.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(id_node.name());
				}
				
				if (symbol.has_value()) {
					const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					if (decl) {
						// Check if it's an array
						auto array_size = calculateArraySize(*decl);
						if (array_size.has_value()) {
							// Return sizeof result for array
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(*array_size) };
						}
						
						// For regular variables, get the type size from the declaration
						const TypeSpecifierNode& var_type = decl->type_node().as<TypeSpecifierNode>();
						if (var_type.type() == Type::Struct) {
							size_t type_index = var_type.type_index();
							if (type_index < gTypeInfo.size()) {
								const TypeInfo& type_info = gTypeInfo[type_index];
								const StructTypeInfo* struct_info = type_info.getStructInfo();
								if (struct_info) {
									return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(struct_info->total_size) };
								}
							}
						} else {
							// Primitive type - use get_type_size_bits to handle cases where size_in_bits wasn't set
							int size_bits = var_type.size_in_bits();
							if (size_bits == 0) {
								size_bits = get_type_size_bits(var_type.type());
							}
							size_in_bytes = size_bits / 8;
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
						}
					}
				}
			}

			// Fall back to default expression handling
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

		// Safety check: if size_in_bytes is still 0, something went wrong
		// This shouldn't happen, but add a fallback just in case
		if (size_in_bytes == 0) {
			FLASH_LOG(Codegen, Warning, "sizeof returned 0, this indicates a bug in type size tracking");
		}

		// Return sizeof result as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
	}

	std::vector<IrOperand> generateAlignofIr(const AlignofExprNode& alignofNode) {
		size_t alignment = 0;

		if (alignofNode.is_type()) {
			// alignof(type)
			const ASTNode& type_node = alignofNode.type_or_expr();
			if (!type_node.is<TypeSpecifierNode>()) {
				assert(false && "alignof type argument must be TypeSpecifierNode");
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

				alignment = struct_info->alignment;
			}
			else {
				// For primitive types, use standard alignment calculation
				size_t size_in_bytes = type_spec.size_in_bits() / 8;
				alignment = calculate_alignment_from_size(size_in_bytes, type);
			}
		}
		else {
			// alignof(expression) - determine the alignment of the expression's type
			const ASTNode& expr_node = alignofNode.type_or_expr();
			if (!expr_node.is<ExpressionNode>()) {
				assert(false && "alignof expression argument must be ExpressionNode");
				return {};
			}

			// Special handling for identifiers: alignof(x) where x is a variable
			const ExpressionNode& expr = expr_node.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
				
				// Look up the identifier in the symbol table
				std::optional<ASTNode> symbol = symbol_table.lookup(id_node.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(id_node.name());
				}
				
				if (symbol.has_value()) {
					const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					if (decl) {
						// Get the type alignment from the declaration
						const TypeSpecifierNode& var_type = decl->type_node().as<TypeSpecifierNode>();
						if (var_type.type() == Type::Struct) {
							size_t type_index = var_type.type_index();
							if (type_index < gTypeInfo.size()) {
								const TypeInfo& type_info = gTypeInfo[type_index];
								const StructTypeInfo* struct_info = type_info.getStructInfo();
								if (struct_info) {
									return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(struct_info->alignment) };
								}
							}
						} else {
							// Primitive type - use get_type_size_bits to handle cases where size_in_bits wasn't set
							int size_bits = var_type.size_in_bits();
							if (size_bits == 0) {
								size_bits = get_type_size_bits(var_type.type());
							}
							size_t size_in_bytes = size_bits / 8;
							alignment = calculate_alignment_from_size(size_in_bytes, var_type.type());
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(alignment) };
						}
					}
				}
			}

			// Fall back to default expression handling
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
				assert(false && "alignof(struct_expression) not fully implemented yet");
				return {};
			}
			else {
				// For primitive types
				size_t size_in_bytes = size_in_bits / 8;
				alignment = calculate_alignment_from_size(size_in_bytes, expr_type);
			}
		}

		// Safety check: alignment should never be 0 for valid types
		assert(alignment != 0 && "alignof returned 0, this indicates a bug in type alignment tracking");

		// Return alignof result as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(alignment) };
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
		const StructMember* member = struct_info->findMemberRecursive(StringTable::getOrInternStringHandle(std::string(member_name)));
		if (!member) {
			assert(false && "Member not found in struct");
			return {};
		}

		// Return offset as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(member->offset) };
	}

	// Helper function to check if a type is a scalar type (arithmetic, enum, pointer, member pointer, nullptr_t)
	bool isScalarType(Type type, bool is_reference, size_t pointer_depth) const {
		if (is_reference) return false;
		if (pointer_depth > 0) return true;  // Pointers are scalar
		return (type == Type::Bool || type == Type::Char || type == Type::Short ||
		        type == Type::Int || type == Type::Long || type == Type::LongLong ||
		        type == Type::UnsignedChar || type == Type::UnsignedShort ||
		        type == Type::UnsignedInt || type == Type::UnsignedLong ||
		        type == Type::UnsignedLongLong || type == Type::Float ||
		        type == Type::Double || type == Type::LongDouble || type == Type::Enum ||
		        type == Type::Nullptr || type == Type::MemberObjectPointer ||
		        type == Type::MemberFunctionPointer);
	}

	bool isArithmeticType(Type type) const {
		// Branchless: arithmetic types are Bool(1) through LongDouble(14)
		// Using range check instead of multiple comparisons
		return (static_cast<int_fast16_t>(type) >= static_cast<int_fast16_t>(Type::Bool)) &
		       (static_cast<int_fast16_t>(type) <= static_cast<int_fast16_t>(Type::LongDouble));
	}

	bool isFundamentalType(Type type) const {
		// Branchless: fundamental types are Void(0), Nullptr(28), or arithmetic types Bool(1) through LongDouble(14)
		// Using bitwise OR of conditions for branchless evaluation
		return (type == Type::Void) | (type == Type::Nullptr) | isArithmeticType(type);
	}

	std::vector<IrOperand> generateTypeTraitIr(const TypeTraitExprNode& traitNode) {
		// Type traits evaluate to a compile-time boolean constant
		bool result = false;

		// Handle no-argument traits first (like __is_constant_evaluated)
		if (traitNode.is_no_arg_trait()) {
			switch (traitNode.kind()) {
				case TypeTraitKind::IsConstantEvaluated:
					// __is_constant_evaluated() - returns true if being evaluated at compile time
					// In runtime code, this always returns false
					// In constexpr context, this would return true
					// For now, return false (runtime context)
					result = false;
					break;
				default:
					result = false;
					break;
			}
			// Return result as a bool constant
			return { Type::Bool, 8, static_cast<unsigned long long>(result ? 1 : 0) };
		}

		// For traits that require type arguments, extract the type information
		const ASTNode& type_node = traitNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			assert(false && "Type trait argument must be TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		Type type = type_spec.type();
		bool is_reference = type_spec.is_reference();
		bool is_rvalue_reference = type_spec.is_rvalue_reference();
		size_t pointer_depth = type_spec.pointer_depth();

		switch (traitNode.kind()) {
			case TypeTraitKind::IsVoid:
				result = (type == Type::Void && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsNullptr:
				// nullptr_t type check
				result = (type == Type::Nullptr && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsIntegral:
				// Integral types: bool, char, short, int, long, long long and their unsigned variants
				result = (type == Type::Bool ||
				         type == Type::Char ||
				         type == Type::Short || type == Type::Int || type == Type::Long || type == Type::LongLong ||
				         type == Type::UnsignedChar || type == Type::UnsignedShort || type == Type::UnsignedInt ||
				         type == Type::UnsignedLong || type == Type::UnsignedLongLong)
				         && !is_reference && pointer_depth == 0;
				break;

			case TypeTraitKind::IsFloatingPoint:
				result = (type == Type::Float || type == Type::Double || type == Type::LongDouble)
				         && !is_reference && pointer_depth == 0;
				break;

			case TypeTraitKind::IsArray:
				// Array type checking - uses the is_array flag in TypeSpecifierNode
				result = type_spec.is_array() && !is_reference && pointer_depth == 0;
				break;

			case TypeTraitKind::IsPointer:
				result = (pointer_depth > 0) && !is_reference;
				break;

			case TypeTraitKind::IsLvalueReference:
				result = is_reference && !is_rvalue_reference;
				break;

			case TypeTraitKind::IsRvalueReference:
				result = is_rvalue_reference;
				break;

			case TypeTraitKind::IsMemberObjectPointer:
				// Member object pointer type (pointer to data member: int MyClass::*)
				result = (type == Type::MemberObjectPointer && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsMemberFunctionPointer:
				// Member function pointer type should not have any reference/pointer qualifiers
				result = (type == Type::MemberFunctionPointer && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsEnum:
				// Enum type should not have any reference/pointer qualifiers
				result = (type == Type::Enum && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnion:
				// Check if the type is a union using is_union field in StructTypeInfo
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					result = struct_info && struct_info->is_union && !is_reference && pointer_depth == 0;
				} else {
					result = false;
				}
				break;

			case TypeTraitKind::IsClass:
				// A class is a struct or class type that is NOT a union
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					result = struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0;
				} else {
					result = false;
				}
				break;

			case TypeTraitKind::IsFunction:
				// A function type (not a pointer to function, not a reference)
				result = (type == Type::Function && !is_reference && pointer_depth == 0);
				break;

			case TypeTraitKind::IsReference:
				// __is_reference - lvalue reference OR rvalue reference
				result = is_reference | is_rvalue_reference;
				break;

			case TypeTraitKind::IsArithmetic:
				// __is_arithmetic - integral or floating point
				result = isArithmeticType(type) & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsFundamental:
				// __is_fundamental - void, nullptr_t, arithmetic types
				result = isFundamentalType(type) & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsObject:
				// __is_object - not function, not reference, not void
				result = (type != Type::Function) & (type != Type::Void) & !is_reference & !is_rvalue_reference;
				break;

			case TypeTraitKind::IsScalar:
				// __is_scalar - arithmetic, pointer, enum, member pointer, or nullptr
				result = (isArithmeticType(type) |
			          (type == Type::Enum) | (type == Type::Nullptr) |
			          (type == Type::MemberObjectPointer) | (type == Type::MemberFunctionPointer) |
			          (pointer_depth > 0))
			          & !is_reference;
				break;

			case TypeTraitKind::IsCompound:
				// __is_compound - array, function, pointer, reference, class, union, enum, member pointer
				// Basically anything that's not fundamental
				// Branchless: use bitwise NOT and AND to avoid branching
				result = !(isFundamentalType(type) & !is_reference & (pointer_depth == 0));
				break;

			case TypeTraitKind::IsBaseOf:
				// __is_base_of(Base, Derived) - Check if Base is a base class of Derived
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& derived_spec = second_type_node.as<TypeSpecifierNode>();
						
						// Both types must be class types (not references, not pointers)
						if (type == Type::Struct && derived_spec.type() == Type::Struct &&
						    !is_reference && pointer_depth == 0 &&
						    !derived_spec.is_reference() && derived_spec.pointer_depth() == 0 &&
						    type_spec.type_index() < gTypeInfo.size() &&
						    derived_spec.type_index() < gTypeInfo.size()) {
							
							const TypeInfo& base_info = gTypeInfo[type_spec.type_index()];
							const TypeInfo& derived_info = gTypeInfo[derived_spec.type_index()];
							const StructTypeInfo* base_struct = base_info.getStructInfo();
							const StructTypeInfo* derived_struct = derived_info.getStructInfo();
							
							if (base_struct && derived_struct) {
								// Same type is considered base of itself
								if (type_spec.type_index() == derived_spec.type_index()) {
									result = true;
								} else {
									// Check if base_struct is in derived_struct's base classes
									for (const auto& base_class : derived_struct->base_classes) {
										if (base_class.type_index == type_spec.type_index()) {
											result = true;
											break;
										}
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsSame:
				// __is_same(T, U) - Check if T and U are the same type (exactly the same)
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& second_spec = second_type_node.as<TypeSpecifierNode>();
						
						// Check if all properties match exactly
						result = (type == second_spec.type() &&
						         is_reference == second_spec.is_reference() &&
						         is_rvalue_reference == second_spec.is_rvalue_reference() &&
						         pointer_depth == second_spec.pointer_depth() &&
						         type_spec.type_index() == second_spec.type_index() &&
						         type_spec.is_array() == second_spec.is_array() &&
						         type_spec.is_const() == second_spec.is_const() &&
						         type_spec.is_volatile() == second_spec.is_volatile());
					}
				}
				break;

			case TypeTraitKind::IsConvertible:
				// __is_convertible(From, To) - Check if From can be converted to To
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& to_spec = second_type_node.as<TypeSpecifierNode>();
						const TypeSpecifierNode& from_spec = type_spec;
						
						Type from_type = from_spec.type();
						Type to_type = to_spec.type();
						bool from_is_ref = from_spec.is_reference();
						bool to_is_ref = to_spec.is_reference();
						size_t from_ptr_depth = from_spec.pointer_depth();
						size_t to_ptr_depth = to_spec.pointer_depth();
						
						// Same type is always convertible
						if (from_type == to_type && from_is_ref == to_is_ref && 
						    from_ptr_depth == to_ptr_depth && 
						    from_spec.type_index() == to_spec.type_index()) {
							result = true;
						}
						// Arithmetic types are generally convertible to each other
						else if (isArithmeticType(from_type) && isArithmeticType(to_type) &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0) {
							result = true;
						}
						// Pointers with same depth and compatible types
						else if (from_ptr_depth > 0 && to_ptr_depth > 0 && 
						         from_ptr_depth == to_ptr_depth && !from_is_ref && !to_is_ref) {
							// Pointer convertibility (same type or derived-to-base)
							result = (from_type == to_type || from_spec.type_index() == to_spec.type_index());
						}
						// nullptr_t is convertible to any pointer type
						else if (from_type == Type::Nullptr && to_ptr_depth > 0 && !to_is_ref) {
							result = true;
						}
						// Derived to base conversion for class types
						else if (from_type == Type::Struct && to_type == Type::Struct &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0 &&
						         from_spec.type_index() < gTypeInfo.size() &&
						         to_spec.type_index() < gTypeInfo.size()) {
							// Check if from_type is derived from to_type
							const TypeInfo& from_info = gTypeInfo[from_spec.type_index()];
							const StructTypeInfo* from_struct = from_info.getStructInfo();
							if (from_struct) {
								for (const auto& base_class : from_struct->base_classes) {
									if (base_class.type_index == to_spec.type_index()) {
										result = true;
										break;
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsPolymorphic:
				// A polymorphic class has at least one virtual function
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					result = struct_info && struct_info->has_vtable;
				}
				break;

			case TypeTraitKind::IsFinal:
				// A final class cannot be derived from
				// Note: This requires tracking 'final' keyword on classes
				// For now, check if any member function is marked final
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Check if any virtual function is marked final
						for (const auto& func : struct_info->member_functions) {
							if (func.is_final) {
								result = true;
								break;
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsAbstract:
				// An abstract class has at least one pure virtual function
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					result = struct_info && struct_info->is_abstract;
				}
				break;

			case TypeTraitKind::IsEmpty:
				// An empty class has no non-static data members (excluding empty base classes)
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Check if there are no non-static data members
						// and no virtual functions (vtable pointer would be a member)
						result = struct_info->members.empty() && !struct_info->has_vtable;
					}
				}
				break;

			case TypeTraitKind::IsAggregate:
				// An aggregate is:
				// - An array type, or
				// - A class type (struct/class/union) with:
				//   - No user-declared or inherited constructors
				//   - No private or protected non-static data members
				//   - No virtual functions
				//   - No virtual, private, or protected base classes
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Check aggregate conditions:
						// 1. No user-declared constructors (check member_functions for non-implicit constructors)
						// 2. No private or protected members (all members are public)
						// 3. No virtual functions (has_vtable flag)
						bool has_user_constructors = false;
						for (const auto& func : struct_info->member_functions) {
							if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
								const ConstructorDeclarationNode& ctor = func.function_decl.as<ConstructorDeclarationNode>();
								if (!ctor.is_implicit()) {
									has_user_constructors = true;
									break;
								}
							}
						}
						
						bool no_virtual = !struct_info->has_vtable;
						bool all_public = true;
						
						for (const auto& member : struct_info->members) {
							if (member.access == AccessSpecifier::Private || 
							    member.access == AccessSpecifier::Protected) {
								all_public = false;
								break;
							}
						}
						
						result = !has_user_constructors && no_virtual && all_public;
					}
				}
				// Arrays are aggregates
				else if (pointer_depth == 0 && !is_reference && type_spec.is_array()) {
					result = true;
				}
				break;

			case TypeTraitKind::IsStandardLayout:
				// A standard-layout class has specific requirements:
				// - No virtual functions or virtual base classes
				// - All non-static data members have same access control
				// - No base classes with non-static data members
				// - No base classes of the same type as first non-static data member
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Basic check: no virtual functions
						result = !struct_info->has_vtable;
						// If all members have the same access specifier, it's a simple standard layout
						if (result && struct_info->members.size() > 1) {
							AccessSpecifier first_access = struct_info->members[0].access;
							for (const auto& member : struct_info->members) {
								if (member.access != first_access) {
									result = false;
									break;
								}
							}
						}
					}
				}
				// Scalar types are standard layout
				else if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				break;

			case TypeTraitKind::HasUniqueObjectRepresentations:
				// Types with no padding bits have unique object representations
				// Integral types (except bool), and trivially copyable types without padding
				if ((type == Type::Char || type == Type::Short || type == Type::Int ||
				     type == Type::Long || type == Type::LongLong || type == Type::UnsignedChar ||
				     type == Type::UnsignedShort || type == Type::UnsignedInt ||
				     type == Type::UnsignedLong || type == Type::UnsignedLongLong)
				    && !is_reference && pointer_depth == 0) {
					result = true;
				}
				// Note: float/double may have padding or non-unique representations
				break;

			case TypeTraitKind::IsTriviallyCopyable:
				// A trivially copyable type can be copied with memcpy
				// - Scalar types (arithmetic, pointers, enums)
				// - Classes with trivial copy/move constructors and destructors, no virtual
				// TODO: Implement proper checking of copy/move constructors and assignment operators
				//       for full C++ standard compliance
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Classes: need to check for trivial special members and no virtual
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simple heuristic: no virtual functions means likely trivially copyable
						// TODO: A more complete check would verify copy/move ctors are trivial
						result = !struct_info->has_vtable;
					}
				}
				break;

			case TypeTraitKind::IsTrivial:
				// A trivial type is trivially copyable and has a trivial default constructor
				// TODO: Full compliance requires checking that:
				//       - Has trivial default constructor
				//       - Has trivial copy constructor
				//       - Has trivial move constructor
				//       - Has trivial copy assignment operator
				//       - Has trivial move assignment operator
				//       - Has trivial destructor
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simple heuristic: no virtual functions and no user-defined constructors
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsPod:
				// POD (Plain Old Data) = trivial + standard layout (C++03 compatible)
				// In C++11+, this is deprecated but still useful
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// POD: no virtual functions, no user-defined ctors, all members same access
						bool is_pod = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
						if (is_pod && struct_info->members.size() > 1) {
							AccessSpecifier first_access = struct_info->members[0].access;
							for (const auto& member : struct_info->members) {
								if (member.access != first_access) {
									is_pod = false;
									break;
								}
							}
						}
						result = is_pod;
					}
				}
				break;

			case TypeTraitKind::IsLiteralType:
				// __is_literal_type - deprecated in C++17, removed in C++20
				FLASH_LOG(Codegen, Warning, "__is_literal_type is deprecated in C++17 and removed in C++20. "
				          "This trait is likely being invoked from a standard library header (e.g., <type_traits>) "
				          "that hasn't been fully updated for C++20. In modern C++, use std::is_constant_evaluated() "
				          "to check for compile-time contexts, or use other appropriate type traits.");
				// A literal type is one that can be used in constexpr context:
				// - Scalar types
				// - References
				// - Arrays of literal types
				// - Class types that have all of:
				//   - Trivial destructor
				//   - Aggregate type OR has at least one constexpr constructor
				//   - All non-static data members are literal types
				if (isScalarType(type, is_reference, pointer_depth) || is_reference) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simplified check: assume literal if trivially copyable
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsConst:
				// __is_const - checks if type has const qualifier
				result = type_spec.is_const();
				break;

			case TypeTraitKind::IsVolatile:
				// __is_volatile - checks if type has volatile qualifier
				result = type_spec.is_volatile();
				break;

			case TypeTraitKind::IsSigned:
				// __is_signed - checks if integral type is signed
				result = ((type == Type::Char) |  // char is signed on most platforms
			          (type == Type::Short) | (type == Type::Int) |
			          (type == Type::Long) | (type == Type::LongLong))
			          & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnsigned:
				// __is_unsigned - checks if integral type is unsigned
				result = ((type == Type::Bool) |  // bool is considered unsigned
			          (type == Type::UnsignedChar) | (type == Type::UnsignedShort) |
			          (type == Type::UnsignedInt) | (type == Type::UnsignedLong) |
			          (type == Type::UnsignedLongLong))
			          & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsBoundedArray:
				// __is_bounded_array - array with known bound (e.g., int[10])
				// Check if it's an array and the size is known
				result = type_spec.is_array() & int(type_spec.array_size() > 0) &
			         !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnboundedArray:
				// __is_unbounded_array - array with unknown bound (e.g., int[])
				// Check if it's an array and the size is unknown (0 or negative)
				result = type_spec.is_array() & int(type_spec.array_size() <= 0) &
			         !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsConstructible:
				// __is_constructible(T, Args...) - Check if T can be constructed with Args...
				// For scalar types, default constructible (no args) or copy constructible (same type arg)
				if (isScalarType(type, is_reference, pointer_depth)) {
					const auto& arg_types = traitNode.additional_type_nodes();
					if (arg_types.empty()) {
						// Default constructible - all scalars are default constructible
						result = true;
					} else if (arg_types.size() == 1 && arg_types[0].is<TypeSpecifierNode>()) {
						// Copy/conversion construction - check if types are compatible
						const TypeSpecifierNode& arg_spec = arg_types[0].as<TypeSpecifierNode>();
						// Same type or convertible arithmetic types
						result = (arg_spec.type() == type) || 
						         (isScalarType(arg_spec.type(), arg_spec.is_reference(), arg_spec.pointer_depth()) &&
						          !arg_spec.is_reference() && arg_spec.pointer_depth() == 0);
					}
				}
				// Class types: check for appropriate constructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						const auto& arg_types = traitNode.additional_type_nodes();
						if (arg_types.empty()) {
							// Default constructible - has default constructor or no user-defined ctors
							result = !struct_info->hasUserDefinedConstructor() || struct_info->hasConstructor();
						} else {
							// Check for matching constructor
							// Simple heuristic: if it has any user-defined constructor, assume constructible
							result = struct_info->hasUserDefinedConstructor();
						}
					}
				}
				break;

			case TypeTraitKind::IsTriviallyConstructible:
				// __is_trivially_constructible(T, Args...) - Check if T can be trivially constructed
				// Scalar types are trivially constructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: no virtual, no user-defined ctors
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsNothrowConstructible:
				// __is_nothrow_constructible(T, Args...) - Check if T can be constructed without throwing
				// Scalar types don't throw
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: similar to trivially constructible for now
				// TODO: Check for noexcept constructors
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsAssignable:
				// __is_assignable(To, From) - Check if From can be assigned to To
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// For scalar types, check type compatibility
						if (isScalarType(type, is_reference, pointer_depth)) {
							// Scalars are assignable from compatible types
							result = isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth());
						}
						// Class types: check for assignment operator
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								// If has copy/move assignment or no user-defined, assume assignable
								result = struct_info->hasCopyAssignmentOperator() || 
								         struct_info->hasMoveAssignmentOperator() ||
								         !struct_info->hasUserDefinedConstructor();
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsTriviallyAssignable:
				// __is_trivially_assignable(To, From) - Check if From can be trivially assigned to To
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// Scalar types are trivially assignable
						if (isScalarType(type, is_reference, pointer_depth) &&
						    isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth())) {
							result = true;
						}
						// Class types: no virtual, no user-defined assignment
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
						         !is_reference && pointer_depth == 0) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								result = !struct_info->has_vtable && 
								         !struct_info->hasCopyAssignmentOperator() && 
								         !struct_info->hasMoveAssignmentOperator();
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsNothrowAssignable:
				// __is_nothrow_assignable(To, From) - Check if From can be assigned without throwing
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// Scalar types don't throw on assignment
						if (isScalarType(type, is_reference, pointer_depth) &&
						    isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth())) {
							result = true;
						}
						// Class types: similar to trivially assignable for now
						// TODO: Check for noexcept assignment operators
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
						         !is_reference && pointer_depth == 0) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								result = !struct_info->has_vtable;
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsDestructible:
				// __is_destructible(T) - Check if T can be destroyed
				// All scalar types are destructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: check for accessible destructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Assume destructible unless we can prove otherwise
						// (no deleted destructor check available yet)
						result = true;
					}
				}
				break;

			case TypeTraitKind::IsTriviallyDestructible:
				// __is_trivially_destructible(T) - Check if T can be trivially destroyed
				// Scalar types are trivially destructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: no virtual, no user-defined destructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Trivially destructible if no vtable and no user-defined destructor
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedDestructor();
					} else if (struct_info && struct_info->is_union) {
						// Unions are trivially destructible if all members are
						result = true;
					}
				}
				break;

			case TypeTraitKind::IsNothrowDestructible:
				// __is_nothrow_destructible(T) - Check if T can be destroyed without throwing
				// Scalar types don't throw on destruction
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: assume noexcept destructor (most are in practice)
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Most destructors are noexcept by default since C++11
						result = true;
					}
				}
				break;

			case TypeTraitKind::IsLayoutCompatible:
				// __is_layout_compatible(T, U) - Check if T and U have the same layout
				if (traitNode.has_second_type()) {
					const ASTNode& second_node = traitNode.second_type_node();
					if (second_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& second_spec = second_node.as<TypeSpecifierNode>();
						
						// Same type is always layout compatible with itself
						if (type == second_spec.type() && 
						    pointer_depth == second_spec.pointer_depth() &&
						    is_reference == second_spec.is_reference()) {
							if (type == Type::Struct) {
								result = (type_spec.type_index() == second_spec.type_index());
							} else {
								result = true;
							}
						}
						// Different standard layout types with same size
						else if (isScalarType(type, is_reference, pointer_depth) &&
						         isScalarType(second_spec.type(), second_spec.is_reference(), second_spec.pointer_depth())) {
							result = (type_spec.size_in_bits() == second_spec.size_in_bits());
						}
					}
				}
				break;

			case TypeTraitKind::IsPointerInterconvertibleBaseOf:
				// __is_pointer_interconvertible_base_of(Base, Derived)
				// Check if Base is pointer-interconvertible with Derived
				// According to C++20: requires both to be standard-layout types and
				// Base is either the first base class or shares address with Derived
				if (traitNode.has_second_type()) {
					const ASTNode& derived_node = traitNode.second_type_node();
					if (derived_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& derived_spec = derived_node.as<TypeSpecifierNode>();
						
						// Both must be class types (not references, not pointers)
						if (type == Type::Struct && derived_spec.type() == Type::Struct &&
						    !is_reference && pointer_depth == 0 &&
						    !derived_spec.is_reference() && derived_spec.pointer_depth() == 0 &&
						    type_spec.type_index() < gTypeInfo.size() &&
						    derived_spec.type_index() < gTypeInfo.size()) {
							
							const TypeInfo& base_info = gTypeInfo[type_spec.type_index()];
							const TypeInfo& derived_info = gTypeInfo[derived_spec.type_index()];
							const StructTypeInfo* base_struct = base_info.getStructInfo();
							const StructTypeInfo* derived_struct = derived_info.getStructInfo();
							
							if (base_struct && derived_struct) {
								// Same type is pointer interconvertible with itself
								if (type_spec.type_index() == derived_spec.type_index()) {
									result = true;
								} else {
									// Both types must be standard-layout for pointer interconvertibility
									bool base_is_standard_layout = base_struct->isStandardLayout();
									bool derived_is_standard_layout = derived_struct->isStandardLayout();
									
									if (base_is_standard_layout && derived_is_standard_layout) {
										// Check if Base is the first base class at offset 0
										for (size_t i = 0; i < derived_struct->base_classes.size(); ++i) {
											if (derived_struct->base_classes[i].type_index == type_spec.type_index()) {
												// First base class at offset 0 is pointer interconvertible
												result = (i == 0);
												break;
											}
										}
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::UnderlyingType:
				// __underlying_type(T) returns the underlying type of an enum
				// This is a type query, not a bool result - handle specially
				if (type == Type::Enum && !is_reference && pointer_depth == 0 &&
				    type_spec.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const EnumTypeInfo* enum_info = type_info.getEnumInfo();
					if (enum_info) {
						// Return the enum's declared underlying type
						return { enum_info->underlying_type, enum_info->underlying_size, 0ULL };
					}
					// Fallback to int if no enum info
					return { Type::Int, 32, 0ULL };
				}
				// For non-enums, this is an error - return false/0
				result = false;
				break;

			default:
				result = false;
				break;
		}

		// Return result as a bool constant
		// Format: [type, size_bits, value]
		return { Type::Bool, 8, static_cast<unsigned long long>(result ? 1 : 0) };
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
			} else if (std::holds_alternative<StringHandle>(address_operands[2])) {
				op.address = std::get<StringHandle>(address_operands[2]);
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
							std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name() << "'\n";
							assert(false && "Cannot instantiate abstract class");
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
			} else if (std::holds_alternative<StringHandle>(size_operands[2])) {
				op.count = std::get<StringHandle>(size_operands[2]);
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
							std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name() << "'\n";
							assert(false && "Cannot instantiate abstract class");
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
		}
		
		// Return pointer to allocated memory
		// The result is a pointer, so we return it with pointer_depth + 1
		return { type, size_in_bits, result_var, 0ULL };
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
		} else if (std::holds_alternative<StringHandle>(ptr_operands[2])) {
			ptr_value = std::get<StringHandle>(ptr_operands[2]);
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

	// Helper function to extract base operand from expression operands
	std::variant<StringHandle, TempVar> extractBaseOperand(
		const std::vector<IrOperand>& expr_operands,
		TempVar fallback_var,
		const char* cast_name = "cast") {
		
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

	// Helper function to mark reference with appropriate value category metadata
	void markReferenceMetadata(
		const std::vector<IrOperand>& expr_operands,
		TempVar result_var,
		Type target_type,
		int target_size,
		bool is_rvalue_ref,
		const char* cast_name = "cast") {
		
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

	// Helper function to generate AddressOf operation for reference casts
	void generateAddressOfForReference(
		const std::variant<StringHandle, TempVar>& base,
		TempVar result_var,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast") {
		
		if (std::holds_alternative<StringHandle>(base)) {
			AddressOfOp addr_op;
			addr_op.result = result_var;
			addr_op.operand.type = target_type;
			addr_op.operand.size_in_bits = target_size;
			addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
			addr_op.operand.value = std::get<StringHandle>(base);
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), token));
		} else {
			FLASH_LOG_FORMAT(Codegen, Debug, "{}: source is TempVar (stack location), using directly", cast_name);
		}
	}

	// Helper function to handle rvalue reference casts (produces xvalue)
	std::vector<IrOperand> handleRValueReferenceCast(
		const std::vector<IrOperand>& expr_operands,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast") {
		
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

	// Helper function to handle lvalue reference casts (produces lvalue)
	std::vector<IrOperand> handleLValueReferenceCast(
		const std::vector<IrOperand>& expr_operands,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast") {
		
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

	std::vector<IrOperand> generateStaticCastIr(const StaticCastNode& staticCastNode) {
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
					assert(false && "Couldn't match IrValue to a known type");
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
					assert(false && "Couldn't match IrValue to a known type");
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
					assert(false && "Couldn't match IrValue to a known type");
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

		// For numeric conversions, we might need to generate a conversion instruction
		// For now, just change the type metadata (works for most cases)
		return { target_type, target_size, expr_operands[2], 0ULL };
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

		// Mark value category for reference types
		if (target_type_node.is_rvalue_reference()) {
			markReferenceMetadata(expr_operands, result_temp, result_type, result_size, true, "dynamic_cast");
		} else if (target_type_node.is_lvalue_reference()) {
			markReferenceMetadata(expr_operands, result_temp, result_type, result_size, false, "dynamic_cast");
		}

		// Return the casted pointer/reference
		return { result_type, result_size, result_temp, 0ULL };
	}

	std::vector<IrOperand> generateConstCastIr(const ConstCastNode& constCastNode) {
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

	std::vector<IrOperand> generateReinterpretCastIr(const ReinterpretCastNode& reinterpretCastNode) {
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

	std::vector<IrOperand> generateLambdaExpressionIr(const LambdaExpressionNode& lambda, std::string_view target_var_name = "") {
		// Collect lambda information for deferred generation
		// Following Clang's approach: generate closure class, operator(), __invoke, and conversion operator
		// If target_var_name is provided, use it as the closure variable name (for variable declarations)
		// Otherwise, create a temporary __closure_N variable

		LambdaInfo info;
		info.lambda_id = lambda.lambda_id();
		
		// Use StringBuilder to create persistent string_views for lambda names
		// This ensures the names remain valid after LambdaInfo is moved
		info.closure_type_name = StringBuilder()
			.append("__lambda_")
			.append(static_cast<int64_t>(lambda.lambda_id()))
			.commit();

		// Build operator_call_name: closure_type_name + "_operator_call"
		info.operator_call_name = StringBuilder()
			.append(info.closure_type_name)
			.append("_operator_call")
			.commit();

		// Build invoke_name: closure_type_name + "_invoke"
		info.invoke_name = StringBuilder()
			.append(info.closure_type_name)
			.append("_invoke")
			.commit();

		// Build conversion_op_name: closure_type_name + "_conversion"
		info.conversion_op_name = StringBuilder()
			.append(info.closure_type_name)
			.append("_conversion")
			.commit();

		info.lambda_token = lambda.lambda_token();
		
		// Store enclosing struct info for [this] capture support
		info.enclosing_struct_name = current_struct_name_.isValid() ? StringTable::getStringView(current_struct_name_) : std::string_view();
		if (current_struct_name_.isValid()) {
			auto type_it = gTypesByName.find(current_struct_name_);
			if (type_it != gTypesByName.end()) {
				info.enclosing_struct_type_index = type_it->second->type_index_;
			}
		}

		// Copy lambda body and captures (we need them later)
		info.lambda_body = lambda.body();
		info.captures = lambda.captures();
		info.is_mutable = lambda.is_mutable();

		// Collect captured variable declarations from current scope
		for (const auto& capture : lambda.captures()) {
			if (capture.is_capture_all()) {
				// Capture-all ([=] or [&]) should have been expanded by the parser into explicit captures
				// If we see one here, it means the parser didn't expand it (shouldn't happen)
				continue;
			}
			
			// Skip [this] and [*this] captures as they don't have an identifier to look up
			if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
			    capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
				continue;
			}

			// Skip init-captures: [x = expr] defines a new variable, doesn't capture existing one
			if (capture.has_initializer()) {
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
		info.return_type_index = 0;
		info.returns_reference = false;
		if (lambda.return_type().has_value()) {
			const auto& ret_type_node = lambda.return_type()->as<TypeSpecifierNode>();
			info.return_type = ret_type_node.type();
			info.return_size = ret_type_node.size_in_bits();
			info.return_type_index = ret_type_node.type_index();
			info.returns_reference = ret_type_node.is_reference();
			// If returning a reference, the size should be 64 bits (pointer size)
			if (info.returns_reference) {
				info.return_size = 64;
			}
		}

		// Collect parameters and detect generic lambda (auto parameters)
		size_t param_index = 0;
		for (const auto& param : lambda.parameters()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				// Detect auto parameters (generic lambda)
				if (param_type.type() == Type::Auto) {
					info.is_generic = true;
					info.auto_param_indices.push_back(param_index);
				}
				
				info.parameters.emplace_back(
					param_type.type(),
					param_type.size_in_bits(),
					static_cast<int>(param_type.pointer_levels().size()),
					std::string(param_decl.identifier_token().value())
				);
				// Also store the actual parameter node for symbol table
				info.parameter_nodes.push_back(param);
			}
			param_index++;
		}

		// Look up the closure type (registered during parsing) BEFORE moving info
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(info.closure_type_name));
		if (type_it == gTypesByName.end()) {
			// Error: closure type not found
			TempVar dummy = var_counter.next();
			return {Type::Int, 32, dummy};
		}

		const TypeInfo* closure_type = type_it->second;

		// Store lambda info for later generation (after we've used closure_type_name)
		collected_lambdas_.push_back(std::move(info));
		const LambdaInfo& lambda_info = collected_lambdas_.back();

		// Use target variable name if provided, otherwise create a temporary closure variable
		std::string_view closure_var_name;
		if (!target_var_name.empty()) {
			// Use the target variable name directly
			// We MUST emit VariableDecl here before any MemberStore operations
			closure_var_name = target_var_name;
			
			// Declare the closure variable with the target name
			VariableDeclOp lambda_decl_op;
			lambda_decl_op.type = Type::Struct;
			lambda_decl_op.size_in_bits = static_cast<int>(closure_type->getStructInfo()->total_size * 8);
			lambda_decl_op.var_name = StringTable::getOrInternStringHandle(closure_var_name);
			lambda_decl_op.custom_alignment = 0;
			lambda_decl_op.is_reference = false;
			lambda_decl_op.is_rvalue_reference = false;
			lambda_decl_op.is_array = false;
			ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(lambda_decl_op), lambda.lambda_token()));
		} else {
			// Create a temporary closure variable name
			closure_var_name = StringBuilder()
				.append("__closure_")
				.append(static_cast<int64_t>(lambda_info.lambda_id))
				.commit();

			// Declare the closure variable
			VariableDeclOp lambda_decl_op;
			lambda_decl_op.type = Type::Struct;
			lambda_decl_op.size_in_bits = static_cast<int>(closure_type->getStructInfo()->total_size * 8);
			lambda_decl_op.var_name = StringTable::getOrInternStringHandle(closure_var_name);
			lambda_decl_op.custom_alignment = 0;
			lambda_decl_op.is_reference = false;
			lambda_decl_op.is_rvalue_reference = false;
			lambda_decl_op.is_array = false;
			ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(lambda_decl_op), lambda.lambda_token()));
		}

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
					
					// Handle [this] capture - stores pointer to enclosing object
					if (capture.kind() == LambdaCaptureNode::CaptureKind::This) {
						const StructMember* member = struct_info->findMember("__this");
						if (member) {
							// Store the enclosing 'this' pointer in the closure
							// Use the 'this' variable name to properly resolve to the member function's this parameter
							MemberStoreOp store_this;
							store_this.value.type = Type::Void;
							store_this.value.size_in_bits = 64;
							store_this.value.value = StringTable::getOrInternStringHandle("this");
							store_this.object = StringTable::getOrInternStringHandle(closure_var_name);
							store_this.member_name = StringTable::getOrInternStringHandle("__this");
							store_this.offset = static_cast<int>(member->offset);
							store_this.is_reference = false;
							store_this.is_rvalue_reference = false;
							store_this.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_this), lambda.lambda_token()));
						}
						continue;
					}
					
					// Handle [*this] capture - stores copy of entire enclosing object
					if (capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
						// For [*this], we need to copy the entire object into the closure
						// The closure should have a member named "__copy_this" of the enclosing struct type
						const StructMember* member = struct_info->findMember("__copy_this");
						if (member && lambda_info.enclosing_struct_type_index > 0) {
							// Copy each member of the enclosing struct into __copy_this
							const TypeInfo* enclosing_type = nullptr;
							for (const auto& ti : gTypeInfo) {
								if (ti.type_index_ == lambda_info.enclosing_struct_type_index) {
									enclosing_type = &ti;
									break;
								}
							}
							if (enclosing_type && enclosing_type->getStructInfo()) {
								const StructTypeInfo* enclosing_struct = enclosing_type->getStructInfo();
								int copy_base_offset = static_cast<int>(member->offset);

								for (const auto& enclosing_member : enclosing_struct->members) {
									// Load from original 'this'
									TempVar loaded_value = var_counter.next();
									MemberLoadOp load_op;
									load_op.result.value = loaded_value;
									load_op.result.type = enclosing_member.type;
									load_op.result.size_in_bits = static_cast<int>(enclosing_member.size * 8);
									load_op.object = StringTable::getOrInternStringHandle("this");
									load_op.member_name = enclosing_member.getName();
									load_op.offset = static_cast<int>(enclosing_member.offset);
									load_op.is_reference = enclosing_member.is_reference;
									load_op.is_rvalue_reference = enclosing_member.is_rvalue_reference;
									load_op.struct_type_info = nullptr;
									ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), lambda.lambda_token()));

									// Store into closure->__copy_this at the appropriate offset
									MemberStoreOp store_copy_this;
									store_copy_this.value.type = enclosing_member.type;
									store_copy_this.value.size_in_bits = static_cast<int>(enclosing_member.size * 8);
									store_copy_this.value.value = loaded_value;
									store_copy_this.object = StringTable::getOrInternStringHandle(closure_var_name);
									store_copy_this.member_name = enclosing_member.getName();
									store_copy_this.offset = copy_base_offset + static_cast<int>(enclosing_member.offset);
									store_copy_this.is_reference = enclosing_member.is_reference;
									store_copy_this.is_rvalue_reference = enclosing_member.is_rvalue_reference;
									store_copy_this.struct_type_info = nullptr;
									ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_copy_this), lambda.lambda_token()));
								}
							}
						}
						continue;
					}

					std::string_view var_name = capture.identifier_name();  // Already a persistent string_view from AST
					StringHandle var_name_str = StringTable::getOrInternStringHandle(var_name);  // Single conversion for both uses below
					const StructMember* member = struct_info->findMember(var_name);

					if (member && (capture.has_initializer() || capture_index < lambda_info.captured_var_decls.size())) {
						// Check if this variable is a captured variable from an enclosing lambda
						bool is_captured_from_enclosing = current_lambda_context_.isActive() &&
						                                   current_lambda_context_.captures.count(var_name_str) > 0;

						// Handle init-captures
						if (capture.has_initializer()) {
							// Init-capture: evaluate the initializer expression and store it
							const ASTNode& init_node = *capture.initializer();
							auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
							
							if (init_operands.size() < 3) {
								capture_index++;
								continue;
							}
							
							// visitExpressionNode returns {type, size, value, ...}
							// The actual value is at index 2
							IrOperand init_value = init_operands[2];
							
							// For init-capture by reference [&y = x], we need to store the address of x
							if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
								// Get the type info from the init operands
								Type init_type = Type::Int;
								int init_size = 32;
								if (init_operands.size() > 0 && std::holds_alternative<Type>(init_operands[0])) {
									init_type = std::get<Type>(init_operands[0]);
								}
								if (init_operands.size() > 1) {
									if (std::holds_alternative<int>(init_operands[1])) {
										init_size = std::get<int>(init_operands[1]);
									} else if (std::holds_alternative<unsigned long long>(init_operands[1])) {
										init_size = static_cast<int>(std::get<unsigned long long>(init_operands[1]));
									}
								}
								
								// Generate AddressOf for the initializer
								TempVar addr_temp = var_counter.next();
								AddressOfOp addr_op;
								addr_op.result = addr_temp;
								addr_op.operand.type = init_type;
								addr_op.operand.size_in_bits = init_size;
								addr_op.operand.pointer_depth = 0;
								
								if (std::holds_alternative<StringHandle>(init_value)) {
									addr_op.operand.value = std::get<StringHandle>(init_value);
								} else if (std::holds_alternative<TempVar>(init_value)) {
									addr_op.operand.value = std::get<TempVar>(init_value);
								} else {
									// For other types, skip
									capture_index++;
									continue;
								}
								
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), lambda.lambda_token()));
								
								// Store the address in the closure member
								MemberStoreOp member_store;
								member_store.value.type = init_type;
								member_store.value.size_in_bits = 64; // pointer size
								member_store.value.value = addr_temp;
								member_store.object = StringTable::getOrInternStringHandle(closure_var_name);
								member_store.member_name = member->getName();
								member_store.offset = static_cast<int>(member->offset);
								member_store.is_reference = true;
								member_store.is_rvalue_reference = false;
								member_store.struct_type_info = nullptr;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), lambda.lambda_token()));
							} else {
								// Init-capture by value [x = expr] - store the value directly
								// Store the value in the closure member
								// We need to convert IrOperand to IrValue
								MemberStoreOp member_store;
								member_store.value.type = member->type;
								member_store.value.size_in_bits = static_cast<int>(member->size * 8);
								
								// Convert IrOperand to IrValue
								// IrValue only supports: unsigned long long, double, TempVar, std::string_view
								if (std::holds_alternative<TempVar>(init_value)) {
									member_store.value.value = std::get<TempVar>(init_value);
								} else if (std::holds_alternative<int>(init_value)) {
									member_store.value.value = static_cast<unsigned long long>(std::get<int>(init_value));
								} else if (std::holds_alternative<unsigned long long>(init_value)) {
									member_store.value.value = std::get<unsigned long long>(init_value);
								} else if (std::holds_alternative<double>(init_value)) {
									member_store.value.value = std::get<double>(init_value);
								} else if (std::holds_alternative<StringHandle>(init_value)) {
									member_store.value.value = std::get<StringHandle>(init_value);
								} else {
									// For other types, skip this capture
									capture_index++;
									continue;
								}
								
								member_store.object = StringTable::getOrInternStringHandle(closure_var_name);
								member_store.member_name = member->getName();
								member_store.offset = static_cast<int>(member->offset);
								member_store.is_reference = member->is_reference;
								member_store.is_rvalue_reference = member->is_rvalue_reference;
								member_store.struct_type_info = nullptr;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), lambda.lambda_token()));
							}
						} else if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
							// By-reference: store the address of the variable
							// Get the original variable type from captured_var_decls
							const ASTNode& var_decl = lambda_info.captured_var_decls[capture_index];
							const DeclarationNode* decl = get_decl_from_symbol(var_decl);
							if (!decl) {
								capture_index++;
								continue;
							}
							const auto& orig_type = decl->type_node().as<TypeSpecifierNode>();

							TempVar addr_temp = var_counter.next();
							
							if (is_captured_from_enclosing) {
								// Variable is captured from enclosing lambda - need to get address from this->x
								// For by-reference capture of an already captured variable, we need to:
								// 1. Load the enclosing lambda's captured value (or pointer if it was by-ref)
								// 2. Take the address of that
								auto enclosing_kind_it = current_lambda_context_.capture_kinds.find(var_name_str);
								bool enclosing_is_ref = (enclosing_kind_it != current_lambda_context_.capture_kinds.end() &&
								                         enclosing_kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);
								
								if (enclosing_is_ref) {
									// Enclosing captured by reference - it already holds a pointer, just copy it
									MemberLoadOp member_load;
									member_load.result.value = addr_temp;
									member_load.result.type = orig_type.type();  // Use original type (pointer semantics handled by IR converter)
									member_load.result.size_in_bits = 64;  // Pointer size
									member_load.object = StringTable::getOrInternStringHandle("this");  // "this" is a string literal
									member_load.member_name = StringTable::getOrInternStringHandle(var_name);  // Intern to StringHandle
									
									// Look up the offset from the enclosing lambda's struct
									int enclosing_offset = -1;
									auto enclosing_type_it = gTypesByName.find(current_lambda_context_.closure_type);
									if (enclosing_type_it != gTypesByName.end()) {
										const TypeInfo* enclosing_type = enclosing_type_it->second;
										if (const StructTypeInfo* enclosing_struct = enclosing_type->getStructInfo()) {
											const StructMember* enclosing_member = enclosing_struct->findMember(var_name);
											if (enclosing_member) {
												enclosing_offset = static_cast<int>(enclosing_member->offset);
											}
										}
									}
									member_load.offset = enclosing_offset;
									member_load.struct_type_info = nullptr;
									member_load.is_reference = true;  // Mark as reference
									member_load.is_rvalue_reference = false;
									ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), lambda.lambda_token()));
								} else {
									// Enclosing captured by value - need to get address of this->x
									// The IR converter's handleAddressOf checks current_lambda_captures_
									// and generates member access to this->var_name instead of direct variable access
									AddressOfOp addr_op;
									addr_op.result = addr_temp;
									addr_op.operand.type = orig_type.type();
									addr_op.operand.size_in_bits = static_cast<int>(orig_type.size_in_bits());
									addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
									addr_op.operand.value = StringTable::getOrInternStringHandle(var_name);
									ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), lambda.lambda_token()));
								}
							} else {
								// Regular variable - generate AddressOf directly
								AddressOfOp addr_op;
								addr_op.result = addr_temp;
								addr_op.operand.type = orig_type.type();
								addr_op.operand.size_in_bits = static_cast<int>(orig_type.size_in_bits());
								addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
								addr_op.operand.value = StringTable::getOrInternStringHandle(var_name);
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), lambda.lambda_token()));
							}

							// Store the address in the closure member
							MemberStoreOp member_store;
							member_store.value.type = member->type;
							member_store.value.size_in_bits = static_cast<int>(member->size * 8);
							member_store.value.value = addr_temp;
							member_store.object = StringTable::getOrInternStringHandle(closure_var_name);  // Already a persistent string_view
							member_store.member_name = member->getName();
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
						
						if (is_captured_from_enclosing) {
							// Variable is captured from enclosing lambda - load it via member access first
							TempVar loaded_value = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = loaded_value;
							member_load.result.type = member->type;
							member_load.result.size_in_bits = static_cast<int>(member->size * 8);
							member_load.object = StringTable::getOrInternStringHandle("this");  // "this" is a string literal
							member_load.member_name = StringTable::getOrInternStringHandle(var_name);  // Intern to StringHandle
							
							// Look up the offset from the enclosing lambda's struct
							int enclosing_offset = -1;
							auto enclosing_type_it = gTypesByName.find(current_lambda_context_.closure_type);
							if (enclosing_type_it != gTypesByName.end()) {
								const TypeInfo* enclosing_type = enclosing_type_it->second;
								if (const StructTypeInfo* enclosing_struct = enclosing_type->getStructInfo()) {
									const StructMember* enclosing_member = enclosing_struct->findMember(var_name_str);
									if (enclosing_member) {
										enclosing_offset = static_cast<int>(enclosing_member->offset);
									}
								}
							}
							member_load.offset = enclosing_offset;
							member_load.struct_type_info = nullptr;
							member_load.is_reference = false;
							member_load.is_rvalue_reference = false;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), lambda.lambda_token()));
							
							member_store.value.value = loaded_value;
						} else {
							// Regular variable - use directly (var_name is already a persistent string_view from AST)
							member_store.value.value = StringTable::getOrInternStringHandle(var_name);
						}
						
						member_store.object = StringTable::getOrInternStringHandle(closure_var_name);  // Already a persistent string_view
						member_store.member_name = member->getName();
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

		// Return the closure variable representing the lambda
		// Format: {type, size, value, type_index}
		// - type: Type::Struct (the closure is a struct)
		// - size: size of the closure in bits
		// - value: closure_var_name (the allocated closure variable)
		// - type_index: the type index for the closure struct
		int closure_size_bits = static_cast<int>(closure_type->getStructInfo()->total_size * 8);
		return {Type::Struct, closure_size_bits, StringTable::getOrInternStringHandle(closure_var_name), static_cast<unsigned long long>(closure_type->type_index_)};
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
		
		// CRITICAL FIX: Add operator() to the closure struct's member_functions list
		// This allows member function calls to find the correct declaration for mangling
		// Without this, lambda calls generate incorrect mangled names
		if (lambda_info.closure_type_name.empty()) {
			return;  // No closure type, can't add member functions
		}
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(lambda_info.closure_type_name));
		if (type_it != gTypesByName.end()) {
			TypeInfo* closure_type = const_cast<TypeInfo*>(type_it->second);
			StructTypeInfo* struct_info = closure_type->getStructInfo();
			if (struct_info) {
				// Create a FunctionDeclarationNode for operator()
				// We need this so member function calls can generate the correct mangled name
				TypeSpecifierNode return_type_node(lambda_info.return_type, lambda_info.return_type_index, 
					lambda_info.return_size, lambda_info.lambda_token);
				ASTNode return_type_ast = ASTNode::emplace_node<TypeSpecifierNode>(return_type_node);
				
				Token operator_token = lambda_info.lambda_token;  // Use lambda token as placeholder
				DeclarationNode& decl_node = gChunkedAnyStorage.emplace_back<DeclarationNode>(return_type_ast, operator_token);
				
				FunctionDeclarationNode& func_decl = gChunkedAnyStorage.emplace_back<FunctionDeclarationNode>(decl_node);
				
				// C++20: Lambda operator() is implicitly constexpr if it satisfies constexpr requirements
				// Mark it as constexpr so the ConstExprEvaluator can evaluate lambda calls at compile time
				func_decl.set_is_constexpr(true);
				
				// Add parameters to the function declaration
				for (const auto& param_node : lambda_info.parameter_nodes) {
					func_decl.add_parameter_node(param_node);
				}
				
				ASTNode func_decl_ast(&func_decl);
				
				// Create StructMemberFunction and add to struct
				StructMemberFunction member_func(
					StringTable::getOrInternStringHandle("operator()"),
					func_decl_ast,
					AccessSpecifier::Public,
					false,  // is_constructor
					false,  // is_destructor
					false,  // is_operator_overload
					""     // operator_symbol
				);
				member_func.is_const = false;  // Mutable lambdas have non-const operator()
				member_func.is_virtual = false;
				member_func.is_pure_virtual = false;
				member_func.is_override = false;
				member_func.is_final = false;
				member_func.vtable_index = 0;
				
				struct_info->member_functions.push_back(std::move(member_func));
			}
		}
	}

	// Generate the operator() member function for a lambda
	void generateLambdaOperatorCallFunction(const LambdaInfo& lambda_info) {
		// Generate function declaration for operator()
		FunctionDeclOp func_decl_op;
		func_decl_op.function_name = StringTable::getOrInternStringHandle("operator()"sv);  // Phase 4: Variant needs explicit type
		func_decl_op.struct_name = StringTable::getOrInternStringHandle(lambda_info.closure_type_name);  // Phase 4: Variant needs explicit type
		func_decl_op.return_type = lambda_info.return_type;
		func_decl_op.return_size_in_bits = lambda_info.return_size;
		func_decl_op.return_pointer_depth = 0;  // pointer depth
		func_decl_op.linkage = Linkage::None;  // C++ linkage
		func_decl_op.is_variadic = false;
		
		// Detect if lambda returns struct by value (needs hidden return parameter for RVO/NRVO)
		// Only non-pointer, non-reference struct returns need this
		bool returns_struct_by_value = (lambda_info.return_type == Type::Struct && !lambda_info.returns_reference);
		func_decl_op.has_hidden_return_param = returns_struct_by_value;
		
		// Track hidden return parameter flag for current function context
		current_function_has_hidden_return_param_ = returns_struct_by_value;
		
		if (returns_struct_by_value) {
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Lambda operator() {} returns struct by value - will use hidden return parameter (RVO/NRVO)",
				StringTable::getStringView(StringTable::getOrInternStringHandle(lambda_info.closure_type_name)));
		}

		// Build TypeSpecifierNode for return type (with proper type_index if struct)
		TypeSpecifierNode return_type_node(lambda_info.return_type, lambda_info.return_type_index, lambda_info.return_size, lambda_info.lambda_token);
		
		// Build TypeSpecifierNodes for parameters using parameter_nodes to preserve type_index
		std::vector<TypeSpecifierNode> param_types;
		size_t param_idx = 0;
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						// Use the deduced type from call site (already has reference flags)
						param_types.push_back(*deduced);
					} else {
						// No deduced type available, fallback to int
						TypeSpecifierNode int_type(Type::Int, 0, 32, lambda_info.lambda_token);
						param_types.push_back(int_type);
					}
				} else {
					// Use the parameter type as-is, preserving all reference flags
					// This ensures mangled names are consistent between call sites and definitions
					param_types.push_back(param_type);
				}
			}
			param_idx++;
		}
		
		// Generate mangled name using the same function as regular member functions
		std::string_view mangled = generateMangledNameForCall(
			"operator()",
			return_type_node,
			param_types,
			false,  // not variadic
			lambda_info.closure_type_name
		);
		func_decl_op.mangled_name = StringTable::getOrInternStringHandle(mangled);

		// Add parameters - use parameter_nodes to get complete type information
		param_idx = 0;
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				FunctionParam func_param;
				func_param.name = StringTable::getOrInternStringHandle(param_decl.identifier_token().value());
				func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						func_param.type = deduced->type();
						func_param.size_in_bits = deduced->size_in_bits();
						// Use reference flags from the deduced type (set at call site)
						func_param.is_reference = deduced->is_reference();
						func_param.is_rvalue_reference = deduced->is_rvalue_reference();
					} else {
						// No deduced type available, fallback to int
						func_param.type = Type::Int;
						func_param.size_in_bits = 32;
						func_param.is_reference = param_type.is_reference();
						func_param.is_rvalue_reference = param_type.is_rvalue_reference();
					}
				} else {
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.is_reference = param_type.is_reference();
					func_param.is_rvalue_reference = param_type.is_rvalue_reference();
				}
				func_param.cv_qualifier = param_type.cv_qualifier();
				func_decl_op.parameters.push_back(func_param);
			}
			param_idx++;
		}
		
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), lambda_info.lambda_token));
		symbol_table.enter_scope(ScopeType::Function);

		// Reset the temporary variable counter for each new function
		// TempVar is 1-based (TempVar() starts at 1). For member functions (operator()),
		// TempVar(1) is reserved for 'this', so we start at TempVar(2).
		var_counter = TempVar(2);

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function return type and size for type checking in return statements
		// This is critical for lambdas returning other lambdas or structs
		current_function_return_type_ = lambda_info.return_type;
		current_function_return_size_ = lambda_info.return_size;
		current_function_returns_reference_ = lambda_info.returns_reference;

		// Set lambda context for captured variable access
		pushLambdaContext(lambda_info);

		// Add lambda parameters to symbol table as function parameters (operator() context)
		// This ensures they're recognized as local parameters, not external symbols
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
		bool has_return_statement = false;
		if (lambda_info.lambda_body.is<BlockNode>()) {
			const auto& body = lambda_info.lambda_body.as<BlockNode>();
			body.get_statements().visit([&](const ASTNode& stmt) {
				visit(stmt);
				if (stmt.is<ReturnStatementNode>()) {
					has_return_statement = true;
				}
			});
		}

		// Add implicit return for void lambdas (matching regular function behavior)
		if (!has_return_statement && lambda_info.return_type == Type::Void) {
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), lambda_info.lambda_token));
		}

		// Restore outer lambda context (if any)
		popLambdaContext();

		symbol_table.exit_scope();
		
		// Note: Nested lambdas collected during body generation will be processed
		// by the main generateCollectedLambdas() loop - no recursive call needed here
	}

	// Generate the __invoke static function for a lambda
	void generateLambdaInvokeFunction(const LambdaInfo& lambda_info) {
		// Generate function declaration for __invoke
		FunctionDeclOp func_decl_op;
		func_decl_op.function_name = StringTable::getOrInternStringHandle(lambda_info.invoke_name);  // Variant needs explicit type
		func_decl_op.struct_name = StringHandle();  // no struct name (static function)
		func_decl_op.return_type = lambda_info.return_type;
		func_decl_op.return_size_in_bits = lambda_info.return_size;
		func_decl_op.return_pointer_depth = 0;  // pointer depth
		func_decl_op.linkage = Linkage::None;  // C++ linkage
		func_decl_op.is_variadic = false;
		
		// Detect if lambda returns struct by value (needs hidden return parameter for RVO/NRVO)
		bool returns_struct_by_value = (lambda_info.return_type == Type::Struct && !lambda_info.returns_reference);
		func_decl_op.has_hidden_return_param = returns_struct_by_value;
		
		// Track hidden return parameter flag for current function context
		current_function_has_hidden_return_param_ = returns_struct_by_value;

		// Build TypeSpecifierNode for return type (with proper type_index if struct)
		TypeSpecifierNode return_type_node(lambda_info.return_type, lambda_info.return_type_index, lambda_info.return_size, lambda_info.lambda_token);
		
		// Build TypeSpecifierNodes for parameters using parameter_nodes to preserve type_index
		std::vector<TypeSpecifierNode> param_types;
		size_t param_idx = 0;
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						// Use the deduced type from call site (already has reference flags)
						param_types.push_back(*deduced);
					} else {
						TypeSpecifierNode int_type(Type::Int, 0, 32, lambda_info.lambda_token);
						param_types.push_back(int_type);
					}
				} else {
					// Use the parameter type as-is, preserving all reference flags
					param_types.push_back(param_type);
				}
			}
			param_idx++;
		}
		
		// Generate mangled name for the __invoke function (free function, not member)
		std::string_view mangled = generateMangledNameForCall(
			lambda_info.invoke_name,
			return_type_node,
			param_types,
			false,  // not variadic
			""  // not a member function
		);
		func_decl_op.mangled_name = StringTable::getOrInternStringHandle(mangled);

		// Add parameters - use parameter_nodes to get complete type information
		param_idx = 0;
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				FunctionParam func_param;
				func_param.name = StringTable::getOrInternStringHandle(param_decl.identifier_token().value());
				func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						func_param.type = deduced->type();
						func_param.size_in_bits = deduced->size_in_bits();
						// Use reference flags from the deduced type (set at call site)
						func_param.is_reference = deduced->is_reference();
						func_param.is_rvalue_reference = deduced->is_rvalue_reference();
					} else {
						func_param.type = Type::Int;
						func_param.size_in_bits = 32;
						func_param.is_reference = param_type.is_reference();
						func_param.is_rvalue_reference = param_type.is_rvalue_reference();
					}
				} else {
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.is_reference = param_type.is_reference();
					func_param.is_rvalue_reference = param_type.is_rvalue_reference();
				}
				func_param.cv_qualifier = param_type.cv_qualifier();
				func_decl_op.parameters.push_back(func_param);
			}
			param_idx++;
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), lambda_info.lambda_token));
		symbol_table.enter_scope(ScopeType::Function);

		// Reset the temporary variable counter for each new function
		// TempVar is 1-based. For static functions (like __invoke), no 'this' pointer,
		// so TempVar() starts at 1 which is the first available slot.
		var_counter = TempVar();

		// Set current function return type and size for type checking in return statements
		// This is critical for lambdas returning other lambdas or structs
		current_function_return_type_ = lambda_info.return_type;
		current_function_return_size_ = lambda_info.return_size;
		current_function_returns_reference_ = lambda_info.returns_reference;

		// Add lambda parameters to symbol table as function parameters (__invoke context)
		// This ensures they're recognized as local parameters, not external symbols
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				symbol_table.insert(param_decl.identifier_token().value(), param_node);
			}
		}

		// Add captured variables to symbol table
		addCapturedVariablesToSymbolTable(lambda_info.captures, lambda_info.captured_var_decls);

		// Push lambda context so that recursive calls via auto&& parameters work correctly
		// This allows the auto-typed callable handling in generateFunctionCallIr to detect
		// that we're inside a lambda and generate the correct operator() call
		pushLambdaContext(lambda_info);

		// Generate the lambda body
		bool has_return_statement = false;
		if (lambda_info.lambda_body.is<BlockNode>()) {
			const auto& body = lambda_info.lambda_body.as<BlockNode>();
			body.get_statements().visit([&](const ASTNode& stmt) {
				visit(stmt);
				if (stmt.is<ReturnStatementNode>()) {
					has_return_statement = true;
				}
			});
		}

		// Add implicit return for void lambdas (matching regular function behavior)
		if (!has_return_statement && lambda_info.return_type == Type::Void) {
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), lambda_info.lambda_token));
		}

		// Restore outer lambda context
		popLambdaContext();

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
				// Capture-all ([=] or [&]) should have been expanded by the parser into explicit captures
				// If we see one here, it means the parser didn't expand it (shouldn't happen)
				continue;
			}
			
			// Skip [this] and [*this] captures - they don't have variable declarations
			if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
			    capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
				continue;
			}

			// Skip init-captures: [x = expr] defines a new variable, doesn't capture existing one
			// These are handled separately by reading from the closure member
			if (capture.has_initializer()) {
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
				func_param.name = StringTable::getOrInternStringHandle(param_decl.identifier_token().value());
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
			Token this_token(Token::Type::Identifier, "this", 
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
			inst_info.struct_name.isValid() ? StringTable::getStringView(inst_info.struct_name) : std::string_view(),  // Pass struct name
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
				constructor_name = StringTable::getOrInternStringHandle(type_spec.token().value());
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
							tv.is_reference = true;  // Mark as reference parameter
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
						if (!tv.is_reference) {
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
