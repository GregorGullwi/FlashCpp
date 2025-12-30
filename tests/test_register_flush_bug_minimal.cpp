// Minimal reproduction of the register flush bug
// This test combines structured bindings, compound assignment, and conditional branches
// to trigger the bug where the wrong register is tested in the conditional branch.
//
// Expected: Returns 42
// Actual (with bug): Returns 1 due to wrong register being tested

struct Point { int x; int y; };

template<typename T, bool v>
struct integral_constant { static constexpr bool value = v; };

template<typename T, typename U>
struct is_same : integral_constant<bool, false> {};

template<typename T>
struct is_same<T, T> : integral_constant<bool, true> {};

int main() {
    int result = 0;
    result += 10;  // Compound assignment
    
    Point p = {20, 12};
    auto [x, y] = p;  // Structured bindings
    result += x + y;
    
    // BUG: is_same<int,int>::value should be true, but the generated code
    // tests RAX (which has a stale value) instead of R8 (which has the
    // correct logical NOT result). This causes the wrong branch to execute.
    if (!is_same<int, int>::value) {
        return 1;  // Bug: this executes when it shouldn't
    }
    
    return result;  // Should return 42
}
