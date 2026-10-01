struct EmptyMemberType {
};

struct NestedMember {
	EmptyMemberType member;
};

struct NestedConflict : EmptyMemberType {
	NestedMember member;
};

struct NestedArrayMember {
	EmptyMemberType members[2];
};

struct ArrayConflict : EmptyMemberType {
	NestedArrayMember member;
};

union NestedUnion {
	EmptyMemberType member;
	int value;
};

struct UnionHolder {
	NestedUnion member;
};

struct UnionConflict : EmptyMemberType {
	UnionHolder member;
};

static_assert(!__is_standard_layout(NestedConflict));
static_assert(!__is_standard_layout(ArrayConflict));
static_assert(!__is_standard_layout(UnionConflict));

int main() {
	int mismatches = 0;
	mismatches |= __is_standard_layout(NestedConflict) ? 1 : 0;
	mismatches |= __is_standard_layout(ArrayConflict) ? 2 : 0;
	mismatches |= __is_standard_layout(UnionConflict) ? 4 : 0;
	return mismatches;
}
