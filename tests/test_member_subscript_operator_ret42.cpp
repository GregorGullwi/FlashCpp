struct Buffer {
	int values[3] = {42, 7, 9};

	int& operator[](int index) {
		return values[index];
	}
};

int main() {
	Buffer buffer;
	return buffer[0];
}
