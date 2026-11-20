// A namespace-scope constexpr initializer resolves unqualified names through
// its enclosing namespaces, including when the namespace is reopened later.

namespace outer {
constexpr int base = 5;
namespace inner {
constexpr int doubled = base * 2;
}
}

namespace reopened {
constexpr int a = 3;
}

namespace reopened {
constexpr int b = a + 4;
}

static_assert(outer::inner::doubled == 10, "enclosing namespace name is visible");
static_assert(reopened::b == 7, "reopened namespace name is visible");

int main() {
	return outer::inner::doubled + reopened::b;
}
