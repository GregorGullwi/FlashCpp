// Two const operator[] overloads are both reachable through integral conversion
// from the int index, so the call is ambiguous. This exercises the compatible
// (const-receiver) candidate path, which the non-const recovered reproducer does
// not reach: there the ambiguity is already visible in the preferred set.

struct ConstAmbiguousSubscript {
	int operator[](long) const {
		return 1;
	}

	int operator[](unsigned) const {
		return 2;
	}
};

int main() {
	const ConstAmbiguousSubscript subscriptable{};
	return subscriptable[0];
}
