// SysV single-eightbyte aggregates that contain only float/double must return
// in XMM0, not the legacy integer RAX path used for INTEGER-class small structs.

struct FloatOnly {
	float value;
};

struct DoubleOnly {
	double value;
};

struct TwoFloats {
	float a;
	float b;
};

FloatOnly make_float_only(float value) {
	return FloatOnly{value};
}

DoubleOnly make_double_only(double value) {
	return DoubleOnly{value};
}

TwoFloats make_two_floats(float a, float b) {
	return TwoFloats{a, b};
}

float consume_float_only(FloatOnly value) {
	return value.value;
}

double consume_double_only(DoubleOnly value) {
	return value.value;
}

float consume_two_floats(TwoFloats value) {
	return value.a + value.b;
}

template <typename T>
T identity_template(T value) {
	return value;
}

int main() {
	FloatOnly made_float = make_float_only(1.5f);
	if (made_float.value != 1.5f) {
		return 1;
	}
	if (consume_float_only(FloatOnly{2.5f}) != 2.5f) {
		return 2;
	}
	if (identity_template(FloatOnly{3.5f}).value != 3.5f) {
		return 3;
	}

	DoubleOnly made_double = make_double_only(4.5);
	if (made_double.value != 4.5) {
		return 4;
	}
	if (consume_double_only(DoubleOnly{5.5}) != 5.5) {
		return 5;
	}
	if (identity_template(DoubleOnly{6.5}).value != 6.5) {
		return 6;
	}

	TwoFloats made_pair = make_two_floats(1.0f, 2.5f);
	if (made_pair.a != 1.0f || made_pair.b != 2.5f) {
		return 7;
	}
	if (consume_two_floats(TwoFloats{3.0f, 4.0f}) != 7.0f) {
		return 8;
	}

	return 0;
}
