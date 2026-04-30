# Known Issues

## Local Array Initialisation from String Literals

**Status:** Pre-existing limitation (not introduced by recent changes)

**Description:**
Local arrays whose size is inferred from a string-literal initialiser are not
correctly allocated or initialised.  Examples:

```cpp
const char s[] = "*hidden";   // unsized – size not inferred from literal
const char s[8] = "*hidden";  // explicitly sized – content not copied
```

Both forms produce a stack slot of only 1 byte and leave the array
uninitialised.  The root cause is twofold:

1. `inferUnsizedArraySizeFromInitializer` (called from
   `IrGenerator_Stmt_Decl.cpp`) only handles `InitializerListNode` as the
   initialiser, not a bare `StringLiteralNode`.  Therefore the array element
   count is computed as 0 for the unsized case, and the variable is allocated
   as a single element.

2. Even when an explicit size is provided, the string-literal bytes are never
   stored into the stack array at compile time or via emitted byte-stores at
   runtime.

**Workaround:**
Use a brace-enclosed initialiser list instead:

```cpp
const char s[] = {'*', 'h', 'i', 'd', 'd', 'e', 'n', 0};  // works correctly
```

Or use a global `const char` array / pointer, whose initialisation from a
string literal is handled correctly in the global variable path.

**Affected files:**
- `src/IrGenerator_Stmt_Decl.cpp` – `inferUnsizedArraySizeFromInitializer`,
  local array declaration path, and the string-literal initialisation branch
  (lines ~1472–1484, ~1487 ff.)
- `src/SemanticAnalysis.cpp` – `applyDeclarationArrayBoundsToTypeSpec` does
  not set array dimensions for unsized arrays (no `array_dimensions_` entries
  for string-literal-inited unsized arrays).
