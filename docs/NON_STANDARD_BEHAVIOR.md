# FlashCpp — Non-Standard and Deviating C++20 Behavior

This document catalogues every known place where FlashCpp's behavior deviates from
the ISO C++20 standard, produces incorrect results, or deliberately accepts non-standard
extensions.  Items that are already fully documented in another `docs/` file are
cross-referenced rather than duplicated.

> **Legend**
> - ✅ Correct — standard-compliant
> - ⚠️ Partial — works in common cases; edge cases deviate
> - ❌ Missing / Wrong — standard requires this; FlashCpp does not implement it or
>   produces incorrect output

---

## 1. Name Lookup & Overload Resolution

### 1.1 Argument-Dependent Lookup (ADL) ✅ (Phase 4A + 4B implemented)

**Standard (C++20 [basic.lookup.argdep]):** For an unqualified call `f(args…)` whose arguments
have class or enumeration type, lookup must also search the *associated namespaces* of the
argument types (Koenig lookup).

**FlashCpp:** ADL is implemented via `SymbolTable::lookup_adl()`.  Both ordinary namespace-scope
functions and inline friend functions (hidden friends) are findable via ADL.

**Location:** `src/SymbolTable.h` — `lookup_adl()`, `insert_into_namespace()`

---

### 1.1a Hidden Friends Also Visible via Ordinary Lookup ⚠️

**Standard (C++20 [basic.lookup.argdep]):** A *hidden friend* (a friend function defined inside a
class body) is deliberately **not** introduced into the enclosing namespace scope for ordinary
unqualified lookup.  It should only be found via ADL.

**FlashCpp:** When an inline friend function definition is parsed (e.g.,
`friend int getVal(X x) { return x.val; }`), FlashCpp registers it in
`SymbolTable::namespace_symbols_` via `insert_into_namespace()`.  This makes the function also
findable by ordinary unqualified lookup — not just ADL — which is **non-standard**.

In practice this widens rather than narrows what compiles, so no correct program is rejected.
All standard ADL usage still works correctly.

**Location:** `src/Parser_Decl_StructEnum.cpp` — `parse_friend_declaration()`

---

### 1.2 Free-Function Operator Overloads Invisible to Binary Operator Resolution ❌

**Standard (C++20 [over.match.oper]):** Resolving `a op b` must consider both member and
non-member (free-function) operator overloads from ordinary lookup and ADL.

**FlashCpp:** `findBinaryOperatorOverload` (OverloadResolution.h:734) only searches member
functions of the left operand's class and its bases.  Free-function operators declared at
namespace scope are silently ignored.

```
// TODO: In the future, also search for free function operator overloads
```

**Location:** `src/OverloadResolution.h:734`

---

### 1.3 Overload Resolution Falls Back to First Overload When No Match Found ❌

**Standard (C++20 [over.match]):** When multiple overloads are viable but none is uniquely
best the call is ill-formed (ambiguous) and must produce a diagnostic.  When no overload is
viable the call is also ill-formed.

**FlashCpp:** After exact-match and count-match phases both fail, `lookup_function`
(SymbolTable.h:585) silently returns the *first* registered overload:

```cpp
// Fallback: return the first overload
return overloads[0];
```

Ambiguous and no-viable-overload cases are accepted without a diagnostic; the program may
link and produce wrong results.

**Location:** `src/SymbolTable.h:585`

---

### 1.4 Conversion Operator Lookup Uses "operator user_defined" Workaround ⚠️

**Standard (C++20 [class.conv.fct]):** Conversion functions are named `operator T()`.
Lookup proceeds through normal member name lookup.

**FlashCpp:** For conversion operators whose return type was stored as `UserDefined` (often
via a template type alias), the compiler falls back to searching for the string literal
`"operator user_defined"` and uses type-size matching as a tiebreaker
(CodeGen_MemberAccess.cpp:3469–3525).  This can produce "undefined reference to
`operator user_defined`" linker errors for pattern templates.

**Location:** `src/CodeGen_MemberAccess.cpp:3469–3525`

---

### 1.5 Name Mangling Uses Wrong Fallback Types for Unknown Kinds ❌

**Standard / ABI:** Both Itanium ABI and MSVC mangling are deterministic.  An unknown or
unrecognised type has no valid fallback encoding; the correct behaviour is a compile error.

**FlashCpp:**
- `appendItaniumTypeCode` (NameMangling.h:327) falls back to `'i'` (int) for unknown scalar
  types and `'v'` (void) for unknown struct types.
- `appendMsvcTypeCode` (NameMangling.h:183, 187) falls back to `'H'` (int).

The resulting symbol names do not match those produced by GCC/Clang/MSVC; linking with
external libraries or system code that uses these types will fail silently.

**Location:** `src/NameMangling.h:183, 187, 321, 327, 562, 566`

---

### 1.6 Two-Phase Template Name Lookup Not Formally Separated ⚠️

**Standard (C++20 [temp.dep]):** Non-dependent names in a template body must be resolved at
*definition* time (Phase 1); dependent names are deferred to *instantiation* time (Phase 2).

**FlashCpp:** `createBoundIdentifier` (Parser.h:1190) performs a single lookup at parse time
and returns `IdentifierBinding::Unresolved` for anything it cannot immediately classify,
including types and structs.  There is no formal Phase 1 / Phase 2 separation.  In practice
most non-dependent names bind correctly because codegen falls back to runtime lookup, but
certain non-dependent names that happen to be types fall through to `Unresolved` and rely on
the codegen fallback path.

**Location:** `src/Parser.h:1186–1246`
**Plan:** Phase 3 of the Identifier Resolution Plan.

---

## 2. Lambda Expressions

### 2.1 Capture-All `[=]` / `[&]` Expanded at Parse Time, Not by ODR-Use ❌

**Standard (C++20 [expr.prim.lambda.capture]):** The set of entities captured by `[=]` or
`[&]` is exactly the set of *odr-used* local variables and parameters within the lambda body.
This set is a semantic property and cannot be determined before the body is fully analysed.

**FlashCpp:** `parse_lambda_expression` (Parser_Expr_ControlFlowStmt.cpp:1036–1114) eagerly
enumerates every symbol currently visible in the symbol table at the point of the `[` token
and converts all of them into explicit per-variable captures.  Consequences:

- Variables that are in scope but never used inside the body are captured unnecessarily.
- Variables declared after the lambda but in the same enclosing scope are never captured.
- Variables that are only odr-used inside a discarded branch may be incorrectly captured.

**Location:** `src/Parser_Expr_ControlFlowStmt.cpp:1036–1114`

---

### 2.2 `[=]` / `[&]` Not Supported in Constexpr Lambda Evaluation ❌

**Standard (C++20 [expr.const]):** A `constexpr` lambda may use implicit captures as long as
all odr-used captured entities satisfy constant-expression constraints.

**FlashCpp:** The constexpr evaluator returns a hard error for `AllByValue` and
`AllByReference` capture kinds:

> *"Implicit capture [=] or [&] not supported in constexpr lambdas – use explicit captures"*

**Location:** `src/ConstExprEvaluator_Core.cpp:989–994`

---

### 2.3 `[this]` and `[*this]` Captures Not Supported in Constexpr Context ❌

**Standard (C++20 [expr.const]):** A `constexpr` lambda in a `constexpr` member function may
capture `this` or `*this`.

**FlashCpp:** The constexpr evaluator returns a hard error:

> *"Capture of 'this' not supported in constexpr lambdas"*

**Location:** `src/ConstExprEvaluator_Core.cpp:996–1000`

---

### 2.4 Missing Named Capture Produces a Warning, Not a Compile Error ❌

**Standard:** All explicitly-named captured variables must be in scope at the lambda
declaration point; failure is ill-formed (compile error).

**FlashCpp:** When a captured variable cannot be resolved during the lambda-collection pass,
a `Warning` is logged but compilation continues.  The resulting closure may be malformed.

**Location:** `src/CodeGen_Lambdas.cpp:80`

---

## 3. Templates

### 3.1 Non-Type Template Parameter (NTTP) Deduction Not Implemented ❌

**Standard (C++20 [temp.deduct]):** Template argument deduction must handle NTTPs, deducing
their values from the corresponding function arguments treated as constant expressions.

**FlashCpp:** `deduceTemplateArguments` (Parser_Templates_Inst_Deduction.cpp:813) has:

```
// TODO: Add support for non-type parameters and more complex deduction
```

NTTPs without default values that cannot be deduced cause a logged error and `std::nullopt`.
NTTPs with defaults are silently skipped.  Users must always supply NTTP values explicitly.

**Location:** `src/Parser_Templates_Inst_Deduction.cpp:813, 1114`

---

### 3.2 Complex Dependent-Type Instantiation Falls Back Silently ⚠️

**Standard:** Template instantiation must correctly substitute all dependent type forms.

**FlashCpp:** When the primary type-resolution path fails during instantiation,
Parser_Templates_Inst_Deduction.cpp:1413 falls back to a "simple substitution (old
behavior)" path without emitting a diagnostic.  Incorrectly instantiated templates can reach
codegen and link.

**Location:** `src/Parser_Templates_Inst_Deduction.cpp:1413`

---

### 3.3 `std::integral_constant::value` Synthesised From Template Arguments, Bypassing Lookup ⚠️

**Standard:** `std::integral_constant<T,v>::value` is a well-defined static `constexpr` data
member accessed through normal name lookup and constexpr evaluation.

**FlashCpp:** `ConstExprEvaluator_Members.cpp:905` synthesises the value directly from the
template argument when the static member is not registered, labelled as:

> *"Fallback: synthesize integral_constant::value from template arguments when static member
> isn't registered"*

This gives the correct result for the primary template but would give wrong answers for any
explicit specialisation of `integral_constant`.

**Location:** `src/ConstExprEvaluator_Members.cpp:905`

---

## 4. Constant Expressions (`constexpr` / `consteval`)

See also `docs/CONSTEXPR_LIMITATIONS.md` for a more detailed treatment of the items
marked **[Known]** below.

### 4.1 Constructor Body Assignments Not Evaluated ❌ [Known]

**Standard (C++20 [expr.const]):** A `constexpr` constructor may initialise members via
assignments in the constructor body.

**FlashCpp:** Only member-initialiser-list entries are evaluated; body assignments are
silently ignored, leaving members at their default-initialised (usually zero) values.

**Location:** `src/ConstExprEvaluator_Members.cpp`, `docs/CONSTEXPR_LIMITATIONS.md:119–140`

---

### 4.2 Multi-Statement `constexpr` Functions Not Evaluated ❌ [Known]

**Standard (C++20 [dcl.constexpr]):** A `constexpr` function may contain conditionals, loops,
local variables, and multiple return paths.

**FlashCpp:** Only single-`return`-expression functions are evaluated at compile time.

**Location:** `docs/CONSTEXPR_LIMITATIONS.md:145–173`

---

### 4.3 `consteval` Not Enforced as Compile-Time-Only ❌ [Known]

**Standard (C++20 [dcl.consteval]):** Calling a `consteval` function outside a
constant-expression context is ill-formed and must produce a compile error.

**FlashCpp:** `consteval` functions are parsed with an `is_consteval` flag but are treated
identically to `constexpr` at codegen time; the "only callable in constant-expression
context" rule is not enforced.

**Location:** `docs/MISSING_FEATURES.md` (consteval 40% complete)

---

### 4.4 `sizeof(arr)` May Be Parsed as `sizeof(type)` in Some Contexts ⚠️

**Standard:** `sizeof(arr)` where `arr` is an array variable must be treated as
`sizeof(expression)`, yielding the full array size.

**FlashCpp:** In some paths the parser incorrectly treats `arr` as a type name.  Two
workarounds exist that detect `size_in_bits == 0` and fall back to a symbol table lookup:

**Location:** `src/ConstExprEvaluator_Core.cpp:209–230`, `src/CodeGen_MemberAccess.cpp:1441`

---

### 4.5 Many Expression Types Return a Runtime Error Instead of a Compile-Time Error ⚠️

**Standard:** If a `constexpr` expression cannot be evaluated at compile time for a required
constant-expression context, the program is ill-formed and the compiler must diagnose it.

**FlashCpp:** `ConstExprEvaluator_Core.cpp:153–154` has a catch-all that returns
`EvalResult::error(…)` at *runtime*, which causes the caller to silently treat the expression
as non-constant and fall back to a codegen path that may produce wrong code without any
visible diagnostic.  Other specific gaps: `sizeof` with complex expressions (line 533),
certain bitwise and shift operators (lines 594–623).

**Location:** `src/ConstExprEvaluator_Core.cpp:153–154, 533, 594–623`

---

### 4.6 Complex Base Expressions in Nested Member Access Not Supported in Constexpr ⚠️ [Partially Known]

**Standard:** Arbitrary nesting depth of member access (`a.b.c.d`, including function-call
result bases) is valid in constant expressions.

**FlashCpp:**
- `ConstExprEvaluator_Members.cpp:1185` — *"Member access on non-struct constexpr variable not supported"*
- `ConstExprEvaluator_Members.cpp:1345` — *"Complex base expression in nested member access not supported"*

Simple `a.b.c` chains work; function-result or cast-result bases do not.

**Location:** `src/ConstExprEvaluator_Members.cpp:1185, 1345`

---

## 5. Calling Convention / ABI

### 5.1 Non-Variadic 9–16 Byte Structs Passed by Pointer (System V AMD64) ❌ [Known]

**ABI (System V AMD64 §3.2.3):** Structs whose total size is 9–16 bytes and whose fields
classify as INTEGER must be passed in two consecutive registers for non-variadic calls.

**FlashCpp:** All non-variadic struct arguments larger than 8 bytes are passed by pointer on
Linux.  Both caller and callee agree, so internal calls work; but calls to or from GCC/Clang
code using the actual ABI produce incorrect results.

**Location:** `src/IRConverter_Emit_CompareBranch.h:1239`, `src/IRConverter_Conv_Calls.h:273–275`
**See also:** `docs/KNOWN_ISSUES.md`

---

### 5.2 No Distinct Pointer Type in the IR `Type` Enum ⚠️

**Standard / ABI:** Pointer arguments must be distinguishable from integer arguments at the
IR level for correct ABI lowering, pointer-arithmetic semantics, and type-based dispatch.

**FlashCpp:** Arrays passed as arguments encode the pointer as a 64-bit value with the
*element* type (e.g., `Char` for `char[]`) rather than a distinct pointer type:

```
// TODO: Add proper pointer type support to the Type enum
```

**Location:** `src/CodeGen_Call_Direct.cpp:978`

---

### 5.3 `pointer_depth` Unconditionally Set to 0 in Multiple Places ⚠️

**Standard:** The pointer depth (indirection nesting level) must be tracked accurately for
correct codegen of multi-level pointer operations.

**FlashCpp:** `addr_op.operand.pointer_depth = 0;` with the comment `// TODO: Verify pointer
depth` appears at ≥ 7 call sites.  Multi-level pointer operations (`int**`, `char***`, etc.)
may produce incorrect IR.

**Location:** `src/CodeGen_Expr_Operators.cpp:863, 940`,
`src/CodeGen_NewDeleteCast.cpp:672`,
`src/CodeGen_Visitors_Decl.cpp:2460`,
`src/CodeGen_Visitors_Namespace.cpp:338`,
`src/CodeGen_Stmt_Decl.cpp:886, 1239, 1651`

---

### 5.4 Parenthesized Declarator Form `(*fp)(params)` Not Parsed in All Contexts ⚠️

**Standard:** The full declarator grammar includes `(*fp)(params)` for function pointer
variables and `(*)(params)` for unnamed function pointer parameters in all declaration
contexts.

**FlashCpp:** `parse_direct_declarator` (Parser_Decl_DeclaratorCore.cpp:898) has:

```
// TODO: Handle parenthesized declarators like (*fp)(params) for function pointers
```

Specialized paths handle many function pointer cases, but certain compound forms (e.g.,
function returning a function pointer, arrays of function pointers in some contexts) may fail
to parse.

**Location:** `src/Parser_Decl_DeclaratorCore.cpp:898`

---

## 6. Exception Handling

The detailed status of every exception-handling feature is in `docs/EXCEPTION_HANDLING.md`.
The summary of deviations:

| Deviation | Platform | Status |
|-----------|----------|--------|
| Nested `try` blocks crash (SIGABRT) | Linux (Itanium EH) | ❌ Known |
| `throw;` (rethrow) not implemented | Linux | ❌ Known |
| Class-type exception object destructors not called | Both | ❌ Known |
| Stack unwinding with local destructors not implemented | Both | ❌ Known |
| Cross-function `catch` fails at runtime | Windows | ❌ Known |
| `_CxxThrowException` called with NULL `ThrowInfo` | Windows | ❌ Known |
| `__cpp_exceptions` not defined despite partial EH support | Both | ⚠️ See §6.1 below |

### 6.1 `__cpp_exceptions` Not Defined; Standard Library Uses Abort Paths ⚠️

**Standard (SD-6):** `__cpp_exceptions` must be defined when exception handling is supported.
Standard library headers (`<stdexcept>`, `<new>`, etc.) guard their exception-throwing paths
with `#if __cpp_exceptions`.

**FlashCpp:** The macro is intentionally absent (FileReader_Macros.cpp:1518–1520).  Even the
partial exception handling that does work (simple `throw`/`catch` of primitives on Linux and
Windows) is therefore invisible to the standard library.  `std::bad_alloc`, `std::out_of_range`,
and similar types will call `std::abort()` rather than throw.

**Location:** `src/FileReader_Macros.cpp:1518–1520`

---

## 7. Preprocessor

### 7.1 `_Pragma()` Only Processes `pack`; All Other Pragmas Silently Discarded ⚠️

**Standard (C++20 [cpp.pragma.op]):** `_Pragma(string-literal)` destringises its argument
and processes it as a `#pragma` directive.

**FlashCpp:** Only `#pragma pack` is processed; `#pragma once`, `#pragma comment`,
`#pragma warning`, `#pragma GCC`, `#pragma clang`, and all others reached via `_Pragma(…)` are
silently discarded.  In particular, `_Pragma("once")` as used in some third-party headers
will silently fail to guard against double-inclusion.

**Location:** `src/FileReader_Macros.cpp:173–176`

---

### 7.2 `__GNUC__` and `_MSC_VER` Both Always Defined ❌

**Standard:** A compiler should identify as one vendor.  Defining both `__GNUC__` (GCC/Clang)
and `_MSC_VER` (MSVC) simultaneously is non-standard.

**FlashCpp:** Both macros are always defined regardless of target mode:
- `__GNUC__ = "12"` (FileReader_Macros.cpp:1377) — for `libstdc++` compatibility.
- `_MSC_VER = "1944"` (FileReader_Macros.cpp:1463) — for MSVC header compatibility.

Headers that use `#if defined(_MSC_VER) && !defined(__GNUC__)` or similar exclusive guards
take unexpected paths.

**Location:** `src/FileReader_Macros.cpp:1377, 1463`

---

### 7.3 Feature-Test Macros Advertise Unimplemented Features ❌

**Standard (SD-6):** `__cpp_*` macros shall only be defined if the corresponding feature is
fully implemented.

**FlashCpp** defines the following at full C++20 values even though the features are
incomplete or absent:

| Macro | Value | Gap |
|-------|-------|-----|
| `__cpp_consteval` | `201811L` | Compile-time-only enforcement not implemented (§4.3) |
| `__cpp_constexpr` | `202002L` | Multi-statement bodies, body-assignment ctors, `new`/`delete` in constexpr absent |
| `__cpp_constexpr_dynamic_alloc` | `201907L` | `constexpr std::string` / `std::vector` explicitly not implemented |

**Location:** `src/FileReader_Macros.cpp:1492–1560`

---

### 7.4 `__asm` / `__asm__` Stripped to Empty String ⚠️

**Context:** `asm` declarations (`extern T f() __asm("impl_name")`) appear in system headers
for symbol renaming.

**FlashCpp:** Both `__asm` and `__asm__` are defined as function-like macros that expand to
the empty string (FileReader_Macros.cpp:1424–1425).  Any `extern T f() __asm("f_impl")`
declaration will have its rename silently dropped; `f()` links as `f` rather than `f_impl`,
producing undefined-symbol linker errors when the system library exposes `f_impl`.

**Location:** `src/FileReader_Macros.cpp:1424–1425`

---

### 7.5 `__restrict` Defined as an Empty Macro ⚠️

**Context:** `__restrict` is a GCC/Clang extension that appears pervasively in system headers.
The correct handling is to treat it as a no-op keyword (not a macro).

**FlashCpp:** `defines_["__restrict"] = DefineDirective{};` expands `__restrict` to the empty
string.  This works for the common case but means `#ifdef __restrict` evaluates to *true*
(defined, but as an empty macro), which differs from GCC/Clang where `__restrict` is a keyword,
not a macro, so `#ifdef __restrict` would be *false*.

**Location:** `src/FileReader_Macros.cpp:1381`

---

## 8. Code Generation

### 8.1 Synthesised Comparison Operators Return `false` When `operator<=>` Not Found ❌

**Standard (C++20 [class.spaceship]):** Synthesised relational operators must only be
generated when `operator<=>` is defaulted.  If the spaceship operator is absent the synthesis
must not silently produce an incorrect result.

**FlashCpp:** `CodeGen_Visitors_Decl.cpp:705–706` has:

```cpp
// Fallback: operator<=> not found, return false for all synthesized operators
emitReturn(IrValue{0ULL}, Type::Bool, 8, …);
```

If `operator<=>` is not found for any reason, all synthesised comparison operators silently
return `false`.  A program using `<` on such a type compiles without error but produces
universally wrong results.

**Location:** `src/CodeGen_Visitors_Decl.cpp:705–706`

---

### 8.2 Unsupported `new[]` Element Initialisers Silently Skip Elements ❌

**Standard:** All initialiser forms for heap-allocated arrays must be evaluated.

**FlashCpp:** `CodeGen_NewDeleteCast.cpp:94–97` and `224–230`:

```cpp
// Skip if the initializer is not supported
if (!init.is<InitializerListNode>() && !init.is<ExpressionNode>()) {
    FLASH_LOG(Codegen, Warning, "Unsupported array initializer type, skipping element ", i);
    continue;
}
```

An unsupported initialiser type (e.g., certain nested designated initialisers) causes the
array element to be left uninitialised with only a warning log.

**Location:** `src/CodeGen_NewDeleteCast.cpp:94–97, 224–230`

---

### 8.3 Unimplemented Unary Operators Throw `InternalError` Instead of a User Diagnostic ⚠️

**Standard:** All unary operators on supported types must compile; unsupported combinations
must produce a compile error with a meaningful message.

**FlashCpp:** `CodeGen_Expr_Conversions.cpp:1514` throws `InternalError("Unary operator not
implemented yet")` in the catch-all else branch, producing a compiler crash rather than a
user-visible error.

**Location:** `src/CodeGen_Expr_Conversions.cpp:1514`

---

### 8.4 Parser Can Return `size_bits = 0` for Valid Variable Declarations ⚠️

**Standard:** All declared variables must have a known, non-zero type size.

**FlashCpp:** `CodeGen_Expr_Primitives.cpp:228–232` contains an explicit workaround:

```cpp
// Fallback: if size_bits is 0, calculate from type (parser bug workaround)
FLASH_LOG(Codegen, Warning,
    "Parser returned size_bits=0 for identifier '...' (type=...) - using fallback calculation");
size_bits = get_type_size_bits(type_node.type());
```

The fallback uses only the scalar type kind and ignores struct sizes entirely; struct
variables that hit this path may be given the wrong size.

**Location:** `src/CodeGen_Expr_Primitives.cpp:228–232`

---

## 9. Cross-Cutting: Already-Documented Issues (Summary)

The following deviations are fully documented in other files and are listed here for
completeness only.

| Issue | Reference |
|-------|-----------|
| Nested `try` blocks crash on Linux | `docs/EXCEPTION_HANDLING.md` |
| `throw;` rethrow not implemented on Linux | `docs/EXCEPTION_HANDLING.md` |
| Class-type exception destructors not called | `docs/EXCEPTION_HANDLING.md` |
| Stack unwinding with local destructors missing | `docs/EXCEPTION_HANDLING.md` |
| Windows cross-function `catch` fails at runtime | `docs/known_ir_issues.md` |
| Windows `_CxxThrowException` with NULL `ThrowInfo` | `docs/EXCEPTION_HANDLING.md` |
| `constexpr` constructor body assignments not evaluated | `docs/CONSTEXPR_LIMITATIONS.md` |
| Multi-statement `constexpr` functions not evaluated | `docs/CONSTEXPR_LIMITATIONS.md` |
| Array member access / subscript in constexpr limited | `docs/CONSTEXPR_LIMITATIONS.md` |
| `consteval` enforcement missing | `docs/MISSING_FEATURES.md` |
| 9–16 byte non-variadic structs passed by pointer (SysV) | `docs/KNOWN_ISSUES.md` |
| Default arg codegen silently drops unknown AST node types | `docs/KNOWN_ISSUES.md` |
| Assignment through reference-returning method treats return as rvalue | `docs/KNOWN_ISSUES.md` |
| Array-of-structs nested brace init not parsed | `docs/KNOWN_ISSUES.md` |
| Suboptimal IR for ref-param compound assignment | `docs/known_ir_issues.md` |
| Implicit designated init as function arg (no type name) | `docs/MISSING_FEATURES.md` |
| C++20 Modules entirely absent | `docs/MISSING_FEATURES.md` |
| Coroutines not implemented | `docs/MISSING_FEATURES.md` |

---

*Last updated: 2026-03-06.  Add new findings to the appropriate section above and update
the summary table in §9 if the item is already tracked in another doc.*
