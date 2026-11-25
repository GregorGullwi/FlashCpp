// A static-member array origin now carries its extent, so decayed pointer
// arithmetic past the member's storage is rejected with the existing
// pointer-range diagnostic instead of producing an out-of-range addend.

struct Table {
	static constexpr int values[2] = {7, 9};
};

constexpr const int* pointer = Table::values + 3;

int main() {
	return *pointer;
}
