// Phase D test: default member initializer using sizeof(T) instead of NTTP.
// The nested struct's default member initializer must evaluate sizeof(T) using
// the enclosing template's instantiation context.
template <typename T>
struct Outer {
	struct Inner {
		int size = static_cast<int>(sizeof(T));
	};
};

int main() {
	Outer<int>::Inner obj_int;         // sizeof(int) = 4
	Outer<double>::Inner obj_double;   // sizeof(double) = 8
	Outer<char>::Inner obj_char;       // sizeof(char) = 1

	if (obj_int.size != 4) return 1;
	if (obj_double.size != 8) return 2;
	if (obj_char.size != 1) return 3;

	// 4 + 8 + 1 + 29 = 42
	return obj_int.size + obj_double.size + obj_char.size + 29;
}
