#pragma once
#include "IRTypes_Registers.h"

#define USE_GLOBAL_OPERAND_STORAGE

#ifndef USE_GLOBAL_OPERAND_STORAGE

// Vector-based storage (current implementation)
class OperandStorage {
public:
	OperandStorage() = default;

	// Constructor from vector (move semantics) - for backward compatibility
	explicit OperandStorage(std::vector<IrOperand>&& operands)
		: operands_(std::move(operands)) {}

	// Add operand directly (for builder pattern)
	void addOperand(IrOperand&& operand) {
		operands_.push_back(std::move(operand));
	}

	// Reserve space for operands (optimization)
	void reserve(size_t capacity) {
		operands_.reserve(capacity);
	}

	// Get operand count
	size_t size() const {
		return operands_.size();
	}

	// Access operand by index
	const IrOperand& operator[](size_t index) const {
		return operands_[index];
	}

	// Safe access with optional
	std::optional<IrOperand> getSafe(size_t index) const {
		return index < operands_.size()
			? std::optional<IrOperand>{ operands_[index] }
			: std::optional<IrOperand>{};
	}

private:
	std::vector<IrOperand> operands_;
};

#else // USE_GLOBAL_OPERAND_STORAGE

// Chunked storage (optimized implementation)
#include <vector>

// Global operand storage - all operands from all instructions stored here
class GlobalOperandStorage {
public:
	static GlobalOperandStorage& instance() {
		static GlobalOperandStorage storage;
		return storage;
	}

	// Reserve space for expected number of operands (optimization)
	void reserve(size_t capacity) {
		operands_.reserve(capacity);
		reserved_capacity_ = capacity;
	}

	// Add operands from vector and return the start index (for backward compatibility)
	size_t addOperands(std::vector<IrOperand>&& operands) {
		size_t start_index = operands_.size();
		for (auto&& op : operands) {
			operands_.push_back(std::move(op));
		}
		return start_index;
	}

	// Add single operand and return its index (for builder pattern)
	size_t addOperand(IrOperand&& operand) {
		size_t index = operands_.size();
		operands_.push_back(std::move(operand));
		return index;
	}

	// Get operand by global index
	const IrOperand& getOperand(size_t index) const {
		return operands_[index];
	}

	// Get total number of operands stored
	size_t totalOperands() const {
		return operands_.size();
	}

	// Get reserved capacity
	size_t reservedCapacity() const {
		return reserved_capacity_;
	}

	// Get actual capacity
	size_t actualCapacity() const {
		return operands_.capacity();
	}

	// Clear all operands (useful for testing)
	void clear() {
		operands_.clear();
		reserved_capacity_ = 0;
	}

	// Print statistics about operand storage
	void printStats() const {
		printf("\n=== GlobalOperandStorage Statistics ===\n");
		printf("Reserved capacity: %zu operands\n", reserved_capacity_);
		printf("Actual used:       %zu operands\n", operands_.size());
		printf("Vector capacity:   %zu operands\n", operands_.capacity());
		if (reserved_capacity_ > 0) {
			double usage_percent = (operands_.size() * 100.0) / reserved_capacity_;
			printf("Usage:             %.1f%% of reserved\n", usage_percent);
			if (operands_.size() > reserved_capacity_) {
				printf("WARNING: Exceeded reserved capacity by %zu operands\n",
				       operands_.size() - reserved_capacity_);
			}
		}
		printf("========================================\n\n");
	}

private:
	GlobalOperandStorage() = default;

	// Using vector for better performance with reserve()
	std::vector<IrOperand> operands_;
	size_t reserved_capacity_ = 0;
};

// ============================================================================
// Global TempVar Metadata Storage (Option 2 Implementation)
// ============================================================================
// Stores value category and lvalue information for all TempVars
// Uses sparse storage - only TempVars with metadata are stored
// ============================================================================
class GlobalTempVarMetadataStorage {
public:
	static GlobalTempVarMetadataStorage& instance() {
		static GlobalTempVarMetadataStorage storage;
		return storage;
	}
	
	// Set metadata for a TempVar
	void setMetadata(const TempVar& temp, TempVarMetadata metadata) {
		metadata_[temp.var_number] = std::move(metadata);
	}
	
	// Get metadata for a TempVar (returns default if not found)
	TempVarMetadata getMetadata(const TempVar& temp) const {
		auto it = metadata_.find(temp.var_number);
		if (it != metadata_.end()) {
			return it->second;
		}
		// Default: prvalue with no lvalue info
		return TempVarMetadata::makePRValue();
	}
	
	// Check if a TempVar has metadata
	bool hasMetadata(const TempVar& temp) const {
		return metadata_.find(temp.var_number) != metadata_.end();
	}
	
	// Check if a TempVar is an lvalue
	bool isLValue(const TempVar& temp) const {
		auto it = metadata_.find(temp.var_number);
		return it != metadata_.end() && it->second.category == ValueCategory::LValue;
	}
	
	// Check if a TempVar is an xvalue
	bool isXValue(const TempVar& temp) const {
		auto it = metadata_.find(temp.var_number);
		return it != metadata_.end() && it->second.category == ValueCategory::XValue;
	}
	
	// Check if a TempVar is a prvalue
	bool isPRValue(const TempVar& temp) const {
		auto it = metadata_.find(temp.var_number);
		return it == metadata_.end() || it->second.category == ValueCategory::PRValue;
	}
	
	// Get lvalue info if available
	std::optional<LValueInfo> getLValueInfo(const TempVar& temp) const {
		auto it = metadata_.find(temp.var_number);
		if (it != metadata_.end()) {
			return it->second.lvalue_info;
		}
		return std::nullopt;
	}
	
	// Clear all metadata (useful for testing and between compilation units)
	void clear() {
		metadata_.clear();
	}
	
	// Get statistics
	size_t size() const {
		return metadata_.size();
	}
	
	// Print statistics
	void printStats() const {
		FLASH_LOG_FORMAT(General, Debug,
			"TempVar metadata entries: {}", metadata_.size());
		
		size_t lvalue_count = 0;
		size_t xvalue_count = 0;
		size_t prvalue_count = 0;
		
		for (const auto& [var_num, meta] : metadata_) {
			switch (meta.category) {
				case ValueCategory::LValue: ++lvalue_count; break;
				case ValueCategory::XValue: ++xvalue_count; break;
				case ValueCategory::PRValue: ++prvalue_count; break;
			}
		}
		
		FLASH_LOG_FORMAT(General, Debug,
			"  LValues: {}, XValues: {}, PRValues: {}",
			lvalue_count, xvalue_count, prvalue_count);
	}

private:
	GlobalTempVarMetadataStorage() = default;
	
	// Map from TempVar number to metadata
	// Using unordered_map for O(1) lookup and sparse storage
	std::unordered_map<size_t, TempVarMetadata> metadata_;
};

// ============================================================================
// TempVar convenience methods for metadata access
// ============================================================================
// These methods are defined here (after GlobalTempVarMetadataStorage)
// to avoid forward declaration issues
// ============================================================================

inline void setTempVarMetadata(const TempVar& temp, TempVarMetadata meta) {
	GlobalTempVarMetadataStorage::instance().setMetadata(temp, std::move(meta));
}

inline TempVarMetadata getTempVarMetadata(const TempVar& temp) {
	return GlobalTempVarMetadataStorage::instance().getMetadata(temp);
}

inline bool isTempVarLValue(const TempVar& temp) {
	return GlobalTempVarMetadataStorage::instance().isLValue(temp);
}

inline bool isTempVarXValue(const TempVar& temp) {
	return GlobalTempVarMetadataStorage::instance().isXValue(temp);
}

inline bool isTempVarPRValue(const TempVar& temp) {
	return GlobalTempVarMetadataStorage::instance().isPRValue(temp);
}

inline std::optional<LValueInfo> getTempVarLValueInfo(const TempVar& temp) {
	return GlobalTempVarMetadataStorage::instance().getLValueInfo(temp);
}

// Check if a TempVar is a reference (has is_address flag set)
inline bool isTempVarReference(const TempVar& temp) {
	auto meta = GlobalTempVarMetadataStorage::instance().getMetadata(temp);
	return meta.is_address && (meta.category == ValueCategory::LValue || meta.category == ValueCategory::XValue);
}

// Get the value type of a reference TempVar (returns Invalid if not a reference)
inline Type getTempVarValueType(const TempVar& temp) {
	auto meta = GlobalTempVarMetadataStorage::instance().getMetadata(temp);
	return meta.value_type;
}

// ============================================================================
// RVO/NRVO (Return Value Optimization) helper functions
// ============================================================================

// Check if a TempVar is eligible for RVO (mandatory C++17 copy elision)
inline bool isTempVarRVOEligible(const TempVar& temp) {
	auto meta = GlobalTempVarMetadataStorage::instance().getMetadata(temp);
	return meta.eligible_for_rvo && meta.category == ValueCategory::PRValue;
}

// Check if a TempVar is eligible for NRVO (named return value optimization)
inline bool isTempVarNRVOEligible(const TempVar& temp) {
	auto meta = GlobalTempVarMetadataStorage::instance().getMetadata(temp);
	return meta.eligible_for_nrvo;
}

// Mark a TempVar as being returned from a function (for RVO/NRVO analysis)
inline void markTempVarAsReturnValue(const TempVar& temp) {
	auto meta = GlobalTempVarMetadataStorage::instance().getMetadata(temp);
	meta.is_return_value = true;
	GlobalTempVarMetadataStorage::instance().setMetadata(temp, std::move(meta));
}

// Get the value size in bits of a reference TempVar (returns 0 if not a reference)
inline int getTempVarValueSizeBits(const TempVar& temp) {
	auto meta = GlobalTempVarMetadataStorage::instance().getMetadata(temp);
	return meta.value_size_bits;
}

// Check if a TempVar is an rvalue reference
inline bool isTempVarRValueReference(const TempVar& temp) {
	auto meta = GlobalTempVarMetadataStorage::instance().getMetadata(temp);
	return meta.is_rvalue_reference;
}

// Helper to create a TempVar with lvalue metadata
inline TempVar makeLValueTempVar(TempVar temp, LValueInfo lv_info) {
	setTempVarMetadata(temp, TempVarMetadata::makeLValue(std::move(lv_info)));
	return temp;
}

// Helper to create a TempVar with xvalue metadata
inline TempVar makeXValueTempVar(TempVar temp, LValueInfo lv_info) {
	setTempVarMetadata(temp, TempVarMetadata::makeXValue(std::move(lv_info)));
	return temp;
}

// Helper to create a TempVar with prvalue metadata
inline TempVar makePRValueTempVar(TempVar temp) {
	setTempVarMetadata(temp, TempVarMetadata::makePRValue());
	return temp;
}

class OperandStorage {
public:
	OperandStorage() : start_index_(0), count_(0) {}

	// Constructor from vector (move semantics) - for backward compatibility
	explicit OperandStorage(std::vector<IrOperand>&& operands)
		: count_(operands.size()) {
		if (count_ > 0) {
			start_index_ = GlobalOperandStorage::instance().addOperands(std::move(operands));
		} else {
			start_index_ = 0;
		}
	}

	// Add operand directly (for builder pattern)
	void addOperand(IrOperand&& operand) {
		if (count_ == 0) {
			// First operand - record the start index
			start_index_ = GlobalOperandStorage::instance().addOperand(std::move(operand));
		} else {
			// Subsequent operands - they should be contiguous
			GlobalOperandStorage::instance().addOperand(std::move(operand));
		}
		++count_;
	}

	// Reserve space (no-op for chunked storage, but kept for API compatibility)
	void reserve([[maybe_unused]] size_t capacity) {
		// No-op: deque doesn't need reservation
	}

	// Get operand count
	size_t size() const {
		return count_;
	}

	// Access operand by index
	const IrOperand& operator[](size_t index) const {
		return GlobalOperandStorage::instance().getOperand(start_index_ + index);
	}

	// Safe access with optional
	std::optional<IrOperand> getSafe(size_t index) const {
		return index < count_
			? std::optional<IrOperand>{ (*this)[index] }
			: std::optional<IrOperand>{};
	}

private:
	size_t start_index_;  // Index into global storage
	size_t count_;        // Number of operands
};

#endif

// ============================================================================
// Typed IR Operand Structures
// ============================================================================

// Typed value - combines IrValue with its type information
struct TypedValue {
	Type type = Type::Void;	// 4 bytes (enum)
	int size_in_bits = 0;	// 4 bytes
	IrValue value;          // 32 bytes (variant)
	ReferenceQualifier ref_qualifier = ReferenceQualifier::None;  // None, LValueReference (&), or RValueReference (&&)
	bool is_signed = false;     // True for signed types (use MOVSX), false for unsigned (use MOVZX)
	TypeIndex type_index = 0;   // Index into gTypeInfo for struct/enum types (0 = not set)
	int pointer_depth = 0;      // Number of pointer indirection levels (0 = not a pointer, 1 = T*, 2 = T**, etc.)
	CVQualifier cv_qualifier = CVQualifier::None;  // CV qualifier for references (const, volatile, etc.)
	
	// Helper methods for reference checks
	bool is_reference() const { return ref_qualifier != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return ref_qualifier == ReferenceQualifier::RValueReference; }
	bool is_lvalue_reference() const { return ref_qualifier == ReferenceQualifier::LValueReference; }
};

// Helper function to print TypedValue
inline void printTypedValue(std::ostringstream& oss, const TypedValue& typedValue) {
	if (std::holds_alternative<unsigned long long>(typedValue.value))
		oss << std::get<unsigned long long>(typedValue.value);
	else if (std::holds_alternative<double>(typedValue.value))
		oss << std::get<double>(typedValue.value);
	else if (std::holds_alternative<TempVar>(typedValue.value))
		oss << '%' << std::get<TempVar>(typedValue.value).var_number;
	else if (std::holds_alternative<StringHandle>(typedValue.value)) {
		StringHandle handle = std::get<StringHandle>(typedValue.value);
		oss << '%' << StringTable::getStringView(handle);
	}
	else
		assert(false && "unsupported typed value");
}

// Binary operations (Add, Subtract, Multiply, Divide, comparisons, etc.)
struct BinaryOp {
	TypedValue lhs;     // 40 bytes (value + type)
	TypedValue rhs;     // 40 bytes
	IrValue result;     // 32 bytes (changed from TempVar to support both temps and variables)
};

// Conditional branch (If)
struct CondBranchOp {
	StringHandle label_true;     // Pure StringHandle
	StringHandle label_false;    // Pure StringHandle
	TypedValue condition;            // 40 bytes (value + type)
	
	// Helper methods
	StringHandle getLabelTrue() const {
		return label_true;
	}
	
	StringHandle getLabelFalse() const {
		return label_false;
	}
};

// Function call
struct CallOp {
	StringHandle function_name;  // Pure StringHandle
	std::vector<TypedValue> args;         // 24 bytes (using TypedValue instead of CallArg)
	TempVar result;                       // 4 bytes
	Type return_type;                     // 4 bytes
	int return_size_in_bits;              // 4 bytes
	TypeIndex return_type_index = 0;      // Type index for struct/class return types
	bool is_member_function = false;      // 1 byte
	bool is_variadic = false;             // 1 byte
	bool is_indirect_call = false;        // 1 byte - True if calling through function pointer/reference
	bool returns_rvalue_reference = false; // 1 byte - True if function returns T&&
	std::optional<TempVar> return_slot;   // Optional temp var representing the return slot location
	
	// Helper to get function_name as StringHandle
	StringHandle getFunctionName() const {
		return function_name;
	}
	
	// Helper to check if using hidden return parameter for RVO
	// Returns true if return_slot is set (instead of duplicating this as a separate bool)
	bool usesReturnSlot() const {
		return return_slot.has_value();
	}
};

// Member access (load member from struct/class)
struct MemberLoadOp {
	TypedValue result;                              // The loaded member value (type, size, result_var)
	std::variant<StringHandle, TempVar> object; // Base object instance
	StringHandle member_name;                       // Which member to access
	int offset;                                     // Byte offset in struct
	const TypeInfo* struct_type_info;               // Parent struct type (nullptr if not available)
	bool is_reference;                              // True if member is declared as T& (describes member declaration, not access)
	bool is_rvalue_reference;                       // True if member is declared as T&& (describes member declaration, not access)
	bool is_pointer_to_member = false;              // True if accessing through pointer (ptr->member), false for direct (obj.member)
	std::optional<size_t> bitfield_width;           // Width in bits for bitfield members
	size_t bitfield_bit_offset = 0;                 // Bit offset within the storage unit for bitfield members
};

// Member store (store value to struct/class member)
struct MemberStoreOp {
	TypedValue value;                               // Value to store (type, size, value_var)
	std::variant<StringHandle, TempVar> object; // Target object instance
	StringHandle member_name;                       // Which member to store to
	int offset;                                     // Byte offset in struct
	const TypeInfo* struct_type_info;               // Parent struct type (nullptr if not available)
	bool is_reference;                              // True if member is declared as T& (describes member declaration, not access)
	bool is_rvalue_reference;                       // True if member is declared as T&& (describes member declaration, not access)
	StringHandle vtable_symbol;						// For vptr initialization - stores vtable symbol name
	bool is_pointer_to_member = false;              // True if accessing through pointer (ptr->member), false for direct (obj.member)
	std::optional<size_t> bitfield_width;           // Width in bits for bitfield members
	size_t bitfield_bit_offset = 0;                 // Bit offset within the storage unit for bitfield members
};

// Label definition
struct LabelOp {
	StringHandle label_name;  // Pure StringHandle
	
	// Helper to get label_name as StringHandle
	StringHandle getLabelName() const {
		return label_name;
	}
};

// Unconditional branch
struct BranchOp {
	StringHandle target_label;  // Pure StringHandle
	
	// Helper to get target_label as StringHandle
	StringHandle getTargetLabel() const {
		return target_label;
	}
};

// Return statement
struct ReturnOp {
	std::optional<IrValue> return_value;    // ~40 bytes
	std::optional<Type> return_type;        // ~8 bytes
	int return_size = 0;                    // 4 bytes
};

// Array access (load element from array)
struct ArrayAccessOp {
	TempVar result;									// Result temp var
	Type element_type = Type::Invalid;				// Element type
	int element_size_in_bits = 0;					// Element size
	std::variant<StringHandle, TempVar> array;		// Array (StringHandle for variables, TempVar for temporaries)
	TypedValue index;								// Index value (type + value)
	int64_t member_offset = 0;						// Offset in bytes for member arrays (0 for non-member)
	bool is_pointer_to_array = false;				// True if 'array' is a pointer (int* arr), false if actual array (int arr[])
};

// Array store (store value to array element)
struct ArrayStoreOp {
	Type element_type = Type::Invalid;				// Element type
	int element_size_in_bits = 0;					// Element size
	std::variant<StringHandle, TempVar> array;		// Array (StringHandle for variables, TempVar for temporaries)
	TypedValue index;								// Index value (type + value)
	TypedValue value;								// Value to store
	int64_t member_offset = 0;						// Offset in bytes for member arrays (0 for non-member)
	bool is_pointer_to_array = false;				// True if 'array' is a pointer (int* arr), false if actual array (int arr[])
};

// Array element address (get address without loading)
struct ArrayElementAddressOp {
	TempVar result;									// Result temp var (pointer to element)
	Type element_type = Type::Invalid;				// Element type
	int element_size_in_bits = 0;					// Element size
	std::variant<StringHandle, TempVar> array;		// Array (StringHandle for variables, TempVar for temporaries)
	TypedValue index;								// Index value (type + value)
	bool is_pointer_to_array = false;				// True if 'array' is a pointer (int* arr), false if actual array (int arr[])
};

// Address-of operator (&x)
struct AddressOfOp {
	TempVar result;                                  // Result temp var (pointer)
	TypedValue operand;                              // Variable or temp to take address of (with full type info)
};

// AddressOf for struct member (&obj.member)
struct AddressOfMemberOp {
	TempVar result;									// Result temp var (pointer to member)
	StringHandle base_object;						// Base object (variable name)
	int member_offset = 0;							// Byte offset of member in struct
	Type member_type;								// Type of the member
	int member_size_in_bits = 0;					// Size of member
};

// One-pass address computation for complex expressions (&arr[i].member1.member2)
struct ComputeAddressOp {
	TempVar result;                                  // Result temporary variable
	
	// Base address (one of these)
	std::variant<StringHandle, TempVar> base;       // Variable name or temp
	
	// Array indexing (optional, can have multiple for nested arrays)
	struct ArrayIndex {
		std::variant<unsigned long long, TempVar, StringHandle> index;
		int element_size_bits;                       // Size of array element
		Type index_type;                             // Type of the index (for proper sign extension)
		int index_size_bits;                         // Size of the index in bits
	};
	std::vector<ArrayIndex> array_indices;
	
	// Member offset accumulation (for chained member access)
	int total_member_offset = 0;                     // Sum of all member offsets
	
	Type result_type = Type::Invalid;                // Type of final address
	int result_size_bits;                            // Size in bits
};

// Dereference operator (*ptr)
struct DereferenceOp {
	TempVar result;                                  // Result temp var
	TypedValue pointer;                              // Pointer to dereference (with full type info including pointer_depth)
};

// Dereference store operator (*ptr = value)
struct DereferenceStoreOp {
	TypedValue value;                                // Value to store
	TypedValue pointer;                              // Pointer to store through (with full type info including pointer_depth)
};

// Constructor call (invoke constructor on object)
struct ConstructorCallOp {
	StringHandle struct_name;                         // Pure StringHandle
	std::variant<StringHandle, TempVar> object;  // Object instance ('this' or temp)
	std::vector<TypedValue> arguments;               // Constructor arguments
	bool use_return_slot = false;                    // True if constructing into caller's return slot (RVO)
	std::optional<int> return_slot_offset;           // Stack offset of return slot (for RVO)
	bool is_heap_allocated = false;                  // True if object is at pointer location (new/placement new), false for stack objects (RVO/member init)
	std::optional<size_t> array_index;               // For array element construction: index of element to construct
	int base_class_offset = 0;                       // Offset to add to 'this' pointer when calling base class constructors in multiple inheritance
};

// Destructor call (invoke destructor on object)
struct DestructorCallOp {
	StringHandle struct_name;                         // Pure StringHandle
	std::variant<StringHandle, TempVar> object;       // Object instance ('this' or temp)
	bool object_is_pointer = false;                   // True if object holds a pointer (heap-allocated)
};

// Virtual function call through vtable
struct VirtualCallOp {
	TypedValue result;                               // Return value (type, size, and result temp var)
	Type object_type;                                // Type of the object
	int object_size;                                 // Size of object in bits
	std::variant<StringHandle, TempVar> object;  // Object instance ('this')
	int vtable_index;                                // Index into vtable
	std::vector<TypedValue> arguments;               // Call arguments
	bool is_pointer_access = false;                  // True if object is a pointer (ptr->method)
};

// String literal
struct StringLiteralOp {
	std::variant<StringHandle, TempVar> result;  // Result variable
	std::string_view content;                         // String content
};

// Stack allocation
struct StackAllocOp {
	std::variant<StringHandle, TempVar> result;  // Result variable
	Type type = Type::Invalid;                        // Type being allocated
	int size_in_bits = 0;                             // Size in bits
};

// Assignment operation
// NOTE: There is no separate StoreOp - AssignmentOp handles both direct assignment and
// indirect stores (assignment through pointers). Use is_pointer_store=true for indirect stores.
// This design keeps the IR simpler while supporting:
// 1. Direct assignment: x = 5 (lhs is variable/tempvar)
// 2. Assignment through pointer: *ptr = 5 (lhs is pointer, is_pointer_store=true)
// 3. Reference member assignment: obj.ref = 5 (loads ref pointer, then stores through it)
struct AssignmentOp {
	std::variant<StringHandle, TempVar> result;  // Result variable (usually same as lhs)
	TypedValue lhs;                                   // Left-hand side (destination)
	TypedValue rhs;                                   // Right-hand side (source)
	bool is_pointer_store = false;                    // True if lhs is a pointer and we should store through it
	bool dereference_rhs_references = true;           // True if RHS references should be dereferenced (default), false to just copy the pointer
};

// Loop begin (marks loop start with labels for break/continue)
struct LoopBeginOp {
	StringHandle loop_start_label;                    // Label for loop start
	StringHandle loop_end_label;                      // Label for break
	StringHandle loop_increment_label;                // Label for continue
};

// Function parameter information
struct FunctionParam {
	Type type = Type::Invalid;
	int size_in_bits = 0;
	int pointer_depth = 0;
	StringHandle name;  // Pure StringHandle
	bool is_reference = false;
	bool is_rvalue_reference = false;
	CVQualifier cv_qualifier = CVQualifier::None;
	
	// Helper to get name as StringHandle
	StringHandle getName() const {
		return name;
	}
};

// Function declaration
struct FunctionDeclOp {
	Type return_type = Type::Void;
	int return_size_in_bits = 0;
	int return_pointer_depth = 0;
	TypeIndex return_type_index = 0;  // Type index for struct/class return types
	bool returns_reference = false;   // True if function returns a reference (T& or T&&)
	bool returns_rvalue_reference = false;  // True if function returns an rvalue reference (T&&)
	StringHandle function_name;  // Pure StringHandle
	StringHandle struct_name;  // Empty for non-member functions
	Linkage linkage = Linkage::None;
	bool is_variadic = false;
	bool has_hidden_return_param = false;  // True if function uses hidden return parameter (struct return)
	bool is_inline = false;  // True if function is inline or implicitly inline (e.g., defined in class body)
	bool is_static_member = false;  // True if this is a static member function (no 'this' pointer)
	StringHandle mangled_name;  // Pure StringHandle
	std::vector<FunctionParam> parameters;
	int temp_var_stack_bytes = 0;  // Total stack space needed for TempVars (set after function body is processed)
	
	// Helper methods
	StringHandle getFunctionName() const {
		return function_name;
	}
	
	StringHandle getStructName() const {
		return struct_name;
	}
	
	StringHandle getMangledName() const {
		return mangled_name;
	}
};

// Unary operations (Negate, LogicalNot, BitwiseNot)
struct UnaryOp {
	TypedValue value;    // 40 bytes
	TempVar result;      // 4 bytes
};

// Helper function to format unary operations for IR output
inline std::string formatUnaryOp(const char* op_name, const UnaryOp& op) {
	std::ostringstream oss;
	
	// Result variable
	oss << '%' << op.result.var_number << " = " << op_name << " ";
	
	// Type and size
	auto type_info = gNativeTypes.find(op.value.type);
	if (type_info != gNativeTypes.end()) {
		oss << type_info->second->name();
	}
	oss << op.value.size_in_bits << " ";
	
	// Operand value
	if (std::holds_alternative<TempVar>(op.value.value)) {
		oss << '%' << std::get<TempVar>(op.value.value).var_number;
	} else if (std::holds_alternative<StringHandle>(op.value.value)) {
		oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.value.value));
	} else if (std::holds_alternative<unsigned long long>(op.value.value)) {
		oss << std::get<unsigned long long>(op.value.value);
	}
	
	return oss.str();
}

// Type conversion operations (SignExtend, ZeroExtend, Truncate)
struct ConversionOp {
	TypedValue from;     // 40 bytes (source type, size, and value)
	Type to_type = Type::Invalid;  // 4 bytes
	int to_size = 0;     // 4 bytes
	TempVar result;      // 4 bytes
};

// Global variable load
struct GlobalLoadOp {
	TypedValue result;           // Result with type, size, and temp var
	StringHandle global_name;  // Pure StringHandle
	bool is_array = false;       // If true, load address (LEA) instead of value (MOV)
	
	// Helper to get global_name as StringHandle
	StringHandle getGlobalName() const {
		return global_name;
	}
};

// Function address (get address of a function)
struct FunctionAddressOp {
	TypedValue result;           // Result with type, size, and temp var (function pointer)
	StringHandle function_name;  // Pure StringHandle
	StringHandle mangled_name;   // Pure StringHandle (optional, for lambdas)
	
	// Helper methods
	StringHandle getFunctionName() const {
		return function_name;
	}
	
	StringHandle getMangledName() const {
		return mangled_name;
	}
};

// Variable declaration (local)
struct VariableDeclOp {
	Type type = Type::Invalid;
	int size_in_bits = 0;
	StringHandle var_name;  // Pure StringHandle
	unsigned long long custom_alignment = 0;
	bool is_reference = false;
	bool is_rvalue_reference = false;
	bool is_array = false;
	// Array info (if is_array)
	std::optional<Type> array_element_type;
	std::optional<int> array_element_size;
	std::optional<size_t> array_count;
	// Initializer (if present)
	std::optional<TypedValue> initializer;
	
	// Helper to get var_name as string_view
	std::string_view getVarName() const {
		return StringTable::getStringView(var_name);
	}
};

// Global variable declaration
struct GlobalVariableDeclOp {
	Type type = Type::Invalid;
	int size_in_bits = 0;          // Size of one element in bits
	StringHandle var_name;  // Pure StringHandle
	bool is_initialized = false;
	size_t element_count = 1;       // Number of elements (1 for scalars, N for arrays)
	std::vector<char> init_data;    // Raw bytes for initialized data
	
	// Helper to get var_name as StringHandle
	StringHandle getVarName() const {
		return var_name;
	}
};

// Heap allocation (new operator)
struct HeapAllocOp {
	TempVar result;              // Result pointer variable
	Type type = Type::Invalid;
	int size_in_bytes = 0;
	int pointer_depth = 0;
};

// Heap array allocation (new[] operator)
struct HeapAllocArrayOp {
	TempVar result;              // Result pointer variable
	Type type = Type::Invalid;
	int size_in_bytes = 0;
	int pointer_depth = 0;
	IrValue count;               // Array element count (TempVar or constant)
	bool needs_cookie = false;   // If true, prepend 8-byte count cookie; result points past cookie
};

// Heap free (delete operator)
struct HeapFreeOp {
	IrValue pointer;             // Pointer to free (TempVar or string_view)
};

// Heap array free (delete[] operator)
struct HeapFreeArrayOp {
	IrValue pointer;             // Pointer to free (TempVar or string_view)
	bool has_cookie = false;     // If true, pointer is past a cookie; free pointer-8
};

// Placement new operator
struct PlacementNewOp {
	TempVar result;              // Result pointer variable
	Type type = Type::Invalid;
	int size_in_bytes = 0;
	int pointer_depth = 0;
	IrValue address;             // Placement address (TempVar, string_view, or constant)
};

// Type conversion operations (FloatToInt, IntToFloat, FloatToFloat)
struct TypeConversionOp {
	TempVar result;              // Result variable
	TypedValue from;             // Source value with type information
	Type to_type = Type::Invalid;   // Target type
	int to_size_in_bits = 0;     // Target size
};

// RTTI: typeid operation
struct TypeidOp {
	TempVar result;              // Result variable (pointer to type_info)
	std::variant<StringHandle, TempVar> operand;  // Type name (StringHandle) or expression (TempVar)
	bool is_type = false;        // true if typeid(Type), false if typeid(expr)
};

// RTTI: dynamic_cast operation
struct DynamicCastOp {
	TempVar result;              // Result variable
	TempVar source;              // Source pointer/reference
	std::string target_type_name;  // Target type name
	bool is_reference = false;   // true for references (throws on failure), false for pointers (returns nullptr)
};

// Function pointer call
struct IndirectCallOp {
	TempVar result;                      // Result variable
	std::variant<StringHandle, TempVar> function_pointer;  // Function pointer variable
	std::vector<TypedValue> arguments;   // Arguments with type information
};

// Catch block begin marker
struct CatchBeginOp {
	TempVar exception_temp;       // Temporary holding the exception object
	TypeIndex type_index;         // Type index for user-defined types
	Type exception_type;          // Type enum for built-in types (Int, Double, etc.)
	std::string_view catch_end_label;  // Label to jump to if not matched
	std::string_view continuation_label;  // Parent-function continuation label after catch completes
	bool is_const;                // True if caught by const
	bool is_reference;            // True if caught by lvalue reference  
	bool is_rvalue_reference;     // True if caught by rvalue reference
	bool is_catch_all;            // True for catch(...) - catches all exceptions
};

// Catch block end marker
struct CatchEndOp {
	std::string_view continuation_label;  // Label to continue parent function execution after catch funclet returns
};

// Throw exception operation
struct ThrowOp {
	TypeIndex type_index;         // Type of exception being thrown
	Type exception_type;          // Actual Type enum for built-in types
	size_t size_in_bytes;         // Size of exception object in bytes
	IrValue exception_value;      // Value to throw (TempVar, unsigned long long, double, or StringHandle)
	bool is_rvalue;               // True if throwing an rvalue (can be moved)
};

// ============================================================================
// Windows SEH (Structured Exception Handling) Operations
// ============================================================================

// SEH __except handler begin marker
struct SehExceptBeginOp {
	TempVar filter_result;        // Temporary holding the filter expression result (for non-constant filters)
	bool is_constant_filter;      // True if filter is a compile-time constant
	int32_t constant_filter_value; // Constant filter value (EXCEPTION_EXECUTE_HANDLER=1, EXCEPTION_CONTINUE_SEARCH=0, etc.)
	std::string_view except_end_label;  // Label to jump to after __except block
};

// SEH __finally funclet call for normal (non-exception) flow
struct SehFinallyCallOp {
	std::string_view funclet_label;  // __finally funclet entry label
	std::string_view end_label;      // Label after __finally (continue execution)
};

// SEH filter funclet end - return filter result in EAX
struct SehFilterEndOp {
	TempVar filter_result;     // Temporary holding the filter expression result (used when !is_constant_result)
	bool is_constant_result;   // True if the filter result is a compile-time constant
	int32_t constant_result;   // Constant filter result value (used when is_constant_result)
};

// SEH __leave operation - jumps to end of current __try block
struct SehLeaveOp {
	std::string_view target_label;  // Label to jump to (end of __try block or __finally)
};

// SEH GetExceptionCode() / GetExceptionInformation() intrinsic result
struct SehExceptionIntrinsicOp {
	TempVar result;  // Temporary to store the result
};

// SEH SehSaveExceptionCode: save ExceptionCode from filter funclet's [rsp+8] to parent frame slot
struct SehSaveExceptionCodeOp {
	TempVar saved_var;  // Parent-frame temp var to save exception code into
};

// SEH SehGetExceptionCodeBody: read exception code from parent-frame slot in __except body
struct SehGetExceptionCodeBodyOp {
	TempVar saved_var;  // Parent-frame slot where exception code was saved during filter
	TempVar result;     // Temporary to store the loaded exception code
};

// SEH _abnormal_termination() / AbnormalTermination(): reads ECX saved in __finally funclet prologue
struct SehAbnormalTerminationOp {
	TempVar result;  // Temporary to store the result (0=normal, non-zero=exception unwind)
};

// Helper function to format conversion operations for IR output
inline std::string formatConversionOp(const char* op_name, const ConversionOp& op) {
	std::ostringstream oss;
	
	// Result variable
	oss << '%' << op.result.var_number << " = " << op_name << " ";
	
	// From type and size
	auto from_type_info = gNativeTypes.find(op.from.type);
	if (from_type_info != gNativeTypes.end()) {
		oss << from_type_info->second->name();
	}
	oss << op.from.size_in_bits << " ";
	
	// Source value
	if (std::holds_alternative<TempVar>(op.from.value)) {
		oss << '%' << std::get<TempVar>(op.from.value).var_number;
	} else if (std::holds_alternative<unsigned long long>(op.from.value)) {
		oss << std::get<unsigned long long>(op.from.value);
	} else if (std::holds_alternative<StringHandle>(op.from.value)) {
		oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.from.value));
	}
	
	oss << " to ";
	
	// To type and size
	auto to_type_info = gNativeTypes.find(op.to_type);
	if (to_type_info != gNativeTypes.end()) {
		oss << to_type_info->second->name();
	}
	oss << op.to_size;
	
	return oss.str();
}

// Helper function to format binary operations for IR output
inline std::string formatBinaryOp(const char* op_name, const BinaryOp& op) {
	std::ostringstream oss;
	
	// Result variable (now an IrValue that could be TempVar or string_view)
	oss << '%';
	if (std::holds_alternative<TempVar>(op.result)) {
		oss << std::get<TempVar>(op.result).var_number;
	} else if (std::holds_alternative<StringHandle>(op.result)) {
		oss << StringTable::getStringView(std::get<StringHandle>(op.result));
	}
	oss << " = " << op_name << " ";
	
	// Type and size (from LHS, but both sides should be same after type promotion)
	auto type_info = gNativeTypes.find(op.lhs.type);
	if (type_info != gNativeTypes.end()) {
		oss << type_info->second->name();
	}
	oss << op.lhs.size_in_bits << " ";
	
	// LHS value
	if (std::holds_alternative<unsigned long long>(op.lhs.value)) {
		oss << std::get<unsigned long long>(op.lhs.value);
	} else if (std::holds_alternative<double>(op.lhs.value)) {
		oss << std::get<double>(op.lhs.value);
	} else if (std::holds_alternative<TempVar>(op.lhs.value)) {
		oss << '%' << std::get<TempVar>(op.lhs.value).var_number;
	} else if (std::holds_alternative<StringHandle>(op.lhs.value)) {
		oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.lhs.value));
	}
	
	oss << ", ";
	
	// RHS value
	if (std::holds_alternative<unsigned long long>(op.rhs.value)) {
		oss << std::get<unsigned long long>(op.rhs.value);
	} else if (std::holds_alternative<double>(op.rhs.value)) {
		oss << std::get<double>(op.rhs.value);
	} else if (std::holds_alternative<TempVar>(op.rhs.value)) {
		oss << '%' << std::get<TempVar>(op.rhs.value).var_number;
	} else if (std::holds_alternative<StringHandle>(op.rhs.value)) {
		oss << '%' << StringTable::getStringView(std::get<StringHandle>(op.rhs.value));
	}
	
	return oss.str();
}

// Helper functions for converting enums to strings (for IR printing)
inline std::string linkageToString(Linkage linkage) {
	switch (linkage) {
		case Linkage::None: return "";
		case Linkage::C: return "extern \"C\"";
		case Linkage::CPlusPlus: return "";  // Default C++ linkage
		case Linkage::DllImport: return "dllimport";
		case Linkage::DllExport: return "dllexport";
		default: return "";
	}
}

inline std::string cvQualifierToString(CVQualifier cv) {
	switch (cv) {
		case CVQualifier::None: return "";
		case CVQualifier::Const: return "const";
		case CVQualifier::Volatile: return "volatile";
		case CVQualifier::ConstVolatile: return "const volatile";
		default: return "";
	}
}

// Helper function to format call operations for IR output
// ============================================================================
// Typed IR Operand Payload - Optional typed alternative to vector operands
// ============================================================================
// Use std::any to store typed payloads (handles incomplete types)

