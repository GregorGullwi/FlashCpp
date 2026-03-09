int main() {
	auto broken = [missing_value]() {
		return 0;
	};

	return broken();
}