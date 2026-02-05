// Test with smaller standard header <cstddef>
#include <cstddef>

int main() {
    std::size_t size = 3;
    std::ptrdiff_t diff = 10;
	
	std::size_t sum = 0;
	for (std::size_t i = 0; i < size; ++i) {
		sum += i;
	}
	
	if constexpr (__cplusplus < 201703L) return 1;
	
	return static_cast<int>(sum);
}
