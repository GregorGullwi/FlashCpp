struct DerefBase {
	int* ptr;

	int& operator*() {
		return *ptr;
	}
};

struct Iter : DerefBase {
	Iter& operator++() {
		++ptr;
		return *this;
	}

	bool operator!=(Iter other) const {
		return ptr != other.ptr;
	}
};

int main() {
	int values[1] = {42};
	Iter it;
	it.ptr = values;
	int x = *it;
	return x - 42;
}
