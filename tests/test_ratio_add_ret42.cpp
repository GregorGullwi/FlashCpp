#include <ratio>

int main() {
	using r1 = std::ratio<1, 2>;
	using r2 = std::ratio<2, 3>;
	using sum = std::ratio_add<r1, r2>;

	sum* ptr = nullptr;
	return ptr == nullptr ? 42 : 0;
}
