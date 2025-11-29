// Add these defines before any includes
#define CLANG_DISABLE_RISCV_VECTOR_INTRINSICS 1
#define CLANG_DISABLE_HIP 1
#define CLANG_DISABLE_HLSL 1
#define CLANG_DISABLE_CUDA 1
#define NOMINMAX 1  // Prevent Windows.h from defining min/max macros

#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <algorithm>
#include <vector>
#include <numeric>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include "FileTree.h"
#include "FileReader.h"
#include "CompileContext.h"
#include "CommandLineParser.h"
#include "Lexer.h"
#include "Parser.h"
#include "Log.h"
#include "LibClangIRGenerator.h"
#include "CodeGen.h"
#include "IRTypes.h"

// Use the C API instead of C++ API to avoid complex linking issues
#include <clang-c/Index.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/IRReader.h> // Include for LLVMParseIRInContext

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::microseconds;

struct TimingResults {
    std::optional<Duration> lexing;
    std::optional<Duration> parsing;
    std::optional<Duration> irGen;
    std::optional<Duration> objGen;
    std::string error;
    
    // New metrics
    size_t tokenCount = 0;
    size_t astNodeCount = 0;
    size_t irInstructionCount = 0;
    size_t objectFileSize = 0;
    size_t peakMemoryUsage = 0;  // in bytes
    
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
        
        std::cout << "  Lexing:  " << formatDuration(lexing) 
                  << " (" << tokenCount << " tokens)\n"
                  << "  Parsing: " << formatDuration(parsing)
                  << " (" << astNodeCount << " AST nodes)\n"
                  << "  IR Gen:  " << formatDuration(irGen)
                  << " (" << irInstructionCount << " IR instructions)\n"
                  << "  Obj Gen: " << formatDuration(objGen)
                  << " (" << objectFileSize << " bytes)\n"
                  << "  Memory:  " << (peakMemoryUsage / 1024.0) << " KB peak\n"
                  << "  Total:   " << (getDuration(lexing) + getDuration(parsing) + 
                                     getDuration(irGen) + getDuration(objGen)).count() / 1000.0 << "ms\n\n";
    }
};

struct BenchmarkStats {
    std::vector<Duration> lexingTimes;
    std::vector<Duration> parsingTimes;
    std::vector<Duration> irGenTimes;
    std::vector<Duration> objGenTimes;
    std::vector<size_t> memoryUsages;
    
    void addResult(const TimingResults& result) {
        if (result.lexing) lexingTimes.push_back(*result.lexing);
        if (result.parsing) parsingTimes.push_back(*result.parsing);
        if (result.irGen) irGenTimes.push_back(*result.irGen);
        if (result.objGen) objGenTimes.push_back(*result.objGen);
        memoryUsages.push_back(result.peakMemoryUsage);
    }
    
    void print(const std::string& prefix) const {
        std::cout << prefix << " statistical analysis:\n";
        
        auto printStats = [](const std::vector<Duration>& times, const std::string& name) {
            if (times.empty()) return;
            
            double mean = 0;
            for (const auto& t : times) mean += t.count();
            mean /= times.size();
            
            std::vector<double> sortedTimes;
            for (const auto& t : times) sortedTimes.push_back(t.count());
            std::sort(sortedTimes.begin(), sortedTimes.end());
            
            double median = sortedTimes[sortedTimes.size() / 2];
            double stddev = 0;
            for (const auto& t : times) {
                double diff = t.count() - mean;
                stddev += diff * diff;
            }
            stddev = std::sqrt(stddev / times.size());
            
            std::cout << "  " << name << ":\n"
                      << "    Mean:   " << (mean / 1000.0) << "ms\n"
                      << "    Median: " << (median / 1000.0) << "ms\n"
                      << "    StdDev: " << (stddev / 1000.0) << "ms\n"
                      << "    Min:    " << (sortedTimes.front() / 1000.0) << "ms\n"
                      << "    Max:    " << (sortedTimes.back() / 1000.0) << "ms\n";
        };
        
        printStats(lexingTimes, "Lexing");
        printStats(parsingTimes, "Parsing");
        printStats(irGenTimes, "IR Generation");
        printStats(objGenTimes, "Object Generation");
        
        if (!memoryUsages.empty()) {
            double meanMem = 0;
            for (const auto& m : memoryUsages) meanMem += m;
            meanMem /= memoryUsages.size();
            
            std::cout << "  Memory Usage:\n"
                      << "    Mean:   " << (meanMem / 1024.0) << " KB\n"
                      << "    Max:    " << (*std::max_element(memoryUsages.begin(), memoryUsages.end()) / 1024.0) << " KB\n";
        }
    }
};

TimingResults compileWithInternal(const std::string& sourceFile) {
    TimingResults results;
    results.error = "Internal compiler not implemented yet";
    return results;
}

TimingResults compileWithLibClang(const std::string& sourceFile) {
    TimingResults results;
    auto start = Clock::now();
    
    // Initialize clang
    CXIndex index = clang_createIndex(0, 0);
    if (!index) {
        results.error = "Failed to create clang index";
        return results;
    }
    
    auto lexStart = Clock::now();
    
    // Parse translation unit
    const char* args[] = {
        "-c",
        "-O0",
        "-emit-llvm",  // Tell Clang to emit LLVM IR
        "-x", "c++",
        "-std=c++20"
    };
    int numArgs = 6;
    
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
    
    // Count tokens and AST nodes
    CXCursor cursor = clang_getTranslationUnitCursor(TU);
    size_t tokenCount = 0;
    size_t astNodeCount = 0;
    
    // Create a pair object that will live for the duration of the visit
    std::pair<size_t*, size_t*> counts(&tokenCount, &astNodeCount);
    
    clang_visitChildren(cursor, [](CXCursor c, CXCursor parent, CXClientData client_data) {
        auto* counts = static_cast<std::pair<size_t*, size_t*>*>(client_data);
        (*counts->first)++;  // Increment token count
        (*counts->second)++; // Increment AST node count
        return CXChildVisit_Continue;
    }, &counts);
    
    results.tokenCount = tokenCount;
    results.astNodeCount = astNodeCount;
    
    // Check for parsing errors
    unsigned numDiags = clang_getNumDiagnostics(TU);
    for (unsigned i = 0; i < numDiags; ++i) {
        CXDiagnostic diag = clang_getDiagnostic(TU, i);
        if (clang_getDiagnosticSeverity(diag) >= CXDiagnostic_Error) {
            CXString str = clang_formatDiagnostic(diag, clang_defaultDiagnosticDisplayOptions());
            std::string errorMsg = clang_getCString(str);
            clang_disposeString(str);
            clang_disposeDiagnostic(diag);
            
            if (results.error.empty()) {
                results.error = errorMsg;
            } else {
                results.error += "\n" + errorMsg;
            }
        }
        clang_disposeDiagnostic(diag);
    }
    
    Duration totalLexParseTime = std::chrono::duration_cast<Duration>(parseEnd - lexStart);
    results.lexing = std::chrono::duration_cast<Duration>(totalLexParseTime * 2 / 3);
    results.parsing = std::chrono::duration_cast<Duration>(totalLexParseTime * 1 / 3);
    
    // IR Generation phase
    auto irGenStart = Clock::now();
    
    std::filesystem::path outputDir = "output";
    std::filesystem::create_directories(outputDir);
    
    // Generate LLVM IR directly from the translation unit
    auto irFile = outputDir / "libclang_output.ll";
    auto outputFile = outputDir / "libclang_output.o";
    
    // Get the LLVM module from the translation unit
    CXString irString = clang_getTranslationUnitSpelling(TU);
    std::string irContent = clang_getCString(irString);
    clang_disposeString(irString);
    
    // Write IR to file
    std::ofstream irStream(irFile);
    if (!irStream) {
        results.error = "Failed to open IR file for writing";
        clang_disposeTranslationUnit(TU);
        clang_disposeIndex(index);
        return results;
    }
    irStream << irContent;
    irStream.close();
    
    // Count IR instructions
    std::ifstream irReadStream(irFile);
    std::string line;
    size_t irInstructionCount = 0;
    while (std::getline(irReadStream, line)) {
        // Count lines that contain actual instructions (not metadata, comments, etc.)
        if (!line.empty() && line[0] != ';' && line[0] != '!' && line[0] != '@' && line[0] != '#') {
            irInstructionCount++;
        }
    }
    results.irInstructionCount = irInstructionCount;
    
    auto irGenEnd = Clock::now();
    results.irGen = std::chrono::duration_cast<Duration>(irGenEnd - irGenStart);
    
    // Object Generation phase
    auto objGenStart = Clock::now();
    
    // Initialize LLVM
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();
    
    // Create a new LLVM context and module
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module = nullptr;
    char* error = nullptr;
    
    // Create a memory buffer from the IR file
    LLVMMemoryBufferRef buffer = nullptr;
    if (LLVMCreateMemoryBufferWithContentsOfFile(irFile.string().c_str(), &buffer, &error)) {
        results.error = std::string("Failed to create memory buffer: ") + (error ? error : "unknown error");
        if (error) LLVMDisposeMessage(error);
        LLVMContextDispose(context);
        clang_disposeTranslationUnit(TU);
        clang_disposeIndex(index);
        return results;
    }
    
    // Parse the IR into a module
    if (LLVMParseIRInContext(context, buffer, &module, &error)) {
        results.error = std::string("Failed to parse IR: ") + (error ? error : "unknown error");
        if (error) LLVMDisposeMessage(error);
        LLVMDisposeMemoryBuffer(buffer);
        LLVMContextDispose(context);
        clang_disposeTranslationUnit(TU);
        clang_disposeIndex(index);
        return results;
    }
    
    LLVMDisposeMemoryBuffer(buffer);
    
    // Create target machine
    char* targetTriple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(targetTriple, &target, &error)) {
        results.error = "Could not get target from triple";
        LLVMDisposeMessage(targetTriple);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        clang_disposeTranslationUnit(TU);
        clang_disposeIndex(index);
        return results;
    }
    
    LLVMTargetMachineRef targetMachine = LLVMCreateTargetMachine(
        target,
        targetTriple,
        "generic",
        "",
        LLVMCodeGenLevelDefault,
        LLVMRelocDefault,
        LLVMCodeModelDefault
    );
    
    LLVMDisposeMessage(targetTriple);
    
    // Emit object file
    if (LLVMTargetMachineEmitToFile(targetMachine, module, outputFile.string().c_str(), LLVMObjectFile, &error)) {
        results.error = std::string("Failed to emit object file: ") + (error ? error : "unknown error");
        if (error) LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(targetMachine);
        LLVMDisposeModule(module);
        LLVMContextDispose(context);
        clang_disposeTranslationUnit(TU);
        clang_disposeIndex(index);
        return results;
    }
    
    // Get object file size
    results.objectFileSize = std::filesystem::file_size(outputFile);
    
    auto objGenEnd = Clock::now();
    results.objGen = std::chrono::duration_cast<Duration>(objGenEnd - objGenStart);
    
    // Cleanup
    LLVMDisposeTargetMachine(targetMachine);
    LLVMDisposeModule(module);
    LLVMContextDispose(context);
    clang_disposeTranslationUnit(TU);
    clang_disposeIndex(index);
    
    // Get peak memory usage
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        results.peakMemoryUsage = pmc.PeakWorkingSetSize;
    }
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        results.peakMemoryUsage = usage.ru_maxrss * 1024; // Convert KB to bytes
    }
#endif
    
    return results;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        FLASH_LOG(General, Error, "Usage: ", argv[0], " <source_file>");
        return 1;
    }
    
    std::string sourceFile = argv[1];
    std::cout << "Compiling " << sourceFile << " with both compilers...\n\n";
    
    const int WARMUP_RUNS = 2;
    const int BENCHMARK_RUNS = 5;
    
    // Warmup runs
    std::cout << "Performing warmup runs...\n";
    for (int i = 0; i < WARMUP_RUNS; ++i) {
        compileWithInternal(sourceFile);
        compileWithLibClang(sourceFile);
    }
    
    // Actual benchmark runs
    std::cout << "\nPerforming benchmark runs...\n";
    BenchmarkStats internalStats;
    BenchmarkStats libclangStats;
    
    for (int i = 0; i < BENCHMARK_RUNS; ++i) {
        std::cout << "\nRun " << (i + 1) << " of " << BENCHMARK_RUNS << ":\n";
        
        auto internalResults = compileWithInternal(sourceFile);
        internalResults.print("Internal compiler");
        internalStats.addResult(internalResults);
        
        auto libclangResults = compileWithLibClang(sourceFile);
        libclangResults.print("LibClang/LLVM");
        libclangStats.addResult(libclangResults);
    }
    
    // Print statistical analysis
    std::cout << "\n=== Final Results ===\n";
    internalStats.print("Internal compiler");
    libclangStats.print("LibClang/LLVM");
    
    if (libclangStats.objGenTimes.size() > 0 && std::filesystem::exists("output/libclang_output.o")) {
        std::string linkCmd = "clang output/libclang_output.o -o output/libclang.exe";
        if (std::system(linkCmd.c_str()) != 0) {
            FLASH_LOG(General, Error, "Failed to link libclang.exe. Command: ", linkCmd);
        } else {
            std::cout << "Generated executable: output/libclang.exe\n";
        }
    }
    
    return 0;
}
