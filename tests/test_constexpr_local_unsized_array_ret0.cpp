constexpr int local_unsized_array_result() {
	int arr[] = {40, 2};
	return arr[0] + arr[1];
}

static_assert(local_unsized_array_result() == 42);

int main() {
	return 0;
}