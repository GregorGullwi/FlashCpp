// Data-member template-id expansions must retain every bound type in order.
struct PackPayload { short first; long long second; };
template<class... T> struct TypeList { int marker; };
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
