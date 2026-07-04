#include <cstddef>

int main() {
	std::byte value = static_cast<std::byte>(1);
	std::byte shifted = value << 1;
	return static_cast<unsigned int>(shifted) == 2 ? 0 : 1;
}
