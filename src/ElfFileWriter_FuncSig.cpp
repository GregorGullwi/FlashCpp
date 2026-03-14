#include "ObjFileWriter.h"
#include "ElfFileWriter.h"

// ElfFileWriter_FuncSig.cpp - Out-of-line method definitions for ElfFileWriter
// Part of ElfFileWriter class (unity build)

// Overload that accepts pre-computed mangled name (without class_name)
void ElfFileWriter::addFunctionSignature([[maybe_unused]] std::string_view name, const TypeSpecifierNode& return_type,
                          const std::vector<TypeSpecifierNode>& parameter_types,
                          Linkage linkage, bool is_variadic,
                          std::string_view mangled_name, bool is_inline) {
	FunctionSignature sig(return_type, parameter_types);
	sig.linkage = linkage;
	sig.is_variadic = is_variadic;
	sig.is_inline = is_inline;
	function_signatures_[std::string(mangled_name)] = sig;
}

// Overloads for member functions (compatibility with ObjectFileWriter)
std::string_view ElfFileWriter::addFunctionSignature(std::string_view name, const TypeSpecifierNode& return_type,
                                const std::vector<TypeSpecifierNode>& parameter_types,
                                std::string_view class_name, Linkage linkage,
                                bool is_variadic) {
	FunctionSignature sig(return_type, parameter_types);
	sig.class_name = class_name;
	sig.linkage = linkage;
	sig.is_variadic = is_variadic;
	return generateMangledName(name, sig);  // generateMangledName now returns string_view to stable storage
}

// Overload that accepts pre-computed mangled name (for function definitions from IR)
void ElfFileWriter::addFunctionSignature([[maybe_unused]] std::string_view name, const TypeSpecifierNode& return_type,
                          const std::vector<TypeSpecifierNode>& parameter_types,
                          std::string_view class_name, Linkage linkage, bool is_variadic,
                          std::string_view mangled_name, bool is_inline) {
	FunctionSignature sig(return_type, parameter_types);
	sig.class_name = class_name;
	sig.linkage = linkage;
	sig.is_variadic = is_variadic;
	sig.is_inline = is_inline;
	function_signatures_[std::string(mangled_name)] = sig;
}

// Debug info methods - DWARF4 implementation
void ElfFileWriter::add_source_file(const std::string& filename) {
	debug_source_files_.push_back(filename);
}

void ElfFileWriter::set_current_function_for_debug(const std::string& name, uint32_t file_id) {
	if (debug_has_current_)
		debug_functions_.push_back(std::move(debug_current_func_));
	debug_current_func_ = {};
	debug_current_func_.name        = name;
	debug_current_func_.text_offset = debug_pending_text_offset_;
	debug_current_func_.file_id     = file_id;
	debug_has_current_ = true;
}

void ElfFileWriter::add_line_mapping(uint32_t code_offset, uint32_t line_number) {
	if (debug_has_current_)
		debug_current_func_.lines.push_back({code_offset, line_number});
}

void ElfFileWriter::add_local_variable(const std::string& name, uint32_t type_index, [[maybe_unused]] uint16_t flags,
                       const std::vector<CodeView::VariableLocation>& locations) {
	if (!debug_has_current_ || locations.empty()) return;
	DebugVarInfo v;
	v.name = name;
	v.cv_type_index = type_index;
	const auto& loc = locations[0];
	// Prefer stack offset if available (loc.offset != 0 for initialized vars in ELF path)
	if (loc.type == CodeView::VariableLocation::REGISTER && loc.offset != 0) {
		v.is_register = false;
		v.stack_off   = loc.offset;
	} else if (loc.type == CodeView::VariableLocation::REGISTER) {
		v.is_register = true;
		v.dwarf_reg   = dwarfRegFromCodeViewReg(loc.register_code);
	} else {
		v.is_register = false;
		v.stack_off   = loc.offset;
	}
	debug_current_func_.vars.push_back(std::move(v));
}

void ElfFileWriter::add_function_parameter(const std::string& name, uint32_t type_index, int32_t stack_offset) {
	if (!debug_has_current_) return;
	DebugVarInfo v;
	v.name         = name;
	v.cv_type_index = type_index;
	v.is_parameter  = true;
	v.is_register   = false;
	v.stack_off     = stack_offset;
	debug_current_func_.vars.push_back(std::move(v));
}

void ElfFileWriter::update_function_length(const std::string_view mangled_name, uint32_t code_length) {
	if (debug_has_current_ && debug_current_func_.name == mangled_name) {
		debug_current_func_.length = code_length;
	}
	// Always update the symbol size using the actual mangled name
	updateSymbolSize(std::string(mangled_name), code_length);
}

void ElfFileWriter::set_function_debug_range([[maybe_unused]] const std::string_view manged_name, [[maybe_unused]] uint32_t prologue_size, [[maybe_unused]] uint32_t epilogue_size) {
	// Not required for DWARF
}

void ElfFileWriter::finalize_current_function() {
	// Not required for DWARF
}

void ElfFileWriter::finalize_debug_info() {
	// Save last function
	if (debug_has_current_) {
		debug_functions_.push_back(std::move(debug_current_func_));
		debug_has_current_ = false;
	}
	if (!debug_functions_.empty())
		generateDwarfSections();
}

// CFI instruction tracking for a single function (defined here for use in add_function_exception_info)

// Exception handling (placeholders - not implemented for Linux MVP)
