struct ValueConversion {
	template<class T>
	operator T() const {
		return T{19};
	}
};

struct PointerConversion {
	template<class T>
	operator T*() const {
		return nullptr;
	}
};

struct ReferenceConversion {
	template<class T>
	operator T&&() const {
		static T value{31};
		return static_cast<T&&>(value);
	}
};

long read(const long& value) {
	return value;
}

int choose(const int&) {
	return 1;
}

int choose(int&& value) {
	return value;
}

void set(short&& value) {
	value = 23;
}

short read_short(const short& value) {
	return value;
}

bool read_pointer(const int* const& value) {
	return value == nullptr;
}

int main() {
	ValueConversion named;
	if (read(named) != 19 || choose(ValueConversion{}) != 19 || !read_pointer(PointerConversion{})) {
		return 1;
	}
	set(ReferenceConversion{});
	if (read_short(ReferenceConversion{}) != 31 || static_cast<short&&>(ReferenceConversion{}) != 23) {
		return 2;
	}
	return 0;
}
