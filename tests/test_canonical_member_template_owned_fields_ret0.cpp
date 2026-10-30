// A data member whose target is a class-template-owned member specialization
// must retain the instantiated owner, not just the member primary.
template <class T>
struct Owner {
	template <class... U>
	struct List {
		int bytes() const { return 0; }
		int owner_size() const { return sizeof(T); }
	};

	template <class Head, class... Tail>
	struct List<Head, Tail...> {
		int bytes() const { return sizeof(Head) + (0 + ... + sizeof(Tail)); }
		int owner_size() const { return sizeof(T); }
	};
};

struct ShortBox {
	typename Owner<short>::template List<int, char> values;
};

struct LongBox {
	typename Owner<long long>::template List<int, char> values;
};

int main() {
	ShortBox short_box{};
	LongBox long_box{};
	if (short_box.values.bytes() != sizeof(int) + sizeof(char))
		return 1;
	if (long_box.values.bytes() != sizeof(int) + sizeof(char))
		return 2;
	if (short_box.values.owner_size() != sizeof(short))
		return 3;
	if (long_box.values.owner_size() != sizeof(long long))
		return 4;
	if (short_box.values.owner_size() == long_box.values.owner_size())
		return 5;
	return 0;
}
