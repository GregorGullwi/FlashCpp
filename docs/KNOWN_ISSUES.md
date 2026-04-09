# Known Issues

## Template instance default member initializers on objects

Template class instantiations can materialize the correct specialization for
lookup, but object initialization still drops instantiated non-static default
member initializers in some cases.

Minimal repro:

```cpp
template <typename T, typename U = int>
struct box {
	int value = sizeof(U) == 4 ? 42 : 0;
};

int main() {
	box<char> tmp;
	return tmp.value; // currently returns 0, expected 42
}
```

The same specialization resolves correctly through static members such as
`box<char>::value`, so this appears to live in template object initialization /
default-member-initializer handling rather than template-owner materialization.
