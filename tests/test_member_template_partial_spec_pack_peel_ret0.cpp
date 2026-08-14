// Member class-template pack peeling: Head plus remaining pack is more
// specialized than `template<typename...>`. libstdc++ <type_traits> uses
// this as __make_unsigned_selector_base::_List.

struct Owner {
	template <typename...>
	struct List {
		static constexpr int kind = 1;
	};

	template <typename Head, typename... Tail>
	struct List<Head, Tail...> {
		static constexpr int kind = 2;
		static constexpr int head_size = sizeof(Head);
		using head = Head;
	};
};

struct Small {
	char value;
};

struct Large {
	long long values[2];
};

int main() {
	const int empty_kind = Owner::List<>::kind;
	const int char_kind = Owner::List<char>::kind;
	const int mixed_kind = Owner::List<char, Small, int, Large>::kind;
	const int char_head = Owner::List<char>::head_size;
	const int small_head = sizeof(typename Owner::List<Small, Large>::head);
	return (empty_kind == 1 && char_kind == 2 && mixed_kind == 2 &&
			char_head == sizeof(char) && small_head == sizeof(Small))
			   ? 0
			   : 1;
}
