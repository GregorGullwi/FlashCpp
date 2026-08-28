int runtime_value();

struct X {
	int value;
};

struct Broken {
	static constexpr X value = { runtime_value() };
};

int main() {
	return Broken::value.value;
}
