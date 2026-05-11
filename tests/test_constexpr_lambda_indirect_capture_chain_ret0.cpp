// Test that constexpr evaluation correctly handles indirect lambda capture chains
// where an inner lambda captures by reference a variable that is not directly
// accessible in the outer lambda's own binding scope.
//
// Previously this produced "Captured variable not found: x" or returned the
// wrong value (1 instead of 2) because by-reference writeback was broken:
// the first call spuriously inserted the captured variable into the outer
// lambda's bindings, causing the second call to miss the stored-writeback path.

// Direct call: baseline — simple by-ref capture called twice.
constexpr int simple_twice() {
    int x = 0;
    auto inc = [&x]() { x++; };
    inc();
    inc();
    return x;
}
static_assert(simple_twice() == 2);

// Indirect chain: inc captures x by ref; twice captures inc by ref.
// Calling twice() must increment x twice and the result must reach the
// enclosing frame's x.
constexpr int f() {
    int x = 0;
    auto inc = [&x]() { x++; };
    auto twice = [&inc]() { inc(); inc(); };
    twice();
    return x;
}
static_assert(f() == 2);

int main() { return 0; }
