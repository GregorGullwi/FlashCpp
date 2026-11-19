static const void* constructed_at = nullptr;

struct alignas(16) AlignedResult {
	double value;
	long long count;
	short tag;

	AlignedResult(double value, long long count, short tag) : value(value), count(count), tag(tag) {
		constructed_at = this;
	}
};

using ResultAlias = AlignedResult;

struct alignas(16) AlignedElement {
	float value;
};

static AlignedResult make_result(double value, long long count, short tag) {
	return AlignedResult(value, count, tag);
}

int main() {
	// An eight-byte local before the destination forces the frame to pad it.
	long long prefix = 17;
	AlignedResult result = make_result(42.5, prefix, 3);
	if (result.value != 42.5 || result.count != prefix || result.tag != 3) {
		return 1;
	}
	if (constructed_at != &result) {
		return 2;
	}
	if (reinterpret_cast<unsigned long long>(constructed_at) % alignof(AlignedResult) != 0) {
		return 3;
	}
	// Align the beginning of storage even when the object is smaller than its requested alignment.
	alignas(16) int scalar = 11;
	char suffix = 7;
	AlignedElement elements[2] = {{1.5f}, {2.5f}};
	ResultAlias second = make_result(21.5, scalar, suffix);
	if (reinterpret_cast<unsigned long long>(&scalar) % 16 != 0) {
		return 4;
	}
	if (reinterpret_cast<unsigned long long>(&elements[0]) % alignof(AlignedElement) != 0 ||
		reinterpret_cast<unsigned long long>(&elements[1]) % alignof(AlignedElement) != 0) {
		return 5;
	}
	if (elements[0].value != 1.5f || elements[1].value != 2.5f) {
		return 6;
	}
	if (constructed_at != &second || reinterpret_cast<unsigned long long>(constructed_at) % alignof(ResultAlias) != 0) {
		return 7;
	}
	if (scalar != 11 || suffix != 7 || second.value != 21.5 || second.count != scalar || second.tag != suffix || prefix != 17) {
		return 8;
	}
	return 0;
}
