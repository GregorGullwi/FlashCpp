// A global constexpr pointer must keep referring to its original array when a
// local variable with the same spelling shadows that array.

constexpr int values[3] = {11, 22, 33};
constexpr const int* pointer = values + 1;

int main() {
	constexpr int values[3] = {1, 2, 3};
	static_assert(*pointer == 22);
	static_assert(pointer[1] == 33);
	static_assert(1[pointer] == 33);
	static_assert(values[0] == 1);
	return 0;
}
