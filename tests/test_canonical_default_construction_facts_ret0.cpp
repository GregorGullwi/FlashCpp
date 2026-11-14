// Default-construction facts must propagate through bases and template members,
// while the default-member-initializer path keeps using the recursive fallback.

struct DefaultMember {
	DefaultMember() = default;
};

struct DeletedDefault {
	DeletedDefault() = delete;
};

struct NontrivialDefault {
	NontrivialDefault() {}
};

struct ThrowingDefault {
	ThrowingDefault() noexcept(false) {}
};

template<typename T>
struct Box {
	T value;
};

struct TrivialHolder {
	Box<DefaultMember> member;
};

struct DeletedHolder {
	Box<DeletedDefault> member;
};

struct NontrivialHolder {
	Box<NontrivialDefault> member;
};

struct ThrowingHolder {
	Box<ThrowingDefault> member;
};

struct NontrivialBase {
	NontrivialBase() {}
};

struct DerivedFromNontrivial : NontrivialBase {
	int value;
};

struct ReferenceHolder {
	int& reference;
};

struct NothrowInitializer {
	NothrowInitializer() noexcept {}
};

struct ThrowingInitializer {
	ThrowingInitializer() noexcept(false) {}
};

struct NothrowInitializerHolder {
	NothrowInitializer member{};
};

struct ThrowingInitializerHolder {
	ThrowingInitializer member{};
};

static_assert(__is_constructible(TrivialHolder));
static_assert(__is_trivially_constructible(TrivialHolder));
static_assert(__is_nothrow_constructible(TrivialHolder));

static_assert(!__is_constructible(DeletedHolder));
static_assert(!__is_trivially_constructible(DeletedHolder));
static_assert(!__is_nothrow_constructible(DeletedHolder));

static_assert(__is_constructible(NontrivialHolder));
static_assert(!__is_trivially_constructible(NontrivialHolder));
static_assert(!__is_nothrow_constructible(NontrivialHolder));

static_assert(__is_constructible(ThrowingHolder));
static_assert(!__is_trivially_constructible(ThrowingHolder));
static_assert(!__is_nothrow_constructible(ThrowingHolder));

static_assert(__is_constructible(DerivedFromNontrivial));
static_assert(!__is_trivially_constructible(DerivedFromNontrivial));
static_assert(!__is_nothrow_constructible(DerivedFromNontrivial));

static_assert(!__is_constructible(ReferenceHolder));

static_assert(__is_nothrow_constructible(NothrowInitializerHolder));
static_assert(!__is_nothrow_constructible(ThrowingInitializerHolder));

int main() {
	return 0;
}
