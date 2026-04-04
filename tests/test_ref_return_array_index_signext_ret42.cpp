struct Buffer {
	unsigned char index;
	int values[256];

	int& slot() {
		return values[index];
	}
};

int main() {
	Buffer buffer{};
	buffer.values[0] = 10;
	buffer.values[127] = 20;
	buffer.values[255] = 40;

	buffer.index = 0;
	buffer.slot() += 1;

	buffer.index = 127;
	buffer.slot() += 2;

	buffer.values[255] = 8;
	buffer.index = 255;
	buffer.slot() += 2;

	return buffer.values[0] + buffer.values[127] + buffer.values[255];
}
