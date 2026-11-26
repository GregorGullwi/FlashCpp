struct Base { int first; int second; };
struct Derived : Base { int tail; };

Derived g{1, 2, 3, 4};

int main() {
	return g.tail;
}
