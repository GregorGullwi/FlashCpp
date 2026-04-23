// Test: fold expressions with member access
struct Val {
	int x;
};

template<typename... Ts>
int sum_x(Ts... args) {
	return (0 + ... + args.x);
}

int main() {
	Val a{10}, b{15}, c{17};
	return sum_x(a, b, c); // = 42
}
