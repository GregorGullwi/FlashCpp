# Known Issues

## KI-001 · Qualified data-member access (`ClassName::member`) emits a global relocation

**Severity:** High — link error / undefined reference  
**Area:** IR generation (`IrGenerator` / codegen)  
**Discovered:** 2026-04-24

### Description

When a qualified name of the form `ClassName::data_member` appears in the body of a member
function, the compiler emits a relocation to a *global* symbol named `data_member` instead
of generating an offset-based load from `this`.  The object file is produced without
diagnostics, but the linker then fails with "undefined reference to `data_member`".

The bug affects:

* plain (non-template) classes — `Foo::x` inside `Foo::someMethod()`
* template classes — `Base<N>::x` inside `Derived<N>::someMethod()`
* any inheritance depth, including the derived-class itself referencing its own data member

Qualified *member function calls* (`Foo::method()`, `Base<N>::method()`) are **not**
affected.

### Minimal reproducer

```cpp
struct Foo {
    int x = 5;
    int f() const { return Foo::x; }   // should be this->x; generates undefined symbol
};
int main() {
    Foo f; f.x = 42;
    return f.f() - 42;   // link error: undefined reference to 'x'
}
```

### Workaround

Use an unqualified access (`return x;`) or cast (`return static_cast<const Foo*>(this)->x;`)
instead of the qualified form.  In template bases, `this->Base<N>::x` may also work via the
`this->` path depending on whether the look-up route is affected.

---

## KI-002 · Qualified template-class method call on the *current* instantiation (`Foo<T>::method()`) errors with "Symbol not found"

**Severity:** Medium — compilation error  
**Area:** IR generation / symbol look-up  
**Discovered:** 2026-04-24

### Description

When a *template* member function calls another member function of the same class using the
fully-qualified instantiated name (`Foo<T>::method()`), the compiler emits:

```
[ERROR][Codegen] Symbol 'Foo' not found in symbol table during code generation
```

and the call is dropped from the output.

The root cause is that the codegen identifier look-up for the qualifier `Foo` tries to find
`Foo` as a symbol in the local/global scope rather than recognising it as an alias for the
enclosing struct type.

Calling the method unqualified (`method()`) or through an explicit `this->` dereference
(`this->method()`) works correctly.  The bug only manifests when the *template-id* form is
used for the current class (e.g., `Foo<T>::method()` inside a function of `Foo<T>`).

### Minimal reproducer

```cpp
template<typename T>
struct Foo {
    T x{};
    T getX() const { return x; }
    T f() const { return Foo<T>::getX(); }  // [ERROR] Symbol 'Foo' not found
};
int main() {
    Foo<int> f; f.x = 42;
    return f.f() - 42;   // crashes at runtime (call is missing)
}
```

### Workaround

Replace `Foo<T>::method()` with an unqualified call (`method()`) or `this->method()`.

