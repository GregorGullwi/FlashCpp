constexpr int local_array_bytes() {
	int arr[] = {1, 2, 3, 4};
	return static_cast<int>(sizeof(arr));
}

constexpr int local_array_count() {
	int arr[] = {7, 8, 9};
	return static_cast<int>(sizeof(arr) / sizeof(arr[0]));
}

static_assert(local_array_bytes() == 16);
static_assert(local_array_count() == 3);

int main() {
	return (local_array_bytes() == 16 && local_array_count() == 3) ? 0 : 1;
}
