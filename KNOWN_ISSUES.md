# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## KI-001 · Constexpr folding gaps in IR generation (residual cases)

**Severity:** QoI (incorrect code quality; correctness is preserved because the stored values
are correct).

**Status:** Partially resolved

### Symptom

`constexpr` identifier and member-access reads are now folded in common scalar cases, but
full expression-level folding is still incomplete in some operator-composition paths.

**Example A — global constexpr scalar:**
```cpp
constexpr int x = 42;
int main() { return x; }
```
*Pre-fix codegen (historical):*
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
*Pre-fix codegen (historical):*
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
*Pre-fix codegen (historical):*
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

The original KI-001 implementation gaps are now addressed:
- `IdentifierNode` constexpr reads are folded in `visitExpressionNode` (non-address context,
  non-member bindings, and non-enum typed identifiers).
- `MemberAccessNode` constexpr reads are folded in `visitExpressionNode` when not taking
  an lvalue address.
- `tryEvaluateAsConstExpr<T>` now preserves exact scalar type/size from evaluator results.

Residual limitation:
- Binary/operator composition is still not fully constant-folded at IR-generation time,
  so expressions like `p.x + p.y` may still emit runtime arithmetic on already-folded
  immediate operands.

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

So the evaluator is **fully capable** of constant-folding `origin.x` and `p.x + p.y`.
The IR layer now invokes it for identifier/member-access reads; remaining gaps are mostly in
operator-level propagation/composition.

### What Is Already Working

- **Global constexpr variable initialization**: The binary representation is correctly packed
  into `.rodata` via `packStructEvalResultIntoInitData`
  (`IrGenerator_Stmt_Decl.cpp`), and common scalar reads now fold to immediates.
- **Local constexpr function-call initializers**: When a local variable's initializer is a
  `CallExprNode`, the evaluator IS called at declaration time
  (`IrGenerator_Stmt_Decl.cpp:1303–1365`), and common scalar reads now reuse constexpr
  folding in expression lowering.
- **`sizeof` / `alignof`**: `tryEvaluateAsConstExpr` is used and produces correct results.
- **`static_assert` / `if constexpr` conditions**: Evaluated at parse time by the
  constexpr evaluator; unaffected by this issue.
- **Array dimension expressions**: Evaluated at compile time via
  `applyDeclarationArrayBoundsToTypeSpec` (`SemanticAnalysis.cpp:19`), which calls
  `ConstExpr::Evaluator::evaluate` directly on each dimension sub-expression.

### Recommended Fix

**Completed in current implementation**
- `tryEvaluateAsConstExpr` preserves scalar type information from evaluator results.
- `IdentifierNode` and `MemberAccessNode` attempt constexpr folding in expression lowering
  (with context/binding safety guards).

**Remaining work**

**Phase 4 — Propagate folded constants through binary operators**

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
