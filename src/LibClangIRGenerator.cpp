#include "LibClangIRGenerator.h"
#include <clang-c/Index.h>
#include <iostream>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/BitWriter.h>
#include <fstream>
#include <filesystem>

namespace FlashCpp {

// Convert AST nodes to C code
std::string generateSourceFromAST(const std::vector<ASTNode>& nodes) {
    std::ostringstream source;
    
    // Simple code generation for main function
    source << "#include <stdio.h>\n\n";
    source << "int main() {\n";
    source << "    printf(\"Hello from FlashCpp!\\n\");\n";
    source << "    return 0;\n";
    source << "}\n";
    
    return source.str();
}

bool GenerateCOFF(const std::vector<ASTNode>& nodes, const std::string& outputFile) {
    // Generate C source from AST
    std::string source = generateSourceFromAST(nodes);
    
    // Save to temporary file
    std::filesystem::path tempDir = std::filesystem::temp_directory_path();
    std::filesystem::path tempFile = tempDir / "temp_source.c";
    
    std::ofstream outFile(tempFile);
    if (!outFile.is_open()) {
        std::cerr << "Failed to create temporary source file" << std::endl;
        return false;
    }
    
    outFile << source;
    outFile.close();
    
    // Use LibClang to parse and compile the C file
    CXIndex index = clang_createIndex(0, 0);
    
    // Command-line arguments for clang
    const char* args[] = {
        "-c",                  // Compile only
        "-o", outputFile.c_str(),  // Output file
        "-O0"                  // No optimization
    };
    
    // Create translation unit from source file
    CXTranslationUnit tu = clang_parseTranslationUnit(
        index,
        tempFile.string().c_str(),
        args, 4,             // Args and arg count
        nullptr, 0,          // No unsaved files
        CXTranslationUnit_None
    );
    
    if (!tu) {
        std::cerr << "Failed to parse translation unit" << std::endl;
        clang_disposeIndex(index);
        return false;
    }
    
    // Check for errors
    unsigned numDiags = clang_getNumDiagnostics(tu);
    bool hasErrors = false;
    
    for (unsigned i = 0; i < numDiags; ++i) {
        CXDiagnostic diag = clang_getDiagnostic(tu, i);
        CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diag);
        
        if (severity >= CXDiagnostic_Error) {
            hasErrors = true;
        }
        
        CXString diagStr = clang_formatDiagnostic(diag, clang_defaultDiagnosticDisplayOptions());
        std::cerr << clang_getCString(diagStr) << std::endl;
        clang_disposeString(diagStr);
        
        clang_disposeDiagnostic(diag);
    }
    
    // Compile the code
    int result = 0;
    if (!hasErrors) {
        result = clang_saveTranslationUnit(tu, outputFile.c_str(), CXSaveTranslationUnit_None);
    }
    
    // Cleanup
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    
    // Remove temporary file
    std::filesystem::remove(tempFile);
    
    return !hasErrors && result == 0;
}

} // namespace FlashCpp