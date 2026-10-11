// A by-value aggregate parameter passed through a function template returns via
// the caller's hidden return slot. The named-reference return path must copy the
// whole pointee aggregate into that slot; dereferencing only the first word
// corrupted the trailing fields (the documented reproducer returned 3). The same
// path returns a reference-to-aggregate by value, so it is covered too.
struct Payload {
	long long wide;
	double fraction;
	int small;
};

template <class T>
T identity(T value) {
	return value;
}

Payload forwardByReference(const Payload& value) {
	return value;
}

int exercise(int input) {
	Payload result = identity(Payload{0x123456789LL + input, 2.5, input});
	long long wide = identity(result.wide) + 7;
	Payload forwarded = forwardByReference(result);
	return wide == 0x123456789LL + input + 7 &&
			forwarded.wide == result.wide &&
			forwarded.small == result.small
		? 0
		: 3;
}

int main() {
	return exercise(2);
}
