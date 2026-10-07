template <class Type>
struct Carrier {};

struct IntResult {
	using result = int;
};

template <class Type>
using ReadPointer = typename Type::result (Carrier<Type>::*)() const;

static_assert(__is_same(ReadPointer<IntResult>, int (Carrier<IntResult>::*)() const));

int main() {
	return 0;
}
