constexpr int mutable_by_value_state_persists() {
	int x = 40;
	auto f = [x]() mutable {
		x += 2;
		return x;
	};
	return f() + f();
}

constexpr int mutable_init_capture_state_persists() {
	int x = 40;
	auto f = [y = x]() mutable {
		y += 2;
		return y;
	};
	return f() + f();
}

static_assert(mutable_by_value_state_persists() == 86);
static_assert(mutable_init_capture_state_persists() == 86);

int main() {
	return 0;
}