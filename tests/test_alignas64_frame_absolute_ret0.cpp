// Over-aligned locals must be aligned in the absolute address, not only as an
// RBP-relative offset. Varying call depth changes the incoming frame address,
// so RBP-modulo padding alone is not enough.

static const void* constructed_at = nullptr;

struct alignas(64) AlignedRecord {
	int value;
	int tag;
};

struct alignas(64) AlignedResult {
	int value;
	int tag;

	AlignedResult(int value, int tag) : value(value), tag(tag) {
		constructed_at = this;
	}
};

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

static AlignedResult make_result(int value, int tag) {
	return AlignedResult(value, tag);
}

static int check_overaligned_scalar(int depth) {
	alignas(64) int scalar = depth + 11;
	if ((reinterpret_cast<unsigned long long>(&scalar) % 64) != 0) {
		return 10;
	}
	if (scalar != depth + 11) {
		return 13;
	}
	if (depth > 0) {
		const int nested = check_overaligned_scalar(depth - 1);
		if (nested != 0) {
			return nested;
		}
	}
	return 0;
}

static int check_overaligned_array(int depth) {
	AlignedRecord elements[2];
	elements[0].value = depth;
	elements[0].tag = 1;
	elements[1].value = depth + 1;
	elements[1].tag = 2;
	if ((reinterpret_cast<unsigned long long>(&elements[0]) % 64) != 0 ||
		(reinterpret_cast<unsigned long long>(&elements[1]) % 64) != 0) {
		return 11;
	}
	if (elements[0].value != depth || elements[1].tag != 2) {
		return 14;
	}
	if (depth > 0) {
		const int nested = check_overaligned_array(depth - 1);
		if (nested != 0) {
			return nested;
		}
	}
	return 0;
}

static int check_overaligned_hidden_return(int depth) {
	AlignedResult hidden = make_result(depth + 3, 9);
	if (constructed_at != &hidden || (reinterpret_cast<unsigned long long>(constructed_at) % 64) != 0) {
		return 12;
	}
	if (hidden.value != depth + 3 || hidden.tag != 9) {
		return 15;
	}
	if (depth > 0) {
		const int nested = check_overaligned_hidden_return(depth - 1);
		if (nested != 0) {
			return nested;
		}
	}
	return 0;
}

static int unwind_through_overaligned_frame(int depth) {
	alignas(64) int scalar = depth + 3;
	Guard guard(&live_guards);
	if ((reinterpret_cast<unsigned long long>(&scalar) % 64) != 0) {
		throw 20;
	}
	if (depth > 0) {
		return unwind_through_overaligned_frame(depth - 1) + scalar;
	}
	throw scalar;
}

int main() {
	const int scalar_status = check_overaligned_scalar(5);
	if (scalar_status != 0) {
		return scalar_status;
	}
	const int array_status = check_overaligned_array(5);
	if (array_status != 0) {
		return array_status;
	}
	const int hidden_status = check_overaligned_hidden_return(5);
	if (hidden_status != 0) {
		return hidden_status;
	}

	int caught = 0;
	try {
		unwind_through_overaligned_frame(4);
	} catch (int value) {
		caught = value;
	}
	if (caught != 3 || live_guards != 0) {
		return 30;
	}
	return 0;
}
