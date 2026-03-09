constexpr auto makeCounterByValue() {
	return [x = 40]() mutable {
		x += 2;
		return x;
	};
}

constexpr auto makeCounterFromLocal() {
	int x = 40;
	return [x]() mutable {
		x += 2;
		return x;
	};
}

constexpr int returned_closure_state_result() {
	auto f = makeCounterByValue();
	auto g = makeCounterFromLocal();
	return f() + f() + g() + g();
}

static_assert(returned_closure_state_result() == 172);

int main() {
	return 0;
}