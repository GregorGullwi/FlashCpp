// Pointer const DOES matter - should error
struct Foo {
	void process(int* ptr);  // declaration: non-const pointer
};

void Foo::process(const int* ptr) {  // definition: const pointer - MISMATCH
}

int main() {
	return 0;
}
