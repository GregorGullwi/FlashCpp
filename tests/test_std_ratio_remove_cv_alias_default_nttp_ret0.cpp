#include <ratio>
#include <type_traits>

int main() {
	using third = std::remove_cv_t<const std::ratio<1, 3>>;
	using half = std::remove_cv_t<const std::ratio<1, 2>>;
	static_assert(std::ratio_less<third, half>::value);
	return 0;
}
