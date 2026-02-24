#pragma once

#include "AstNodeTypes.h"
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <functional>

// Shared structures between ObjectFileWriter and ElfFileWriter
// This avoids duplication while maintaining compatibility

namespace ObjectFileCommon {

	// Transparent hash for std::string-keyed maps enabling string_view lookups without allocation
	struct StringViewHash {
		using is_transparent = void;
		size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
		size_t operator()(const std::string& s) const { return std::hash<std::string_view>{}(s); }
	};

	// Function signature information for mangling
	struct FunctionSignature {
		TypeSpecifierNode return_type;
		std::vector<TypeSpecifierNode> parameter_types;
		bool is_const = false;
		bool is_static = false;
		bool is_variadic = false;  // True if function has ... ellipsis parameter
		bool is_inline = false;    // True if function is inline (for weak symbol binding)
		CallingConvention calling_convention = CallingConvention::Default;
		std::string namespace_name;
		std::string class_name;
		Linkage linkage = Linkage::None;  // C vs C++ linkage

		FunctionSignature() = default;
		FunctionSignature(const TypeSpecifierNode& ret_type, std::vector<TypeSpecifierNode> params)
			: return_type(ret_type), parameter_types(std::move(params)) {}
	};

	// Exception handling information for a catch handler
	struct CatchHandlerInfo {
		uint32_t type_index;      // Type to catch (0 for catch-all)
		uint32_t handler_offset;  // Code offset of catch handler relative to function start
		uint32_t handler_end_offset; // Code offset of catch handler end relative to function start
		uint32_t funclet_entry_offset = 0; // Code offset of catch funclet entry relative to function start
		uint32_t funclet_end_offset = 0;   // Code offset of catch funclet end relative to function start
		bool is_catch_all;        // True for catch(...)
		std::string type_name;    // Name of the caught type (empty for catch-all or when type_index is 0)
		bool is_const;            // True if caught by const
		bool is_reference;        // True if caught by lvalue reference
		bool is_rvalue_reference; // True if caught by rvalue reference
		int32_t catch_obj_offset; // Frame offset where caught exception object is stored (negative RBP offset)
	};

	// Unwind map entry for destructor calls during exception unwinding
	struct UnwindMapEntryInfo {
		int to_state;             // State to transition to after unwinding (-1 = no more unwinding)
		std::string action;       // Name of destructor/cleanup function to call (empty = no action)
	};

	// Exception handling information for a try block
	struct TryBlockInfo {
		uint32_t try_start_offset;  // Code offset where try block starts
		uint32_t try_end_offset;    // Code offset where try block ends
		std::vector<CatchHandlerInfo> catch_handlers;
	};

	// Windows SEH (Structured Exception Handling) information

	// SEH __except handler information
	struct SehExceptHandlerInfo {
		uint32_t handler_offset;  // Code offset of __except handler
		uint32_t filter_result;   // Filter expression evaluation result (temp var number)
		bool is_constant_filter;  // True if filter is a compile-time constant
		int32_t constant_filter_value; // Constant filter value (EXCEPTION_EXECUTE_HANDLER=1, EXCEPTION_CONTINUE_SEARCH=0, etc.)
		uint32_t filter_funclet_offset = 0; // Code offset of filter funclet (for non-constant filters)
	};

	// SEH __finally handler information
	struct SehFinallyHandlerInfo {
		uint32_t handler_offset;  // Code offset of __finally handler
	};

	// SEH try block information
	struct SehTryBlockInfo {
		uint32_t try_start_offset;  // Code offset where __try block starts
		uint32_t try_end_offset;    // Code offset where __try block ends
		bool has_except_handler;    // True if this has an __except handler
		bool has_finally_handler;   // True if this has a __finally handler
		SehExceptHandlerInfo except_handler;   // __except handler (if has_except_handler is true)
		SehFinallyHandlerInfo finally_handler; // __finally handler (if has_finally_handler is true)
	};

	// Base class descriptor info for RTTI emission
	struct BaseClassDescriptorInfo {
		std::string name;            // Base class name
		uint32_t num_contained_bases; // Number of bases this base has
		uint32_t offset;             // Offset of base in derived class (mdisp)
		bool is_virtual;             // Whether this is a virtual base
	};

	// Byte-packing helpers for RTTI/vtable data construction
	// Replaces 30+ inline for-loops with clean one-liners

	/// Append a value in little-endian byte order to a char vector
	template<typename T>
	inline void appendLE(std::vector<char>& buf, T value) {
		for (size_t i = 0; i < sizeof(T); ++i) {
			buf.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
		}
	}

	/// Append N zero bytes to a char vector
	inline void appendZeros(std::vector<char>& buf, size_t count) {
		buf.insert(buf.end(), count, 0);
	}

} // namespace ObjectFileCommon
