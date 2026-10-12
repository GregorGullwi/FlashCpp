// An exact-match operator[] must win over widened and const overloads without
// being reported as ambiguous. Before the ambiguity owner existed, sema silently
// dropped the resolution for a tie; this guard ensures the fix does not swing
// the other way and reject a unique exact match.

struct ExactMatchSubscript {
	int data[4];

	int operator[](int index) {
		return data[index];
	}

	int operator[](unsigned index) {
		return data[index] + 100;
	}

	int operator[](short index) const {
		return data[index] + 200;
	}
};

int main() {
	ExactMatchSubscript subscriptable;
	subscriptable.data[0] = 10;
	subscriptable.data[1] = 32;
	int literal = 1;
	return subscriptable[0] + subscriptable[literal]; // 10 + 32 = 42
}
