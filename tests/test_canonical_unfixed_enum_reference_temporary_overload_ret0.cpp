enum SmallUnfixedEnum {
	small_unfixed_value = 7
};

SmallUnfixedEnum small_enum_lvalue = small_unfixed_value;

int rank_enum_lvalue(const int& value) {
	return value;
}

long rank_enum_lvalue(const long& value) {
	return value;
}

int rank_mutable_reference(const int& value) {
	return value;
}

long rank_mutable_reference(int& value) {
	return value;
}

int rank_enum_rvalue(int&& value) {
	return value;
}

long rank_enum_rvalue(long&& value) {
	return value;
}

static_assert(__is_same(
	decltype(rank_enum_lvalue(small_enum_lvalue)), int));
static_assert(__is_same(
	decltype(rank_mutable_reference(small_enum_lvalue)), int));
static_assert(__is_same(
	decltype(rank_enum_rvalue(
		static_cast<SmallUnfixedEnum>(small_unfixed_value))),
	int));

int main() {
	return rank_enum_lvalue(small_enum_lvalue) == 7 &&
		rank_mutable_reference(small_enum_lvalue) == 7 &&
		rank_enum_rvalue(
			static_cast<SmallUnfixedEnum>(small_unfixed_value)) == 7
		? 0
		: 1;
}
