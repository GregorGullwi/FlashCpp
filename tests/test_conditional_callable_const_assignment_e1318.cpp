using Callback = int (*)(int);

int target(int value) {
	return value;
}

int main() {
	Callback mutable_callback = &target;
	const Callback constant_callback = &target;
	(true ? mutable_callback : constant_callback) = &target;
	return 0;
}
