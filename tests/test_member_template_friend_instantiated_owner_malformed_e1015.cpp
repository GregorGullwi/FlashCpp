// A qualified friend class-template declarator with a dangling scope operator
// must be rejected, not silently misparsed by the component-wise owner-chain
// parse (which accepts owners carrying template arguments such as
// Outer<int>::Box<char>).
template <typename T>
struct Outer {
	template <typename U>
	struct Box {};
};

struct Host {
	friend struct Outer<int>::;
};
