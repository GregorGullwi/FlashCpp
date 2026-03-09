constexpr int named_ref_capture_result() {
	int x = 40;
	auto f = [&x]() mutable {
		x += 2;
		return x;
	};
	return f() + x;
}

constexpr int default_ref_capture_result() {
	int x = 40;
	auto f = [&]() mutable {
		x += 2;
		return x;
	};
	return f() + x;
}

static_assert(named_ref_capture_result() == 84);
static_assert(default_ref_capture_result() == 84);

int main() {
	return 0;
}