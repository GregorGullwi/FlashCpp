// Regression test: constexpr lambda capture chain with 3+ hops.
// A 3-level deep by-reference capture chain (thrice -> twice -> inc -> x)
// must correctly propagate mutations all the way back to the outermost scope.

constexpr int three_hop() {
	int x = 0;
	auto inc = [&x]() { x++; };
	auto twice = [&inc]() { inc(); inc(); };
	auto thrice = [&twice]() { twice(); twice(); twice(); };
	thrice();
	return x;  // inc called 2*3=6 times, x should be 6
}

static_assert(three_hop() == 6);

// Also test that intermediate lambdas are updated correctly
constexpr int two_hop_counter() {
	int count = 0;
	auto add = [&count](int n) { count += n; };
	auto run = [&add]() { add(3); add(5); };
	run();
	return count;  // 3+5 = 8
}

static_assert(two_hop_counter() == 8);

int main() { return 0; }
