struct ConstPointerMember {
	int* const pointer;
};

int main() {
	ConstPointerMember object;
	return object.pointer == nullptr;
}
