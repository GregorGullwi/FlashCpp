#include <ratio>

static_assert(std::ratio_equal<std::ratio<1, 2>, std::ratio<2, 4>>::value);

int main() {
	return 0;
}
