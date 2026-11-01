struct Payload { long long value; short tag; };
template<class Tag> struct Owner {};
template<class Tag, class Argument> struct MemberHolder {
	long long (Owner<Tag>::*pointer)(Argument) const & noexcept;
};
struct ConcreteMember { long long (Owner<char>::*pointer)(short) const & noexcept; };
struct DifferentMember { long long (Owner<double>::*pointer)(short) const & noexcept; };

template<class Tag> struct Outer {
	template<class Value> struct Inner { Tag tag; Value value; };
};
template<class Tag, class Value> struct Holder {
	typename Outer<Tag>::template Inner<Value> item;
};

static_assert(__is_same(decltype(MemberHolder<char, short>::pointer), decltype(ConcreteMember::pointer)));
static_assert(!__is_same(decltype(MemberHolder<char, short>::pointer), decltype(DifferentMember::pointer)));
static_assert(__is_same(decltype(Holder<char, Payload>::item), Outer<char>::Inner<Payload>));
static_assert(!__is_same(decltype(Holder<char, Payload>::item), Outer<double>::Inner<Payload>));

int main() {
	Holder<char, Payload> narrow;
	Holder<double, short> wide;
	narrow.item.tag = 3;
	narrow.item.value.value = 6000000000LL;
	narrow.item.value.tag = 7;
	wide.item.tag = 2.5;
	wide.item.value = 11;
	if (narrow.item.tag != 3 || narrow.item.value.value != 6000000000LL || narrow.item.value.tag != 7) return 1;
	if (wide.item.tag != 2.5 || wide.item.value != 11) return 2;
	return 0;
}
