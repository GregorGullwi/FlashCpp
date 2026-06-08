struct Counter {};

Counter& operator++(Counter& value) {
	return value;
}

Counter operator++(Counter& value, int) {
	return value;
}

int main() {
	return 0;
}
