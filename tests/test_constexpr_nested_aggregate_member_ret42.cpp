// Test constexpr nested struct member access when struct is aggregate-initialized
// Verifies that o.inner.val works when o is initialized as constexpr Outer o = {{20}, 22}

struct Inner {
int val;
};

struct Outer {
Inner inner;
int extra;
};

constexpr Outer o = {{20}, 22};

constexpr int result = o.inner.val + o.extra;  // 20 + 22 = 42

int main() {
return result;
}
