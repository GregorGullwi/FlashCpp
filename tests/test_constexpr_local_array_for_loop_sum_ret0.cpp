constexpr int local_array_for_loop_sum_result() {
	int arr[] = {20, 22};
	int sum = 0;
	for (int i = 0; i < 2; ++i) {
		sum += arr[i];
	}
	return sum;
}

static_assert(local_array_for_loop_sum_result() == 42);

int main() {
	return 0;
}