// Test that constructor parameters in partial specializations get template
// parameter substitution.  The pattern-based instantiation path copies
// constructor parameters; if it forgets to substitute T -> concrete type
// the constructor will have the wrong parameter type and codegen may fail
// or produce incorrect results.

// Primary template
template<typename T, typename U>
struct Pair {
	T first;
	U second;
	Pair(T a, U b) : first(a), second(b) {}
};

// Partial specialization for pointer first type.
// Constructor takes T (deduced from T*) and U — both must be substituted.
// Using two differently-sized types (char=1byte, int=4bytes) exposes
// register/stack sizing bugs when parameter types aren't substituted.
template<typename T, typename U>
struct Pair<T*, U> {
	T deref_first;
	U second;
	Pair(T a, U b) : deref_first(a), second(b) {}
	T get_first() { return deref_first; }
	U get_second() { return second; }
};

int main() {
	// Primary template: Pair<int, char>
	Pair<int, char> p1(10, 'X');
	if (p1.first != 10) return 1;
	if (p1.second != 'X') return 2;

	// Partial specialization: Pair<int*, char> matches Pair<T*, U> with T=int, U=char
	// Constructor should take (int, char), not (T, U) unsubstituted
	Pair<int*, char> p2(42, 'Y');
	if (p2.get_first() != 42) return 3;
	if (p2.get_second() != 'Y') return 4;

	// Partial specialization with different sizes: Pair<char*, int> with T=char, U=int
	// char is 1 byte, int is 4 bytes — wrong parameter types would corrupt values
	Pair<char*, int> p3('A', 99);
	if (p3.get_first() != 'A') return 5;
	if (p3.get_second() != 99) return 6;

	// Partial specialization: Pair<long long*, short> with T=long long, U=short
	// 8-byte vs 2-byte params — maximizes chance of size mismatch detection
	Pair<long long*, short> p4(123456789LL, 7);
	if (p4.get_first() != 123456789LL) return 7;
	if (p4.get_second() != 7) return 8;

	return 0;
}
