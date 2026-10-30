// Data-member template-id expansions must retain every bound type in order.
struct PackPayload { short first; long long second; };
template<class... T> struct TypeList {
	int marker;
	int bytes() const { return (0 + ... + sizeof(T)); }
	int count() const { return sizeof...(T); }
};
template<class T> struct PackOwner {
	template<class... U> struct Box {};
	template<class Head, class... Tail> struct Box<Head, Tail...> {
		TypeList<T, Head, Tail...> values;
		TypeList<const Tail*...> pointers;
	};
};
int main() {
	PackOwner<short>::Box<int> empty{};
	PackOwner<short>::Box<int, char> single{};
	PackOwner<long long>::Box<int, char, double> multiple{};
	PackOwner<short>::Box<int, char, double> same_arguments{};
	PackOwner<short>::Box<int, PackPayload> structured{};
	if (empty.values.bytes() != sizeof(short) + sizeof(int) || empty.pointers.bytes() != 0) return 1;
	if (single.values.bytes() != sizeof(short) + sizeof(int) + sizeof(char)) return 2;
	if (multiple.values.bytes() != sizeof(long long) + sizeof(int) + sizeof(char) + sizeof(double)) return 3;
	if (same_arguments.values.bytes() != sizeof(short) + sizeof(int) + sizeof(char) + sizeof(double)) return 4;
	if (structured.values.bytes() != sizeof(short) + sizeof(int) + sizeof(PackPayload)) return 5;
	if (single.pointers.bytes() != sizeof(const char*) || multiple.pointers.bytes() != sizeof(const char*) + sizeof(const double*)) return 6;
	if (structured.pointers.bytes() != sizeof(const PackPayload*)) return 7;
	if (empty.pointers.count() != 0 || single.pointers.count() != 1 || multiple.pointers.count() != 2) return 8;
	if (empty.values.count() != 2 || single.values.count() != 3 || multiple.values.count() != 4 || structured.values.count() != 3) return 9;
	structured.values.marker = 8;
	structured.pointers.marker = 9;
	empty.pointers.marker = 4;
	single.pointers.marker = 5;
	multiple.pointers.marker = 6;
	same_arguments.values.marker = 7;
	empty.values.marker = 1;
	single.values.marker = 2;
	multiple.values.marker = 3;
	return empty.values.marker + single.values.marker + multiple.values.marker + empty.pointers.marker + single.pointers.marker
		+ multiple.pointers.marker + same_arguments.values.marker + structured.values.marker + structured.pointers.marker - 45;
}
