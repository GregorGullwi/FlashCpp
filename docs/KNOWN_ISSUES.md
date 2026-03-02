# Known Issues

## Global Function Pointer Initialization

Global variables initialized with a function address (e.g., `int (*fp)(int, int) = add;`)
produce a zeroed pointer at runtime. The codegen emits "Non-constant initializer" and
falls back to zero. A relocation in the `.data` section is needed instead.

**Workaround**: Initialize the function pointer inside a function body rather than at
global scope.

**See**: `docs/TODO_ANALYSIS.md` item 25, `src/CodeGen_Stmt_Decl.cpp:100`

## `extern "C"` Function References Use C++ Mangling

When a function declared inside `extern "C" { }` is referenced by name (e.g., assigned
to a function pointer), the reference uses the C++ mangled name (`_Z5add_cii`) instead
of the unmangled C name (`add_c`). The function definition is emitted correctly with the
C name, but the reference doesn't match, causing a linker error.

**Workaround**: Avoid taking the address of `extern "C"` functions in contexts where the
compiler generates an indirect reference. Use a non-`extern "C"` wrapper function.

**See**: `nm` output shows `T add_c` (defined) vs `U _Z5add_cii` (referenced)
