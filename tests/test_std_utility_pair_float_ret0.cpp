#include <utility>

int main() {
	std::pair<int, float> value(42, 3.14f);
	return value.first == 42 ? 0 : 1;
}
