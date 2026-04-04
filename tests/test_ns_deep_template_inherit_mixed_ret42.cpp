// Test: deep namespace template inheritance with mixed types
// Verifies namespace-aware member resolution with nested namespaces

namespace outer {
namespace inner {
template <typename T>
struct Base {
	T x;
	short y;
};
} // namespace inner
} // namespace outer

namespace other {
struct Derived : outer::inner::Base<int> {
	int get_sum() {
		return x + y;
	}
};
} // namespace other

int main() {
	other::Derived d;
	d.x = 40;
	d.y = 2;
	return d.get_sum();
}
