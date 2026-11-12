// A deleted constructor participates in overload resolution. When it is the
// best match, construction is ill-formed even if a worse public constructor
// could accept the argument.

struct DeletedInt { DeletedInt(int) = delete; DeletedInt(char); };
struct DeletedExact { DeletedExact(int) = delete; DeletedExact(long); };
struct DeletedDefaultArg { DeletedDefaultArg(int, int = 0) = delete; DeletedDefaultArg(char); };
struct DeletedDefault { DeletedDefault() = delete; DeletedDefault(char); };

static_assert(!__is_constructible(DeletedInt, int));
static_assert(__is_constructible(DeletedInt, char));
static_assert(!__is_nothrow_constructible(DeletedInt, int));
static_assert(!__is_trivially_constructible(DeletedInt, int));

static_assert(!__is_constructible(DeletedExact, int));
static_assert(__is_constructible(DeletedExact, long));

static_assert(!__is_constructible(DeletedDefaultArg, int));
static_assert(__is_constructible(DeletedDefaultArg, char));

static_assert(__is_constructible(DeletedDefault, char));

int main() {
	return 0;
}
