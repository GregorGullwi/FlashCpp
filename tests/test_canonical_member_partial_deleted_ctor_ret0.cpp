// Deleted general constructors in member class-template partial
// specializations must survive instantiation so a deleted best match rejects
// constructibility.

struct Host {
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

	template <class T> struct PartialDeletedCopy;

	template <class T> struct PartialDeletedCopy<T*> {
		PartialDeletedCopy();
		PartialDeletedCopy(const PartialDeletedCopy&) = delete;
	};

	template <class T> struct PartialDeletedMove;

	template <class T> struct PartialDeletedMove<T*> {
		PartialDeletedMove();
		PartialDeletedMove(PartialDeletedMove&&) = delete;
	};
};

static_assert(!__is_constructible(Host::PartialDeletedInt<int*>, int));
static_assert(__is_constructible(Host::PartialDeletedInt<int*>, char));

static_assert(!__is_constructible(Host::PartialDeletedParam<int*>, int));
static_assert(__is_constructible(Host::PartialDeletedParam<int*>, char));

static_assert(__is_constructible(Host::PartialDeletedDefault<int*>));
static_assert(!__is_constructible(Host::PartialDeletedDefault<int*>, int));

static_assert(__is_constructible(Host::PartialDeletedCopy<int*>));
static_assert(!__is_constructible(Host::PartialDeletedCopy<int*>, const Host::PartialDeletedCopy<int*>&));

static_assert(__is_constructible(Host::PartialDeletedMove<int*>));
static_assert(!__is_constructible(Host::PartialDeletedMove<int*>, Host::PartialDeletedMove<int*>&&));

int main() {
	return 0;
}
