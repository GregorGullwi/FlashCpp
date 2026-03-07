struct Widget {
	int value = 5;

	int run() {
		auto lambda = [this]() {
			return value;
		};
		return lambda();
	}
};

int main() {
	Widget widget;
	return widget.run();
}