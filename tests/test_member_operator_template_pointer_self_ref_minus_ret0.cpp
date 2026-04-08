template<typename T>
struct Iter {
	T* ptr;

	long operator-(const Iter& other) const;
};

int main() {
	using DiffType = decltype(Iter<int>{} - Iter<int>{});
	using BuiltinDiffType = decltype((int*)0 - (int*)0);
	return sizeof(DiffType) == sizeof(BuiltinDiffType) ? 0 : 1;
}
