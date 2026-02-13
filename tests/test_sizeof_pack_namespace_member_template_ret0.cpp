// Regression: sizeof...(Pack) in a class template member template must honor the
// enclosing class namespace during lookup (no hardcoded std:: fallback).

namespace custom_ns {
template<typename... Elements>
struct Holder {
	static constexpr int size() {
		return static_cast<int>(sizeof...(Elements));
	}
};
}

int main() {
	int size = custom_ns::Holder<int, float, double>::size();
	return size == 3 ? 0 : 1;
}
