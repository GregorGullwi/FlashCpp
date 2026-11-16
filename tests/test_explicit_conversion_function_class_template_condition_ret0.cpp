// A conditional explicit specifier on a non-template conversion function must
// be evaluated with the enclosing class-template arguments.

template<class T>
struct ConditionalClassTemplateConversion {
	explicit(sizeof(T) > sizeof(int)) operator int() const {
		return sizeof(T) == sizeof(int) ? 1 : 2;
	}
};

int main() {
	ConditionalClassTemplateConversion<int> implicit_int;
	int value = implicit_int;
	if (value != 1) {
		return 1;
	}
	ConditionalClassTemplateConversion<long long> explicit_long_long;
	if (static_cast<int>(explicit_long_long) != 2) {
		return 2;
	}
	return 0;
}
