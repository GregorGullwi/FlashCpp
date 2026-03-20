// Test: copy vs move constructor selection with unified helper.
// Validates that lvalue→copy selection works correctly.

struct Widget {
	int id;
	bool was_copied;
	Widget(int i) : id(i), was_copied(false) {}
	Widget(const Widget& other) : id(other.id), was_copied(true) {}
};

int main() {
	Widget a(10);
	Widget b(a);  // copy: was_copied == true, id == 10

	int result = 0;
	if (!b.was_copied) result += 1;  // should be 0 (was_copied is true)
	if (b.id != 10) result += 2;
	return result;  // 0
}
