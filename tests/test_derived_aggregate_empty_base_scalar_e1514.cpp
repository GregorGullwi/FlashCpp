struct Empty {};
struct Derived : Empty { int value; };

int main() {
	Derived object{1};
	return object.value;
}
