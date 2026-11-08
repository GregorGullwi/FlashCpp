// Regression: a callable alias whose return type is cv-qualified must keep
// that qualification on the return subtree, leaving the pointer object mutable.
using Callback = const int (*)(short);
using NestedCallback = Callback;

const int target(short value) {
	return value + 3;
}

const int direct_target(short value) {
	return value + 5;
}

int main() {
	Callback callback = &target;
	callback = &target;
	NestedCallback nested = &target;
	nested = &target;
	const int (*direct)(short) = &direct_target;
	direct = &direct_target;
	return callback(1) + nested(2) + direct(3) == 17 ? 0 : 1;
}
