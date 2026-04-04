// Test: cross-namespace template inheritance member resolution
// ns1::Base used from ns2::Derived — verifies that member resolution
// finds inherited members through the namespace-aware path.

namespace ns1 {
template <typename T>
struct Base {
	T value;
};
} // namespace ns1

namespace ns2 {
template <typename T>
struct Derived : ns1::Base<T> {
	T get_value() {
		return this->value;
	}
};
} // namespace ns2

int main() {
	ns2::Derived<int> d;
	d.value = 42;
	return d.get_value();
}
