struct LambdaConstThisOverload {
	int value;

	constexpr int pick() { return 1; }
	constexpr int pick() const { return value; }

	constexpr int run() const {
		auto lambda = [this]() {
			return this->pick();
		};
		return lambda();
	}
};

constexpr int testLambdaConstThis() {
	LambdaConstThisOverload value{60};
	return value.run();
}

static_assert(testLambdaConstThis() == 60);

int main() {
	if (testLambdaConstThis() != 60) return 1;
	return 0;
}
