template<typename T>
struct Iter {
	T* ptr;

	decltype((T*)0 - (T*)0) operator-(const Iter& other) const;
};

int main() {
	using DiffType = decltype(Iter<int>{} - Iter<int>{});
	using BuiltinDiffType = decltype((int*)0 - (int*)0);
	return sizeof(DiffType) == sizeof(BuiltinDiffType) ? 0 : 1;
}
