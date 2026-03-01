#pragma once

#include <stdexcept>
#include <string>

// Semantic compilation error - distinct from std::runtime_error so that
// per-function codegen error recovery can let these propagate while still
// catching codegen limitation errors (unsupported types, register allocation, etc.)
class CompileError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};
