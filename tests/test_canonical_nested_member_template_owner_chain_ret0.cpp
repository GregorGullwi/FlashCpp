// A member class template nested in another member class template composes the
// whole instantiated owner chain. Same-spelling Inner templates under different
// outer or middle arguments are distinct specializations with their own fields.
template<class T> struct Owner {
	template<class U> struct Box {
		template<class V> struct Inner {
			T outer;
			U middle;
			V inner;
		};
	};
};

struct Payload {
	short first;
	int second;
};

using ShortIntChar = Owner<short>::Box<int>::Inner<char>;
using WideIntChar = Owner<long long>::Box<int>::Inner<char>;
using ShortLongChar = Owner<short>::Box<long long>::Inner<char>;
using ShortIntPayload = Owner<short>::Box<int>::Inner<Payload>;

static_assert(sizeof(ShortIntChar) < sizeof(WideIntChar));
static_assert(sizeof(ShortIntChar) < sizeof(ShortLongChar));
static_assert(sizeof(ShortIntPayload) > sizeof(ShortIntChar));

int main() {
	ShortIntChar narrow{1, 2, 'a'};
	WideIntChar wide{3, 4, 'b'};
	ShortLongChar middle{5, 6, 'c'};
	ShortIntPayload payload{7, 8, Payload{9, 10}};
	const ShortIntChar narrow_copy = narrow;
	const int total = static_cast<int>(narrow.outer) + static_cast<int>(wide.outer) +
		static_cast<int>(middle.middle) + payload.middle + payload.inner.first +
		payload.inner.second + static_cast<int>(narrow_copy.inner);
	return total - 134;
}
