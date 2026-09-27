int pointerReference(int* const&) {
	return 27;
}

int pointerReference(int*&) {
	return 37;
}

int pointerRvalueReference(int*&&) {
	return 58;
}

int pointerRvalueReference(int*&) {
	return 68;
}

int constPointerReference(const int* const&) {
	return 48;
}

int charPointerReference(const char* const&) {
	return 68;
}

struct RecordElement {
	int value;
};

int recordPointerReference(RecordElement* const&) {
	return 78;
}

int convertedRvalueReference(double&&) {
	return 88;
}

int main() {
	int values[3] = {7, 8, 9};
	char characters[2] = {'a', 'b'};
	RecordElement records[2];
	int scalar = 5;
	if (pointerReference(values) != 27) {
		return 1;
	}
	if (constPointerReference(values) != 48) {
		return 2;
	}
	if (pointerRvalueReference(values) != 58) {
		return 3;
	}
	if (charPointerReference(characters) != 68) {
		return 4;
	}
	if (recordPointerReference(records) != 78) {
		return 5;
	}
	if (convertedRvalueReference(scalar) != 88) {
		return 6;
	}
	return 0;
}
