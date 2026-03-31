struct FloatTarget {
	float value;

	FloatTarget(const double& d) : value(static_cast<float>(d)) {}
};

int main() {
	FloatTarget positive = 3.14;
	if (!(positive.value > 3.0f && positive.value < 3.2f))
		return 1;

	FloatTarget negative = -2.25;
	if (!(negative.value < -2.0f && negative.value > -2.5f))
		return 2;

	return 0;
}
