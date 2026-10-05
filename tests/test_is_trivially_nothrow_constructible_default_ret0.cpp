// The trivially- and nothrow-constructible traits with no constructor
// arguments ask whether default construction is trivial or non-throwing. The
// record classifier ignored a deleted or inaccessible default constructor for
// both, and never consulted a user-provided constructor's exception
// specification for the nothrow form.
struct Plain {
	int value;
	long tag;
};

struct UserDefault {
	UserDefault() {}
};

struct NothrowDefault {
	NothrowDefault() noexcept {}
};

struct ThrowingDefault {
	ThrowingDefault() {}
};

struct DeletedDefault {
	DeletedDefault() = delete;
};

struct Abstract {
	virtual void method() = 0;
};

struct PrivateDefault {
private:
	PrivateDefault();
};

static_assert(__is_trivially_constructible(Plain), "aggregate default construction is trivial");
static_assert(!__is_trivially_constructible(UserDefault), "user-provided default is not trivial");
static_assert(!__is_trivially_constructible(DeletedDefault), "deleted default is not trivial");
static_assert(!__is_trivially_constructible(Abstract), "abstract is not trivially constructible");
static_assert(!__is_trivially_constructible(PrivateDefault), "private default is not trivially constructible");

static_assert(__is_nothrow_constructible(Plain), "aggregate default construction is non-throwing");
static_assert(__is_nothrow_constructible(NothrowDefault), "noexcept default is non-throwing");
static_assert(!__is_nothrow_constructible(ThrowingDefault), "throwing default is not non-throwing");
static_assert(!__is_nothrow_constructible(DeletedDefault), "deleted default is not non-throwing");
static_assert(!__is_nothrow_constructible(Abstract), "abstract is not non-throw constructible");
static_assert(!__is_nothrow_constructible(PrivateDefault), "private default is not non-throw constructible");

int main() {
	return 0;
}
