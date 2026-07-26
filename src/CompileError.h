#pragma once

#include <stdexcept>
#include <string>

// Semantic compilation error - distinct from InternalError so convert() can let
// these propagate unchanged while still catching internal codegen limitations.
// At the IR-to-object boundary, InternalError is rethrown as CompileError with
// the enclosing function name attached.
class CompileError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Internal codegen limitation error - distinct from CompileError so convert()
// can catch these specifically, attach function context, and surface them as
// CompileError while letting semantic CompileError instances propagate.
// Examples: unsupported types, register allocation failures, unimplemented features.
class InternalError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};
