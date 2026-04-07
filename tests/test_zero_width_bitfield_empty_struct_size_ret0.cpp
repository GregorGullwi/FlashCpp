struct EmptyWithZeroWidthBitfield {
	int : 0;
};

int main() {
	return sizeof(EmptyWithZeroWidthBitfield) == 1 &&
		   alignof(EmptyWithZeroWidthBitfield) == 1 ? 0 : 1;
}
