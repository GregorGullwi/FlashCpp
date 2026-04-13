constexpr int ptr_ref_init_capture_mutation_result() {
	int x = 40;
	int* p = &x;
	auto f = [&y = *p]() mutable {
		y += 2;
		return y;
	};
	return f() + x;
}

constexpr int ptr_ref_init_capture_repeated_calls_result() {
	int x = 40;
	int* p = &x;
	auto f = [&y = *p]() mutable {
		y += 2;
		return y;
	};
	return f() + f() + x;
}

static_assert(ptr_ref_init_capture_mutation_result() == 84);
static_assert(ptr_ref_init_capture_repeated_calls_result() == 130);

int main() {
	return 0;
}
