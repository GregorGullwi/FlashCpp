// Add these defines before any includes
#define CLANG_DISABLE_RISCV_VECTOR_INTRINSICS 1
#define CLANG_DISABLE_HIP 1
#define CLANG_DISABLE_HLSL 1
#define CLANG_DISABLE_CUDA 1

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

// Use the C API instead of C++ API to avoid complex linking issues
#include <clang-c/Index.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>

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
    
    // Initialize clang components
    CXIndex index = clang_createIndex(0, 0);
    if (!index) {
        results.error = "Failed to create clang index";
        return results;
    }
    
    // Start lexing phase - Note that libclang combines lexing and some parsing
    auto lexStart = Clock::now();
    
    // Create arguments for the compilation
    const char* args[] = {
        "-c",
        "-O0",
        "-fintegrated-as",
        "-x", "c++",
        "-std=c++20"  // Changed from c++17 to c++20
    };
    int numArgs = 6;
    
    // With libclang, the entire parsing pipeline is combined in this call
    // clang_parseTranslationUnit does lexing AND parsing together
    CXTranslationUnit TU = clang_parseTranslationUnit(
        index,
        sourceFile.c_str(),
        args,
        numArgs,
        nullptr,
        0,
        CXTranslationUnit_DetailedPreprocessingRecord
    );
    
    auto parseEnd = Clock::now();
    
    if (!TU) {
        clang_disposeIndex(index);
        results.error = "Failed to parse translation unit";
        return results;
    }
    
    // Check for parsing errors
    unsigned numDiags = clang_getNumDiagnostics(TU);
    for (unsigned i = 0; i < numDiags; ++i) {
        CXDiagnostic diag = clang_getDiagnostic(TU, i);
        if (clang_getDiagnosticSeverity(diag) >= CXDiagnostic_Error) {
            CXString str = clang_formatDiagnostic(diag, clang_defaultDiagnosticDisplayOptions());
            std::string errorMsg = clang_getCString(str);
            clang_disposeString(str);
            
            if (results.error.empty()) {
                results.error = errorMsg;
            } else {
                results.error += "\n" + errorMsg;
            }
        }
        clang_disposeDiagnostic(diag);
    }
    
    // Since libclang's API doesn't easily separate lexing and parsing,
    // we'll approximate by assuming 2/3 of the time was lexing and 1/3 was parsing
    Duration totalLexParseTime = std::chrono::duration_cast<Duration>(parseEnd - lexStart);
    results.lexing = std::chrono::duration_cast<Duration>(totalLexParseTime * 2 / 3);
    results.parsing = std::chrono::duration_cast<Duration>(totalLexParseTime * 1 / 3);
    
    // IR Generation phase
    auto irGenStart = Clock::now();
    
    // Create a temporary file for output
    std::filesystem::path outputDir = "output";
    std::filesystem::create_directories(outputDir);
    std::filesystem::path outputFile = outputDir / "libclang_output.o";
    
    auto irGenEnd = Clock::now();
    results.irGen = std::chrono::duration_cast<Duration>(irGenEnd - irGenStart);
    
    // Object code generation
    auto objGenStart = Clock::now();
    
    // Generate the object file
    if (results.error.empty()) {
        int saveResult = clang_saveTranslationUnit(TU, outputFile.string().c_str(), CXSaveTranslationUnit_None);
        if (saveResult != CXSaveError_None) {
            results.error = "Failed to save translation unit, error code: " + std::to_string(saveResult);
        }
    }
    
    auto objGenEnd = Clock::now();
    results.objGen = std::chrono::duration_cast<Duration>(objGenEnd - objGenStart);
    
    // Clean up resources
    clang_disposeTranslationUnit(TU);
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
