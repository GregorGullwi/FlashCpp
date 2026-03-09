constexpr int ref_init_capture_mutation_result() {
	int x = 40;
	auto f = [&y = x]() mutable {
		y += 2;
		return y;
	};
	return f() + x;
}

constexpr int ref_init_capture_repeated_calls_result() {
	int x = 40;
	auto f = [&y = x]() mutable {
		y += 2;
		return y;
	};
	return f() + f() + x;
}

static_assert(ref_init_capture_mutation_result() == 84);
static_assert(ref_init_capture_repeated_calls_result() == 130);

int main() {
	return 0;
}