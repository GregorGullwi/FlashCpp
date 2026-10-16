template <int Offset>
struct Value {
	int value;

	Value(int input) : value(input + Offset) {
	}
};

template <int Offset>
struct Owner {
	using Alias = Value<Offset>;
};

int main() {
	const int parenthesized = Owner<1>::Alias(41).value;
	const int braced = Owner<2>::Alias{40}.value;
	return parenthesized == 42 && braced == 42 ? 0 : 1;
}
