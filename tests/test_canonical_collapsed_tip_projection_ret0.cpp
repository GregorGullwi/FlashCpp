// Collapsed dependent tips project Builtin TypeIds onto TypeIndex before the
// restamp Clear path drops dependent_name_type_, so template member aliases
// remain usable after substitute+resolve. Mix native sizes and a nested class
// hop so tip resolve plus projection are both exercised.
struct Owner {
	using type = int;
	typedef short tag;
};

struct Outer {
	struct Inner {
		using type = unsigned;
	};
};

template<typename T>
struct Box {
	using result = typename T::type;
	using label = typename T::tag;
};

template<typename T>
struct Wrap {
	using nested = typename T::Inner::type;
};

int main() {
	Box<Owner>::result count = 5;
	Box<Owner>::label width = 2;
	Wrap<Outer>::nested depth = 3u;
	return static_cast<int>(count - width) - static_cast<int>(depth);
}
