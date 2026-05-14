# Known Issues

- Temporary aggregate initialization with floating-point members can produce incorrect runtime values in some cases.
  - Repro:
    ```cpp
    struct S {
        int x;
        float y;
    };

    int main() {
        S s = S{2, 40.0f};
        return static_cast<int>(s.y); // expected 40, currently observed 0
    }
    ```
  - Notes: This appears to be an aggregate materialization/codegen issue and is not specific to tuple-like structured bindings.

- Dependent non-type template argument expressions can collapse during instantiation identity/materialization.
  - Repro:
    ```cpp
    template <int N>
    struct Box {
        static constexpr int value = N;
    };

    template <typename T>
    struct UseDependentExprs {
        static int first() { return Box<sizeof(T)>::value; }
        static int second() { return Box<sizeof(T) + 1>::value; }
    };

    int main() {
        int first = UseDependentExprs<int>::first();
        int second = UseDependentExprs<int>::second();
        return first == (int)sizeof(int) && second == (int)sizeof(int) + 1 && first != second ? 0 : 1;
    }
    ```
  - Notes: This is separate from preserving `is_dependent` for type template arguments. The remaining gap is that unresolved dependent NTTP expressions such as `sizeof(T)` and `sizeof(T) + 1` need expression-aware identity/materialization so they do not reuse the same `Box<N>` instantiation or evaluate to the same value.
