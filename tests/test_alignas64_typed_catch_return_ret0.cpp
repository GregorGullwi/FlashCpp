// Typed catch objects and catch-return continuations must agree with the CRT
// about the pre-and frame top when the parent realigns RBP.

struct Guard {
	int* counter;
	explicit Guard(int* counter) : counter(counter) {
		++(*counter);
	}
	~Guard() {
		--(*counter);
	}
};

static int live_guards = 0;

static int throw_typed_through_overaligned(int depth) {
	alignas(64) int local = depth + 7;
	Guard guard(&live_guards);
	if ((reinterpret_cast<unsigned long long>(&local) % 64) != 0) {
		throw 40;
	}
	if (depth > 0) {
		return throw_typed_through_overaligned(depth - 1) + local;
	}
	throw local;
}

static int catch_typed_with_overaligned_frame(int expected) {
	alignas(64) int marker = 9;
	int caught = 0;
	try {
		throw_typed_through_overaligned(2);
	} catch (int value) {
		if ((reinterpret_cast<unsigned long long>(&marker) % 64) != 0) {
			return 30;
		}
		caught = value;
	}
	if (caught != expected) {
		return 31;
	}
	if (live_guards != 0) {
		return 32;
	}
	return 0;
}

static int catch_typed_and_return(int expected) {
	alignas(64) int marker = 9;
	try {
		throw expected;
	} catch (int value) {
		if ((reinterpret_cast<unsigned long long>(&marker) % 64) != 0) {
			return 33;
		}
		if (value != expected) {
			return 34;
		}
		return value;
	}
	return 35;
}

int main() {
	const int fallthrough = catch_typed_with_overaligned_frame(7);
	if (fallthrough != 0) {
		return fallthrough;
	}
	const int caught = catch_typed_and_return(7);
	if (caught != 7) {
		return caught == 0 ? 36 : caught;
	}
	return 0;
}
