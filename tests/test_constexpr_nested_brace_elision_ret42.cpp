// Test Bug 3 fix: scalar inter_init path in evaluate_nested_member_access must check
// that final_member_name matches the first member of the inner struct (brace elision).
// Without the fix, any final_member_name would silently return the scalar value.

struct Inner {
	int val;
};

struct Outer {
	Inner inner;
	int extra;
};

// Brace elision: 20 initializes inner.val (scalar path), 22 initializes extra
constexpr Outer o = {20, 22};

constexpr int result = o.inner.val + o.extra;  // 20 + 22 = 42

int main() {
	return result;
}
