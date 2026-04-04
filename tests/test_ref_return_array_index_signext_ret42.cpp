struct Buffer {
	unsigned char index;
	int values[256];

	int& slot() {
		return values[index];
	}
};

int main() {
	Buffer buffer{};
	buffer.values[0] = 1;
	buffer.values[127] = 20;
	buffer.values[255] = 11;

	buffer.index = 0;
	buffer.slot() = 10;
	if (buffer.values[0] != 10) {
		return 1;
	}

	buffer.index = 127;
	buffer.slot() += 1;
	if (buffer.values[127] != 21) {
		return 2;
	}

	buffer.index = 255;
	if (buffer.slot() != 11) {
		return 3;
	}

	return buffer.values[0] + buffer.values[127] + buffer.values[255];
}
