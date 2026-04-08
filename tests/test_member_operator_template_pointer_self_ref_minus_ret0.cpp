template<typename T>
struct Iter {
	T* ptr;

	long operator-(const Iter& other) const;
};

int main() {
	using DiffType = decltype(Iter<int>{} - Iter<int>{});
	return sizeof(DiffType) == sizeof(long) ? 0 : 1;
}
