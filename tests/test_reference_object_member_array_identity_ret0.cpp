// Array member subscripting through an object reference must preserve the
// object's address-based storage and use each member's byte offset.
struct Storage {
	int at_zero[3];
	int padding[4];
	short after_padding[3];
	int matrix[2][2];
	double values[2];
};

int main() {
	Storage storage{};
	Storage& object = storage;

	storage.at_zero[1] = 4;
	if (object.at_zero[1] != 4) return 1;
	object.at_zero[1] = 5;
	if (storage.at_zero[1] != 5) return 2;

	storage.after_padding[0] = 7;
	storage.after_padding[2] = 9;
	if (object.after_padding[0] != 7 || object.after_padding[2] != 9) return 3;
	object.after_padding[1] = 8;
	if (storage.after_padding[1] != 8) return 4;

	storage.matrix[1][1] = 12;
	if (object.matrix[1][1] != 12) return 5;
	object.matrix[0][1] = 13;
	if (storage.matrix[0][1] != 13) return 6;

	storage.values[1] = 2.5;
	if (object.values[1] != 2.5) return 7;
	object.values[0] = 3.5;
	if (storage.values[0] != 3.5) return 8;

	return 0;
}
