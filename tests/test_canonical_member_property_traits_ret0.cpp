struct Empty {};

struct EmptyNoUnique {
	[[no_unique_address]] Empty member;
};

struct NonEmpty {
	int value;
};

struct NonEmptyNoUnique {
	[[no_unique_address]] NonEmpty member;
};

struct DeletedDtor {
	~DeletedDtor() = delete;
};

struct NonTrivialDtor {
	~NonTrivialDtor();
};

struct MemberDeletedDtor {
	DeletedDtor member;
};

struct UnionDeletedDtor {
	union {
		DeletedDtor member;
		int value;
	};
};

struct UnionNonTrivialDtor {
	union {
		NonTrivialDtor member;
		int value;
	};
};

static_assert(__is_empty(EmptyNoUnique));
static_assert(!__is_empty(NonEmptyNoUnique));
static_assert(!__is_destructible(MemberDeletedDtor));
static_assert(!__is_destructible(UnionDeletedDtor));
static_assert(!__is_destructible(UnionNonTrivialDtor));
static_assert(!__is_nothrow_destructible(UnionNonTrivialDtor));

int main() {
	return 0;
}
