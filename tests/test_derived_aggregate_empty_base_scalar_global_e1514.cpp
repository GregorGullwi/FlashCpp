struct Empty {};
struct Derived : Empty { int value; };

Derived object{1};

int main() {
	return object.value;
}
