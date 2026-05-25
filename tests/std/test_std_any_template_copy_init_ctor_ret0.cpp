#include <any>

int source_value() {
	return 42;
}

int main() {
	std::any a = source_value();
	return std::any_cast<int>(a) == 42 ? 0 : 1;
}
