# Investigation Report: Namespace-Qualified Template Instantiation Bug

## Summary
Templates defined in namespaces cannot be instantiated using qualified syntax (e.g., `ns::Template<Args>`). The parser fails during namespace closure, before reaching the usage line.

## Reproduction
```cpp
namespace ns { 
    template<typename T> struct S { static int v; }; 
}
int main() { return ns::S<int>::v; }  // Fails during parsing
```

Error: "Failed to parse top-level construct" at `int main()` line

## Investigation Findings

### What Works ✅
1. **Templates outside namespaces**: `template<typename T> struct S {}; S<int> x;`
2. **Templates in namespaces (no usage)**: `namespace ns { template<typename T> struct S {}; }`
3. **Non-template structs with qualified access**: `namespace ns { struct S {}; } ns::S x;`
4. **Templates with using declaration**: `namespace ns { template<typename T> struct S {}; } using ns::S; S<int> x;`

### What Fails ❌
**Templates in namespaces with qualified instantiation**: `namespace ns { template<typename T> struct S {}; } ns::S<int> x;`

## Root Cause Analysis

### Key Observations
1. Error occurs AFTER namespace parsing completes
2. Error occurs BEFORE usage line is parsed
3. The mere PRESENCE of qualified template usage in the file causes failure
4. Suggests token-level or preprocessing issue, not runtime parsing

### Attempted Fixes
1. **Dual Registration** (Partial): Register templates with both simple and qualified names
   - Location: Parser.cpp:17345-17370
   - Status: Implemented but insufficient

2. **Qualified Template Lookup** (Partial): Parse template arguments after `ns::Template`
   - Location: Parser.cpp:10889-10945
   - Status: Implemented but never reached due to earlier failure

### Hypotheses
1. **Token consumption issue**: Template parsing in namespace may consume extra tokens
2. **State corruption**: Parser state after namespace+template may be invalid
3. **Lookahead problem**: Tokenizer may be doing lookahead and getting confused by `<>`
4. **Namespace scope issue**: Template registration may interfere with namespace closure

## Recommended Next Steps

### Short-term (High Priority)
1. Add detailed logging to namespace parsing to track token consumption
2. Compare token positions before/after template parsing in namespace
3. Check if `ScopedTokenPosition` is being properly managed
4. Investigate if template deferred body parsing interferes with namespace

### Medium-term
1. Review how template registration interacts with namespace scope stack
2. Consider separating template registration from namespace parsing
3. Implement proper qualified name resolution for templates
4. Add comprehensive test suite for namespace+template combinations

### Long-term (Architectural)
1. Redesign template registry to be namespace-aware
2. Implement proper scoped template lookup
3. Consider two-phase template instantiation for better namespace handling

## Workaround
Use `using` declarations to bring templates into current scope:
```cpp
namespace ns { template<typename T> struct S {}; }
using ns::S;
int main() { S<int> x; }  // Works!
```

## Test Cases Created
- `/tmp/test_case_matrix.cpp` - Documents working vs failing cases
- `/tmp/minimal2.cpp` - Minimal reproduction (2 lines)
- `tests/test_namespace_template_instantiation_fail.cpp` - Original test case

## References
- MISSING_FEATURES.md - Priority 11
- Parser.cpp:5665-5868 - Namespace parsing
- Parser.cpp:17300-17378 - Template declaration parsing
- Parser.cpp:10860-11000 - Qualified identifier parsing

## Conclusion
This is a deep architectural issue requiring careful refactoring of how templates and namespaces interact. The dual registration and qualified lookup changes are necessary but not sufficient. The core issue appears to be in early-stage processing (tokenization or namespace state management) that requires more investigation to safely fix.
