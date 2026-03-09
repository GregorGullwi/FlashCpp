constexpr int explicit_local_capture() {
	constexpr int x = 40;
	auto f = [x]() {
		return x + 2;
	};
	return f();
}

constexpr int default_local_capture_by_value() {
	constexpr int x = 40;
	auto f = [=]() {
		return x + 2;
	};
	return f();
}

constexpr int default_local_capture_by_reference() {
	constexpr int x = 40;
	auto f = [&]() {
		return x + 2;
	};
	return f();
}

constexpr int init_capture_from_local() {
	constexpr int x = 40;
	auto f = [y = x + 2]() {
		return y;
	};
	return f();
}

constexpr int immediate_local_capture() {
	constexpr int x = 40;
	return [x]() {
		return x + 2;
	}();
}

static_assert(explicit_local_capture() == 42);
static_assert(default_local_capture_by_value() == 42);
static_assert(default_local_capture_by_reference() == 42);
static_assert(init_capture_from_local() == 42);
static_assert(immediate_local_capture() == 42);

int main() {
	return 0;
}