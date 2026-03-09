int immediate_local_capture_runtime() {
	int x = 40;
	return [x]() {
		return x + 2;
	}();
}

int main() {
	return immediate_local_capture_runtime() == 42 ? 0 : 1;
}