// Phase 3 regression: static locals in non-template member functions of
// class templates must be unique per instantiated class.

template <typename T>
struct Box {
	int next() {
		static int counter = 0;
		counter = counter + 1;
		return counter;
	}
};

int main() {
	Box<int> ints;
	Box<char> chars;

	int first_int = ints.next();
	int first_char = chars.next();
	int second_int = ints.next();

	return (first_int == 1 && first_char == 1 && second_int == 2) ? 0 : 8;
}
