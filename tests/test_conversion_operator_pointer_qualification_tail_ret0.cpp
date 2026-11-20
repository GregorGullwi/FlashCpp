// The conversion-operator value tail must cover pointer qualification, object
// pointer to cv void, and pointer to bool as structural conversions, and must
// rank an exact pointer result ahead of a qualification-adjusting one.
int exact_target = 3;
int adjusted_target = 4;

struct QualificationRanking {
	operator int*() const { return &adjusted_target; }
	operator const int*() const { return &exact_target; }
};

struct VoidTailSource {
	operator int*() const { return &exact_target; }
};

struct NestedQualificationSource {
	int* row = &exact_target;
	operator int**() { return &row; }
};

int select_pointer(const int*) { return 11; }
int select_pointer(...) { return 22; }

int main() {
	QualificationRanking ranking;
	const int* selected = ranking;
	if (selected != &exact_target) {
		return 1;
	}
	if (select_pointer(ranking) != 11) {
		return 2;
	}
	VoidTailSource void_source;
	void* address = void_source;
	if (address != static_cast<void*>(&exact_target)) {
		return 3;
	}
	NestedQualificationSource nested;
	const int* const* nested_pointer = nested;
	if (nested_pointer != static_cast<const int* const*>(&nested.row) || *nested_pointer != &exact_target) {
		return 4;
	}
	return 0;
}
