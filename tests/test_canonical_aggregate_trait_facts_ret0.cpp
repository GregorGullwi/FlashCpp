// Aggregate facts combine bases, template members, and anonymous union
// variants. Existing material covers each family separately; this checks the
// combined propagation through the canonical record facts.

struct TrivialLeaf {
	int value;
};

struct NontrivialDestructor {
	~NontrivialDestructor();
};

template<typename T>
struct Box {
	T value;
};

struct TrivialBase {
	TrivialLeaf base;
};

struct DerivedTrivialMember : TrivialBase {
	Box<TrivialLeaf> member;
};

struct DerivedNontrivialMember : TrivialBase {
	Box<NontrivialDestructor> member;
};

struct AnonymousUnionNontrivial {
	union {
		NontrivialDestructor member;
		int value;
	};
};

static_assert(__is_trivially_copyable(DerivedTrivialMember));
static_assert(__is_trivial(DerivedTrivialMember));
static_assert(__is_destructible(DerivedTrivialMember));
static_assert(__is_trivially_destructible(DerivedTrivialMember));
static_assert(__is_nothrow_destructible(DerivedTrivialMember));
static_assert(__has_trivial_destructor(DerivedTrivialMember));

static_assert(!__is_trivially_copyable(DerivedNontrivialMember));
static_assert(!__is_trivial(DerivedNontrivialMember));
static_assert(__is_destructible(DerivedNontrivialMember));
static_assert(!__is_trivially_destructible(DerivedNontrivialMember));
static_assert(__is_nothrow_destructible(DerivedNontrivialMember));
static_assert(!__has_trivial_destructor(DerivedNontrivialMember));

static_assert(!__is_destructible(AnonymousUnionNontrivial));
static_assert(!__is_trivially_destructible(AnonymousUnionNontrivial));
static_assert(!__is_nothrow_destructible(AnonymousUnionNontrivial));

int main() {
	return 0;
}
