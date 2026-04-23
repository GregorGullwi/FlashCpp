# Known Issues

## Phase 6: unsized array with pack-expanded initializer — array keeps pre-expansion size 1

Discovered 2026-04-23. Pack expansion `{args...}` in brace-initializer lists is now parsed and substituted correctly (see `tests/test_pack_expansion_brace_init_ret42.cpp`). However, an **unsized** array declared with a pack-expansion initializer keeps the pre-expansion size of 1 rather than the N of the expanded pack:

```cpp
template<typename... Ts>
int f(Ts... args) {
	int arr[] = {static_cast<int>(args)...};
	return (int)(sizeof(arr) / sizeof(arr[0])); // returns 1, expected sizeof...(args)
}
```

Root cause: `Parser::deduceArraySize` in `src/Parser_Statements.cpp` uses `init_list.initializers().size()` at parse time, which counts the single `PackExpansionExprNode`. The expanded element count is only known after template substitution, but the array type is fixed before substitution. Workaround: use an explicitly-sized array `int arr[3] = {args...}` which does work.

## Phase 6: aggregate brace-init of a struct with pack-expanded initializer — parser rejects the call site

Discovered 2026-04-23. `Triple t = {static_cast<int>(args)...};` where `Triple` is an aggregate struct fails to parse during instantiation with "Failed to parse initializer expression" at the call site. Array form with explicit size works; aggregate struct form does not. Root cause likely in a different brace-init path that does not yet route through the pack-expansion-aware substitution helper.

