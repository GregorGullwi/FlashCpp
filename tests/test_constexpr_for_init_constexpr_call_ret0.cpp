constexpr int runtime_value() { return 1; }

constexpr int for_init_constexpr_call() {
	int sum = 0;
	for (int i = runtime_value(); i < 3; i++) {
		sum += i;
	}
	return sum;
}

static_assert(for_init_constexpr_call() == 3);

int main() {
	return for_init_constexpr_call() == 3 ? 0 : 1;
}
