int runtime_value() {
	return 3;
}

struct Broken {
	static constexpr int value = runtime_value();
};

int main() {
	return Broken::value;
}
