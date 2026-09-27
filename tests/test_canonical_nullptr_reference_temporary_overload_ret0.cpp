struct NullptrMemberOwner {
	int value;
};

int objectPointerReference(int* const&) {
	return 10;
}

int objectPointerReference(int*&) {
	return 20;
}

int memberPointerReference(int NullptrMemberOwner::* const&) {
	return 30;
}

int memberPointerReference(int NullptrMemberOwner::*&) {
	return 40;
}

int main() {
	if (objectPointerReference(nullptr) != 10) {
		return 1;
	}
	if (memberPointerReference(nullptr) != 30) {
		return 2;
	}
	return 0;
}
