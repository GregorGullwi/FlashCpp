struct Buffer {
	unsigned char index;
	int values[256];

	int& slot() {
		return values[index];
	}
};

int main() {
	Buffer buffer{};
	buffer.index = 255;
	buffer.values[255] = 40;

	buffer.slot() += 2;

	return buffer.values[255];
}
