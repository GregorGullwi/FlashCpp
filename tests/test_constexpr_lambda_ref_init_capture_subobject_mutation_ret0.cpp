struct S {
	int value;
};

constexpr int member_ref_init_capture_mutation_result() {
	S s{40};
	auto f = [&x = s.value]() mutable {
		x += 2;
		return x;
	};
	return f() + s.value;
}

constexpr int array_ref_init_capture_mutation_result() {
	int arr[3] = {1, 2, 3};
	auto f = [&x = arr[1]]() mutable {
		x += 5;
		return x;
	};
	return f() + arr[1];
}

static_assert(member_ref_init_capture_mutation_result() == 84);
static_assert(array_ref_init_capture_mutation_result() == 14);

int main() {
	return 0;
}
