// [class.copy.ctor] classifies a deleted constructor as a copy or move
// constructor only when its reference parameter names the class itself. A
// pointer parameter such as T* const& is an ordinary constructor and must not
// suppress the implicit copy constructor. A const rvalue reference is still a
// move constructor.

struct PointerParam {
	PointerParam();
	PointerParam(PointerParam* const&) = delete;
};

static_assert(__is_constructible(PointerParam, PointerParam&));

struct Host {
	template <class T> struct PartialPointerParam;

	template <class T> struct PartialPointerParam<T*> {
		PartialPointerParam();
		PartialPointerParam(PartialPointerParam* const&) = delete;
	};

	template <class T> struct PartialConstRvalueMove;

	template <class T> struct PartialConstRvalueMove<T*> {
		PartialConstRvalueMove();
		PartialConstRvalueMove(const PartialConstRvalueMove&&) = delete;
	};
};

static_assert(__is_constructible(Host::PartialPointerParam<int*>, Host::PartialPointerParam<int*>&));
static_assert(!__is_constructible(Host::PartialConstRvalueMove<int*>, Host::PartialConstRvalueMove<int*>&));

int main() {
	return 0;
}
