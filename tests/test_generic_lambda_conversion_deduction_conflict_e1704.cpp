// The explicit lambda template parameter T is deduced from both target
// parameter positions. int and double are a deduction conflict, so this
// conversion is ill-formed and the call has no viable candidate.

static int apply(int (*function)(int, double), int left, double right) {
	return function(left, right);
}

int main() {
	auto add = []<class T>(T left, T right) {
		return left + right;
	};
	return apply(add, 1, 2.0);
}
