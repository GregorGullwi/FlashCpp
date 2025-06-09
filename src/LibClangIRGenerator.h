#pragma once

#include <string>
#include <vector>
#include "AstNodeTypes.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/TargetMachine.h>
#include <clang-c/Index.h>

namespace FlashCpp {

class LibClangIRGenerator {
public:
    LibClangIRGenerator() = default;

    // Generate COFF object file directly
    bool GenerateCOFF(const std::vector<ASTNode>& astNodes, const std::string& outputFilename);

    // Generate LLVM IR and save to file
    bool GenerateLLVMIR(const std::vector<ASTNode>& astNodes, const std::string& outputFilename);

    // Generate LLVM IR and pass to Clang for compilation
    bool GenerateWithClang(const std::vector<ASTNode>& astNodes, 
                          const std::string& outputFilename,
                          const std::vector<std::string>& clangArgs = {});

private:
    // Helper methods
    LLVMModuleRef CreateModuleFromAST(const std::vector<ASTNode>& astNodes, LLVMContextRef context);
    bool InitializeLLVM();
};

// Free functions for convenience
bool GenerateCOFF(const std::vector<ASTNode>& astNodes, const std::string& outputFilename);
bool GenerateLLVMIR(const std::vector<ASTNode>& astNodes, const std::string& outputFilename);
bool GenerateWithClang(const std::vector<ASTNode>& astNodes, 
                      const std::string& outputFilename,
                      const std::vector<std::string>& clangArgs = {});

} // namespace FlashCpp