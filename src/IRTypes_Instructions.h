#pragma once
#include "IRTypes_Ops.h"

class IrInstruction {
public:
	// Constructor from vector (backward compatibility)
	IrInstruction(IrOpcode opcode, std::vector<IrOperand>&& operands, Token first_token)
		: opcode_(opcode), operands_(std::move(operands)), first_token_(first_token) {}

	// Builder-style constructor (no temporary vector allocation)
	IrInstruction(IrOpcode opcode, Token first_token, size_t expected_operand_count = 0)
		: opcode_(opcode), operands_(), first_token_(first_token) {
		if (expected_operand_count > 0) {
			operands_.reserve(expected_operand_count);
		}
	}

	// Typed constructor - defined in IROperandHelpers.h where types are complete
	template<typename PayloadType>
	IrInstruction(IrOpcode opcode, PayloadType&& payload, Token first_token);

	// Add operand (builder pattern)
	void addOperand(IrOperand&& operand) {
		operands_.addOperand(std::move(operand));
	}

	// Convenience template for adding operands
	template<typename T>
	void addOperand(T&& value) {
		operands_.addOperand(IrOperand(std::forward<T>(value)));
	}

	IrOpcode getOpcode() const { return opcode_; }
	size_t getOperandCount() const { return operands_.size(); }
	size_t getLineNumber() const { return first_token_.line(); }

	std::optional<IrOperand> getOperandSafe(size_t index) const {
		return operands_.getSafe(index);
	}

	const IrOperand& getOperand(size_t index) const {
		return operands_[index];
	}

	template<class TClass>
	const TClass& getOperandAs(size_t index) const {
		return std::get<TClass>(operands_[index]);
	}

	// Safe version of getOperandAs for int - returns default value if not found or wrong type
	int getOperandAsIntSafe(size_t index, int default_value = 0) const {
		if (index >= operands_.size())
			return default_value;
		if (isOperandType<int>(index))
			return getOperandAs<int>(index);
		return default_value;
	}

	std::string_view getOperandAsTypeString(size_t index) const {
		if (index >= operands_.size())
			return "";

		if (!isOperandType<Type>(index))
			return "<not-a-type>";

		const Type type = getOperandAs<Type>(index);
		if (const TypeInfo* type_info = findNativeType(typeToCategory(type)))
			return StringTable::getStringView(type_info->name());
		return "";
	}

	template<class TClass>
	bool isOperandType(size_t index) const {
		return std::holds_alternative<TClass>(operands_[index]);
	}

	std::string getReadableString() const {
		std::ostringstream oss{};

		switch (opcode_) {
		case IrOpcode::Add:
			oss << formatBinaryOp("add", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::Subtract:
			oss << formatBinaryOp("sub", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::Multiply:
			oss << formatBinaryOp("mul", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::Divide:
			oss << formatBinaryOp("div", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedDivide:
			oss << formatBinaryOp("udiv", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::ShiftLeft:
			oss << formatBinaryOp("shl", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::ShiftRight:
			oss << formatBinaryOp("shr", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedShiftRight:
			oss << formatBinaryOp("lshr", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::Modulo:
			oss << formatBinaryOp("srem", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedModulo:
			oss << formatBinaryOp("urem", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::BitwiseAnd:
			oss << formatBinaryOp("and", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::BitwiseOr:
			oss << formatBinaryOp("or", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::BitwiseXor:
			oss << formatBinaryOp("xor", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::BitwiseNot:
			oss << formatUnaryOp("not", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::Equal:
			oss << formatBinaryOp("icmp eq", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::NotEqual:
			oss << formatBinaryOp("icmp ne", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::LessThan:
			oss << formatBinaryOp("icmp slt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::LessEqual:
			oss << formatBinaryOp("icmp sle", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::GreaterThan:
			oss << formatBinaryOp("icmp sgt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::GreaterEqual:
			oss << formatBinaryOp("icmp sge", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedLessThan:
			oss << formatBinaryOp("icmp ult", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedLessEqual:
			oss << formatBinaryOp("icmp ule", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedGreaterThan:
			oss << formatBinaryOp("icmp ugt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::UnsignedGreaterEqual:
			oss << formatBinaryOp("icmp uge", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::LogicalAnd:
			oss << formatBinaryOp("and i1", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::LogicalOr:
			oss << formatBinaryOp("or i1", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::LogicalNot:
			oss << formatUnaryOp("lnot", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::Negate:
			oss << formatUnaryOp("neg", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::SignExtend:
			oss << formatConversionOp("sext", getTypedPayload<ConversionOp>());
			break;
		case IrOpcode::ZeroExtend:
			oss << formatConversionOp("zext", getTypedPayload<ConversionOp>());
			break;
		case IrOpcode::Truncate:
			oss << formatConversionOp("trunc", getTypedPayload<ConversionOp>());
			break;

		case IrOpcode::Return:
		{
			const auto& op = getTypedPayload<ReturnOp>();
			oss << "ret ";

			if (op.return_value.has_value() && op.return_type_index.category() != TypeCategory::Invalid) {
				// Return with value
				if (const TypeInfo* type_info = findNativeType(op.return_type_index.category())) {
					oss << type_info->name();
				}
				oss << op.return_size << " ";

				const auto& val = op.return_value.value();
				if (const auto* ull_val = std::get_if<unsigned long long>(&val)) {
					oss << *ull_val;
				} else if (const auto* temp_var = std::get_if<TempVar>(&val)) {
					oss << '%' << temp_var->var_number;
				} else if (const auto* string = std::get_if<StringHandle>(&val)) {
					oss << '%' << StringTable::getStringView(*string);
				} else if (const auto* d_val = std::get_if<double>(&val)) {
					oss << *d_val;
				}
			} else {
				// Void return
				oss << "void";
			}
		}
		break;

		case IrOpcode::FunctionDecl:
		{
			const auto& op = getTypedPayload<FunctionDeclOp>();

			// Linkage
			oss << "define ";
			if (op.linkage != Linkage::None && op.linkage != Linkage::CPlusPlus) {
				oss << linkageToString(op.linkage) << " ";
			}

			// Return type
			if (const TypeInfo* ret_type_info = findNativeType(op.return_type_index.category())) {
				oss << ret_type_info->name();
			}
			for (int i = 0; i < op.return_pointer_depth.value; ++i) {
				oss << "*";
			}
			oss << op.return_size_in_bits;

			// Return type reference qualifiers
			if (op.returns_rvalue_reference) {
				oss << "&&";
			} else if (op.returns_reference) {
				oss << "&";
			}

			oss << " ";

			// Function name (Phase 4: Use helper)
			oss << "@";
			StringHandle mangled = op.getMangledName();
			if (mangled.handle != 0) {
				oss << StringTable::getStringView(mangled);
			} else {
				oss << StringTable::getStringView(op.getFunctionName());
			}
			oss << "(";

			// Parameters
			for (size_t i = 0; i < op.parameters.size(); ++i) {
				if (i > 0) oss << ", ";

				const auto& param = op.parameters[i];

				// Type
				if (const TypeInfo* param_type_info = findNativeType(param.type_index.category())) {
					oss << param_type_info->name();
				}
				// Print pointer levels, but exclude the extra level added for lvalue references
				// (that level is represented by the & suffix instead)
				int effective_pointer_depth = param.pointer_depth.value;
				if (param.is_reference() && !param.is_rvalue_reference() && effective_pointer_depth > 0) {
					effective_pointer_depth -= 1;  // Lvalue ref was represented as +1 pointer depth
				}
				for (int j = 0; j < effective_pointer_depth; ++j) {
					oss << "*";
				}
				oss << param.size_in_bits;

				// Reference qualifiers
				if (param.is_rvalue_reference()) {
					oss << "&&";
				} else if (param.is_reference()) {
					oss << "&";
				}

				// CV qualifiers
				if (param.cv_qualifier != CVQualifier::None) {
					oss << " " << cvQualifierToString(param.cv_qualifier);
				}

				// Name (Phase 4: Use helper)
				StringHandle param_name_handle = param.getName();
				if (param_name_handle.handle != 0) {
					oss << " %" << StringTable::getStringView(param_name_handle);
				}
			}

			if (op.is_variadic) {
				if (!op.parameters.empty()) oss << ", ";
				oss << "...";
			}

			oss << ")";

			// Struct context (Phase 4: Use helper)
			StringHandle struct_name_handle = op.getStructName();
			if (struct_name_handle.handle != 0) {
				oss << " [" << StringTable::getStringView(struct_name_handle) << "]";
			}
		}
		break;

		case IrOpcode::FunctionCall:
		{
			const auto& op = getTypedPayload<CallOp>();

			// Result variable (Phase 4: Use helper)
			oss << '%' << op.result.var_number << " = call @" << op.getFunctionName() << "(";

			// Arguments
			for (size_t i = 0; i < op.args.size(); ++i) {
				if (i > 0) oss << ", ";

				const auto& arg = op.args[i];

				// Type and size
				if (const TypeInfo* type_info = findNativeType(typeToCategory(arg.type))) {
					oss << type_info->name();
				}
				oss << arg.size_in_bits << " ";

				// Value - use the helper function that handles all types including double
				printTypedValue(oss, arg);
			}

			oss << ")";
		}
		break;

		case IrOpcode::StackAlloc:
		{
			const StackAllocOp& op = getTypedPayload<StackAllocOp>();
			// %name = alloca [Type][SizeInBits]
			oss << '%';
			if (const auto* string_ptr = std::get_if<StringHandle>(&op.result))
				oss << StringTable::getStringView(*string_ptr);
			else
				oss << std::get<TempVar>(op.result).var_number;
			oss << " = alloca ";
			if (const TypeInfo* type_info = findNativeType(op.type_index.category()))
				oss << type_info->name();
			oss << op.size_in_bits;
		}
		break;

		case IrOpcode::Branch:
		{
			const auto& op = getTypedPayload<BranchOp>();
			oss << "br label %" << op.getTargetLabel();  // Phase 4: Use helper
		}
		break;

		case IrOpcode::ConditionalBranch:
		{
			const auto& op = getTypedPayload<CondBranchOp>();
			oss << "br i1 ";

			// Condition value
			const auto& val = op.condition.value;
			if (const auto* ull_val = std::get_if<unsigned long long>(&val)) {
				oss << *ull_val;
			} else if (const auto* temp_var = std::get_if<TempVar>(&val)) {
				oss << '%' << temp_var->var_number;
			} else if (const auto* string = std::get_if<StringHandle>(&val)) {
				oss << '%' << StringTable::getStringView(*string);
			}

			oss << ", label %" << op.getLabelTrue();   // Phase 4: Use helper
			oss << ", label %" << op.getLabelFalse();  // Phase 4: Use helper
		}
		break;

		case IrOpcode::Label:
		{
			const auto& op = getTypedPayload<LabelOp>();
			oss << op.getLabelName() << ":";  // Phase 4: Use helper
		}
		break;

		case IrOpcode::LoopBegin:
		{
			assert(hasTypedPayload() && "LoopBegin instruction must use typed payload");
			const auto& op = getTypedPayload<LoopBeginOp>();
			oss << "loop_begin %" << op.loop_start_label
				<< " %" << op.loop_end_label
				<< " %" << op.loop_increment_label;
		}
		break;

		case IrOpcode::LoopEnd:
		{
			// loop_end (no operands)
			assert(getOperandCount() == 0 && "LoopEnd instruction must have exactly 0 operands");
			oss << "loop_end";
		}
		break;

		case IrOpcode::ScopeBegin:
		{
			// scope_begin (no operands)
			assert(getOperandCount() == 0 && "ScopeBegin instruction must have exactly 0 operands");
			oss << "scope_begin";
		}
		break;

		case IrOpcode::ScopeEnd:
		{
			// scope_end (no operands)
			assert(getOperandCount() == 0 && "ScopeEnd instruction must have exactly 0 operands");
			oss << "scope_end";
		}
		break;

		case IrOpcode::Break:
		{
			// break (no operands - uses loop context stack)
			assert(getOperandCount() == 0 && "Break instruction must have exactly 0 operands");
			oss << "break";
		}
		break;

		case IrOpcode::Continue:
		{
			// continue (no operands - uses loop context stack)
			assert(getOperandCount() == 0 && "Continue instruction must have exactly 0 operands");
			oss << "continue";
		}
		break;

		case IrOpcode::ArrayAccess:
		{
			assert (hasTypedPayload() && "expected ArrayAccess to have typed payload");
			const ArrayAccessOp& op = std::any_cast<const ArrayAccessOp&>(getTypedPayload());
			oss << '%' << op.result.var_number << " = array_access ";
			oss << "[" << static_cast<int>(op.elementType()) << "][" << op.element_size_in_bits << "] ";

			if (const auto* string = std::get_if<StringHandle>(&op.array))
				oss << '%' << StringTable::getStringView(*string);
			else
				oss << '%' << std::get<TempVar>(op.array).var_number;

			oss << ", [" << static_cast<int>(op.index.type) << "][" << op.index.size_in_bits << "] ";

			if (const auto* ull_val = std::get_if<unsigned long long>(&op.index.value))
				oss << *ull_val;
			else if (std::holds_alternative<TempVar>(op.index.value))
				oss << '%' << std::get<TempVar>(op.index.value).var_number;
			else if (std::holds_alternative<StringHandle>(op.index.value))
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.index.value));
		}
		break;

		case IrOpcode::ArrayStore:
		{
			assert (hasTypedPayload() && "expected ArrayStore to have typed payload");
			const ArrayStoreOp& op = std::any_cast<const ArrayStoreOp&>(getTypedPayload());
			oss << "array_store [" << static_cast<int>(op.elementType()) << "][" << op.element_size_in_bits << "] ";

			if (const auto* string = std::get_if<StringHandle>(&op.array))
				oss << '%' << StringTable::getStringView(*string);
			else
				oss << '%' << std::get<TempVar>(op.array).var_number;

			oss << ", [" << static_cast<int>(op.index.type) << "][" << op.index.size_in_bits << "] ";

			printTypedValue(oss, op.index);

			oss << ", [" << static_cast<int>(op.value.type) << "][" << op.value.size_in_bits << "] ";

			printTypedValue(oss, op.value);
			break;
		}
		break;

		case IrOpcode::ArrayElementAddress:
		{
			assert(hasTypedPayload() && "ArrayElementAddress instruction must use typed payload");
			const auto& op = getTypedPayload<ArrayElementAddressOp>();
			oss << '%' << op.result.var_number << " = array_element_address ";
			oss << "[" << static_cast<int>(op.elementType()) << "]" << op.element_size_in_bits << " ";

			// Array
			if (const auto* string = std::get_if<StringHandle>(&op.array))
				oss << '%' << StringTable::getStringView(*string);
			else if (std::holds_alternative<TempVar>(op.array))
				oss << '%' << std::get<TempVar>(op.array).var_number;

			oss << "[";
			printTypedValue(oss, op.index);
			oss << "]";
		}
		break;

		case IrOpcode::AddressOf:
		{
			assert(hasTypedPayload() && "AddressOf instruction must use typed payload");
			const auto& op = getTypedPayload<AddressOfOp>();
			oss << '%' << op.result.var_number << " = addressof ";

			// Print type and size from TypedValue
			if (const TypeInfo* type_info = findNativeType(typeToCategory(op.operand.type))) {
				oss << type_info->name();
			}
			oss << op.operand.size_in_bits;

			// Show pointer depth if any
			if (op.operand.pointer_depth.is_pointer()) {
				oss << " (ptr_depth=" << op.operand.pointer_depth.value << ")";
			}
			oss << " ";

			// Print operand value
			if (const auto* string_ptr = std::get_if<StringHandle>(&op.operand.value))
				oss << '%' << StringTable::getStringView(*string_ptr);
			else if (std::holds_alternative<TempVar>(op.operand.value))
				oss << '%' << std::get<TempVar>(op.operand.value).var_number;
		}
		break;

		case IrOpcode::AddressOfMember:
		{
			assert(hasTypedPayload() && "AddressOfMember instruction must use typed payload");
			const auto& op = getTypedPayload<AddressOfMemberOp>();
			oss << '%' << op.result.var_number << " = addressof_member ";
			oss << "[" << static_cast<int>(op.memberType()) << "]" << op.member_size_in_bits << " ";
			oss << '%' << StringTable::getStringView(op.base_object);
			oss << " (offset: " << op.member_offset << ")";
		}
		break;

		case IrOpcode::ComputeAddress:
		{
			assert(hasTypedPayload() && "ComputeAddress instruction must use typed payload");
			const auto& op = getTypedPayload<ComputeAddressOp>();
			oss << '%' << op.result.var_number << " = compute_address ";
			oss << "[" << static_cast<int>(op.resultType()) << "]" << op.result_size_bits << " ";

			// Print base
			if (const auto* string = std::get_if<StringHandle>(&op.base)) {
				oss << "base: %" << StringTable::getStringView(*string);
			} else {
				oss << "base: %" << std::get<TempVar>(op.base).var_number;
			}

			// Print array indices if any
			for (size_t i = 0; i < op.array_indices.size(); ++i) {
				const auto& arr_idx = op.array_indices[i];
				oss << ", idx" << i << ": ";
				if (const auto* ull_val = std::get_if<unsigned long long>(&arr_idx.index)) {
					oss << *ull_val;
				} else if (const auto* temp_var = std::get_if<TempVar>(&arr_idx.index)) {
					oss << "%" << temp_var->var_number;
				} else {
					oss << "%" << StringTable::getStringView(std::get<StringHandle>(arr_idx.index));
				}
				oss << " [" << static_cast<int>(arr_idx.indexType()) << "]" << arr_idx.index_size_bits;
				oss << " (elem_size: " << arr_idx.element_size_bits << " bits)";
			}

			// Print total member offset if any
			if (op.total_member_offset > 0) {
				oss << ", member_offset: " << op.total_member_offset;
			}
		}
		break;

		case IrOpcode::Dereference:
		{
			assert(hasTypedPayload() && "Dereference instruction must use typed payload");
			const auto& op = getTypedPayload<DereferenceOp>();
			oss << '%' << op.result.var_number << " = dereference ";

			// Print type and size from TypedValue
			// If pointer_depth > 1, result is still a pointer (64 bits)
			// If pointer_depth == 1, result is the pointee type
			if (const TypeInfo* type_info = findNativeType(op.pointer.category())) {
				oss << type_info->name();
			}

			int deref_size = (op.pointer.pointer_depth.value > 1) ? 64 : op.pointer.size_in_bits.value;
			oss << deref_size;

			// Show pointer depth for debugging
			if (op.pointer.pointer_depth.is_pointer()) {
				oss << " (ptr_depth=" << op.pointer.pointer_depth.value << ")";
			}
			oss << " ";

			// Print pointer value
			if (const auto* string = std::get_if<StringHandle>(&op.pointer.value))
				oss << '%' << StringTable::getStringView(*string);
			else if (std::holds_alternative<TempVar>(op.pointer.value))
				oss << '%' << std::get<TempVar>(op.pointer.value).var_number;
		}
		break;

		case IrOpcode::DereferenceStore:
		{
			assert(hasTypedPayload() && "DereferenceStore instruction must use typed payload");
			const auto& op = getTypedPayload<DereferenceStoreOp>();
			oss << "store_through_ptr ";

			// Print pointer type and size
			if (const TypeInfo* ptr_type_info = findNativeType(op.pointer.category())) {
				oss << ptr_type_info->name();
			}
			oss << op.pointer.size_in_bits;

			// Show pointer depth if any
			if (op.pointer.pointer_depth.is_pointer()) {
				oss << " (ptr_depth=" << op.pointer.pointer_depth.value << ")";
			}
			oss << " ";

			// Print pointer value
			if (const auto* string = std::get_if<StringHandle>(&op.pointer.value))
				oss << "%" << StringTable::getStringView(*string);
			else if (std::holds_alternative<TempVar>(op.pointer.value))
				oss << "%" << std::get<TempVar>(op.pointer.value).var_number;

			oss << ", ";

			// Value being stored
			if (const auto* ull_val = std::get_if<unsigned long long>(&op.value.value))
				oss << *ull_val;
			else if (std::holds_alternative<double>(op.value.value))
				oss << std::get<double>(op.value.value);
			else if (std::holds_alternative<TempVar>(op.value.value))
				oss << "%" << std::get<TempVar>(op.value.value).var_number;
			else if (std::holds_alternative<StringHandle>(op.value.value))
				oss << "%" << StringTable::getStringView(std::get<StringHandle>(op.value.value));
		}
		break;

		case IrOpcode::MemberAccess:
		{
			// %result = member_access [MemberType][MemberSize] %object.member_name (offset: N) [ref]
			assert(hasTypedPayload() && "MemberAccess instruction must use typed payload");
			const auto& op = getTypedPayload<MemberLoadOp>();

			oss << '%';
			if (const auto* temp_var = std::get_if<TempVar>(&op.result.value))
				oss << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.result.value))
				oss << StringTable::getStringView(std::get<StringHandle>(op.result.value));

			oss << " = member_access ";

			// Type and size
			if (const TypeInfo* type_info = findNativeType(typeToCategory(op.result.type))) {
				oss << type_info->name();
			}
			oss << op.result.size_in_bits << " ";

			// Object
			if (const auto* temp_var = std::get_if<TempVar>(&op.object))
				oss << '%' << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.object))
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.object));

			oss << "." << op.member_name;
			oss << " (offset: " << op.offset << ")";
			if (op.is_reference()) {
				oss << " [ref]";
			}
			if (op.is_rvalue_reference()) {
				oss << " [rvalue_ref]";
			}
		}
		break;

		case IrOpcode::MemberStore:
		{
			// member_store [MemberType][MemberSize] %object.member_name (offset: N) [ref], %value
			assert(hasTypedPayload() && "MemberStore instruction must use typed payload");
			const auto& op = getTypedPayload<MemberStoreOp>();

			oss << "member_store ";

			// Type and size
			if (const TypeInfo* type_info = findNativeType(typeToCategory(op.value.type))) {
				oss << type_info->name();
			}
			oss << op.value.size_in_bits << " ";

			// Object
			if (const auto* temp_var = std::get_if<TempVar>(&op.object))
				oss << '%' << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.object))
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.object));

			oss << "." << op.member_name;
			oss << " (offset: " << op.offset << ")";
			if (op.is_reference()) {
				oss << " [ref]";
			}
			if (op.is_rvalue_reference()) {
				oss << " [rvalue_ref]";
			}
			oss << ", ";

			// Value - use printTypedValue helper
			printTypedValue(oss, op.value);
		}
		break;

		case IrOpcode::ConstructorCall:
		{
			// constructor_call StructName %object_var [param1_type, param1_size, param1_value, ...]
			const ConstructorCallOp& op = getTypedPayload<ConstructorCallOp>();
			oss << "constructor_call " << op.struct_name << " %";

			// Object can be either string_view or TempVar
			if (const auto* string_ptr = std::get_if<StringHandle>(&op.object))
				oss << StringTable::getStringView(*string_ptr);
			else if (std::holds_alternative<TempVar>(op.object))
				oss << std::get<TempVar>(op.object).var_number;

			// Add constructor arguments
			for (const auto& arg : op.arguments) {
				oss << " ";
				const TypeCategory arg_cat = typeToCategory(arg.type);
				if (const TypeInfo* type_info = findNativeType(arg_cat)) {
					oss << type_info->name();
				} else if (needs_type_index(arg_cat)) {
					// Try to get the type name from gTypeInfo using type_index
					if (arg.type_index.is_valid() && arg.type_index.index() < getTypeInfoCount()) {
						oss << getTypeInfo(arg.type_index).name();
					} else {
						oss << "struct/enum";
					}
				}
				oss << arg.size_in_bits << " ";
				// Print the IrValue directly (not TypedValue)
				if (const auto* temp_var = std::get_if<TempVar>(&arg.value))
					oss << '%' << temp_var->var_number;
				else if (std::holds_alternative<StringHandle>(arg.value))
					oss << '%' << StringTable::getStringView(std::get<StringHandle>(arg.value));
				else if (std::holds_alternative<unsigned long long>(arg.value))
					oss << std::get<unsigned long long>(arg.value);
				else if (std::holds_alternative<double>(arg.value))
					oss << std::get<double>(arg.value);
			}
		}
		break;

		case IrOpcode::DestructorCall:
		{
			// destructor_call StructName %object_var
			const DestructorCallOp& op = getTypedPayload<DestructorCallOp>();
			oss << "destructor_call " << op.struct_name << " %";

			// Object can be either string or TempVar
			if (const auto* string_ptr = std::get_if<StringHandle>(&op.object))
				oss << StringTable::getStringView(*string_ptr);
			else if (std::holds_alternative<TempVar>(op.object))
				oss << std::get<TempVar>(op.object).var_number;
		}
		break;

		case IrOpcode::VirtualCall:
		{
			// %result = virtual_call %object, vtable_index, [args...]
			const VirtualCallOp& op = getTypedPayload<VirtualCallOp>();
			assert(std::holds_alternative<TempVar>(op.result.value) && "VirtualCallOp result must be a TempVar");
			oss << '%' << std::get<TempVar>(op.result.value).var_number << " = virtual_call ";

			// Object type and size
			if (const TypeInfo* type_info = findNativeType(op.object_type_index.category())) {
				oss << type_info->name();
			}
			oss << op.object_size << " %";

			// Object (this pointer)
			if (const auto* temp_var = std::get_if<TempVar>(&op.object))
				oss << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.object))
				oss << StringTable::getStringView(std::get<StringHandle>(op.object));

			// VTable index
			oss << ", vtable[" << op.vtable_index << "]";

			// Arguments (if any)
			if (!op.arguments.empty()) {
				oss << "(";
				for (size_t i = 0; i < op.arguments.size(); ++i) {
					if (i > 0) oss << ", ";

					const auto& arg = op.arguments[i];

					// Type and size
					if (const TypeInfo* arg_type_info = findNativeType(typeToCategory(arg.type))) {
						oss << arg_type_info->name();
					}
					oss << arg.size_in_bits << " ";

					// Value
					if (const auto* ull_val = std::get_if<unsigned long long>(&arg.value))
						oss << *ull_val;
					else if (std::holds_alternative<TempVar>(arg.value))
						oss << '%' << std::get<TempVar>(arg.value).var_number;
					else if (std::holds_alternative<StringHandle>(arg.value))
						oss << '%' << StringTable::getStringView(std::get<StringHandle>(arg.value));
				}
				oss << ")";
			}
		}
		break;

		case IrOpcode::StringLiteral:
		{
			// %result = string_literal "content"
			const StringLiteralOp& op = getTypedPayload<StringLiteralOp>();
			oss << '%';

			if (const auto* temp_var = std::get_if<TempVar>(&op.result))
				oss << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.result))
				oss << StringTable::getStringView(std::get<StringHandle>(op.result));

			oss << " = string_literal " << op.content;
		}
		break;

		case IrOpcode::HeapAlloc:
		{
			// %result = heap_alloc [Type][Size][PointerDepth]
			const HeapAllocOp& op = getTypedPayload<HeapAllocOp>();
			oss << '%' << op.result.var_number << " = heap_alloc ["
				<< static_cast<int>(op.opType()) << "]["
				<< op.size_in_bytes << "][" << op.pointer_depth.value << "]";
		}
		break;

		case IrOpcode::HeapAllocArray:
		{
			// %result = heap_alloc_array [Type][Size][PointerDepth] %count
			const HeapAllocArrayOp& op = getTypedPayload<HeapAllocArrayOp>();
			oss << '%' << op.result.var_number << " = heap_alloc_array ["
				<< static_cast<int>(op.opType()) << "]["
				<< op.size_in_bytes << "][" << op.pointer_depth.value << "] ";

			if (const auto* temp_var = std::get_if<TempVar>(&op.count))
				oss << '%' << temp_var->var_number;
			else if (std::holds_alternative<unsigned long long>(op.count))
				oss << std::get<unsigned long long>(op.count);
			else if (std::holds_alternative<StringHandle>(op.count))
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.count));
		}
		break;

		case IrOpcode::HeapFree:
		{
			// heap_free %ptr
			const HeapFreeOp& op = getTypedPayload<HeapFreeOp>();
			oss << "heap_free ";
			if (const auto* temp_var = std::get_if<TempVar>(&op.pointer))
				oss << '%' << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.pointer))
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.pointer));
		}
		break;

		case IrOpcode::HeapFreeArray:
		{
			// heap_free_array %ptr
			const HeapFreeArrayOp& op = getTypedPayload<HeapFreeArrayOp>();
			oss << "heap_free_array ";
			if (const auto* temp_var = std::get_if<TempVar>(&op.pointer))
				oss << '%' << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.pointer))
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.pointer));
		}
		break;

		case IrOpcode::PlacementNew:
		{
			// %result = placement_new %address [Type][Size]
			const PlacementNewOp& op = getTypedPayload<PlacementNewOp>();
			oss << '%' << op.result.var_number << " = placement_new ";
			if (const auto* temp_var = std::get_if<TempVar>(&op.address))
				oss << '%' << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.address))
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.address));
			else if (std::holds_alternative<unsigned long long>(op.address))
				oss << std::get<unsigned long long>(op.address);
			oss << " [" << static_cast<int>(op.opType()) << "][" << op.size_in_bytes << "]";
		}
		break;

		case IrOpcode::Typeid:
		{
			// %result = typeid [type_name_or_expr] [is_type]
			auto& op = getTypedPayload<TypeidOp>();
			oss << '%' << op.result.var_number << " = typeid ";
			if (const auto* string = std::get_if<StringHandle>(&op.operand)) {
				oss << StringTable::getStringView(*string);
			} else {
				oss << '%' << std::get<TempVar>(op.operand).var_number;
			}
			oss << " [is_type=" << (op.is_type ? "true" : "false") << "]";
		}
		break;

		case IrOpcode::DynamicCast:
		{
			// %result = dynamic_cast %source_ptr [target_type] [is_reference]
			auto& op = getTypedPayload<DynamicCastOp>();
			oss << '%' << op.result.var_number << " = dynamic_cast %" << op.source.var_number;
			oss << " [" << op.target_type_name << "]";
			oss << " [is_ref=" << (op.is_reference ? "true" : "false") << "]";
		}
		break;

		case IrOpcode::PreIncrement:
			oss << formatUnaryOp("pre_inc", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::PostIncrement:
			oss << formatUnaryOp("post_inc", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::PreDecrement:
			oss << formatUnaryOp("pre_dec", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::PostDecrement:
			oss << formatUnaryOp("post_dec", getTypedPayload<UnaryOp>());
			break;

		case IrOpcode::AddAssign:
			oss << formatBinaryOp("add", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::SubAssign:
			oss << formatBinaryOp("sub", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::MulAssign:
			oss << formatBinaryOp("mul", getTypedPayload<BinaryOp>());
			break;		case IrOpcode::DivAssign:
				oss << formatBinaryOp("sdiv", getTypedPayload<BinaryOp>());
				break;

		case IrOpcode::ModAssign:
			oss << formatBinaryOp("srem", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::AndAssign:
			oss << formatBinaryOp("and", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::OrAssign:
			oss << formatBinaryOp("or", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::XorAssign:
			oss << formatBinaryOp("xor", getTypedPayload<BinaryOp>());
			break;	case IrOpcode::ShlAssign:
			oss << formatBinaryOp("shl", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::ShrAssign:
			oss << formatBinaryOp("ashr", getTypedPayload<BinaryOp>());
			break;

		// Float arithmetic operations
		case IrOpcode::FloatAdd:
			oss << formatBinaryOp("fadd", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatSubtract:
			oss << formatBinaryOp("fsub", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatMultiply:
			oss << formatBinaryOp("fmul", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatDivide:
			oss << formatBinaryOp("fdiv", getTypedPayload<BinaryOp>());
			break;

		// Float comparison operations
		case IrOpcode::FloatEqual:
			oss << formatBinaryOp("fcmp oeq", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatNotEqual:
			oss << formatBinaryOp("fcmp one", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatLessThan:
			oss << formatBinaryOp("fcmp olt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatLessEqual:
			oss << formatBinaryOp("fcmp ole", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatGreaterThan:
			oss << formatBinaryOp("fcmp ogt", getTypedPayload<BinaryOp>());
			break;
		case IrOpcode::FloatGreaterEqual:
			oss << formatBinaryOp("fcmp oge", getTypedPayload<BinaryOp>());
			break;

		case IrOpcode::Assignment:
		{
			const AssignmentOp& op = getTypedPayload<AssignmentOp>();
			// assign %lhs = %rhs (simple assignment a = b)
			oss << "assign %";

			// Print LHS
			if (const auto* temp_var = std::get_if<TempVar>(&op.lhs.value))
				oss << temp_var->var_number;
			else if (std::holds_alternative<StringHandle>(op.lhs.value))
				oss << StringTable::getStringView(std::get<StringHandle>(op.lhs.value));
			else if (std::holds_alternative<unsigned long long>(op.lhs.value))
				oss << std::get<unsigned long long>(op.lhs.value);

			oss << " = ";

			// Print RHS
			if (const auto* ull_val = std::get_if<unsigned long long>(&op.rhs.value))
				oss << *ull_val;
			else if (std::holds_alternative<TempVar>(op.rhs.value))
				oss << '%' << std::get<TempVar>(op.rhs.value).var_number;
			else if (std::holds_alternative<StringHandle>(op.rhs.value))
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.rhs.value));
			else if (std::holds_alternative<double>(op.rhs.value))
				oss << std::get<double>(op.rhs.value);
		}
		break;

		case IrOpcode::VariableDecl:
		{
			const VariableDeclOp& op = getTypedPayload<VariableDeclOp>();
			std::string_view var_name = op.getVarName();  // Phase 4: Use helper
			oss << "%" << var_name << " = alloc ";

			if (op.is_array && op.array_count.has_value()) {
				// For arrays, print element type and count: int32[5]
				if (const TypeInfo* type_info = findNativeType(op.type_index.category()))
					oss << type_info->name();
				oss << op.size_in_bits << "[" << op.array_count.value() << "]";
			} else {
				// For scalars, print type and size: int32
				if (const TypeInfo* type_info = findNativeType(op.type_index.category()))
					oss << type_info->name();
				oss << op.size_in_bits;
			}

			if (op.custom_alignment > 0) {
				oss << " alignas(" << op.custom_alignment << ")";
			}
			oss << (op.is_reference() ? " [&]" : "");
			if (op.initializer.has_value()) {
				oss << "\nassign %" << var_name << " = ";  // Phase 4: Use var_name
				const auto& init = op.initializer.value();
				// Check if operand is a literal value or a variable/TempVar
				if (const auto* ull_val = std::get_if<unsigned long long>(&init.value))
					oss << *ull_val;
				else if (std::holds_alternative<double>(init.value))
					oss << std::get<double>(init.value);
				else if (std::holds_alternative<TempVar>(init.value))
					oss << '%' << std::get<TempVar>(init.value).var_number;
				else if (std::holds_alternative<StringHandle>(init.value))
					oss << '%' << StringTable::getStringView(std::get<StringHandle>(init.value));
			}
			break;
		}

		case IrOpcode::GlobalVariableDecl:
		{
			const GlobalVariableDeclOp& op = getTypedPayload<GlobalVariableDeclOp>();
			StringHandle var_name_handle = op.getVarName();
		std::string_view var_name = StringTable::getStringView(var_name_handle);  // Use helper for backward compatibility

			oss << "global_var ";
			if (const TypeInfo* type_info = findNativeType(op.type_index.category()))
				oss << type_info->name();
			oss << op.size_in_bits << " @" << std::string(var_name);
			if (op.element_count > 1) {
				oss << "[" << op.element_count << "]";
			}
			oss << " " << (op.is_initialized ? "initialized" : "uninitialized");
		}
		break;

		case IrOpcode::GlobalLoad:
		{
			const GlobalLoadOp& op = getTypedPayload<GlobalLoadOp>();
			// %result = global_load @global_name
			if (const auto* temp_var = std::get_if<TempVar>(&op.result.value)) {
				oss << '%' << temp_var->var_number;
			} else if (const auto* string_ptr = std::get_if<StringHandle>(&op.result.value)) {
				oss << '%' << StringTable::getStringView(*string_ptr);
			}
			oss << " = global_load @" << op.getGlobalName();  // Phase 4: Use helper
		}
		break;

		case IrOpcode::GlobalStore:
		{
			// global_store @global_name, %value
			// Format: [global_name, value]
			assert(getOperandCount() == 2 && "GlobalStore must have exactly 2 operands");
			oss << "global_store @" << StringTable::getStringView(getOperandAs<StringHandle>(0)) << ", %" << getOperandAs<TempVar>(1).var_number;
		}
		break;

		case IrOpcode::FunctionAddress:
		{
			// %result = function_address @function_name
			auto& op = getTypedPayload<FunctionAddressOp>();
			if (const auto* temp_var = std::get_if<TempVar>(&op.result.value)) {
				oss << '%' << temp_var->var_number;
			} else if (const auto* string_ptr = std::get_if<StringHandle>(&op.result.value)) {
				oss << '%' << StringTable::getStringView(*string_ptr);
			}
			oss << " = function_address @" << op.getFunctionName();  // Phase 4: Use helper
		}
		break;

		case IrOpcode::IndirectCall:
		{
			// %result = indirect_call %func_ptr, arg1, arg2, ...
			auto& op = getTypedPayload<IndirectCallOp>();
			oss << '%' << op.result.var_number << " = indirect_call ";

			// Function pointer can be either a TempVar or a variable name (string_view)
			if (const auto* temp_var = std::get_if<TempVar>(&op.function_pointer)) {
				oss << '%' << temp_var->var_number;
			} else {
				oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.function_pointer));
			}

			// Arguments with type information
			for (const auto& arg : op.arguments) {
				oss << ", ";
				if (const TypeInfo* type_info = findNativeType(typeToCategory(arg.type))) {
					oss << type_info->name();
				}
				oss << arg.size_in_bits << " ";
				if (const auto* temp_var_ptr = std::get_if<TempVar>(&arg.value)) {
					oss << '%' << temp_var_ptr->var_number;
				} else if (const auto* string = std::get_if<StringHandle>(&arg.value)) {
					oss << '%' << StringTable::getStringView(*string);
				} else if (const auto* ull_val = std::get_if<unsigned long long>(&arg.value)) {
					oss << *ull_val;
				} else if (const auto* d_val = std::get_if<double>(&arg.value)) {
					oss << *d_val;
				}
			}
		}
		break;

		case IrOpcode::FloatToInt:
		case IrOpcode::IntToFloat:
		case IrOpcode::FloatToFloat:
		{
			// %result = opcode from_val : from_type -> to_type
			auto& op = getTypedPayload<TypeConversionOp>();
			oss << '%' << op.result.var_number << " = ";
			switch (opcode_) {
				case IrOpcode::FloatToInt: oss << "float_to_int "; break;
				case IrOpcode::IntToFloat: oss << "int_to_float "; break;
				case IrOpcode::FloatToFloat: oss << "float_to_float "; break;
				default: break;
			}
			// Format: from_type from_size from_value to to_type to_size
			if (const TypeInfo* from_type_info = findNativeType(typeToCategory(op.from.type))) {
				oss << from_type_info->name();
			}
			oss << op.from.size_in_bits << " ";
			if (const auto* temp_var = std::get_if<TempVar>(&op.from.value)) {
				oss << '%' << temp_var->var_number;
			} else if (const auto* string = std::get_if<StringHandle>(&op.from.value)) {
				oss << '%' << StringTable::getStringView(*string);
			} else if (const auto* ull_val = std::get_if<unsigned long long>(&op.from.value)) {
				oss << *ull_val;
			} else if (const auto* d_val = std::get_if<double>(&op.from.value)) {
				oss << *d_val;
			}
			oss << " to ";
			if (const TypeInfo* to_type_info = findNativeType(op.to_type_index.category())) {
				oss << to_type_info->name();
			}
			oss << op.to_size_in_bits;
		}
		break;

		// Exception handling opcodes
		case IrOpcode::TryBegin:
		{
			const auto& op = getTypedPayload<BranchOp>();
			oss << "try_begin @" << op.getTargetLabel();  // Phase 4: Use helper
		}
		break;

		case IrOpcode::TryEnd:
			oss << "try_end";
			break;

		case IrOpcode::CatchBegin:
		{
			const auto& op = getTypedPayload<CatchBeginOp>();
			oss << "catch_begin ";
			if (!op.type_index.is_valid()) {
				oss << "...";  // catch-all
			} else {
				oss << "type_" << op.type_index.index();
			}
			oss << " %" << op.exception_temp.var_number;
			if (op.is_const) oss << " const";
			if (op.is_reference()) oss << "&";
			if (op.is_rvalue_reference()) oss << "&&";
			oss << " -> @" << op.catch_end_label;
		}
		break;

		case IrOpcode::CatchEnd:
		{
			if (hasTypedPayload()) {
				const auto& op = getTypedPayload<CatchEndOp>();
				oss << "catch_end -> @" << op.continuation_label;
			} else {
				oss << "catch_end";
			}
		}
		break;

		case IrOpcode::Throw:
		{
			const auto& op = getTypedPayload<ThrowOp>();
			oss << "throw ";
			// Print the exception value based on IrValue variant type
			if (const auto* temp_var = std::get_if<TempVar>(&op.exception_value)) {
				oss << "%" << temp_var->var_number;
			} else if (const auto* ull_val = std::get_if<unsigned long long>(&op.exception_value)) {
				oss << *ull_val;
			} else if (const auto* d_val = std::get_if<double>(&op.exception_value)) {
				oss << *d_val;
			} else if (const auto* string_ptr = std::get_if<StringHandle>(&op.exception_value)) {
				// StringHandle represents a string constant - print as quoted string
				oss << "\"" << StringTable::getStringView(*string_ptr) << "\"";
			}
			oss << " : type_" << op.type_index.index() << " (" << op.size_in_bytes << " bytes)";
			if (op.is_rvalue) oss << " rvalue";
		}
		break;

		case IrOpcode::Rethrow:
			oss << "rethrow";
			break;

		case IrOpcode::FunctionCleanupLP:
		{
			if (hasTypedPayload()) {
				const auto& op = getTypedPayload<FunctionCleanupLPOp>();
				oss << "function_cleanup_lp [" << op.cleanup_vars.size() << " vars]";
			} else {
				oss << "function_cleanup_lp";
			}
		}
		break;

		case IrOpcode::ElfCatchNoMatch:
			oss << "elf_catch_no_match";
			break;

		// Windows SEH opcodes
		case IrOpcode::SehTryBegin:
		{
			const auto& op = getTypedPayload<BranchOp>();
			oss << "seh_try_begin @" << op.getTargetLabel();
		}
		break;

		case IrOpcode::SehTryEnd:
			oss << "seh_try_end";
			break;

		case IrOpcode::SehExceptBegin:
		{
			const auto& op = getTypedPayload<SehExceptBeginOp>();
			oss << "seh_except_begin %" << op.filter_result.var_number;
			oss << " -> @" << op.except_end_label;
		}
		break;

		case IrOpcode::SehExceptEnd:
			oss << "seh_except_end";
			break;

		case IrOpcode::SehFinallyBegin:
			oss << "seh_finally_begin";
			break;

		case IrOpcode::SehFinallyEnd:
			oss << "seh_finally_end";
			break;

		case IrOpcode::SehFinallyCall:
		{
			const auto& op = getTypedPayload<SehFinallyCallOp>();
			oss << "seh_finally_call @" << op.funclet_label << " -> @" << op.end_label;
		}
		break;

		case IrOpcode::SehFilterBegin:
			oss << "seh_filter_begin";
			break;

		case IrOpcode::SehFilterEnd:
		{
			const auto& op = getTypedPayload<SehFilterEndOp>();
			if (op.is_constant_result) {
				oss << "seh_filter_end constant=" << op.constant_result;
			} else {
				oss << "seh_filter_end %" << op.filter_result.var_number;
			}
		}
		break;

		case IrOpcode::SehLeave:
		{
			const auto& op = getTypedPayload<SehLeaveOp>();
			oss << "seh_leave @" << op.target_label;
		}
		break;

		case IrOpcode::SehGetExceptionCode:
		{
			const auto& op = getTypedPayload<SehExceptionIntrinsicOp>();
			oss << "%" << op.result.var_number << " = seh_get_exception_code";
		}
		break;

		case IrOpcode::SehGetExceptionInfo:
		{
			const auto& op = getTypedPayload<SehExceptionIntrinsicOp>();
			oss << "%" << op.result.var_number << " = seh_get_exception_info";
		}
		break;

		case IrOpcode::SehSaveExceptionCode:
		{
			const auto& op = getTypedPayload<SehSaveExceptionCodeOp>();
			oss << "seh_save_exception_code -> %" << op.saved_var.var_number;
		}
		break;

		case IrOpcode::SehGetExceptionCodeBody:
		{
			const auto& op = getTypedPayload<SehGetExceptionCodeBodyOp>();
			oss << "%" << op.result.var_number << " = seh_get_exception_code_body(%" << op.saved_var.var_number << ")";
		}
		break;

		case IrOpcode::SehAbnormalTermination:
		{
			const auto& op = getTypedPayload<SehAbnormalTerminationOp>();
			oss << "%" << op.result.var_number << " = seh_abnormal_termination";
		}
		break;

		default:
			FLASH_LOG(Codegen, Error, "Unhandled opcode: ", static_cast<std::underlying_type_t<IrOpcode>>(opcode_));
			assert(false && "Unhandled opcode");
			break;
		}

		return oss.str();
	}

	// Check if instruction has typed payload
	bool hasTypedPayload() const {
		return typed_payload_.has_value();
	}

	// Get typed payload (for helpers in IROperandHelpers.h)
	const std::any& getTypedPayload() const {
		assert(typed_payload_.has_value() && "Instruction must have typed payload");
		return typed_payload_;
	}

	std::any& getTypedPayload() {
		assert(typed_payload_.has_value() && "Instruction must have typed payload");
		return typed_payload_;
	}

	// Template version that casts to the requested type
	template<typename T>
	const T& getTypedPayload() const {
		assert(typed_payload_.has_value() && "Instruction must have typed payload");
		const T* ptr = std::any_cast<T>(&typed_payload_);
		assert(ptr && "Typed payload has wrong type");
		return *ptr;
	}

	template<typename T>
	T& getTypedPayload() {
		assert(typed_payload_.has_value() && "Instruction must have typed payload");
		T* ptr = std::any_cast<T>(&typed_payload_);
		assert(ptr && "Typed payload has wrong type");
		return *ptr;
	}

private:
	IrOpcode opcode_;
	OperandStorage operands_;
	Token first_token_;
	std::any typed_payload_;  // Optional typed payload
};

class Ir {
public:
	void addInstruction(const IrInstruction& instruction) {
		instructions.push_back(instruction);
	}

	void addInstruction(IrInstruction&& instruction) {
		instructions.push_back(std::move(instruction));
	}

	// Backward compatibility
	void addInstruction(IrOpcode&& opcode,
		std::vector<IrOperand>&& operands, Token first_token) {
		instructions.emplace_back(opcode, std::move(operands), first_token);
	}

	// Add instruction with typed payload (template for any payload type)
	template<typename PayloadType>
	void addInstruction(IrOpcode&& opcode, PayloadType&& payload, Token first_token) {
		instructions.emplace_back(opcode, std::forward<PayloadType>(payload), first_token);
	}

	// Builder-style: start building an instruction
	IrInstruction& beginInstruction(IrOpcode opcode, Token first_token, size_t expected_operand_count = 0) {
		instructions.emplace_back(opcode, first_token, expected_operand_count);
		return instructions.back();
	}

	const std::vector<IrInstruction>& getInstructions() const {
		return instructions;
	}

	// Reserve space for instructions (optimization)
	void reserve(size_t capacity) {
		instructions.reserve(capacity);
		reserved_capacity_ = capacity;
	}

	// Get statistics
	size_t instructionCount() const {
		return instructions.size();
	}

	size_t reservedCapacity() const {
		return reserved_capacity_;
	}

	size_t actualCapacity() const {
		return instructions.capacity();
	}

	// Print statistics
	void printStats() const {
		printf("\n=== IR Instruction Storage Statistics ===\n");
		printf("Reserved capacity: %zu instructions\n", reserved_capacity_);
		printf("Actual used:       %zu instructions\n", instructions.size());
		printf("Vector capacity:   %zu instructions\n", instructions.capacity());
		if (reserved_capacity_ > 0) {
			double usage_percent = (instructions.size() * 100.0) / reserved_capacity_;
			printf("Usage:             %.1f%% of reserved\n", usage_percent);
			if (instructions.size() > reserved_capacity_) {
				printf("WARNING: Exceeded reserved capacity by %zu instructions\n",
				       instructions.size() - reserved_capacity_);
			}
		}
		printf("==========================================\n\n");
	}

private:
	std::vector<IrInstruction> instructions;
	size_t reserved_capacity_ = 0;
};

// Include helper functions now that all types are defined
#include "IROperandHelpers.h"
