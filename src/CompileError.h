#pragma once

#include <stdexcept>
#include <string>

// Semantic compilation error - distinct from std::runtime_error so that
// per-function codegen error recovery can let these propagate while still
// catching internal codegen limitation errors.
class CompileError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Internal codegen limitation error - distinct from std::runtime_error so that
// per-function error recovery can catch these specifically while letting
// CompileError (semantic errors) propagate.
// Examples: unsupported types, register allocation failures, unimplemented features.
class InternalError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};
