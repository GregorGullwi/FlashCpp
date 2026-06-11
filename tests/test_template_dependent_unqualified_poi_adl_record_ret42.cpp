// Regression: dependent unqualified call completion can materialize a resolved
// direct call whose final point-of-instantiation target comes from ADL rather
// than the definition-bound ordinary lookup. Preserve that completed target as
// structured metadata instead of relying on late recovery.

constexpr int adl_pick(long) {
	return 1;
}

namespace N {
	struct Tag {
		friend constexpr int adl_pick(Tag) {
			return 42;
		}
	};
}

template <typename T>
int runAdlPick() {
	return adl_pick(T{});
}

int main() {
	return runAdlPick<N::Tag>();
}
