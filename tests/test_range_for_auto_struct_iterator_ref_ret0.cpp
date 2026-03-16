struct IntIter {
	int* ptr;

	int& operator*();
	IntIter& operator++();
	bool operator!=(IntIter other);
};

int& IntIter::operator*() {
	return *ptr;
}

IntIter& IntIter::operator++() {
	++ptr;
	return *this;
}

bool IntIter::operator!=(IntIter other) {
	return ptr != other.ptr;
}

struct Container {
	int data[3];

	IntIter begin();
	IntIter end();
};

IntIter Container::begin() {
	IntIter it{&data[0]};
	return it;
}

IntIter Container::end() {
	IntIter it{&data[3]};
	return it;
}

int main() {
	Container c{{1, 2, 3}};
	for (auto& value : c) {
		value += 10;
	}

	return (c.data[0] == 11 && c.data[1] == 12 && c.data[2] == 13) ? 0 : 1;
}
