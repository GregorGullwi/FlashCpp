constexpr int copied_before_calls_keeps_independent_state() {
	auto f = [x = 40]() mutable {
		x += 2;
		return x;
	};
	auto g = f;
	return f() + g() + f() + g();
}

constexpr int copied_after_mutation_preserves_current_state() {
	auto f = [x = 40]() mutable {
		x += 2;
		return x;
	};
	int first = f();
	auto g = f;
	return first + f() + g();
}

static_assert(copied_before_calls_keeps_independent_state() == 172);
static_assert(copied_after_mutation_preserves_current_state() == 130);

int main() {
	return 0;
}