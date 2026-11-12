// Triviality and destructor facts must match clang for deleted, protected, and
// explicitly defaulted special members. The deleted destructor stays trivial
// for copy/trivial/POD classification while remaining non-destructible, and a
// first-declaration defaulted copy or move constructor stays trivial.

struct DeletedDtor { int value; ~DeletedDtor() = delete; };
struct DeletedDtorBase : DeletedDtor {};
struct DeletedDtorMember { DeletedDtor member; };
struct ProtectedDefaultedDtor { protected: ~ProtectedDefaultedDtor() = default; public: int value; };
struct AbstractDeletedDtor { virtual void f() = 0; ~AbstractDeletedDtor() = delete; };
struct DefaultedCopyCtor { int value; DefaultedCopyCtor(const DefaultedCopyCtor&) = default; };
struct UserProvidedCopyCtor { int value; UserProvidedCopyCtor(const UserProvidedCopyCtor&) {} };
struct DeletedCopyCtor { int value; DeletedCopyCtor(const DeletedCopyCtor&) = delete; };
struct DefaultedMoveCtor { int value; DefaultedMoveCtor(DefaultedMoveCtor&&) = default; };
struct UserProvidedMoveCtor { int value; UserProvidedMoveCtor(UserProvidedMoveCtor&&) {} };
struct DeletedCopyAssignment { int value; DeletedCopyAssignment& operator=(const DeletedCopyAssignment&) = delete; };
struct DeletedMoveAssignment { int value; DeletedMoveAssignment& operator=(DeletedMoveAssignment&&) = delete; };
struct NontrivialDtor { ~NontrivialDtor() {} };
struct MemberNontrivialDtor { NontrivialDtor member; };
struct BaseNontrivialDtor : NontrivialDtor { int value; };

static_assert(__is_trivially_copyable(DeletedDtor));
static_assert(__is_trivial(DeletedDtor));
static_assert(__is_pod(DeletedDtor));
static_assert(__has_trivial_destructor(DeletedDtor));
static_assert(!__is_trivially_destructible(DeletedDtor));
static_assert(!__is_destructible(DeletedDtor));

static_assert(__is_trivially_copyable(DeletedDtorBase));
static_assert(__is_trivial(DeletedDtorBase));
static_assert(__has_trivial_destructor(DeletedDtorBase));
static_assert(!__is_trivially_destructible(DeletedDtorBase));
static_assert(!__is_destructible(DeletedDtorBase));

static_assert(__is_trivially_copyable(DeletedDtorMember));
static_assert(__is_trivial(DeletedDtorMember));
static_assert(__has_trivial_destructor(DeletedDtorMember));
static_assert(!__is_trivially_destructible(DeletedDtorMember));
static_assert(!__is_destructible(DeletedDtorMember));

static_assert(__is_trivially_copyable(ProtectedDefaultedDtor));
static_assert(__is_trivial(ProtectedDefaultedDtor));
static_assert(__has_trivial_destructor(ProtectedDefaultedDtor));
static_assert(!__is_trivially_destructible(ProtectedDefaultedDtor));
static_assert(!__is_destructible(ProtectedDefaultedDtor));

static_assert(!__is_trivially_copyable(AbstractDeletedDtor));
static_assert(!__is_trivial(AbstractDeletedDtor));
static_assert(__has_trivial_destructor(AbstractDeletedDtor));
static_assert(!__is_trivially_destructible(AbstractDeletedDtor));
static_assert(!__is_destructible(AbstractDeletedDtor));

static_assert(__is_trivially_copyable(DefaultedCopyCtor));
static_assert(!__is_trivial(DefaultedCopyCtor));
static_assert(__has_trivial_destructor(DefaultedCopyCtor));
static_assert(__is_trivially_destructible(DefaultedCopyCtor));

static_assert(!__is_trivially_copyable(UserProvidedCopyCtor));
static_assert(!__is_trivial(UserProvidedCopyCtor));

static_assert(__is_trivially_copyable(DeletedCopyCtor));
static_assert(!__is_trivial(DeletedCopyCtor));

static_assert(__is_trivially_copyable(DefaultedMoveCtor));
static_assert(!__is_trivial(DefaultedMoveCtor));

static_assert(!__is_trivially_copyable(UserProvidedMoveCtor));
static_assert(!__is_trivial(UserProvidedMoveCtor));

static_assert(__is_trivially_copyable(DeletedCopyAssignment));
static_assert(__is_trivial(DeletedCopyAssignment));
static_assert(__is_pod(DeletedCopyAssignment));

static_assert(__is_trivially_copyable(DeletedMoveAssignment));
static_assert(__is_trivial(DeletedMoveAssignment));
static_assert(__is_pod(DeletedMoveAssignment));

static_assert(!__is_trivially_copyable(MemberNontrivialDtor));
static_assert(!__is_trivial(MemberNontrivialDtor));
static_assert(!__has_trivial_destructor(MemberNontrivialDtor));
static_assert(!__is_trivially_destructible(MemberNontrivialDtor));

static_assert(!__is_trivially_copyable(BaseNontrivialDtor));
static_assert(!__is_trivial(BaseNontrivialDtor));
static_assert(!__has_trivial_destructor(BaseNontrivialDtor));

int main() {
	return 0;
}
