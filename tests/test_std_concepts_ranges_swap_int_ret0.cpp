#include <concepts>

int main() {
	int left = 1;
	int right = 2;
	std::ranges::swap(left, right);
	return left == 2 && right == 1 ? 0 : 1;
}
