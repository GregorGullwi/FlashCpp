# Known Issues

## Dependent `sizeof`/`alignof` non-type template arguments can lose size information

While investigating PR #1242 review feedback, a targeted regression using
`SizeValue<sizeof(T)>` inside a class template compiled but produced the wrong
runtime result, and `FlashCpp -v` logged:

> `sizeof returned 0, this indicates a bug in type size tracking`

The warning is emitted in `src/IrGenerator_MemberAccess.cpp:2091` when codegen
lowers `sizeof` and the tracked byte size is still zero. A current reproducer is
equivalent to:

```cpp
template <unsigned long long N>
struct SizeValue { using type = char[(int)N]; };

template <typename T>
struct Holder {
	using size_type = SizeValue<sizeof(T)>;
	static int get() { return (int)sizeof(typename size_type::type); }
};
```

`Holder<int>::get()` currently behaves as if `sizeof(T)` were zero during the
dependent instantiation path.

This appears adjacent to the Phase 1 template-argument identity work, but the
observed failure is deeper in dependent type-size propagation rather than in the
new hash/equality cleanup itself.
