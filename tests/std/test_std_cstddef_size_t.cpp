// Verify that <cstddef> exposes size_t and that __cplusplus is honored in #if

#include <cstddef>
int main() {
	std::size_t n = 3;
	std::size_t sum = 0;
	for (std::size_t i = 0; i < n; ++i) {
		sum += i;
	}
	if constexpr (__cplusplus < 201703L) return 1;
	return static_cast<int>(sum);
}
