#include "LibClangIRGenerator.h"
#include "AstNodeTypes.h" // Include your AST node definitions
#include <clang-c/Index.h>
#include <iostream>
#include <sstream>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/TargetMachine.h>
#include <filesystem>
#include <cstdlib>

namespace FlashCpp {

namespace {
    bool g_llvmInitialized = false;
}

bool LibClangIRGenerator::InitializeLLVM() {
    if (g_llvmInitialized) return true;
    
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();
    
    g_llvmInitialized = true;
    return true;
}

LLVMModuleRef LibClangIRGenerator::CreateModuleFromAST(const std::vector<ASTNode>& astNodes, LLVMContextRef context) {
    LLVMModuleRef module = LLVMModuleCreateWithName("flash_module");
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(context);

    // Generate LLVM IR from the AST nodes
    for (const auto& node : astNodes) {
        if (node.is<DeclarationNode>()) {
            const DeclarationNode& decl = node.as<DeclarationNode>();
            LLVMTypeRef intType = LLVMIntTypeInContext(context, 32);
            LLVMValueRef globalVariable = LLVMAddGlobal(module, intType, decl.identifier_token().value().data());
            LLVMSetInitializer(globalVariable, LLVMConstInt(intType, 0, 0));
        } else if (node.is<NumericLiteralNode>()) {
            const NumericLiteralNode& literal = node.as<NumericLiteralNode>();
            LLVMTypeRef intType = LLVMIntTypeInContext(context, 32);
            LLVMValueRef constant = LLVMConstInt(intType, std::stoll(literal.token().data()), 0);
        }
        // Add more node type handling here
    }

    LLVMDisposeBuilder(builder);
    return module;
}

bool LibClangIRGenerator::GenerateLLVMIR(const std::vector<ASTNode>& astNodes, const std::string& outputFilename) {
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module = CreateModuleFromAST(astNodes, context);

    // Verify the module
    char* error = nullptr;
    if (LLVMVerifyModule(module, LLVMAbortProcessAction, &error)) {
        std::cerr << "Error: " << error << std::endl;
        LLVMDisposeMessage(error);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        return false;
    }

    // Write LLVM IR to file
    if (LLVMPrintModuleToFile(module, outputFilename.c_str(), &error)) {
        std::cerr << "Error writing LLVM IR: " << error << std::endl;
        LLVMDisposeMessage(error);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        return false;
    }

    LLVMDisposeModule(module);
    LLVMContextDispose(context);
    return true;
}

bool LibClangIRGenerator::GenerateWithClang(const std::vector<ASTNode>& astNodes, 
                                          const std::string& outputFilename,
                                          const std::vector<std::string>& clangArgs) {
    // First generate LLVM IR to a temporary file
    std::string tempIRFile = outputFilename + ".ll";
    if (!GenerateLLVMIR(astNodes, tempIRFile)) {
        return false;
    }

    // Build clang command
    std::stringstream cmd;
    cmd << "clang ";
    
    // Add user-provided arguments
    for (const auto& arg : clangArgs) {
        cmd << arg << " ";
    }
    
    // Add our files
    cmd << tempIRFile << " -o " << outputFilename;
    
    // Execute clang
    int result = std::system(cmd.str().c_str());
    
    // Clean up temporary file
    std::filesystem::remove(tempIRFile);
    
    return result == 0;
}

bool LibClangIRGenerator::GenerateCOFF(const std::vector<ASTNode>& astNodes, const std::string& outputFilename) {
    if (!InitializeLLVM()) {
        return false;
    }

    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module = CreateModuleFromAST(astNodes, context);

    // Create target machine
    char* targetTriple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char* error = nullptr;
    
    if (LLVMGetTargetFromTriple(targetTriple, &target, &error)) {
        std::cerr << "Error: Could not get target from triple." << std::endl;
        LLVMDisposeMessage(targetTriple);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        return false;
    }

    // Create target machine with minimal options
    LLVMTargetMachineRef targetMachine = LLVMCreateTargetMachine(
        target,
        targetTriple,
        "generic",
        "",
        LLVMCodeGenLevelDefault,
        LLVMRelocDefault,
        LLVMCodeModelDefault);

    LLVMDisposeMessage(targetTriple);

    // Set data layout
    LLVMTargetDataRef dataLayout = LLVMCreateTargetDataLayout(targetMachine);
    LLVMSetModuleDataLayout(module, dataLayout);

    // Write to file
    if (LLVMTargetMachineEmitToFile(targetMachine, module, outputFilename.c_str(), LLVMObjectFile, &error)) {
        std::cerr << "Error: " << error << std::endl;
        LLVMDisposeMessage(error);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        LLVMDisposeTargetMachine(targetMachine);
        return false;
    }

    // Cleanup
    LLVMDisposeModule(module);
    LLVMContextDispose(context);
    LLVMDisposeTargetMachine(targetMachine);

    return true;
}

// Free function implementations
bool GenerateCOFF(const std::vector<ASTNode>& astNodes, const std::string& outputFilename) {
    LibClangIRGenerator generator;
    return generator.GenerateCOFF(astNodes, outputFilename);
}

bool GenerateLLVMIR(const std::vector<ASTNode>& astNodes, const std::string& outputFilename) {
    LibClangIRGenerator generator;
    return generator.GenerateLLVMIR(astNodes, outputFilename);
}

bool GenerateWithClang(const std::vector<ASTNode>& astNodes, 
                      const std::string& outputFilename,
                      const std::vector<std::string>& clangArgs) {
    LibClangIRGenerator generator;
    return generator.GenerateWithClang(astNodes, outputFilename, clangArgs);
}

} // namespace FlashCpp