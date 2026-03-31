// Phase 3 regression: static locals in function template instantiations
// must be unique per instantiated function, not shared across specializations.

template <typename T>
int next_value() {
	static int counter = 0;
	counter = counter + 1;
	return counter;
}

int main() {
	int first_int = next_value<int>();
	int first_char = next_value<char>();
	int second_int = next_value<int>();

	return (first_int == 1 && first_char == 1 && second_int == 2) ? 0 : 7;
}
