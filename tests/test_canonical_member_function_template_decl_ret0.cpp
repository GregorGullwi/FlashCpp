// A member function template directly nested in a published non-template class
// must retain its declaration identity while its body is replayed.
struct MemberTemplateOwner {
	template<typename T, typename U>
	U select(T, U value) {
		return value;
	}
};

int test() {
	MemberTemplateOwner owner;
	return owner.select(4, 9) - 9;
}
