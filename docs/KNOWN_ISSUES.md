# Known Issues

## Deleted constructor not enforced in template specializations

**Test:** `tests/test_template_spec_deleted_ctor_fail.cpp`

FlashCpp does not reject calls to deleted constructors in template specializations. For example:

```cpp
template<typename T>
struct Foo<T*> {
    Foo() = delete;
    int value = 42;
};

Foo<int*> f{}; // Should be a compile error, but FlashCpp accepts it
```

Clang/GCC correctly reject this with: `error: call to deleted constructor of 'Foo<int *>'`.
