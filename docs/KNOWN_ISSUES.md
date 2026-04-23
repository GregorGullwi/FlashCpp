# Known Issues

## KI-001: Variadic-template pack call result not stored correctly when it is the first call in a function

**Affected area:** Code generation (IRConverter / ABI calling convention)

**Symptom:** When a variadic-template function whose parameter pack expands to struct arguments (e.g. `Pair<Ts,Us>... pairs`) is the very first function call in a scope, storing its return value into a local variable and then reading that variable produces a garbage value. Using the return value directly in an expression (e.g. `return f() - 20;`) works correctly.

**Minimal reproducer:**
```cpp
template<typename T, typename U>
struct Pair { T first; U second; Pair(T f, U s) : first(f), second(s) {} };

template<int N, typename... Ts, typename... Us>
int sum_with_offset(Pair<Ts, Us>... pairs) {
    return N + (0 + ... + (static_cast<int>(pairs.first) + static_cast<int>(pairs.second)));
}

int main() {
    Pair<int, double> p1(1, 2.0);
    Pair<char, int>   p2(3, 4);
    int r = sum_with_offset<10>(p1, p2);  // r is garbage, not 20
    return r != 20;                        // unexpectedly returns 1
}
```

**Workaround:** Ensure at least one other function call (even a no-argument one) precedes the pack call in the same scope. This changes the stack-frame layout in a way that avoids the corruption:
```cpp
int dummy = count_pairs();  // any prior call is sufficient
int r = sum_with_offset<10>(p1, p2);  // now r == 20 correctly
```

**Root cause (suspected):** Stack-frame layout or `rsp` alignment/adjustment is incorrect after setting up struct-valued arguments for the first pack call in a function. A prior call allocates and tears down a frame, leaving `rsp` in a state that avoids the problem.

**Status:** Open — workaround applied in affected tests.

