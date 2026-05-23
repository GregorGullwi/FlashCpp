// Deferred template-base replay must preserve definition-context lookup for
// non-dependent template arguments.

constexpr int pick(long) { return 11; }

template<typename T, int V>
struct BaseN {
	static constexpr int value = V;
};

template<typename T>
struct Derived : BaseN<T, pick(0)> {};

constexpr int pick(int) { return 22; }

int main() {
	return Derived<int>::value == 11 ? 0 : 1;
}
