struct CallableReferenceOwner {
	int member() {
		return 7;
	}
};

int functionPointerReference(int (* const& pointer)()) {
	return pointer == nullptr ? 1 : 0;
}

int functionPointerReference(int (*& pointer)()) {
	return pointer == nullptr ? 2 : 0;
}

int functionPointerRvalueReference(int (* const&& pointer)()) {
	return pointer == nullptr ? 3 : 0;
}

int main() {
	if (functionPointerReference(nullptr) != 1) {
		return 1;
	}
	if (functionPointerRvalueReference(nullptr) != 3) {
		return 2;
	}
	int (CallableReferenceOwner::* const& member_pointer)() = nullptr;
	if (member_pointer != nullptr) {
		return 3;
	}
	return 0;
}
