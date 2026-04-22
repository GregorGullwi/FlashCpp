// Test: mixed explicit + deduced template arguments
//
// Covers three scenarios from Phase 6 explicit-deduction audit:
// 1. repeated_add<int, 4>(10): T=int explicit, N=4 explicit (NTTP), value deduced from call → 40
// 2. convert_and_add<int>(1, 1): Ret=int explicit, T=int deduced from call args → 2
// 3. get_default<int>(0): T=int explicit override of a defaulted type param → 0
//
// Sum: 40 + 2 + 0 = 42

template <typename T, int N>
T repeated_add(T val) {
	T result = T{};
	for (int i = 0; i < N; ++i) result = result + val;
	return result;
}

template <typename Ret, typename T>
Ret convert_and_add(T a, T b) {
	return static_cast<Ret>(a + b);
}

template <typename T = int>
T get_default(T v) { return v; }

int main() {
	int x = repeated_add<int, 4>(10);   // T=int explicit, N=4 explicit: 10*4 = 40
	int y = convert_and_add<int>(1, 1); // Ret=int explicit, T=int deduced:  1+1 = 2
	int z = get_default<int>(0);        // T=int explicit override of default: 0
	return x + y + z;                   // 40 + 2 + 0 = 42
}
