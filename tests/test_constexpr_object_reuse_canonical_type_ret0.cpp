// Two constexpr objects may share one value only when their types are the same
// canonical type. A typedef spelling of the same record keeps the fully
// materialized nested member bindings that same-type copy-list-initialization
// reuses, while an object of a different record must keep its own members: if
// the reuse rule collapsed the two records, `Wrap wrapped{Leaf{42}}` would hand
// back the Leaf object and `wrapped.value` would read 42 instead of the Wrap
// member's zero.
namespace std {
template <typename T>
class initializer_list {
public:
	const T* first_;
	const T* last_;

	constexpr initializer_list(const T* f, const T* l) noexcept : first_(f), last_(l) {}

	constexpr const T* begin() const noexcept { return first_; }
	constexpr const T* end() const noexcept { return last_; }
};
} // namespace std

struct IterHolder {
	const int* p;

	constexpr int read() const {
		return *p;
	}
};

struct Outer {
	IterHolder holder;
};

using OuterAlias = Outer;

constexpr OuterAlias makeAliasedOuter(std::initializer_list<int> values) {
	return OuterAlias{IterHolder{values.begin()}};
}

// The alias spelling names one canonical record with Outer, so the copy shares
// the source object and its nested bindings survive. Losing that identity makes
// the returned member call fail to resolve instead of returning 7.
constexpr int aliasSpellingSharesObject(std::initializer_list<int> values) {
	Outer source = makeAliasedOuter(values);
	OuterAlias destination{source};
	return destination.holder.read();
}

struct Leaf {
	int value;
};

struct Wrap {
	Leaf leaf;
	int value;
};

// Distinct records stay distinct. Sharing here would expose Leaf::value as
// Wrap::value, so a rule that compared only the terminal record category would
// return 42 instead of the default-initialized Wrap member.
constexpr int distinctRecordsKeepOwnMembers() {
	Wrap wrapped{Leaf{42}};
	return wrapped.value;
}

int main() {
	static_assert(aliasSpellingSharesObject({7, 8}) == 7);
	static_assert(distinctRecordsKeepOwnMembers() == 0);
	const bool ok = aliasSpellingSharesObject({7, 8}) == 7 && distinctRecordsKeepOwnMembers() == 0;
	return ok ? 0 : 1;
}
