namespace sequence {
	struct Sentinel { short tag; long long* last; };
	struct Iterator {
		long long* current;
		long long operator*() const { return *current; }
		Iterator& operator++() { ++current; return *this; }
	};
	bool operator==(const Iterator& iterator, const Sentinel& sentinel) {
		return iterator.current == sentinel.last;
	}
	struct Range {
		long long* data;
		int count;
		Iterator begin() { return Iterator{data}; }
		Sentinel end() { return Sentinel{7, data + count}; }
	};
}
int main() {
	long long values[3] = {11, 14, 17};
	sequence::Range range{values, 3};
	long long sum = 0;
	for (auto value : range) sum += value;
	return sum == 42 ? 0 : 1;
}
