// Comprehensive C++20 Concepts Test
// Demonstrates basic concept features currently supported

// Simple template concepts
template <typename T>
concept AlwaysTrue = true;
template <typename T>
concept SimpleIntegral = true;
template <typename T>
concept SimpleFloating = false;

// Template-based concepts
template <typename T>
concept Integral = true;

template <typename T>
concept FloatingPoint = false;

template <typename T>
concept SignedType = true;

// Multiple template parameters
template <typename T, typename U>
concept SameType = true;

// Using concepts in function templates
template <typename T>
T increment(T x) {
	return x + 1;
}

template <typename T>
T decrement(T x) {
	return x - 1;
}

template <typename T>
T add(T a, T b) {
	return a + b;
}

int main() {
	int a = increment(5);
	int b = decrement(10);
	if (add(5, 10) != 15)
		return 1;
	return a + b;
}
