struct IntegerConvertible {
	operator int() const {
		return 17;
	}
};

int choose(float value) {
	return value == 17.0f ? 0 : 1;
}

long choose(...) {
	return 2;
}

int main() {
	float converted = IntegerConvertible{};
	if (converted != 17.0f) {
		return 1;
	}
	if (!IntegerConvertible{}) {
		return 2;
	}
	return choose(IntegerConvertible{});
}
