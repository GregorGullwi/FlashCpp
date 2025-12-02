#pragma once

// Crash handler for Windows and Linux - generates crash logs with stack traces
// This module provides automatic crash logging when the compiler encounters
// an unhandled exception (Windows) or fatal signal (Linux/macOS). 
// The crash log includes:
// - Timestamp
// - Exception/signal type and address
// - Full stack trace with function names, source files, and line numbers
// - System information
//
// Safety notes:
// - Uses preallocated static buffers to avoid memory allocation during crash handling
// - On Linux/macOS, some functions used (backtrace, fprintf) are not strictly 
//   async-signal-safe, but are commonly used in crash handlers and work reliably

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <ctime>
#include <csignal>
#include <cstdlib>

#pragma comment(lib, "dbghelp.lib")

namespace CrashHandler {

// Constants
constexpr int kMaxStackFrames = 64;
constexpr int kMaxPathLength = 512;
constexpr int kTimestampBufferSize = 64;

// Preallocated static buffers - avoid memory allocation during crash
static char s_filenameBuffer[kMaxPathLength];
static char s_timestampBuffer[kTimestampBufferSize];

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

// Generate a timestamp string into the provided buffer (no allocation)
inline void getTimestampString(char* buffer, size_t bufferSize) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    strftime(buffer, bufferSize, "%Y%m%d_%H%M%S", &timeinfo);
}

// Generate a human-readable timestamp string into the provided buffer (no allocation)
inline void getReadableTimestamp(char* buffer, size_t bufferSize) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", &timeinfo);
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

    while (frameNum < kMaxStackFrames) {
        if (!StackWalk64(machineType, process, thread, &stackFrame, context,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }

        // Skip if the address is null
        if (stackFrame.AddrPC.Offset == 0) {
            break;
        }

        // Get symbol information
        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)];
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
    // Generate crash log filename with timestamp using preallocated buffers
    getTimestampString(s_timestampBuffer, kTimestampBufferSize);
    snprintf(s_filenameBuffer, kMaxPathLength, "flashcpp_crash_%s.log", s_timestampBuffer);

    FILE* file = nullptr;
    if (fopen_s(&file, s_filenameBuffer, "w") != 0 || file == nullptr) {
        // Failed to open crash log file, output to stderr
        fprintf(stderr, "\n=== CRASH DETECTED ===\n");
        fprintf(stderr, "Failed to create crash log file: %s\n", s_filenameBuffer);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Write header
    fprintf(file, "=== FlashCpp Crash Report ===\n\n");

    // Write timestamp (reuse buffer for readable format)
    getReadableTimestamp(s_timestampBuffer, kTimestampBufferSize);
    fprintf(file, "Timestamp: %s\n", s_timestampBuffer);

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
    fprintf(stderr, "A crash log has been written to: %s\n", s_filenameBuffer);
    fprintf(stderr, "Please report this issue with the crash log attached.\n");
    fprintf(stderr, "==========================================================\n");

    return EXCEPTION_CONTINUE_SEARCH;
}

// Vectored exception handler for even earlier crash detection
inline LONG WINAPI vectoredExceptionHandler(EXCEPTION_POINTERS* exceptionInfo) {
    // Only handle unrecoverable exceptions
    DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
        code == EXCEPTION_PRIV_INSTRUCTION) {
        unhandledExceptionFilter(exceptionInfo);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Invalid parameter handler for CRT errors
inline void invalidParameterHandler(const wchar_t* /*expression*/,
                                     const wchar_t* /*function*/,
                                     const wchar_t* /*file*/,
                                     unsigned int /*line*/,
                                     uintptr_t /*reserved*/) {
    // Trigger an access violation to get a stack trace
    RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

// Pure virtual function call handler
inline void purecallHandler() {
    RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

// SIGABRT handler
inline void abortHandler(int /*signal*/) {
    RaiseException(EXCEPTION_NONCONTINUABLE_EXCEPTION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

// Install the crash handler - call this at program startup
inline void install() {
    // Install vectored exception handler (highest priority)
    AddVectoredExceptionHandler(1, vectoredExceptionHandler);
    
    // Install unhandled exception filter (fallback)
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    
    // Install CRT error handlers
    _set_invalid_parameter_handler(invalidParameterHandler);
    _set_purecall_handler(purecallHandler);
    
    // Install signal handler for abort
    signal(SIGABRT, abortHandler);
}

} // namespace CrashHandler

#elif defined(__linux__) || defined(__APPLE__)

// Linux/macOS implementation using signal handlers and backtrace
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <execinfo.h>
#include <sys/utsname.h>
#include <dlfcn.h>
#include <cxxabi.h>

namespace CrashHandler {

// Constants
constexpr int kMaxStackFrames = 64;
constexpr int kMaxPathLength = 512;
constexpr int kTimestampBufferSize = 64;
constexpr int kMaxSymbolLength = 256;
constexpr int kMaxCommandLength = 640;  // kMaxPathLength + room for addr2line command
constexpr int kMaxSourceLocationLength = 560;  // Path + ":" + line number

// Preallocated static buffers - avoid memory allocation during crash handling
// This is important for signal safety and handling out-of-memory crashes
static char s_filenameBuffer[kMaxPathLength];
static char s_timestampBuffer[kTimestampBufferSize];
static char s_commandBuffer[kMaxCommandLength];
static char s_demangledBuffer[kMaxSymbolLength];

// Get signal name as a human-readable string
inline const char* getSignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation fault)";
        case SIGABRT: return "SIGABRT (Abort)";
        case SIGFPE:  return "SIGFPE (Floating point exception)";
        case SIGILL:  return "SIGILL (Illegal instruction)";
        case SIGBUS:  return "SIGBUS (Bus error)";
        case SIGTRAP: return "SIGTRAP (Trap)";
        default:      return "Unknown signal";
    }
}

// Get signal code description for SIGSEGV
inline const char* getSegfaultCodeDescription(int code) {
    switch (code) {
        case SEGV_MAPERR: return "Address not mapped to object";
        case SEGV_ACCERR: return "Invalid permissions for mapped object";
        default:          return "Unknown";
    }
}

// Get signal code description for SIGFPE
inline const char* getFpeCodeDescription(int code) {
    switch (code) {
        case FPE_INTDIV: return "Integer divide by zero";
        case FPE_INTOVF: return "Integer overflow";
        case FPE_FLTDIV: return "Floating-point divide by zero";
        case FPE_FLTOVF: return "Floating-point overflow";
        case FPE_FLTUND: return "Floating-point underflow";
        case FPE_FLTRES: return "Floating-point inexact result";
        case FPE_FLTINV: return "Floating-point invalid operation";
        case FPE_FLTSUB: return "Subscript out of range";
        default:         return "Unknown";
    }
}

// Generate a timestamp string into the provided buffer (no allocation)
inline void getTimestampString(char* buffer, size_t bufferSize) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buffer, bufferSize, "%Y%m%d_%H%M%S", &timeinfo);
}

// Generate a human-readable timestamp string into the provided buffer (no allocation)
inline void getReadableTimestamp(char* buffer, size_t bufferSize) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

// Demangle a C++ symbol name. Returns the demangled name or the original if demangling fails.
// Note: Uses static buffer, not thread-safe but acceptable in crash handler context.
inline const char* demangleSymbol(const char* mangledName) {
    if (mangledName == nullptr || mangledName[0] == '\0') {
        return "???";
    }
    
    int status = 0;
    size_t length = kMaxSymbolLength;
    char* result = abi::__cxa_demangle(mangledName, s_demangledBuffer, &length, &status);
    
    if (status == 0 && result != nullptr) {
        return result;
    }
    return mangledName;
}

// Get source file and line information from an address using addr2line.
// Writes the result to the provided buffer. Returns true if successful.
// 
// Safety notes:
// - popen() is not strictly async-signal-safe, but is commonly used in crash handlers
//   and works reliably in practice. The alternative (fork/exec/pipe) is more complex
//   and also not fully async-signal-safe.
// - The trade-off is that this approach may not work correctly in pathological cases
//   (e.g., crash during malloc), but it provides much more useful stack traces in
//   the common case.
// - The executable path is validated to reject characters that could be interpreted
//   by the shell to prevent command injection.
inline bool getSourceLocation(const char* executablePath, void* addr, char* buffer, size_t bufferSize) {
    if (executablePath == nullptr || addr == nullptr) {
        buffer[0] = '\0';
        return false;
    }
    
    // Validate the executable path to prevent shell injection
    // Only allow alphanumeric characters, forward slashes, dots, dashes, and underscores
    for (const char* p = executablePath; *p != '\0'; ++p) {
        char c = *p;
        bool isValid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '/' || c == '.' ||
                       c == '-' || c == '_' || c == '+';
        if (!isValid) {
            buffer[0] = '\0';
            return false;
        }
    }
    
    // Build addr2line command
    snprintf(s_commandBuffer, kMaxCommandLength, 
             "addr2line -e \"%s\" %p 2>/dev/null", executablePath, addr);
    
    FILE* pipe = popen(s_commandBuffer, "r");
    if (pipe == nullptr) {
        buffer[0] = '\0';
        return false;
    }
    
    // Read the output (expected format: "filename:line" or "??:?" on failure)
    if (fgets(buffer, static_cast<int>(bufferSize), pipe) == nullptr) {
        pclose(pipe);
        buffer[0] = '\0';
        return false;
    }
    pclose(pipe);
    
    // Remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
    
    // Check if addr2line found the source (returns "??:?" if not found)
    if (strcmp(buffer, "??:?") == 0 || strcmp(buffer, "??:0") == 0) {
        buffer[0] = '\0';
        return false;
    }
    
    return true;
}

// Write a single stack frame to the file with symbol and source information
inline void writeStackFrame(FILE* file, int frameNum, void* addr) {
    Dl_info dlInfo;
    
    fprintf(file, "[%2d] ", frameNum);
    
    if (dladdr(addr, &dlInfo)) {
        // Calculate relative address for position-independent executables
        void* relativeAddr = reinterpret_cast<void*>(
            reinterpret_cast<char*>(addr) - reinterpret_cast<char*>(dlInfo.dli_fbase));
        
        // Print function name (demangled if possible)
        if (dlInfo.dli_sname != nullptr) {
            fprintf(file, "%s", demangleSymbol(dlInfo.dli_sname));
        } else {
            fprintf(file, "%p", addr);
        }
        
        // Try to get source file and line using addr2line
        if (dlInfo.dli_fname != nullptr) {
            char sourceLocation[kMaxSourceLocationLength];
            if (getSourceLocation(dlInfo.dli_fname, relativeAddr, sourceLocation, sizeof(sourceLocation))) {
                fprintf(file, " (%s)", sourceLocation);
            }
        }
        
        // Print offset from symbol if available
        if (dlInfo.dli_saddr != nullptr) {
            ptrdiff_t offset = reinterpret_cast<char*>(addr) - reinterpret_cast<char*>(dlInfo.dli_saddr);
            if (offset != 0) {
                fprintf(file, " + 0x%tx", offset);
            }
        }
        
        // Print module name
        if (dlInfo.dli_fname != nullptr) {
            // Extract just the filename from the path
            const char* moduleName = strrchr(dlInfo.dli_fname, '/');
            moduleName = moduleName ? moduleName + 1 : dlInfo.dli_fname;
            fprintf(file, " [%s]", moduleName);
        }
    } else {
        // dladdr failed, just print the address
        fprintf(file, "%p", addr);
    }
    
    fprintf(file, "\n");
}

// Signal handler - called when the process receives a fatal signal
// Note: The 'context' parameter contains CPU registers and could be used for
// additional debugging info, but is not used here to keep the implementation simple.
// Note: Some functions used here (backtrace, fprintf) are not strictly async-signal-safe,
// but are commonly used in crash handlers and work reliably in practice.
// Safety: Uses preallocated static buffers to avoid memory allocation.
inline void signalHandler(int sig, siginfo_t* info, void* /*context*/) {
    // Generate crash log filename with timestamp using preallocated buffers
    getTimestampString(s_timestampBuffer, kTimestampBufferSize);
    snprintf(s_filenameBuffer, kMaxPathLength, "flashcpp_crash_%s.log", s_timestampBuffer);

    FILE* file = fopen(s_filenameBuffer, "w");
    if (file == nullptr) {
        // Failed to open crash log file, write to stderr instead
        fprintf(stderr, "\n=== CRASH DETECTED ===\n");
        fprintf(stderr, "Failed to create crash log file: %s\n", s_filenameBuffer);
        fprintf(stderr, "Signal: %d (%s)\n", sig, getSignalName(sig));
        if (info != nullptr) {
            fprintf(stderr, "Fault Address: %p\n", info->si_addr);
        }
        // Re-raise signal with default handler
        signal(sig, SIG_DFL);
        kill(getpid(), sig);
        _exit(1);
    }

    // Write header
    fprintf(file, "=== FlashCpp Crash Report ===\n\n");

    // Write timestamp (reuse buffer for readable format)
    getReadableTimestamp(s_timestampBuffer, kTimestampBufferSize);
    fprintf(file, "Timestamp: %s\n", s_timestampBuffer);

    // Write signal information
    fprintf(file, "Signal: %d (%s)\n", sig, getSignalName(sig));
    
    if (info != nullptr) {
        fprintf(file, "Fault Address: %p\n", info->si_addr);
        
        // Additional info based on signal type
        if (sig == SIGSEGV) {
            fprintf(file, "Segfault Code: %s\n", getSegfaultCodeDescription(info->si_code));
        } else if (sig == SIGFPE) {
            fprintf(file, "FPE Code: %s\n", getFpeCodeDescription(info->si_code));
        }
    }

    // Write stack trace
    fprintf(file, "\n=== Stack Trace ===\n\n");
    
    void* stackFrames[kMaxStackFrames];
    int frameCount = backtrace(stackFrames, kMaxStackFrames);
    
    if (frameCount > 0) {
        // Write each stack frame with resolved symbol and source information
        for (int i = 0; i < frameCount; ++i) {
            writeStackFrame(file, i, stackFrames[i]);
        }
    } else {
        fprintf(file, "No stack frames captured.\n");
    }

    // Write system information
    fprintf(file, "\n=== System Information ===\n\n");
    
    struct utsname sysInfo;
    if (uname(&sysInfo) == 0) {
        fprintf(file, "System: %s\n", sysInfo.sysname);
        fprintf(file, "Node: %s\n", sysInfo.nodename);
        fprintf(file, "Release: %s\n", sysInfo.release);
        fprintf(file, "Version: %s\n", sysInfo.version);
        fprintf(file, "Machine: %s\n", sysInfo.machine);
    }

    fprintf(file, "\n=== End of Crash Report ===\n");
    fclose(file);

    // Also print to stderr so the user knows what happened
    fprintf(stderr, "\n");
    fprintf(stderr, "==========================================================\n");
    fprintf(stderr, "                    FLASHCPP CRASHED!\n");
    fprintf(stderr, "==========================================================\n");
    fprintf(stderr, "Signal: %s\n", getSignalName(sig));
    fprintf(stderr, "A crash log has been written to: %s\n", s_filenameBuffer);
    fprintf(stderr, "Please report this issue with the crash log attached.\n");
    fprintf(stderr, "==========================================================\n");

    // Re-raise the signal to get the default behavior (core dump, etc.)
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
}

// Install the crash handler - call this at program startup
inline void install() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    // Install handlers for common crash signals
    sigaction(SIGSEGV, &sa, nullptr);  // Segmentation fault
    sigaction(SIGABRT, &sa, nullptr);  // Abort
    sigaction(SIGFPE, &sa, nullptr);   // Floating point exception
    sigaction(SIGILL, &sa, nullptr);   // Illegal instruction
    sigaction(SIGBUS, &sa, nullptr);   // Bus error
}

} // namespace CrashHandler

#else // Other platforms

// Stub implementation for unsupported platforms
namespace CrashHandler {
    inline void install() {
        // No-op on unsupported platforms
    }
}

#endif // _WIN32
