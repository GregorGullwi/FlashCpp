# Refactoring Plan (GPT-5.1): Unifying Function / Member / Template Parsing

## 1. Goals and Constraints

- **Reduce duplication** between:
  - Free function parsing (declarations + definitions).
  - Member function parsing (inline + out-of-line).
  - Function template parsing (free + member + out-of-line).
- **Preserve behavior**: no observable parse tree / IR changes for existing valid code.
- **Keep error messages stable** where possible.
- **Support future features** (requires-clauses, attributes, contracts) from a single code path.

All work should be **incremental**, always keeping the parser in a compilable, test-passing state.

---

## 2. Inventory and Baseline

1. **Identify all relevant entry-points** (names based on the current codebase; adjust to exact symbols):
   - `parse_declaration_or_function_definition`
   - `parse_function_declaration` / `parse_function_definition`
   - `parse_member_function_definition`
   - `parse_template_declaration`
   - `parse_member_function_template`
   - `try_parse_out_of_line_template_member`
   - Any helpers that already parse parameters, attributes, CV/ref qualifiers, `noexcept`, trailing return types, or template parameter lists.
2. **Classify responsibilities** for each entry point:
   - Where are **declaration-only** vs **definition-with-body** responsibilities located?
   - Where is **symbol table registration** performed?
   - Where is **out-of-line member lookup / signature matching** performed?
3. **Document divergences**:
   - Cases where templates behave differently from non-templates.
   - Differences in how attributes / `constexpr` / `noexcept` / default arguments are handled.

Outcome: a short internal checklist (comments or a local note) mapping: *"feature X is handled in these N places"*.

---

## 3. Target Abstractions

Introduce a small set of reusable, orthogonal building blocks inside `Parser` (or a closely-related helper) that all of the above entry points can share.

### 3.1 ParsedFunctionHeader

A POD-like struct representing the *syntactic* header of a function (free or member), **without** its body:

```cpp
struct ParsedFunctionHeader {
	ASTTypeNode* return_type = nullptr;          // or appropriate node type
	Token name_token;                            // identifier or operator-token
	ASTNode* declarator = nullptr;               // if the project uses a declarator tree
	std::vector<ASTNode*> parameters;            // or ParamDeclNode*, matching existing AST
	TemplateParameterListNode* template_params = nullptr; // may be null for non-templates
	CVQualifier cv_qualifiers = CVQualifier::None;
	RefQualifier ref_qualifier = RefQualifier::None;
	bool is_noexcept = false;
	bool is_constexpr = false;
	bool is_consteval = false;
	bool is_deleted = false;
	bool is_defaulted = false;
	bool is_constructor = false;
	bool is_destructor = false;
	bool is_vararg = false;
	RequiresClauseNode* requires_clause = nullptr; // future-proof
	AttributeListNode* attributes = nullptr;
};
```

### 3.2 ParsedFunctionContext

A light-weight context object describing *where* the function lives and what kind it is:

```cpp
enum class FunctionOwnerKind { Free, Member, Namespace }; // extend as needed

struct ParsedFunctionContext {
	FunctionOwnerKind owner_kind;
	StructTypeInfo* owner_struct = nullptr; // for members; null otherwise
	TemplateParameterListNode* enclosing_template_params = nullptr; // class / function templates
	bool is_out_of_line = false;          // A::f outside the class
	bool is_template_definition = false;  // has its own template parameter list
};
```

### 3.3 Unified Header Parsing API

Provide a single entry-point that parses *everything up to (but not including) the body*:

```cpp
// Precondition: return-type and any leading specifiers already parsed
ParseResult<ParsedFunctionHeader> Parser::parseFunctionHeader(
	const ParsedFunctionContext& context,
	ASTTypeNode* already_parsed_return_type,
	/* possibly: already parsed attributes / storage-class */
);
```

Responsibilities of `parseFunctionHeader`:
- Given a context and pre-parsed type + attributes, consume:
  - Function name or qualified-id (`A::f`, `A<T>::f`, `operator+`, etc.).
  - Parameter list `(T x, U y, ...)` including default arguments.
  - CV/ref qualifiers.
  - `noexcept` / exception-specification.
  - Trailing return type `-> T` if present.
  - Function-level attributes that appear after the parameter list.
  - `requires`-clause if supported.
- Produce a fully-formed `ParsedFunctionHeader` and **do not**:
  - Open/close scopes.
  - Insert into symbol tables.
  - Parse the body or `= default` / `= delete` semantics beyond flagging them.

### 3.4 Unified Body Parsing API

Once a header is available, provide a shared routine for parsing the function body or handling `= default` / `= delete`:

```cpp
struct ParsedFunctionBodyResult {
	CompoundStmtNode* body = nullptr; // null for declarations / defaulted / deleted
	// possibly flags or auxiliary info, e.g., ctor-initializer list
};

ParseResult<ParsedFunctionBodyResult> Parser::parseFunctionBody(
	const ParsedFunctionContext& context,
	const ParsedFunctionHeader& header
);
```

Responsibilities of `parseFunctionBody`:
- Enter `ScopeType::Function` with the appropriate owner.
- Declare parameters and (for members) `this` in the symbol table.
- If constructor: parse and attach the mem-initializer list.
- If the function is defaulted or deleted: consume `= default;` / `= delete;`, do not parse a body.
- Otherwise: parse the compound-statement `{ ... }` and exit scope cleanly.

### 3.5 Centralized Signature Matching

For out-of-line members and template specializations, centralize signature comparison:

```cpp
bool Parser::signaturesMatch(
	const FunctionDeclNode& declaration,
	const ParsedFunctionHeader& definition_header,
	const ParsedFunctionContext& context
);
```

This keeps all the annoying C++ rules (top-level `const`, reference collapsing, default args, template parameter equivalence) in one place.

---

## 4. Refactoring Phases

Each phase should end with all tests passing and no behavior changes other than bug fixes clearly understood from diffs.

### Phase 0: Characterize Current Behavior

- Add or extend focused tests in `tests/FlashCppTest` for the following cases (if not already covered):
  - Free functions: declarations, definitions, overload sets, default args.
  - Member functions: inline + out-of-line, const / ref qualifiers, `noexcept`.
  - Function templates (free + member) with default template arguments.
  - Out-of-line template member definitions.
  - Edge cases already known to be tricky in this codebase (check existing bugs / TODOs).
- Use verbose parser / IR tracing (`FlashCpp.exe -v`) on a representative suite and store dumps locally (not committed) as a reference when refactoring.

### Phase 1: Extract Shared Parameter / Qualifier Parsing

Goal: factor out the *lowest-risk, most obviously duplicated* pieces first.

1. Identify and extract helpers for:
   - Parameter list parsing: `parseParameterClause` (or similar name), which returns a vector of parameter nodes + vararg flag.
   - CV / ref / `noexcept` / trailing return type parsing: small, orthogonal functions (e.g., `parseCvRefNoexceptAndTrailingReturn`).
2. Replace call-sites in:
   - Free function parsing.
   - Member function parsing.
   - Template function parsing.
3. Keep these helpers internal; do **not** introduce `ParsedFunctionHeader` yet.

Checkpoint: All call-sites share the same low-level parsing logic; tests still pass.

### Phase 2: Introduce `ParsedFunctionHeader` for Free Functions

Goal: introduce the `ParsedFunctionHeader` abstraction in the simplest context.

1. Implement the `ParsedFunctionHeader` struct and `ParsedFunctionContext` enum/struct.
2. Implement `Parser::parseFunctionHeader` but initially **only support free functions** (`owner_kind == FunctionOwnerKind::Free`).
3. Update the free-function path in `parse_declaration_or_function_definition` / `parse_function_declaration` to:
   - Build a `ParsedFunctionContext` for a free function.
   - Call `parseFunctionHeader`.
   - Build the existing AST nodes from the resulting header (no semantic change).
4. Do **not** touch member functions or templates in this phase.

Checkpoint: All free-function parsing paths go through `parseFunctionHeader`, semantics unchanged.

### Phase 3: Wire `parseFunctionBody` for Free Functions

1. Introduce `ParsedFunctionBodyResult` and `parseFunctionBody`.
2. Refactor free-function **definition** parsing to:
   - Build the header using `parseFunctionHeader`.
   - Call `parseFunctionBody` for anything that has a body or `= default` / `= delete`.
   - Assemble the final AST as before.
3. Ensure symbol table interactions in the old code are moved into `parseFunctionBody` (or a helper it uses), not duplicated at call-sites.

Checkpoint: Free-function declarations + definitions (non-member, non-template) are entirely expressed in terms of `parseFunctionHeader` + `parseFunctionBody`.

### Phase 4: Extend Header Parsing to Member Functions

1. Extend `ParsedFunctionContext` to represent member functions:
   - Set `owner_kind = FunctionOwnerKind::Member` and `owner_struct`.
   - Add `is_out_of_line` flag for `A::f` definitions.
2. Update `parse_member_function_definition` and any inline member parsing logic to:
   - Construct an appropriate `ParsedFunctionContext`.
   - Call `parseFunctionHeader`.
3. For **inline members inside the class**:
   - Ensure the context includes the current class template parameters (if any).
4. For **out-of-line members**:
   - Use `parseFunctionHeader` after parsing the qualified name.

Checkpoint: All member function parsing paths (inline + out-of-line, non-template) share `parseFunctionHeader`. Body parsing may still be split; unification happens in the next step.

### Phase 5: Use `parseFunctionBody` for Members

1. Update member function **definitions** to use `parseFunctionBody`:
   - Ensure correct `this` parameter insertion and constructor initializer handling are now centralized.
   - Remove duplicated scope set-up / tear-down code from individual member parsing functions.
2. Validate behavior for:
   - Const and ref-qualified members.
   - Constructors / destructors.
   - Out-of-line definitions.

Checkpoint: Free + member non-template functions uniformly use `parseFunctionHeader` + `parseFunctionBody`.

### Phase 6: Integrate Function Templates

1. Reuse existing template-parameter parsing helper (`parse_template_parameter_list` or similar) so it returns a `TemplateParameterListNode`.
2. Extend `ParsedFunctionHeader` to contain `template_params` (already present in the struct) and ensure `parseFunctionHeader` can:
   - Handle functions that have their own `template<...>` list.
   - Honor enclosing class template parameters via `ParsedFunctionContext::enclosing_template_params`.
3. Update:
   - `parse_template_declaration`.
   - `parse_member_function_template`.
   - Any additional template-specific entry points.
   so they:
   - Parse template params (if any).
   - Build a `ParsedFunctionContext` with `is_template_definition = true` and the right template parameter lists.
   - Call `parseFunctionHeader`.
4. For template **definitions**, route bodies through `parseFunctionBody` as for non-templates.

Checkpoint: All function templates (free + member, inline + out-of-line) rely on the same header / body parsing machinery.

### Phase 7: Centralize Signature Matching for Out-of-Line Definitions

1. Implement `Parser::signaturesMatch` using `ParsedFunctionHeader` (for the definition) and existing AST nodes (for the prior declaration).
2. Refactor:
   - Out-of-line member definition handling.
   - Out-of-line template member handling (`try_parse_out_of_line_template_member`, etc.).
   - Any other place that currently re-implements comparison logic.
3. Keep the matching rules identical; add comments/tests for any deliberate behavior change.

Checkpoint: All signature comparison logic lives in one place and uses the unified representation.

---

## 5. Safety Practices During Refactor

- **Small commits / steps**: keep each phase (or sub-phase) limited so that diffs are mechanically checkable.
- **Tests after each phase**:
  - `./build_test.bat` or `make test` depending on environment.
  - Spot-check `FlashCpp.exe -v` output on a few canonical examples (free function, member, template) and compare to pre-refactor dumps.
- **Feature flags if needed**:
  - For any high-risk rewrite, consider temporarily plumbing a debug flag or macro that can switch between old and new paths for local A/B comparisons.
- **Avoid AST shape changes** unless necessary:
  - Where possible, keep the AST node graph identical and only change how it is constructed. If new nodes/fields are required, add tests that lock down their behavior.

---

## 6. Future Extensions Enabled by This Refactor

Once all parsing flows share `ParsedFunctionHeader` + `ParsedFunctionBody` + centralized signature matching, it becomes easier to:

- Add new function-level features (contracts, attributes, `constexpr`/`consteval` variations) by touching a single codepath.
- Introduce more precise diagnostics for mismatched out-of-line definitions.
- Simplify later stages (semantic analysis / IR generation) by having a more uniform representation of functions and templates.
