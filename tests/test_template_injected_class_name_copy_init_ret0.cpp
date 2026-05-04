namespace std {
template <typename T>
struct ReverseLike {
	using iterator_type = T;

	explicit ReverseLike(iterator_type) {}
	ReverseLike(const ReverseLike&) {}

	ReverseLike(int, iterator_type) {
		ReverseLike copy = *this;
	}
};
}

int main() {
	std::ReverseLike<int> first(0, 0);
	return 0;
}
