// A decayed pointer past the array's valid range is not a constant expression.
// The addend path must keep the existing pointer-range diagnostic.

constexpr int values[2] = {7, 9};
constexpr const int* pointer = values + 3;

int main() {
	return *pointer;
}
