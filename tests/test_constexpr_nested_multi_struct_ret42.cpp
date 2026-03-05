// Test Bug 2 fix: pos_idx in evaluate_nested_member_access must only advance for positional elements.
// Outer struct with three Inner members initialized positionally; all three must be accessible.

struct Inner {
    int val;
};

struct Outer {
    Inner a;
    Inner b;
    Inner c;
};

constexpr Outer o = {{10}, {12}, {20}};

constexpr int sum = o.a.val + o.b.val + o.c.val;  // 10 + 12 + 20 = 42

int main() {
    return sum;
}
