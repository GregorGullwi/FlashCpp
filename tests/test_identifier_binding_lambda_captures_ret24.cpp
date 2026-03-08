int test_explicit_capture_bindings() {
	int x = 5;
	int y = 2;
	auto lambda = [x, &y]() mutable {
		x += 3;
		y = x;
		return x + y;
	};
	int result = lambda();
	return result + y;
}

int main() {
	return test_explicit_capture_bindings();
}