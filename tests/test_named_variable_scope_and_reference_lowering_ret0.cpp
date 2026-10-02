// Named stack variables must keep their own frame slot through the whole
// declaration path: mixed native types and sizes, arrays, aggregates,
// reference locals, and a local whose slot is reused by a later declaration.
// Any variable that loses its recorded slot, size, or indirect-storage
// classification changes the running total.
//
// Each stage checks the running total and returns a distinct code on failure,
// so a wrong read is identified by the exit status alone.
//
// Shadowed locals are covered separately by
// test_shadowed_local_frame_identity_ret0.cpp.

struct Pair {
	int first;
	short second;
	char third;
	double fourth;
};

int accumulate(int& target, int value) {
	target += value;
	return target;
}

int widen(int& slot, int value) {
	slot = value * 2;
	return slot;
}

int main() {
	// Mixed widths: the load size of each named local decides how much of the
	// slot is read, so a wrong size shows up in the running total.
	char tiny = 5;
	short small_value = 300;
	int medium = 40000;
	long wide = 500000;
	float single = 1.5f;
	double twin = 2.25;
	bool flag = true;

	// Array locals decay to a pointer, and their element type is narrower than
	// the slot stride.
	int numbers[4] = {1, 2, 3, 4};
	char letters[3] = {'a', 'b', 'c'};

	Pair pair = {};
	pair.first = 7;
	pair.second = 8;
	pair.third = 9;
	pair.fourth = 10.5;

	// Reference locals hold an address: reading one must load the pointed-to
	// value rather than the pointer itself.
	int counter = 0;
	int& counter_ref = counter;
	double scale = 3.0;
	double& scale_ref = scale;

	int total = tiny + small_value + medium + static_cast<int>(wide) +
				static_cast<int>(single) + static_cast<int>(twin) + (flag ? 1 : 0) +
				numbers[0] + numbers[3] + letters[0] + letters[2] +
				pair.first + pair.second + pair.third + static_cast<int>(pair.fourth);
	if (total != 540544) return 11;

	accumulate(counter_ref, total);
	accumulate(counter_ref, numbers[1]);
	total = counter;
	if (total != 540546) return 12;

	widen(counter_ref, 21);
	total += counter;
	if (total != 540588) return 13;

	total += static_cast<int>(scale_ref * 2.0);
	if (total != 540594) return 14;

	// Inner-scope locals must not disturb the enclosing frame slots.
	{
		int inner_value = 2000;
		double inner_scale = 0.5;
		total += inner_value + static_cast<int>(inner_scale * 4.0);
	}
	if (total != 542596) return 15;

	// A later declaration that lands on a reused slot must not inherit the
	// reference classification of the variable that previously lived there.
	{
		int reused = 77;
		total += reused;
	}
	if (total != 542673) return 16;

	return 0;
}