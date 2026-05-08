namespace std {
template <typename T>
struct __is_integer {
	enum { __value = 0 };
};

template <>
struct __is_integer<int> {
	enum { __value = 1 };
};
}

namespace __gnu_cxx {
template <typename T, bool = std::__is_integer<T>::__value>
struct __promote {
	using __type = double;
};

template <typename T>
struct __promote<T, false> {
};
}

using promoted = __gnu_cxx::__promote<int>::__type;

int main() {
	return sizeof(promoted) == sizeof(double) ? 0 : 1;
}
