#pragma once

// Crash handler for Windows - generates crash logs with stack traces
// This module provides automatic crash logging when the compiler encounters
// an unhandled exception. The crash log includes:
// - Timestamp
// - Exception type and address
// - Full stack trace with function names, source files, and line numbers
// - Module information

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <ctime>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace CrashHandler {

// Get the exception code as a human-readable string
inline const char* getExceptionCodeString(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
        default:                                 return "UNKNOWN_EXCEPTION";
    }
}

// Generate a timestamp string for the crash log filename
inline std::string getTimestampString() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &timeinfo);
    return std::string(buffer);
}

// Write the stack trace to a file
inline void writeStackTrace(FILE* file, CONTEXT* context) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    // Initialize symbol handler
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    if (!SymInitialize(process, nullptr, TRUE)) {
        fprintf(file, "Failed to initialize symbol handler. Error: %lu\n", GetLastError());
        return;
    }

    // Set up the stack frame for walking
    STACKFRAME64 stackFrame = {};
    DWORD machineType;

#ifdef _M_X64
    machineType = IMAGE_FILE_MACHINE_AMD64;
    stackFrame.AddrPC.Offset = context->Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
    machineType = IMAGE_FILE_MACHINE_I386;
    stackFrame.AddrPC.Offset = context->Eip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Ebp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Esp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
    machineType = IMAGE_FILE_MACHINE_ARM64;
    stackFrame.AddrPC.Offset = context->Pc;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context->Fp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context->Sp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#else
    fprintf(file, "Unsupported architecture for stack walking\n");
    SymCleanup(process);
    return;
#endif

    fprintf(file, "\n=== Stack Trace ===\n\n");

    // Walk the stack
    int frameNum = 0;
    const int maxFrames = 64;

    while (frameNum < maxFrames) {
        if (!StackWalk64(machineType, process, thread, &stackFrame, context,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }

        // Skip if the address is null
        if (stackFrame.AddrPC.Offset == 0) {
            break;
        }

        // Get symbol information
        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement64 = 0;
        DWORD displacement = 0;

        fprintf(file, "[%2d] ", frameNum);

        if (SymFromAddr(process, stackFrame.AddrPC.Offset, &displacement64, symbol)) {
            fprintf(file, "%s", symbol->Name);

            // Try to get source file and line information
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

            if (SymGetLineFromAddr64(process, stackFrame.AddrPC.Offset, &displacement, &line)) {
                fprintf(file, " (%s:%lu)", line.FileName, line.LineNumber);
            }

            fprintf(file, " + 0x%llx", displacement64);
        } else {
            // No symbol found, just print the address
            fprintf(file, "0x%016llx", stackFrame.AddrPC.Offset);
        }

        // Get module information
        IMAGEHLP_MODULE64 moduleInfo = {};
        moduleInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        if (SymGetModuleInfo64(process, stackFrame.AddrPC.Offset, &moduleInfo)) {
            fprintf(file, " [%s]", moduleInfo.ModuleName);
        }

        fprintf(file, "\n");
        frameNum++;
    }

    if (frameNum == 0) {
        fprintf(file, "No stack frames captured.\n");
    }

    SymCleanup(process);
}

// The unhandled exception filter - called when the process crashes
inline LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    // Generate crash log filename with timestamp
    std::string filename = "flashcpp_crash_" + getTimestampString() + ".log";

    FILE* file = nullptr;
    if (fopen_s(&file, filename.c_str(), "w") != 0 || file == nullptr) {
        // Failed to open crash log file, output to stderr
        fprintf(stderr, "\n=== CRASH DETECTED ===\n");
        fprintf(stderr, "Failed to create crash log file: %s\n", filename.c_str());
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Write header
    fprintf(file, "=== FlashCpp Crash Report ===\n\n");

    // Write timestamp
    time_t now = time(nullptr);
    char timeBuffer[64];
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    fprintf(file, "Timestamp: %s\n", timeBuffer);

    // Write exception information
    EXCEPTION_RECORD* record = exceptionInfo->ExceptionRecord;
    fprintf(file, "Exception Code: 0x%08lX (%s)\n",
            record->ExceptionCode, getExceptionCodeString(record->ExceptionCode));
    fprintf(file, "Exception Address: 0x%p\n", record->ExceptionAddress);

    // Additional info for access violations
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const char* operation = record->ExceptionInformation[0] == 0 ? "read" : "write";
        fprintf(file, "Access Violation: Attempted to %s address 0x%p\n",
                operation, reinterpret_cast<void*>(record->ExceptionInformation[1]));
    }

    // Write stack trace
    writeStackTrace(file, exceptionInfo->ContextRecord);

    // Write system information
    fprintf(file, "\n=== System Information ===\n\n");
    
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    fprintf(file, "Processor Architecture: ");
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: fprintf(file, "x64 (AMD64)\n"); break;
        case PROCESSOR_ARCHITECTURE_INTEL: fprintf(file, "x86\n"); break;
        case PROCESSOR_ARCHITECTURE_ARM64: fprintf(file, "ARM64\n"); break;
        default: fprintf(file, "Unknown (%d)\n", sysInfo.wProcessorArchitecture); break;
    }
    fprintf(file, "Number of Processors: %lu\n", sysInfo.dwNumberOfProcessors);

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        fprintf(file, "Memory Usage: %lu%%\n", memStatus.dwMemoryLoad);
        fprintf(file, "Total Physical Memory: %.2f GB\n", memStatus.ullTotalPhys / (1024.0 * 1024.0 * 1024.0));
        fprintf(file, "Available Physical Memory: %.2f GB\n", memStatus.ullAvailPhys / (1024.0 * 1024.0 * 1024.0));
    }

    fprintf(file, "\n=== End of Crash Report ===\n");
    fclose(file);

    // Also print to stderr so the user knows what happened
    fprintf(stderr, "\n");
    fprintf(stderr, "==========================================================\n");
    fprintf(stderr, "                    FLASHCPP CRASHED!\n");
    fprintf(stderr, "==========================================================\n");
    fprintf(stderr, "Exception: %s (0x%08lX)\n",
            getExceptionCodeString(record->ExceptionCode), record->ExceptionCode);
    fprintf(stderr, "A crash log has been written to: %s\n", filename.c_str());
    fprintf(stderr, "Please report this issue with the crash log attached.\n");
    fprintf(stderr, "==========================================================\n");

    return EXCEPTION_CONTINUE_SEARCH;
}

// Install the crash handler - call this at program startup
inline void install() {
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
}

} // namespace CrashHandler

#else // !_WIN32

// Stub implementation for non-Windows platforms
namespace CrashHandler {
    inline void install() {
        // No-op on non-Windows platforms
    }
}

#endif // _WIN32
