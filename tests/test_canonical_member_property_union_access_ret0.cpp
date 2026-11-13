// Union variant members inherit their own lifetime facts. A deleted or
// inaccessible variant destructor must delete the implicit union destructor,
// while a trivial variant keeps the union trivially destructible.

struct TrivialVariant {
	int value;
};

struct DeletedVariant {
	~DeletedVariant() = delete;
};

struct PrivateVariant {
private:
	~PrivateVariant() = default;
};

struct NontrivialVariant {
	~NontrivialVariant();
};

union TrivialUnion {
	TrivialVariant member;
	int value;
};

union DeletedUnion {
	DeletedVariant member;
	int value;
};

union PrivateUnion {
	PrivateVariant member;
	int value;
};

union NontrivialUnion {
	NontrivialVariant member;
	int value;
};

static_assert(__is_destructible(TrivialUnion));
static_assert(__is_trivially_destructible(TrivialUnion));
static_assert(__is_nothrow_destructible(TrivialUnion));
static_assert(__is_trivially_copyable(TrivialUnion));

static_assert(!__is_destructible(DeletedUnion));
static_assert(!__is_trivially_destructible(DeletedUnion));
static_assert(!__is_nothrow_destructible(DeletedUnion));

static_assert(!__is_destructible(PrivateUnion));
static_assert(!__is_trivially_destructible(PrivateUnion));
static_assert(!__is_nothrow_destructible(PrivateUnion));

static_assert(!__is_destructible(NontrivialUnion));
static_assert(!__is_trivially_destructible(NontrivialUnion));
static_assert(!__is_nothrow_destructible(NontrivialUnion));
static_assert(!__has_trivial_destructor(NontrivialUnion));

int main() {
	return 0;
}
