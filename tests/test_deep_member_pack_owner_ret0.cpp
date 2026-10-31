template<int N> struct Tag { long long value; };
template<class T> struct Owner { template<class... U> struct List { T payload; int value; }; };
template<class T> struct Holder {
	template<class... U> struct Box {};
	template<class Head, class... Tail> struct Box<Head, Tail...> { typename Owner<T>::template List<Head, Tail...> field; };
};
template<int N> struct Chain { Chain<N-1> previous; typename Holder<Tag<N>>::template Box<int, char> item; };
template<> struct Chain<0> { int value; };
int main() {
	Chain<24> c{};
	if (sizeof(c) < 24 * sizeof(Holder<Tag<24>>::Box<int, char>)) return 1;
	c.item.field.value = 7;
	return c.item.field.value - 7;
}
