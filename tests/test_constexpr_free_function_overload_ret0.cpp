constexpr int pick(int value) {
	return value + 1;
}

constexpr double pick(double value) {
	return value + 0.5;
}

constexpr double global_result = pick(2.5);
static_assert(global_result == 3.0);

constexpr double call_local() {
	return pick(2.5);
}

static_assert(call_local() == 3.0);

int main() {
	if (global_result != 3.0)
		return 1;
	if (call_local() != 3.0)
		return 2;
	if (pick(2.5) != 3.0)
		return 3;
	return 0;
}
