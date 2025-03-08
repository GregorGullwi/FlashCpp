// Disable warnings from LLVM headers
#pragma warning(push)
#pragma warning(disable: 4100)  // unreferenced formal parameter
#pragma warning(disable: 4141)  // 'inline': used more than once
#pragma warning(disable: 4146)  // unary minus operator applied to unsigned type
#pragma warning(disable: 4244)  // possible loss of data
#pragma warning(disable: 4245)  // signed/unsigned mismatch
#pragma warning(disable: 4267)  // possible loss of data
#pragma warning(disable: 4624)  // destructor was implicitly defined as deleted
#pragma warning(disable: 4800)  // forcing value to bool

// Include LLVM/Clang headers here
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <clang/AST/ASTContext.h>

#pragma warning(pop) 