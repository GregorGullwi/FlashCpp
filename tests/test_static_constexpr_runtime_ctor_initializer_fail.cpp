int runtime_value();

struct X {
	int value;
	X(int input) : value(input) {}
};

struct Broken {
	static constexpr X value = X(runtime_value());
};

int main() {
	return Broken::value.value;
}
