#include <cstddef>
#include <utility>

template <class T>
constexpr bool swap_probe_v = noexcept(std::swap(static_cast<T&>(*static_cast<T*>(0)), static_cast<T&>(*static_cast<T*>(0))));

int main() {
	return swap_probe_v<std::byte> ? 0 : 1;
}
