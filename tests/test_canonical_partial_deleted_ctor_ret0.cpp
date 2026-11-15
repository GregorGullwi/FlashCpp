// Deleted general constructors in class-template partial specializations must
// survive instantiation so a deleted best match rejects constructibility.

template <class T> struct PartialDeletedInt;

template <class T> struct PartialDeletedInt<T*> {
	PartialDeletedInt(int) = delete;
	PartialDeletedInt(char);
};

template <class T> struct PartialDeletedParam;

template <class T> struct PartialDeletedParam<T*> {
	PartialDeletedParam(T) = delete;
	PartialDeletedParam(char);
};

template <class T> struct PartialDeletedDefault;

template <class T> struct PartialDeletedDefault<T*> {
	PartialDeletedDefault();
	PartialDeletedDefault(int) = delete;
};

static_assert(!__is_constructible(PartialDeletedInt<int*>, int));
static_assert(__is_constructible(PartialDeletedInt<int*>, char));

static_assert(!__is_constructible(PartialDeletedParam<int*>, int));
static_assert(__is_constructible(PartialDeletedParam<int*>, char));

static_assert(__is_constructible(PartialDeletedDefault<int*>));
static_assert(!__is_constructible(PartialDeletedDefault<int*>, int));

int main() {
	return 0;
}
