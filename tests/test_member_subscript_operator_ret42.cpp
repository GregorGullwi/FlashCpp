struct Buffer {
	int values[3];

	Buffer()
		: values{42, 7, 9} {}

	int operator[](int index) const {
		return values[index];
	}
};

int main() {
	Buffer buffer;
	return buffer[0];
}
