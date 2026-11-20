// Member-pointer access through an inaccessible base is rejected.

struct B {
	int b;
};

struct D : private B {};

int main() {
	int B::* pointer = &B::b;
	D object;
	return object.*pointer;
}
