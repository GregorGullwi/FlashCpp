struct Pair {
	int first;
	int second;
};

struct Buffer {
	Pair values[2];

	Buffer() {
		values[0].first = 1;
		values[0].second = 2;
		values[1].first = 40;
		values[1].second = 2;
	}

	Pair& operator[](unsigned long long index) {
		return values[index];
	}
};

int main() {
	Buffer buffer;
	unsigned long long index = 1;
	return buffer[index].first + buffer[index].second;
}
