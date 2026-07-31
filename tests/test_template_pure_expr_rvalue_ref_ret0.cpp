template<typename T>
T&& forwardValue(T& value) {
	static_assert(sizeof(T) > 0, "T must be complete");
	return static_cast<T&&>(value);
}

int main() {
	float value = 3.14f;
	float result = forwardValue<float>(value);
	return result > 3.0f && result < 3.3f ? 0 : 1;
}
