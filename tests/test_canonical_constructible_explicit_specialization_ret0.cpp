// An explicit (full) class-template specialization is a distinct class with its
// own constructors, so its constructibility answer must follow the
// specialization rather than the primary template.
template <class T>
struct Box {
	Box(int) noexcept(true) {}
	int tag = 0;
};

template <>
struct Box<int> {
	Box(int) noexcept(false) {}
	int tag = 0;
};

static_assert(__is_constructible(Box<int>, int), "explicit specialization is constructible");
static_assert(!__is_nothrow_constructible(Box<int>, int), "explicit specialization throwing constructor");
static_assert(__is_nothrow_constructible(Box<double>, int), "primary instantiation nothrow constructor");

int main() {
	Box<int> specialized{1};
	Box<double> primary{2};
	return specialized.tag == 0 && primary.tag == 0 ? 0 : 1;
}
