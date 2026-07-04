#include <cstddef>
#include <type_traits>

int main() {
	return std::is_nothrow_move_assignable_v<std::byte> ? 0 : 1;
}
