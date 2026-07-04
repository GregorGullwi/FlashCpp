#include <cstddef>
#include <utility>

int main() {
	std::byte left = static_cast<std::byte>(1);
	std::byte right = static_cast<std::byte>(2);
	std::swap(left, right);
	return static_cast<unsigned int>(left) == 2 &&
		static_cast<unsigned int>(right) == 1 ? 0 : 1;
}
