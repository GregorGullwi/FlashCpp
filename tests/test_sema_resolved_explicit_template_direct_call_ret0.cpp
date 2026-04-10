template <typename T>
int addTwo(T value) {
	return static_cast<int>(value) + 2;
}

namespace math {
	template <typename T>
	int multiplyBySix(T value) {
		return static_cast<int>(value) * 6;
	}
}

int main() {
	if (addTwo<int>(40) != 42)
		return 1;
	if (math::multiplyBySix<int>(7) != 42)
		return 2;
	return 0;
}
