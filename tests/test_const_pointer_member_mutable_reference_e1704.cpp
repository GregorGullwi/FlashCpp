int take(int*& value) { return value == nullptr ? 0 : 1; }

struct Record {
	int* data;
};

int main() {
	const Record object{nullptr};
	return take(object.data);
}
