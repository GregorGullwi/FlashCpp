// Two namespaces each provide an overload of `add`.
// `using namespace` brings both into scope.
// The call should resolve to the best match from the merged overload set.
namespace A {
	int add(int a, int b) { return a + b; }
}
namespace B {
	int add(double a, double b) { return static_cast<int>(a + b); }
}

using namespace A;
using namespace B;

int main() {
	// Should call A::add(int,int) -- best match for int args from merged set
	return add(0, 0);  // returns 0
}
