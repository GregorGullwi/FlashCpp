template <bool B, class T>
struct enable_if {};

template <class T>
struct enable_if<true, T> {
	using type = T;
};

template <bool B, class T>
using enable_if_t = typename enable_if<B, T>::type;

template <class T, enable_if_t<true, int> = 0>
void redeclared_default(T& left, T& right);

template <class T, enable_if_t<true, int>>
void redeclared_default(T& left, T& right) {
	T tmp = left;
	left = right;
	right = tmp;
}

int main() {
	int left = 1;
	int right = 2;
	redeclared_default(left, right);
	return left == 2 && right == 1 ? 0 : 1;
}
