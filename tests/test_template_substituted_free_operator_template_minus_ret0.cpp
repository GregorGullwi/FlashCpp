template<class I>
struct reverse_like {
	I current;

	constexpr I base() const {
		return current;
	}
};

template<class L, class R>
constexpr auto operator-(const reverse_like<L>& x, const reverse_like<R>& y)
	-> decltype(y.base() - x.base()) {
	return y.base() - x.base();
}

template<class T, class U>
int distance_like(reverse_like<T> first, reverse_like<U> last) {
	return first - last;
}

int main() {
	int values[3] = {};
	reverse_like<int*> first{values};
	reverse_like<int*> last{values + 1};
	int distance = distance_like(first, last);
	return distance - distance;
}
