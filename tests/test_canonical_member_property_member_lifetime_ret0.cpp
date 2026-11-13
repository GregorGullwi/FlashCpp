// Member-property facts must keep triviality independent of access/deletion
// while lifetime answers reject an inaccessible or deleted member destructor.

struct TrivialMember {
	int value;
};

struct NontrivialDestructor {
	~NontrivialDestructor();
};

struct DeletedDestructor {
	~DeletedDestructor() = delete;
};

struct PrivateDestructor {
private:
	~PrivateDestructor() = default;
};

struct ThrowingDestructor {
	~ThrowingDestructor() noexcept(false);
};

struct TrivialHolder {
	TrivialMember member;
};

struct NontrivialHolder {
	NontrivialDestructor member;
};

struct DeletedHolder {
	DeletedDestructor member;
};

struct PrivateHolder {
	PrivateDestructor member;
};

struct ThrowingHolder {
	ThrowingDestructor member;
};

struct TrivialArrayHolder {
	TrivialMember members[2];
};

struct NontrivialArrayHolder {
	NontrivialDestructor members[2];
};

static_assert(__is_trivially_copyable(TrivialHolder));
static_assert(__is_trivial(TrivialHolder));
static_assert(__is_destructible(TrivialHolder));
static_assert(__is_trivially_destructible(TrivialHolder));
static_assert(__is_nothrow_destructible(TrivialHolder));
static_assert(__has_trivial_destructor(TrivialHolder));

static_assert(!__is_trivially_copyable(NontrivialHolder));
static_assert(!__is_trivial(NontrivialHolder));
static_assert(__is_destructible(NontrivialHolder));
static_assert(!__is_trivially_destructible(NontrivialHolder));
static_assert(__is_nothrow_destructible(NontrivialHolder));
static_assert(!__has_trivial_destructor(NontrivialHolder));

static_assert(!__is_destructible(DeletedHolder));
static_assert(!__is_trivially_destructible(DeletedHolder));
static_assert(!__is_nothrow_destructible(DeletedHolder));
static_assert(__has_trivial_destructor(DeletedHolder));

static_assert(!__is_destructible(PrivateHolder));
static_assert(!__is_trivially_destructible(PrivateHolder));
static_assert(!__is_nothrow_destructible(PrivateHolder));
static_assert(__has_trivial_destructor(PrivateHolder));

static_assert(__is_destructible(ThrowingHolder));
static_assert(!__is_trivially_destructible(ThrowingHolder));
static_assert(!__is_nothrow_destructible(ThrowingHolder));
static_assert(!__has_trivial_destructor(ThrowingHolder));

static_assert(__is_trivially_copyable(TrivialArrayHolder));
static_assert(__is_trivial(TrivialArrayHolder));
static_assert(__is_trivially_destructible(TrivialArrayHolder));
static_assert(__has_trivial_destructor(TrivialArrayHolder));

static_assert(!__is_trivially_copyable(NontrivialArrayHolder));
static_assert(!__is_trivial(NontrivialArrayHolder));
static_assert(__is_destructible(NontrivialArrayHolder));
static_assert(!__is_trivially_destructible(NontrivialArrayHolder));
static_assert(!__has_trivial_destructor(NontrivialArrayHolder));

int main() {
	return 0;
}
