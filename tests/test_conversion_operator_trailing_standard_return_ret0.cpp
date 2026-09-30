struct IntegerConvertible {
	operator int() const {
		return 23;
	}
};

float make_value() {
	return IntegerConvertible{};
}

int main() {
	return make_value() == 23.0f ? 0 : 1;
}
