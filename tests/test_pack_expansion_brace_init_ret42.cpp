// Test: pack expansion in brace initializer list for a fixed-size array
// Tests that {args...} is parsed and expanded correctly at template instantiation.
template<typename... Ts>
int sum_arr3(Ts... args) {
	int arr[3] = {static_cast<int>(args)...};
	return arr[0] + arr[1] + arr[2];
}

int main() {
	int r = sum_arr3(10, 15, 17); // = 42
	return r;
}
