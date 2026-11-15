// Distinct member partial specializations can synthesize the same internal
// pattern name because the name only records pointer/reference/cv modifiers.
// Each pattern must keep its own deleted constructor schema, otherwise a
// deleted constructor from one partial suppresses construction through another.

struct Host {
	template <class T, class U> struct SharedMemberCopy {
		SharedMemberCopy();
	};

	template <class T> struct SharedMemberCopy<T*, int> {
		SharedMemberCopy();
		SharedMemberCopy(const SharedMemberCopy&) = delete;
	};

	template <class T> struct SharedMemberCopy<T*, char> {
		SharedMemberCopy();
	};

	template <class T, class U> struct SharedMemberGeneral {
		SharedMemberGeneral(int);
	};

	template <class T> struct SharedMemberGeneral<T*, int> {
		SharedMemberGeneral(int) = delete;
		SharedMemberGeneral(char);
	};

	template <class T> struct SharedMemberGeneral<T*, char> {
		SharedMemberGeneral(int);
	};
};

static_assert(!__is_constructible(Host::SharedMemberCopy<int*, int>, const Host::SharedMemberCopy<int*, int>&));
static_assert(__is_constructible(Host::SharedMemberCopy<int*, char>, const Host::SharedMemberCopy<int*, char>&));
static_assert(__is_constructible(Host::SharedMemberCopy<int*, char>));

static_assert(!__is_constructible(Host::SharedMemberGeneral<int*, int>, int));
static_assert(__is_constructible(Host::SharedMemberGeneral<int*, int>, char));
static_assert(__is_constructible(Host::SharedMemberGeneral<int*, char>, int));

int main() {
	return 0;
}
