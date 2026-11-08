// A same-class by-value parameter accepts a cv-qualified or reference argument
// as an identity conversion. The native canonical test pins the canonical-only
// answer; this regression keeps the folded and lazy paths in agreement with it.
struct Payload {
	int value;
};

struct TakesPayload {
	TakesPayload(Payload) noexcept {}
};

template <typename Type, typename Argument>
concept ConstructibleFrom = __is_constructible(Type, Argument);

template <typename Type, typename Argument>
requires ConstructibleFrom<Type, Argument>
int probe(Type*, Argument*) {
	return 1;
}

int probe(...) {
	return 2;
}

template <typename Type, typename Argument>
requires ConstructibleFrom<Type, Argument>
int probe_reference(Type*, Argument&&) {
	return 1;
}

int probe_reference(...) {
	return 2;
}

static_assert(__is_constructible(TakesPayload, const Payload), "const same-class argument");
static_assert(__is_nothrow_constructible(TakesPayload, const Payload), "const same-class argument stays nothrow");
static_assert(__is_constructible(TakesPayload, Payload&), "lvalue same-class argument");

int main() {
	if (probe((TakesPayload*)nullptr, (const Payload*)nullptr) != 1) {
		return 1;
	}
	Payload payload{0};
	if (probe_reference((TakesPayload*)nullptr, payload) != 1) {
		return 2;
	}
	return 0;
}
