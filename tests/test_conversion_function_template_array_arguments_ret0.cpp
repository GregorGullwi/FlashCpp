template<class T>
using Row = T[3];

struct ArrayConversion {
	template<class T>
	operator Row<T>&() const {
		static Row<T> values{7, 13, 17};
		return values;
	}
};

void set(short (&values)[3]) {
	values[1] = 23;
}

int read(short* values) {
	return values[1];
}

int main() {
	set(ArrayConversion{});
	if (read(ArrayConversion{}) != 23) {
		return 1;
	}
	return 0;
}
