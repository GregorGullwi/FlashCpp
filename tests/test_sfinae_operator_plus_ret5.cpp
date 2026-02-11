// Test: SFINAE with operator+ in decltype
// decltype(a + b) should succeed for types with operator+ and fail for types without
struct HasPlus { int val; };
HasPlus operator+(HasPlus a, HasPlus b) { HasPlus r; r.val = a.val + b.val; return r; }

struct NoPlus { int val; };

template<typename T>
auto can_add(T a, T b) -> decltype(a + b, true) { return true; }

template<typename T>
auto can_add(...) -> bool { return false; }

int main() {
	HasPlus hp; hp.val = 1;
	NoPlus np; np.val = 1;
	bool a = can_add<HasPlus>(hp, hp);
	bool b = can_add<NoPlus>(np, np);
	return (a && !b) ? 5 : 0;
}
