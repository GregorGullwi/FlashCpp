// Test parenthesized function-template declarations

// Declaration only (forward declaration with parenthesized name)
template <class T>
int (maxval)(T, T);

// Definition with parenthesized name
template <class T>
T (mymin)(T a, T b) {
	return a < b ? a : b;
}

// Reference return type variant (from test_std_algorithm_max_ret3)
template<typename T>
const T& (mymax)(const T& left, const T& right) {
	return left > right ? left : right;
}

// Non-template version
int (add)(int a, int b) {
	return a + b;
}

int main() {
	int r = mymin(40, 42);   // 40
	int s = add(1, 1);        // 2
	int t = mymax(1, 2);         // 2
	return r + s + t - 44;       // 40 + 2 + 2 - 44 = 0
}
