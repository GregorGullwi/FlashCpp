// Test proper variable scoping in constexpr functions.
// Variables declared in inner scopes (for-init, block, if-init, range-for)
// must not leak into the outer scope.

// 1. For-loop init variable does not leak after the loop
constexpr int for_init_no_leak() {
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        sum += i;  // 0+1+2 = 3
    }
    // 'i' must not be visible here: declare a new 'i' that starts fresh.
    int i = 99;
    return sum + i;  // 3 + 99 = 102
}
static_assert(for_init_no_leak() == 102);

// 2. Two sequential for-loops with the same variable name
constexpr int two_loops_same_var() {
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        sum += i;  // 0+1+2 = 3
    }
    for (int i = 100; i < 103; i++) {  // fresh i starting at 100
        sum += i;  // 100+101+102 = 303
    }
    return sum;  // 3 + 303 = 306
}
static_assert(two_loops_same_var() == 306);

// 3. Block-local variable does not leak to outer scope
constexpr int block_no_leak() {
    int a = 1;
    {
        int b = 2;
        a = b + 1;  // a becomes 3
    }
    // 'b' must not be visible here; new b = 50
    int b = 50;
    return a + b;  // 3 + 50 = 53
}
static_assert(block_no_leak() == 53);

// 4. Inner block variable with same name as outer — outer value is restored
constexpr int shadow_restored() {
    int x = 1;
    {
        int x = 100;  // shadows outer x inside this block
        x = x + 1;   // inner x is now 101
    }
    // outer x should be 1 again after the block
    return x;
}
static_assert(shadow_restored() == 1);

// 5. Mutations to outer-scope variables from inside loops are preserved
constexpr int outer_mutation_preserved() {
    int total = 0;
    for (int i = 1; i <= 5; i++) {
        total += i;  // Mutating outer 'total' — must survive the loop
    }
    return total;  // 1+2+3+4+5 = 15
}
static_assert(outer_mutation_preserved() == 15);

// 6. Range-for loop variable does not leak
constexpr int range_for_no_leak() {
    int arr[] = {10, 20, 30};
    int sum = 0;
    for (int x : arr) {
        sum += x;
    }
    // 'x' must not be visible here; new x = 5
    int x = 5;
    return sum + x;  // 60 + 5 = 65
}
static_assert(range_for_no_leak() == 65);

int main() { return 0; }
