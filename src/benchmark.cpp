#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>

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
    std::optional<Duration> lexing;
    std::optional<Duration> parsing;
    std::optional<Duration> irGen;
    std::optional<Duration> objGen;
    std::string error;
    
    void print(const std::string& prefix) const {
        std::cout << prefix;
        if (!error.empty()) {
            std::cout << " partial results (error: " << error << "):\n";
        } else {
            std::cout << " timing results:\n";
        }
        
        auto formatDuration = [](const std::optional<Duration>& d) {
            if (d) return std::to_string(d->count() / 1000.0) + "ms";
            return std::string("N/A");
        };
        
        auto getDuration = [](const std::optional<Duration>& d) {
            return d ? *d : Duration{0};
        };
        
        std::cout << "  Lexing:  " << formatDuration(lexing) << "\n"
                  << "  Parsing: " << formatDuration(parsing) << "\n"
                  << "  IR Gen:  " << formatDuration(irGen) << "\n"
                  << "  Obj Gen: " << formatDuration(objGen) << "\n"
                  << "  Total:   " << (getDuration(lexing) + getDuration(parsing) + 
                                     getDuration(irGen) + getDuration(objGen)).count() / 1000.0 << "ms\n\n";
    }
};

TimingResults compileWithInternal(const std::string& sourceFile) {
    TimingResults results;
    auto start = Clock::now();
    
    CompileContext context;
    FileTree fileTree;
    FileReader fileReader(context, fileTree);
    if (!fileReader.readFile(sourceFile)) {
        results.error = "Failed to read input file";
        return results;
    }
    
    Lexer lexer(fileReader.get_result());
    auto lexEnd = Clock::now();
    results.lexing = std::chrono::duration_cast<Duration>(lexEnd - start);
    
    Parser parser(lexer);
    parser.parse();
    auto parseEnd = Clock::now();
    results.parsing = std::chrono::duration_cast<Duration>(parseEnd - lexEnd);
    
    auto irEnd = Clock::now();
    results.irGen = std::chrono::duration_cast<Duration>(irEnd - parseEnd);
    
    // Create output directory and file path
    auto outputDir = std::filesystem::path("output");
    auto outputPath = outputDir / "internal_output.o";
    
    std::cout << "Creating output directory: '" << outputDir.string() << "'\n";
    
    try {
        std::filesystem::create_directories(outputDir);
    } catch (const std::filesystem::filesystem_error& e) {
        results.error = std::string("Failed to create directories: ") + e.what();
        return results;
    }
    
    // Generate object file with full path
    try {
        std::cout << "Attempting to generate object file at: '" << outputPath.string() << "'\n";
        if (!parser.generate_coff(outputPath.string())) {
            auto error = parser.get_last_error();
            std::cout << "Debug: Internal compiler errors:\n" << error << "\n";
            results.error = std::string("Failed to generate object file: ") + error;
            return results;
        }
        std::cout << "Successfully generated object file\n";
    } catch (const std::exception& e) {
        results.error = std::string("Object file generation failed: ") + e.what();
        return results;
    }
    
    auto objEnd = Clock::now();
    results.objGen = std::chrono::duration_cast<Duration>(objEnd - irEnd);
    
    return results;
}

TimingResults compileWithLibClang(const std::string& sourceFile) {
    TimingResults results;
    auto start = Clock::now();
    
    // Initialize LibClang
    CXIndex index = clang_createIndex(0, 0);
    
    // Add required compilation flags for Windows
    const char* args[] = {
        "-c",
        "-D_WINDOWS",
        "-D_CONSOLE",
        "-DWIN32",
        "-D_WINDLL"
    };
    
    // Parse the translation unit (includes lexing)
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index,
        sourceFile.c_str(),
        args,
        sizeof(args)/sizeof(args[0]),
        nullptr, 0,
        CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_KeepGoing
    );
    
    auto parseEnd = Clock::now();
    results.lexing = std::chrono::duration_cast<Duration>(parseEnd - start);
    
    if (!unit) {
        clang_disposeIndex(index);
        results.error = "Failed to parse translation unit";
        return results;
    }

    // Check for parse errors
    unsigned numDiags = clang_getNumDiagnostics(unit);
    if (numDiags > 0) {
        std::string errors;
        for (unsigned i = 0; i < numDiags; ++i) {
            CXDiagnostic diag = clang_getDiagnostic(unit, i);
            CXString str = clang_formatDiagnostic(diag, clang_defaultDiagnosticDisplayOptions());
            errors += clang_getCString(str);
            errors += "\n";
            clang_disposeString(str);
            clang_disposeDiagnostic(diag);
        }
        if (!errors.empty()) {
            results.error = "Parse errors:\n" + errors;
            clang_disposeTranslationUnit(unit);
            clang_disposeIndex(index);
            return results;
        }
    }
    
    results.parsing = std::chrono::duration_cast<Duration>(parseEnd - start);
    
    // IR Generation using LLVM
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module = LLVMModuleCreateWithName("libclang_module");
    
    // Here you would use LibClang's AST visitor to generate LLVM IR
    auto irEnd = Clock::now();
    results.irGen = std::chrono::duration_cast<Duration>(irEnd - parseEnd);
    
    // Rest of LLVM setup
    char* error = nullptr;
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();

    char* targetTriple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, targetTriple);

    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(targetTriple, &target, &error)) {
        std::string errorMsg(error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(targetTriple);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        clang_disposeTranslationUnit(unit);
        clang_disposeIndex(index);
        results.error = "Failed to get target: " + errorMsg;
        return results;
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

    auto outputPath = std::filesystem::path("output") / "libclang_output.o";
    
    if (LLVMTargetMachineEmitToFile(targetMachine, module, outputPath.string().c_str(),
                                   LLVMObjectFile, &error) != 0) {
        std::string errorMsg(error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(targetMachine);
        LLVMDisposeMessage(targetTriple);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        clang_disposeTranslationUnit(unit);
        clang_disposeIndex(index);
        results.error = "Failed to emit object file: " + errorMsg;
        return results;
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
    
    std::string sourceFile = argv[1];
    std::cout << "Compiling " << sourceFile << " with both compilers...\n\n";
    
    auto internalResults = compileWithInternal(sourceFile);
    internalResults.print("Internal compiler");
    
    auto libclangResults = compileWithLibClang(sourceFile);
    libclangResults.print("LibClang/LLVM");
    
    if (libclangResults.error.empty() && std::filesystem::exists("output/libclang_output.o")) {
        std::string linkCmd = "clang output/libclang_output.o -o output/libclang.exe";
        if (std::system(linkCmd.c_str()) != 0) {
            std::cerr << "Failed to link libclang.exe. Command: " << linkCmd << "\n";
        } else {
            std::cout << "Generated executable: output/libclang.exe\n";
        }
    }
    
    return 0;
}
