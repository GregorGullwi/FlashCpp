#include <ratio>
#include <type_traits>

int main() {
	using third = std::remove_cv_t<const std::ratio<1, 3>>;
	using half = std::remove_cv_t<const std::ratio<1, 2>>;
	using less_type = std::ratio_less<third, half>;
	(void)sizeof(less_type);
	return 0;
}
