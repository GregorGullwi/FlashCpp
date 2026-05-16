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

- Dependent type-trait NTTP expressions can still collapse during evaluation/materialization.
  - Repro:
    ```cpp
    template <bool B>
    struct Flag {
        static constexpr int value = B ? 1 : 0;
    };

    template <typename T>
    struct TraitUse {
        static int first() { return Flag<__is_same(T, T)>::value; }
        static int second() { return Flag<__is_same(T, const T)>::value; }
    };

    int main() {
        return TraitUse<int>::first() != TraitUse<int>::second() ? 0 : 1;
    }
    ```
  - Notes: The dependent-expression identity layer now distinguishes the two expressions, but the later type-trait evaluation/materialization phase still canonicalizes them to the same result. The collapse happens during evaluation/materialization rather than identity tracking, so the remaining fix belongs in the evaluator/materializer rather than the dependent-expression identity layer.
