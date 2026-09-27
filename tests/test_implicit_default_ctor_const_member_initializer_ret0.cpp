struct InitializedConstMember {
	const int value = 42;
};

struct PointerToConstMember {
	const int* pointer;
};

struct InitializedConstPointerMember {
	int* const pointer = nullptr;
};

int main() {
	InitializedConstMember initialized;
	PointerToConstMember pointer_to_const;
	InitializedConstPointerMember initialized_pointer;
	return initialized.value == 42 && initialized_pointer.pointer == nullptr ? 0 : 1;
}
