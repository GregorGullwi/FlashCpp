// Complete variable template test

template <typename T>
constexpr T pi = T(3.14159265);

template <typename T>
T max_val = T(100);

template <typename T>
constexpr T val = T(42);

int main() {
	// Basic instantiation
	float pi_f = pi<float>;
	double pi_d = pi<double>;

	int max_i = max_val<int>;
	int answer = val<int>;
	if (answer != 42)
		return 1;

	return 0;
}
