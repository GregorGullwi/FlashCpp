// Regression: if constexpr should evaluate conditions using the active local
// scope, not only global constexpr bindings.

constexpr bool flag = false;

int select_value() {
	constexpr bool flag = true;
	if constexpr (flag) {
		return 1;
	}
	return 2;
}

int main() {
	return select_value() - 1;
}
