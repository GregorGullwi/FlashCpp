#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "FileTree.h"
#include "FileReader.h"
#include "CompileContext.h"
#include "CommandLineParser.h"
#include "Lexer.h"
#include "Parser.h"
#include "LibClangIRGenerator.h"

// LibClang/LLVM includes
#include <clang-c/Index.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>

// Initialize all targets (function declarations)
extern "C" {
    void LLVMInitializeAllTargetInfos(void);
    void LLVMInitializeAllTargets(void);
    void LLVMInitializeAllTargetMCs(void);
    void LLVMInitializeAllAsmParsers(void);
    void LLVMInitializeAllAsmPrinters(void);
}

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::microseconds;

struct TimingResults {
    Duration lexing{};
    Duration parsing{};
    Duration irGen{};
    Duration objGen{};
    
    void print(const std::string& prefix) const {
        std::cout << prefix << " timing results:\n"
                  << "  Lexing:  " << (lexing.count() / 1000.0) << "ms\n"
                  << "  Parsing: " << (parsing.count() / 1000.0) << "ms\n"
                  << "  IR Gen:  " << (irGen.count() / 1000.0) << "ms\n"
                  << "  Obj Gen: " << (objGen.count() / 1000.0) << "ms\n"
                  << "  Total:   " << (lexing + parsing + irGen + objGen).count() / 1000.0 << "ms\n\n";
    }
};

TimingResults compileWithInternal(const std::string& sourceFile) {
    TimingResults results;
    auto start = Clock::now();
    
    // Setup context
    CompileContext context;
    context.setInputFile(sourceFile);
    context.setOutputFile("internal_output.o");
    
    // Lexing
    FileTree fileTree;
    FileReader fileReader(context, fileTree);
    if (!fileReader.readFile(sourceFile)) {
        throw std::runtime_error("Failed to read input file");
    }
    
    Lexer lexer(fileReader.get_result());
    auto lexEnd = Clock::now();
    results.lexing = std::chrono::duration_cast<Duration>(lexEnd - start);
    
    // Parsing
    Parser parser(lexer);
    parser.parse();
    auto parseEnd = Clock::now();
    results.parsing = std::chrono::duration_cast<Duration>(parseEnd - lexEnd);
    
    // IR Generation (assuming you have this step)
    // This is where your internal IR generation would go
    auto irEnd = Clock::now();
    results.irGen = std::chrono::duration_cast<Duration>(irEnd - parseEnd);
    
    // Object file generation
    parser.generate_coff("internal_output.o");
    auto objEnd = Clock::now();
    results.objGen = std::chrono::duration_cast<Duration>(objEnd - irEnd);
    
    return results;
}

TimingResults compileWithLibClang(const std::string& sourceFile) {
    TimingResults results;
    auto start = Clock::now();
    
    // Initialize LibClang
    CXIndex index = clang_createIndex(0, 0);
    const char* args[] = {"-c"}; // Minimal args for compilation
    
    // Parse the translation unit (includes lexing)
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index,
        sourceFile.c_str(),
        args, 1,
        nullptr, 0,
        CXTranslationUnit_None
    );
    
    if (!unit) {
        clang_disposeIndex(index);
        throw std::runtime_error("Failed to parse translation unit");
    }
    
    auto parseEnd = Clock::now();
    results.lexing = std::chrono::duration_cast<Duration>(parseEnd - start);
    
    // IR Generation using LLVM
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module = LLVMModuleCreateWithName("libclang_module");
    
    // Here you would use LibClang's AST visitor to generate LLVM IR
    auto irEnd = Clock::now();
    results.irGen = std::chrono::duration_cast<Duration>(irEnd - parseEnd);
    
    // Generate object file
    char* error = nullptr;
    
    // Initialize all targets
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();

    // Get target triple
    char* targetTriple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, targetTriple);

    // Get target
    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(targetTriple, &target, &error)) {
        std::string errorMsg(error);
        LLVMDisposeMessage(error);
        throw std::runtime_error("Failed to get target: " + errorMsg);
    }

    // Create target machine
    LLVMTargetMachineRef targetMachine = LLVMCreateTargetMachine(
        target,
        targetTriple,
        "generic",
        "",
        LLVMCodeGenLevelDefault,
        LLVMRelocDefault,
        LLVMCodeModelDefault
    );

    if (LLVMTargetMachineEmitToFile(targetMachine, module, "libclang_output.o",
                                   LLVMObjectFile, &error) != 0) {
        std::string errorMsg(error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(targetMachine);
        throw std::runtime_error("Failed to emit object file: " + errorMsg);
    }

    LLVMDisposeTargetMachine(targetMachine);
    LLVMDisposeMessage(targetTriple);

    auto objEnd = Clock::now();
    results.objGen = std::chrono::duration_cast<Duration>(objEnd - irEnd);
    
    // Cleanup
    LLVMDisposeModule(module);
    LLVMContextDispose(context);
    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
    
    return results;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <source_file>\n";
        return 1;
    }
    
    try {
        std::string sourceFile = argv[1];
        
        std::cout << "Compiling " << sourceFile << " with both compilers...\n\n";
        
        auto internalResults = compileWithInternal(sourceFile);
        auto libclangResults = compileWithLibClang(sourceFile);
        
        internalResults.print("Internal compiler");
        libclangResults.print("LibClang/LLVM");
        
        // Link both outputs to executables
        std::system("clang internal_output.o -o internal.exe");
        std::system("clang libclang_output.o -o libclang.exe");
        
        std::cout << "Generated executables: internal.exe and libclang.exe\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
