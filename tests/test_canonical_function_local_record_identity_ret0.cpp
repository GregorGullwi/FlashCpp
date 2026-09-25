auto makeFirstLocalIdentity() {
	struct LocalIdentity {
		int value;
	};
	return LocalIdentity{1};
}

auto makeSecondLocalIdentity() {
	struct LocalIdentity {
		double value;
	};
	return LocalIdentity{2.0};
}

template <typename First, typename Second>
constexpr bool hasDistinctIdentity = !__is_same(First, Second);

static_assert(hasDistinctIdentity<
	decltype(makeFirstLocalIdentity()),
	decltype(makeSecondLocalIdentity())>);

int main() {
	return hasDistinctIdentity<
		decltype(makeFirstLocalIdentity()),
		decltype(makeSecondLocalIdentity())> ? 0 : 1;
}
