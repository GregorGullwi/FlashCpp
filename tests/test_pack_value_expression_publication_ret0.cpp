struct PackPayload { long long first; long long second; };
template<class T> struct CheckedPackValue {
	static_assert(sizeof(T) > 0);
	T value;
};
template<int... N> struct PublishedPackValues {
	int count() const { return sizeof...(N); }
	int total() const { return (0 + ... + N); }
};
template<class... T> struct PackValueHolder {
	PublishedPackValues<static_cast<int>(sizeof(CheckedPackValue<T>))...> values;
	PublishedPackValues<7, static_cast<int>(sizeof(CheckedPackValue<T>))...> with_prefix;
};
int main() {
	PackValueHolder<short, PackPayload, double> full{};
	PackValueHolder<> empty{};
	PackValueHolder<const int*> single{};
	if (full.values.count() != 3) return 1;
	if (full.values.total() != sizeof(short) + sizeof(PackPayload) + sizeof(double)) return 2;
	if (empty.values.count() != 0 || empty.values.total() != 0) return 3;
	if (single.values.count() != 1 || single.values.total() != sizeof(const int*)) return 4;
	if (full.with_prefix.count() != 4 || full.with_prefix.total() != full.values.total() + 7) return 5;
	if (empty.with_prefix.count() != 1 || empty.with_prefix.total() != 7) return 6;
	return 0;
}
