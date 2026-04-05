template<typename T>
struct PtrIter {
	using difference_type = long;

	T* p;

	PtrIter operator+(difference_type n) const {
		return PtrIter{p + n};
	}

	PtrIter operator-(difference_type n) const {
		return PtrIter{p - n};
	}

	difference_type operator-(const PtrIter& other) const {
		return p - other.p;
	}

	bool operator==(const PtrIter& other) const {
		return p == other.p;
	}
};

template<typename It>
struct Rev {
	using difference_type = typename It::difference_type;

	It current;

	Rev operator+(difference_type n) const {
		return Rev{current - n};
	}

	difference_type operator-(const Rev& other) const {
		return other.current - current;
	}
};

int main() {
	int data[4] = {0, 0, 0, 0};
	Rev<PtrIter<int>> begin{{data + 4}};
	Rev<PtrIter<int>> end{{data + 1}};
	auto delta = end - begin;
	auto advanced = begin + delta;
	return advanced.current == end.current ? 0 : 1;
}
