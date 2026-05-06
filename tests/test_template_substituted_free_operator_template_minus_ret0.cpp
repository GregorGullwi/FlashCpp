template<class I>
struct ReverseLike {
	I current;

	constexpr I base() const {
		return current;
	}
};

template<class L, class R>
constexpr auto operator-(const ReverseLike<L>& x, const ReverseLike<R>& y)
	-> decltype(y.base() - x.base()) {
	return y.base() - x.base();
}

template<class T, class U>
auto distanceLike(ReverseLike<T> first, ReverseLike<U> last) {
	return first - last;
}

int main() {
	int values[2] = {};
	ReverseLike<int*> first{values};
	ReverseLike<int*> last{values + 1};
	int distance = static_cast<int>(distanceLike(first, last));

	short values2[3] = {};
	ReverseLike<short*> first2{values2};
	ReverseLike<short*> last2{values2 + 1};
	int distance2 = static_cast<int>(distanceLike(first2, last2));

	return distance + distance2 - 2;
}
