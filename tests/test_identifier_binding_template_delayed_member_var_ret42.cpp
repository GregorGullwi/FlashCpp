// Phase 3B regression: delayed parsing inside a class template member body
// should still bind later members correctly after instantiation.

template<typename T>
struct Box {
	int get() {
		return value;
	}

	int value = 42;
};

int main() {
	Box<int> box;
	return box.get();
}

