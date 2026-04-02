struct Buffer {
	int values[3] = {42, 7, 9};

	int& operator[](unsigned long long index) {
		return values[index];
	}
};

int main() {
	Buffer buffer;
	unsigned long long index = 0;
	return buffer[index];
}
