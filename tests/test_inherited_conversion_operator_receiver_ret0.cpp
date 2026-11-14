using Row = short[3];

struct Prefix {
	short guard[3];
};

template <typename T>
struct ArraySource {
	Row values;
	T marker;

	operator Row&() {
		return values;
	}

	operator T() const {
		return marker;
	}
};

struct Rows : Prefix, ArraySource<long long> {};

struct PrivateRows : Prefix, private ArraySource<long long> {
	using ArraySource<long long>::operator long long;

	void setMarker(long long value) {
		marker = value;
	}
};

long long readMarker(long long marker) {
	return marker;
}

int main() {
	Rows rows;
	rows.guard[1] = 7;
	rows.values[1] = 17;
	rows.marker = 0x123456789LL;

	rows[1] = 42;
	if (rows.values[1] != 42 || rows.guard[1] != 7) {
		return 1;
	}

	const Rows& view = rows;
	if (readMarker(view) != 0x123456789LL) {
		return 2;
	}

	PrivateRows private_rows;
	private_rows.setMarker(1234);
	const long long combined = readMarker(rows) + readMarker(view) + readMarker(private_rows);
	if (combined != 0x123456789LL * 2 + 1234) {
		return 3;
	}
	return 0;
}
