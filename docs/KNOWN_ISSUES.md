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
