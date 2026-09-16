// A qualified friend class-template specialization must name a previously
// declared member template. An unknown member under an instantiated owner
// (the component-wise parse accepts Outer<Args>::Box<Args>) must be rejected
// rather than silently accepted.
template <typename T>
struct Outer {
	template <typename U>
	struct Box {};
};

struct Host {
	friend struct Outer<int>::Missing<char>;
	int x = 1;
};
