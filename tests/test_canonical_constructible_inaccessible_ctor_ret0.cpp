// A non-public constructor still participates in overload resolution. When it
// is the best match, construction is ill-formed even if a worse public
// constructor could otherwise accept the argument.

struct ProtectedInt { protected: ProtectedInt(int); public: ProtectedInt(char); };
struct PrivateInt { private: PrivateInt(int); public: PrivateInt(char); };
struct PrivateLong { private: PrivateLong(long); public: PrivateLong(int); };

static_assert(!__is_constructible(ProtectedInt, int));
static_assert(__is_constructible(ProtectedInt, char));
static_assert(!__is_nothrow_constructible(ProtectedInt, int));
static_assert(!__is_trivially_constructible(ProtectedInt, int));

static_assert(!__is_constructible(PrivateInt, int));
static_assert(__is_constructible(PrivateInt, char));
static_assert(!__is_nothrow_constructible(PrivateInt, int));
static_assert(!__is_trivially_constructible(PrivateInt, int));

static_assert(__is_constructible(PrivateLong, int));
static_assert(!__is_constructible(PrivateLong, long));

int main() {
	return 0;
}
