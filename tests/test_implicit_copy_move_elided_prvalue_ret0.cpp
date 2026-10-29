// Guaranteed copy elision: copy-initialization from a prvalue of the same type
// initializes the destination directly. Semantic analysis must not publish an
// unnecessary copy/move constructor ODR use for this path.
struct Derived {
	int value;
};

int main() {
	Derived d = Derived{};
	d.value = 42;
	return d.value == 42 ? 0 : 1;
}
