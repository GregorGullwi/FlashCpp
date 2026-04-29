# Known Issues

## Template Array Type Arguments

### Pre-existing: Global template struct instances with array members crash at runtime

**File(s)**: Various codegen paths  
**Severity**: Medium  
**Status**: Pre-existing, not introduced by recent changes  

Global variable declarations of template struct instances whose template argument
is an array type (e.g. `Box<int[3]> b;` at namespace scope) compile and link
successfully but crash at runtime with SIGSEGV.  Stack-local variables of the
same type work correctly (e.g. `Box<int[3]> b;` inside a function body).

This is a code generation issue in the backend when initialising BSS-section
globals for template struct specialisations containing array-typed members.

**Workaround**: Use stack-local or static-const variables instead of
namespace-scope mutable globals for such types.
