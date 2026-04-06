template <typename... Ts>
constexpr bool allPositive(Ts... values) {
	return ((values > 0) && ...);
}

int main() {
	return allPositive(1, 2, 3, 4) ? 0 : 1;
}
