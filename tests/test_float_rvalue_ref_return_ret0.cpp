float&& forwardFloat(float& value) {
	return static_cast<float&&>(value);
}

int main() {
	float value = 3.14f;
	float result = forwardFloat(value);
	return result > 3.0f && result < 3.3f ? 0 : 1;
}
