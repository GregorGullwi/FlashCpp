# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## KI-001 · No constant folding for `constexpr` variable reads at IR-generation time

**Severity:** QoI (incorrect code quality; correctness is preserved because the stored values
are correct).

**Status:** Open

### Symptom

A `constexpr` variable — whether scalar or aggregate — is read from memory at runtime
instead of being replaced with an inline immediate constant in the generated assembly.

**Example A — global constexpr scalar:**
```cpp
constexpr int x = 42;
int main() { return x; }
```
*Current codegen (main):*
```asm
mov    eax, DWORD PTR [rip+0x0]   ; loads x from .rodata
mov    DWORD PTR [rbp-0x30], eax
mov    eax, DWORD PTR [rbp-0x30]
pop    rbp
ret
```
*Expected (standard-conforming optimum):*
```asm
mov    eax, 42
ret
```

**Example B — global constexpr struct member access:**
```cpp
struct Point { int x; int y; };
constexpr Point origin{10, 32};
int main() { return origin.x + origin.y; }
```
*Current codegen (main):*
```asm
lea    rax, [rip+0x0]             ; load &origin
mov    eax, DWORD PTR [rax]       ; read origin.x
; ... store/reload temps ...
lea    rax, [rip+0x0]             ; load &origin (again)
mov    eax, DWORD PTR [rax+0x4]   ; read origin.y
add    eax, ecx                   ; add at runtime
```
*Expected:*
```asm
mov    eax, 42
ret
```

**Example C — local constexpr scalar:**
```cpp
int main() { constexpr int x = 42; return x; }
```
*Current codegen:*
```asm
movabs rax, 0x2a                  ; load 42 as immediate ...
mov    DWORD PTR [rbp-0x2c], eax  ; ... into a stack slot
mov    eax, DWORD PTR [rbp-0x2c]  ; then reload from stack (not folded)
```
*Expected:*
```asm
mov    eax, 42
ret
```

**Example D — local constexpr struct member:**
```cpp
struct Point { int x; int y; };
int main() { constexpr Point p{10,32}; return p.x + p.y; }
```
Generates 12 instructions (stack stores + reloads + runtime add) instead of 2.

### Root Cause

Three independent gaps in `IrGenerator_Expr_Primitives.cpp` and `AstToIr.h`:

#### Gap 1 — `generateIdentifierIr` never invokes the constexpr evaluator

`generateIdentifierIr` (`IrGenerator_Expr_Primitives.cpp`) resolves variable references
but contains **no check for `is_constexpr()`** and never calls
`ConstExpr::Evaluator::evaluate`. It always emits:
- A `GlobalLoad` IR instruction for globals (→ RIP-relative memory read in codegen).
- The variable name as a raw operand for locals (→ stack LOAD in codegen).

#### Gap 2 — `generateMemberAccessIr` never attempts constexpr folding

`generateMemberAccessIr` (`IrGenerator_MemberAccess.cpp`) always emits a `MemberLoadOp`
with opcode `IrOpcode::MemberAccess`, even when the base object is a `constexpr` variable
whose value is completely known at compile time.

#### Gap 3 — `tryEvaluateAsConstExpr` discards type information and is not reused

`tryEvaluateAsConstExpr<T>` (`AstToIr.h:494`) is a template helper that wraps
`ConstExpr::Evaluator::evaluate` and correctly populates local and global symbol tables.
It is currently called only for `SizeofExprNode` and `AlignofExprNode`
(`IrGenerator_Expr_Primitives.cpp:34,41`).

Additionally, on success it **always returns `TypeCategory::UnsignedLongLong` / 64-bit**
regardless of the actual type of the constant (`int`, `bool`, `double`, etc.). This means
any extension of this helper to cover constexpr variable reads would emit values with the
wrong IR type unless the type derivation is fixed alongside.

### What the ConstExpr Evaluator Can Already Do

`ConstExpr::Evaluator::evaluate_identifier` (`ConstExprEvaluator_Core.cpp:2882`) already:
- Looks up the variable in local then global symbol tables.
- Checks `is_constexpr()` (line 3162) and refuses non-constexpr variables.
- Recursively evaluates the initializer (handles `InitializerListNode`, `ConstructorCallNode`,
  nested structs).

`ConstExpr::Evaluator::evaluate_member_access` (`ConstExprEvaluator_Members.cpp:4314`) already:
- Evaluates the base object expression (calling `evaluate_identifier` above).
- Extracts the named member from `object_member_bindings`.
- Handles arrow access, nested member access, array subscript, and function-call results.

So the evaluator is **fully capable** of constant-folding `origin.x` and `p.x + p.y` in all
four examples above; it simply is never invoked from the IR generation layer for these cases.

### What Is Already Working

- **Global constexpr variable initialization**: The binary representation is correctly packed
  into `.rodata` via `packStructEvalResultIntoInitData`
  (`IrGenerator_Stmt_Decl.cpp`). Values are correct; only the reads are suboptimal.
- **Local constexpr function-call initializers**: When a local variable's initializer is a
  `CallExprNode`, the evaluator IS called at declaration time
  (`IrGenerator_Stmt_Decl.cpp:1303–1365`), and the `VariableDeclOp` receives a constant
  initializer. However, reads of that variable still generate stack loads.
- **`sizeof` / `alignof`**: `tryEvaluateAsConstExpr` is used and produces correct results
  (these always return `size_t`, so the type issue in Gap 3 does not manifest).
- **`static_assert` / `if constexpr` conditions**: Evaluated at parse time by the
  constexpr evaluator; unaffected by this issue.
- **Array dimension expressions**: Evaluated at compile time via
  `applyDeclarationArrayBoundsToTypeSpec` (`SemanticAnalysis.cpp:19`), which calls
  `ConstExpr::Evaluator::evaluate` directly on each dimension sub-expression.

### Recommended Fix

**Phase 1 — Fix `tryEvaluateAsConstExpr` to preserve type** (prerequisite)

In `AstToIr.h:516–524`, after a successful evaluation, derive the return `TypeCategory`,
`SizeInBits`, and `is_signed` from `eval_result.exact_type` (when set) or from the variant
discriminant (`long long` → `Int64`/`Int32`, `unsigned long long` → matching unsigned,
`bool` → `Bool`, `double` → `Double`). This prevents downstream sign-extension errors.

**Phase 2 — Fold `IdentifierNode` reads of constexpr scalars**

`visitExpressionNode` is implemented as a `std::visit` lambda over a `std::variant`
expression node (`IrGenerator_Expr_Primitives.cpp:5–13`). Inside that lambda, `T` is the
concrete node type and `expr` is the concrete value. The `IdentifierNode` arm currently
reads:

```cpp
} else if constexpr (std::is_same_v<T, IdentifierNode>) {
    return generateIdentifierIr(expr, context);
```

Change it to attempt constexpr folding first:

```cpp
} else if constexpr (std::is_same_v<T, IdentifierNode>) {
    // Attempt to fold scalar constexpr variable references to immediates.
    auto const_result = tryEvaluateAsConstExpr(expr);
    if (const_result.effectiveIrType() != IrType::Void)
        return const_result;
    return generateIdentifierIr(expr, context);
```

The evaluator's `is_constexpr()` check within `evaluate_identifier`
(`ConstExprEvaluator_Core.cpp`, function starts at line 2882, check at line 3162) ensures
non-constexpr variables return an error result and fall through to the normal path
harmlessly. Struct-typed constexpr variables whose `EvalResult` does not carry a scalar
variant also produce a void result and fall through.

**Phase 3 — Fold `MemberAccessNode` for constexpr struct objects**

In `visitExpressionNode`, before dispatching `MemberAccessNode` to `generateMemberAccessIr`,
apply the same `tryEvaluateAsConstExpr(expr)` pattern (identical to the `sizeof`/`alignof`
lines 34 and 41). The evaluator's `evaluate_member_access` already handles this path end-to-end.

**Phase 4 — Propagate folded constants through binary operators** (bonus)

With phases 2–3 in place, `origin.x + origin.y` still results in a runtime ADD of two
folded constants. Constant folding for `BinaryOperatorNode` with constant operands is the
next logical step, but is orthogonal to this issue.

**Phase 5 (optional, invasive) — Cache `EvalResult` on `VariableDeclarationNode`**

Add `std::optional<EvalResult> cached_value_` to `VariableDeclarationNode`
(`AstNodeTypes_Template.h:303`). (The trailing underscore matches the existing private-field
convention in that class — see `is_constexpr_`, `is_constinit_`, etc. at lines 366–367.) Populate it at declaration time when `is_constexpr()`.
Use it in `generateIdentifierIr` and `generateMemberAccessIr` to avoid re-evaluating the
initializer on every read. Improves compile-time performance for heavily-used constexpr
variables but requires AST changes.

### Affected Files

| File | Role |
|---|---|
| `src/IrGenerator_Expr_Primitives.cpp` | Primary fix site (dispatcher + identifier read) |
| `src/IrGenerator_MemberAccess.cpp` | Secondary fix site (member access folding) |
| `src/AstToIr.h` | Fix `tryEvaluateAsConstExpr` type derivation |
| `src/AstNodeTypes_Template.h` | Optional: add `cached_value_` to `VariableDeclarationNode` |

### Regression Risk

Low. The evaluator gates on `is_constexpr()`, so non-constexpr variables are unaffected.
For constexpr variables: the stored binary representation (in `.rodata`) is already correct,
so any fold that produces the same value is safe. All 2,249 existing tests continue to pass
because correctness is maintained; only code quality changes.

---

## KI-002 · Implicit constructor / assignment-operator bodies emitted for simple aggregates

**Severity:** QoI (unnecessary code size increase; no correctness impact).

**Status:** Open

### Symptom

For a plain aggregate struct with no user-declared constructors or operators:

```cpp
struct Point { int x; int y; };
```

FlashCpp emits full function bodies for the default constructor, copy constructor,
move constructor, copy assignment operator, and move assignment operator
(confirmed in disassembly of `/tmp/probe.o`). GCC and Clang elide these entirely when the
type is trivially copyable and the implicitly-defined functions are never ODR-used.

### Root Cause

The implicit-member generation pass in the IR generator emits codegen bodies for all
implicitly-declared special members unconditionally, rather than checking whether they are
ODR-used before emitting.

### Recommended Fix

Track ODR-use of implicit special members and defer emission until a use is observed.
Trivially copyable types whose special members are never explicitly called (e.g., a
`constexpr` global struct that is only read by field) should produce zero emitted bodies.

---
