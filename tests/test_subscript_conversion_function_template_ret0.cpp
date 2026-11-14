using Row = short[3];
using Grid = short[2][3];

struct ArraySource {
	short data[2][3];

	template<class Tag = void>
	operator Grid&() & {
		return data;
	}
};

template<class T>
concept Wide = sizeof(T) == 4;

struct PointerSource {
	int data[2];

	template<class T = int>
	operator const int*() const requires Wide<T> {
		return data;
	}

	template<class T = short>
	operator const short*() const requires Wide<T> {
		return nullptr;
	}
};

struct Mixed {
	int data[1];

	operator const int*() const {
		return data;
	}

	template<class Tag = void>
	operator const int*() const = delete;
};

struct Prefix {
	short guard[3];
};

struct Level1 : ArraySource {};
struct Level2 : Level1 {};
struct Level3 : Level2 {};
struct Level4 : Level3 {};
struct Level5 : Level4 {};
struct Level6 : Level5 {};
struct Level7 : Level6 {};
struct Level8 : Level7 {};
struct Level9 : Level8 {};
struct Derived : Prefix, Level9 {};

int main() {
	Derived source;
	source.guard[1] = 11;
	source.data[0][2] = 7;
	source[1][2] = 42;
	if (sizeof(source[1]) != sizeof(Row) || source.guard[1] != 11 || source.data[1][2] != 42 || 0[source][2] != 7) {
		return 1;
	}

	const PointerSource pointers{{13, 23}};
	if (pointers[1] != 23) {
		return 2;
	}

	const Mixed mixed{{31}};
	return mixed[0] == 31 ? 0 : 3;
}
