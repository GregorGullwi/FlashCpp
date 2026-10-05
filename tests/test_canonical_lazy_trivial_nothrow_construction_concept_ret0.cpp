// Concepts built on zero-argument __is_trivially_constructible and
// __is_nothrow_constructible must be answered from the canonical construction
// facts in the lazy constraint evaluator. The constrained overloads take a
// pointer so the query does not construct the argument.
struct Plain {
	int value;
	long tag;
};

struct NonTrivialMember {
	NonTrivialMember() {}
};

struct HasNonTrivialMember {
	NonTrivialMember member;
};

struct NothrowMember {
	NothrowMember() noexcept {}
};

struct HasNothrowMember {
	NothrowMember member;
};

struct ThrowingMember {
	ThrowingMember() {}
};

struct HasThrowingMember {
	ThrowingMember member;
};

template <typename Type>
concept TriviallyDefaultConstructible = __is_trivially_constructible(Type);

template <typename Type>
concept NothrowDefaultConstructible = __is_nothrow_constructible(Type);

template <typename Type>
requires TriviallyDefaultConstructible<Type>
int probeTrivial(Type*) {
	return 1;
}

int probeTrivial(...) {
	return 2;
}

template <typename Type>
requires NothrowDefaultConstructible<Type>
int probeNothrow(Type*) {
	return 3;
}

int probeNothrow(...) {
	return 4;
}

int main() {
	Plain plain{};
	HasNonTrivialMember non_trivial{};
	HasNothrowMember nothrow{};
	HasThrowingMember throwing{};

	int mismatches = 0;
	if (probeTrivial(&plain) != 1) {
		mismatches |= 1;
	}
	if (probeTrivial(&non_trivial) != 2) {
		mismatches |= 2;
	}
	if (probeNothrow(&nothrow) != 3) {
		mismatches |= 4;
	}
	if (probeNothrow(&throwing) != 4) {
		mismatches |= 8;
	}
	return mismatches == 0 ? 0 : 1;
}
