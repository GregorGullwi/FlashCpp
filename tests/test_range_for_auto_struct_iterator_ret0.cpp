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
	Container c{{10, 20, 30}};
	int sum = 0;
	for (auto value : c) {
		sum += value;
	}
	return 60 - sum;
}
