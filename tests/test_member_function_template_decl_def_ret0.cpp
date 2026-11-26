// A member function template declared and then defined inside a class template
// is one template. Both spellings must reuse the same TemplateDeclId even though
// the in-class definition's own type parameters carry the enclosing class
// template's provisional stamp until the definition publishes. Distinct overload
// shapes and a second class-template owner must stay separate. An out-of-line
// definition of the same member template must keep resolving to the declaration.
template <typename Outer>
struct MemberFunctionTemplateOwner {
	template <typename Left, typename Right>
	Right select(Left left, Right right);
	template <typename First, typename Second>
	Second select(First left, Second right) {
		return static_cast<Second>(left + right);
	}

	template <typename Value>
	Value select(Value value) {
		return value;
	}

	template <typename T>
	T* address(T* value) {
		return value;
	}
};

template <typename Outer>
struct OutOfLineMemberFunctionTemplateOwner {
	template <typename Left, typename Right>
	Right combine(Left left, Right right);
};

template <typename Outer>
template <typename Left, typename Right>
Right OutOfLineMemberFunctionTemplateOwner<Outer>::combine(Left left, Right right) {
	return static_cast<Right>(left + right);
}

template <typename Outer>
struct OtherMemberFunctionTemplateOwner {
	template <typename T>
	T* address(T* value) {
		return value;
	}
};

int main() {
	MemberFunctionTemplateOwner<int> owner;
	if (owner.select(19, 23) != 42) {
		return 1;
	}
	if (owner.select(-42) != -42) {
		return 2;
	}

	long long wide = 40;
	long long* wide_address = owner.address(&wide);
	if (wide_address != &wide || *wide_address != 40) {
		return 3;
	}

	OutOfLineMemberFunctionTemplateOwner<int> out_of_line;
	if (out_of_line.combine(20, 22) != 42) {
		return 4;
	}

	OtherMemberFunctionTemplateOwner<int> other;
	int narrow = 41;
	int* narrow_address = other.address(&narrow);
	if (narrow_address != &narrow || *narrow_address != 41) {
		return 5;
	}

	return 0;
}
