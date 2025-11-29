// Reference const DOES matter - should error  
struct Foo {
	void process(int& ref);  // declaration: non-const reference
};

void Foo::process(const int& ref) {  // definition: const reference - MISMATCH
}

int main() {
	return 0;
}
