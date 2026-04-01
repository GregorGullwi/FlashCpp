// Hardening test: multiple instantiations of nested type constructor via make()
// Ensures that type remapping works correctly for different template arguments.
template <int N>
struct Box {
	struct Item {
		int val = N;
	};

	Item create() {
		return Item{};
	}
};

int main() {
	Box<10> b10;
	Box<32> b32;
	Box<10>::Item i10 = b10.create();
	Box<32>::Item i32 = b32.create();

	if (i10.val != 10) return 1;
	if (i32.val != 32) return 2;

	return i10.val + i32.val;  // 10 + 32 = 42
}
