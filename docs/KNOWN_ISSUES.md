# Known Issues

## Phase 6: inner member function pack in class template context — fold returns only first pack element

Discovered 2026-04-23. When a class template with an outer pack parameter has an inner member function template with its own pack, a unary fold expression over the inner pack expands but only returns the first element at runtime.

Reproducer:

```cpp
template<typename... Ts>
struct Wrapper {
	int value;

	template<typename... Us>
	int call(Us... args) {
		return (0 + ... + args);
	}
};

int main() {
	Wrapper<int, double> w;
	w.value = 0;
	return w.call(10, 15, 17); // expected 42, actual 10
}
```

The test compiles successfully but returns `10` instead of `42`. This indicates the fold expansion count or the per-element substitution is broken for inner packs inside a class-template instantiation context. Root cause not yet diagnosed.

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

