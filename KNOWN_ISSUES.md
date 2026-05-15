# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## 1) Variable-template template-id fallback still treated as callable in dependent static_assert

- **Symptom**: During `<string_view>` instantiation, expressions such as `is_same_v<...>` can fail with  
  `Identifier is not a function or callable object: is_same_v`.
- **Root cause**: When variable-template instantiation fails in dependent contexts, the evaluator still falls
  back to call-expression semantics instead of preserving a dependent value-template expression.
- **Affected path**: `ExpressionSubstitutor::substituteFunctionCallImpl` / variable-template instantiation +
  constexpr call-evaluation fallback.
- **Impact**: Blocks portions of libstdc++ headers that rely on variable templates in dependent checks.

## 2) Out-of-line template member signature validation is still too strict for dependent aliases

- **Symptom**: Parser warnings like `Parameter 1 type mismatch in out-of-line template member ...` remain for
  valid declarations/definitions that differ only by dependent spelling/aliases.
- **Root cause**: `validate_signature_match` compares low-level type fields (`type_index`, category, pointer/ref)
  too rigidly for dependent contexts and does not use a more canonicalized/dependent-aware type equivalence.
- **Affected path**: `Parser::validate_signature_match`, invoked from out-of-line template member parsing.
- **Impact**: Noisy diagnostics and increased risk of picking/validating against suboptimal overload candidates.

## 3) Structured-binding tuple-like `get<I>` still prefers non-member specialization when both member and free forms exist

- **Symptom**: In tuple-like structured binding, when a type provides both `e.get<I>()` and `get<I>(e)` protocol forms,
  decomposition can resolve to the free `get` specialization instead of the member one.
- **Root cause**: `normalizeStructuredBinding` resolves `get<I>` through specialization-name probing; member and free
  specializations can collide in registry lookup and selection does not consistently preserve [dcl.struct.bind]/3
  member preference across all registration shapes.
- **Affected path**: `SemanticAnalysis::normalizeStructuredBinding` tuple-like protocol specialization resolution.
- **Impact**: Non-conforming binding semantics for mixed member/non-member tuple protocol definitions.
