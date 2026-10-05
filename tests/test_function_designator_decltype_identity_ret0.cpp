// A bare function designator's decltype is the function type itself, not a
// pointer-to-function; the function-to-pointer conversion applies only to an
// address-of or an expression value.
struct Payload {
	int value;
};

int freeFn(int);
Payload transform(Payload);

using Fn = int(int);
using Fp = int (*)(int);
using PayloadFn = Payload(Payload);

static_assert(__is_same(decltype(freeFn), Fn), "designator is the function type");
static_assert(!__is_same(decltype(freeFn), Fp), "designator is not a function pointer");
static_assert(__is_same(decltype(&freeFn), Fp), "address-of is a function pointer");
static_assert(__is_function(decltype(freeFn)), "designator is a function");
static_assert(!__is_pointer(decltype(freeFn)), "designator is not a pointer");

static_assert(__is_same(decltype(transform), PayloadFn), "record-parameter designator is the function type");
static_assert(!__is_same(decltype(transform), Payload (*)(Payload)), "record-parameter designator is not a pointer");

int main() {
	return 0;
}
