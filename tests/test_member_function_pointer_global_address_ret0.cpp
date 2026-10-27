// A global member-function-pointer initializer must use the sema-resolved
// member address, including an instantiated member function template.

template <class Type>
struct Box {};

struct S {
	int plain() {
		return 42;
	}

	template <class Type>
	Box<Type> templated(Box<Type>*) {
		return Box<Type>{};
	}
};

using PlainPointer = int (S::*)();
using TemplatedPointer = Box<int> (S::*)(Box<int>*);

PlainPointer global_plain = &S::plain;
TemplatedPointer global_templated = &S::templated<int>;

int main() {
	return global_plain != nullptr && global_templated != nullptr ? 0 : 1;
}
