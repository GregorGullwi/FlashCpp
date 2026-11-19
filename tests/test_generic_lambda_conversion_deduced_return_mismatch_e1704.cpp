// After the invented template parameter is deduced as int, the call operator
// returns double. A function pointer returning int would require a return-type
// conversion that [expr.prim.lambda.closure]/8 does not allow, so the call has
// no viable candidate.

static int apply(int (*function)(int), int value) {
	return function(value);
}

int main() {
	auto scale = [](auto value) {
		return value * 0.5;
	};
	return apply(scale, 2);
}
