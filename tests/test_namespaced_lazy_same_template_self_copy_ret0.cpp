// Regression: a namespace-qualified class-template specialization must keep
// the same complete object value when a deferred member copies *this through
// the current-instantiation type.

namespace sample {

struct Other;

template<typename T>
struct ReverseIter {
	T current;

	ReverseIter() : current() {}
	explicit ReverseIter(T value) : current(value) {}
	ReverseIter(const ReverseIter& other) : current(other.current) {}
	explicit ReverseIter(const Other& other);

	ReverseIter copy_self() const {
		ReverseIter copy = *this;
		return copy;
	}
};

} // namespace sample

int main() {
	int value = 42;
	sample::ReverseIter<int*> original(&value);
	sample::ReverseIter<int*> copy = original.copy_self();
	return copy.current == &value ? 0 : 1;
}
