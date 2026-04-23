# Known Issues

## Phase 6: static member function template — qualified call leaves body unmaterialized

Discovered 2026-04-23. When a class template has a static member function template (e.g. `template<typename... Ts> struct S { template<typename... Us> static int f(Us...); };`) and the static member is called as `S<T1,T2>::f(args...)`, the body is not materialized and link fails with an undefined reference to the mangled member symbol.

Reproducer:

```cpp
template<typename... Ts>
struct MultiStore {
	template<typename... Us>
	static int sum_inner(Us... args) {
		return (0 + ... + args);
	}
};

int main() {
	return MultiStore<int, double>::sum_inner(14, 14, 14) - 42;
}
```

Compile succeeds, link fails: `undefined reference to 'MultiStore$<hash>::sum_inner'`. Likely cause: the member function template's body is not queued for codegen when called through a qualified static member access on an instantiated class template.

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

