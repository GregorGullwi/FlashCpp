// Stub implementations of Itanium C++ ABI exception handling functions
// These allow basic testing of exception-related code without full unwinding support
// 
// Note: These are MINIMAL stubs for testing only. For production use,
// link with libstdc++ or libc++abi which provide full implementations.

#include <cstdlib>
#include <cstdio>

extern "C" {

// Simple exception object tracking
static void* current_exception = nullptr;
static void* current_exception_type = nullptr;

void* __cxa_allocate_exception(size_t thrown_size) {
    // Allocate memory for the exception object
    void* exception_obj = malloc(thrown_size);
    if (!exception_obj) {
        fprintf(stderr, "STUB: __cxa_allocate_exception failed to allocate %zu bytes\n", thrown_size);
        abort();
    }
    fprintf(stderr, "STUB: __cxa_allocate_exception(%zu) -> %p\n", thrown_size, exception_obj);
    return exception_obj;
}

void __cxa_free_exception(void* thrown_exception) {
    fprintf(stderr, "STUB: __cxa_free_exception(%p)\n", thrown_exception);
    free(thrown_exception);
}

[[noreturn]] void __cxa_throw(void* thrown_exception, void* tinfo, void (*dest)(void*)) {
    fprintf(stderr, "STUB: __cxa_throw(%p, %p, %p)\n", thrown_exception, tinfo, (void*)dest);
    fprintf(stderr, "STUB: Exception thrown but no exception tables present!\n");
    fprintf(stderr, "STUB: Cannot find catch handlers without .eh_frame and .gcc_except_table\n");
    fprintf(stderr, "STUB: Calling std::terminate()\n");
    
    // In a real implementation, this would:
    // 1. Save exception info to thread-local storage
    // 2. Begin stack unwinding using .eh_frame
    // 3. Call personality routine for each frame
    // 4. Find matching catch handler using .gcc_except_table
    // 5. Transfer control to landing pad
    //
    // Without exception tables, we can only terminate
    abort();
}

void* __cxa_begin_catch(void* exc_obj_in) {
    fprintf(stderr, "STUB: __cxa_begin_catch(%p)\n", exc_obj_in);
    current_exception = exc_obj_in;
    // In real implementation: decrement uncaught_exception count
    return exc_obj_in;
}

void __cxa_end_catch() {
    fprintf(stderr, "STUB: __cxa_end_catch()\n");
    // In real implementation: destroy exception if last handler
    if (current_exception) {
        free(current_exception);
        current_exception = nullptr;
    }
}

[[noreturn]] void __cxa_rethrow() {
    fprintf(stderr, "STUB: __cxa_rethrow()\n");
    fprintf(stderr, "STUB: Rethrow not supported without full exception tables\n");
    abort();
}

void* __cxa_get_exception_ptr(void* exc_obj_in) {
    fprintf(stderr, "STUB: __cxa_get_exception_ptr(%p)\n", exc_obj_in);
    return exc_obj_in;
}

// Type info for built-in types (minimal stubs)
// These would normally be provided by the C++ runtime
struct __cxa_type_info {
    const void* vtable;
    const char* name;
};

// Stub type_info for int
__cxa_type_info _ZTIi = { nullptr, "i" };
__cxa_type_info _ZTIv = { nullptr, "v" };
__cxa_type_info _ZTIb = { nullptr, "b" };
__cxa_type_info _ZTIc = { nullptr, "c" };
__cxa_type_info _ZTIl = { nullptr, "l" };
__cxa_type_info _ZTIf = { nullptr, "f" };
__cxa_type_info _ZTId = { nullptr, "d" };

} // extern "C"
