// An abstract function type is a valid type-id argument and must parse where a
// type-id is expected: as a type-trait argument, an alias target, and a
// template argument.
using Fn = int(int);
using Fp = int (*)(int);

static_assert(__is_same(Fn, int(int)), "alias equals the function spelling");
static_assert(!__is_same(Fn, Fp), "function type differs from a function pointer");

template <class Type>
struct Box {
	Type value;
};

Box<int(int)> boxed;

static_assert(__is_same(decltype(boxed), Box<int(int)>), "function type as a template argument");

int main() {
	return 0;
}
