struct IntegerConvertible {
	operator int() const {
		return 29;
	}
};

float global_value = 0.0f;

int main() {
	float value = 0.0f;
	value = IntegerConvertible{};
	global_value = IntegerConvertible{};
	return value == 29.0f && global_value == 29.0f ? 0 : 1;
}
