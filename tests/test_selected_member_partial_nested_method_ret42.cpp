// A nested class in a selected member partial specialization must publish its
// methods through source aliases of both the member template and the nested type.
template<class T>
struct Owner {
	template<class U>
	struct Box {};

	template<class U>
	struct Box<U*> {
		struct Child {
			T echo(T value) { return value; }
		};
	};
};

using Box = Owner<long long>::Box<int*>;
using Child = Box::Child;

int main() {
	Child child{};
	if (child.echo(5000000000LL) != 5000000000LL) return 1;
	return 42;
}
