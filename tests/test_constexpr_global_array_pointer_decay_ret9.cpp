// A decayed pointer to a global array is emitted as a relocation, so the
// stored pointer is a real address rather than the array value payload.

constexpr int values[2] = {7, 9};
constexpr const int* pointer = values;

static_assert(pointer != nullptr, "array decay produces a non-null pointer");

int main() {
	return pointer[1];
}
