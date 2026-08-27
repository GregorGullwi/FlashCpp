// C++20 [over.ics.rank]: an integral promotion ranks above a conversion.

int select(int) {
	return 0;
}

int select(unsigned int) {
	return 1;
}

int main() {
	unsigned char value = 200;
	return select(value);
}
