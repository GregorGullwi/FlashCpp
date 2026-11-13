struct PointerConversion {
	template<class T>
	operator T*() const {
		return nullptr;
	}
};

struct Receiver {
	int result;

	explicit Receiver(int value) : result(value) {}
	explicit Receiver(const short* value) : result(value == nullptr ? 7 : 1) {}

	int read(int value) const {
		return value;
	}

	int read(const int* value) const {
		return value == nullptr ? 13 : 2;
	}

	static int read_static(int value) {
		return value;
	}

	static int read_static(long* value) {
		return value == nullptr ? 17 : 3;
	}

	int operator()(int value) const {
		return value;
	}

	int operator()(short* value) const {
		return value == nullptr ? 23 : 4;
	}
};

namespace Calls {
	int read(int value) {
		return value;
	}

	int read(int* value) {
		return value == nullptr ? 29 : 5;
	}
}

int main() {
	PointerConversion source;
	Receiver direct(source);
	Receiver braced{PointerConversion{}};
	if (direct.result != 7 || braced.result != 7) {
		return 1;
	}
	if (direct.read(source) != 13 || Receiver::read_static(source) != 17 || direct(source) != 23 || Calls::read(source) != 29) {
		return 2;
	}
	return 0;
}
