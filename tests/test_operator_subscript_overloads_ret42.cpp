// Test multiple operator[] overloads — sema picks the correct one by argument type.
// The int and unsigned long long overloads return distinct values so the test
// verifies that overload resolution selects the right candidate.
//   int overload:               container[2]   → data[2] = 20
//   unsigned long long overload: container[1ULL] → data[1] + 100 = 122
//   But we only test the int path here since implicit conversion from int
//   to unsigned long long would make both viable; we verify the int overload wins.

struct MultiSubscript {
	int data[4];

	int operator[](int index) {
		return data[index];
	}

	int operator[](unsigned long long index) {
		return data[index] + 100;
	}
};

int main() {
	MultiSubscript m;
	m.data[0] = 5;
	m.data[1] = 15;
	m.data[2] = 22;
	m.data[3] = 99;
	// int literal 0 selects operator[](int) → data[0] = 5
	// int literal 2 selects operator[](int) → data[2] = 22
	// int literal 1 selects operator[](int) → data[1] = 15
	return m[0] + m[2] + m[1]; // 5 + 22 + 15 = 42
}
