# Known Issues

- Dependent expression-form member template access with `::template` is still not parsed correctly in some contexts.
  - Repro:
    ```cpp
    struct RebindCarrier {
        template <typename T>
        struct Rebind {
            static constexpr int value = sizeof(T);
        };
    };

    template <typename T>
    struct UseExpr {
        static int value() {
            return T::template Rebind<int>::value;
        }
    };

    int main() {
        return UseExpr<RebindCarrier>::value() == (int)sizeof(int) ? 0 : 1;
    }
    ```
  - Notes: This currently fails during parsing with an error near the dependent `::template` member access in expression context. It is a parser bug separate from dependent NTTP identity/equivalence.
