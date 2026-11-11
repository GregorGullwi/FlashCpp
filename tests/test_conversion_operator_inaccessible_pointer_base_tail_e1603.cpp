struct Base {};
class Derived : private Base {};

struct PointerSource {
	operator Derived*() const { return nullptr; }
};

int main() {
	Base* pointer = PointerSource{};
	return 0;
}
