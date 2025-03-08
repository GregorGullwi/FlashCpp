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

namespace FlashCpp {

class LibClangIRGenerator {
public:
    LibClangIRGenerator() = default;

    bool GenerateCOFF(const std::vector<ASTNode>& astNodes, const std::string& outputFilename) {
        LLVMContextRef context = LLVMContextCreate();
        LLVMModuleRef module = LLVMModuleCreateWithName("my_module");
        LLVMBuilderRef builder = LLVMCreateBuilderInContext(context);

        // Generate LLVM IR from the AST nodes
        for (const auto& node : astNodes) {
            // This is a placeholder - you'll need to implement the logic
            // to convert each ASTNode to its LLVM IR representation
            if (node.is<DeclarationNode>()) {
                const DeclarationNode& decl = node.as<DeclarationNode>();
                // Example: Create a global variable (This is just an example, adjust as needed)
                LLVMTypeRef intType = LLVMIntTypeInContext(context, 32);
                LLVMValueRef globalVariable = LLVMAddGlobal(module, intType, decl.identifier_token().value().data());
                LLVMSetInitializer(globalVariable, LLVMConstInt(intType, 0, 0));
            } else if (node.is<NumericLiteralNode>()) {
                const NumericLiteralNode& literal = node.as<NumericLiteralNode>();
                // Example: Create a constant integer (This is just an example, adjust as needed)
                LLVMTypeRef intType = LLVMIntTypeInContext(context, 32);
                LLVMValueRef constant = LLVMConstInt(intType, std::stoll(literal.token().data()), 0);
            }
            else {
                // Handle other AST node types
            }
        }

        // Verify the module
        char* error = nullptr;
        LLVMVerifyModule(module, LLVMAbortProcessAction, &error);
        if (error) {
            std::cerr << "Error: " << error << std::endl;
            LLVMDisposeMessage(error);
            LLVMDisposeBuilder(builder);
            LLVMDisposeModule(module);
            LLVMContextDispose(context);
            return false;
        }

        // Create a target machine
        LLVMInitializeAllTargetInfos();
        LLVMInitializeAllTargets();
        LLVMInitializeAllTargetMCs();
        LLVMInitializeAllAsmParsers();
        LLVMInitializeAllAsmPrinters();

        char* targetTriple = LLVMGetDefaultTargetTriple();
        LLVMTargetRef target;
        if (LLVMGetTargetFromTriple(targetTriple, &target, &error)) {
            std::cerr << "Error: Could not get target from triple." << std::endl;
            LLVMDisposeMessage(targetTriple);
            LLVMDisposeBuilder(builder);
            LLVMDisposeModule(module);
            LLVMContextDispose(context);
            return false;
        }
        LLVMDisposeMessage(targetTriple);

        LLVMTargetMachineRef targetMachine = LLVMCreateTargetMachine(
            target,
            targetTriple,
            "generic",
            "",
            LLVMCodeGenLevelDefault,
            LLVMRelocDefault,
            LLVMCodeModelDefault);

        // Set the data layout
        LLVMTargetDataRef dataLayout = LLVMCreateTargetDataLayout(targetMachine);
        LLVMSetModuleDataLayout(module, dataLayout);

        // Emit the object file
        if (LLVMTargetMachineEmitToFile(targetMachine, module, outputFilename.c_str(), LLVMObjectFile, &error)) {
            std::cerr << "Error: " << error << std::endl;
            LLVMDisposeMessage(error);
            LLVMDisposeBuilder(builder);
            LLVMDisposeModule(module);
            LLVMContextDispose(context);
            LLVMDisposeTargetMachine(targetMachine);
            return false;
        }

        // Dispose of the LLVM objects
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        LLVMDisposeTargetMachine(targetMachine);

        return true;
    }
};

bool GenerateCOFF(const std::vector<ASTNode>& astNodes, const std::string& outputFilename) {
    LibClangIRGenerator generator;
    return generator.GenerateCOFF(astNodes, outputFilename);
}

} // namespace FlashCpp