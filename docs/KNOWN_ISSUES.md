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

- Silently returning 0 when `static constexpr` member initializer evaluation fails is not standards-compliant.
  - C++ requires that `static constexpr` initializer evaluation either succeeds or is a hard compile error.
  - FlashCpp currently zero-initializes the member and continues compilation when the constexpr evaluator cannot resolve the expression.
  - Repro: any `static constexpr` member whose initializer references a type that the constexpr evaluator fails to resolve will silently yield 0 instead of producing a diagnostic.
