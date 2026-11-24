// Valid pointer qualification conversions must keep working. [conv.qual]/3
// allows adding const (and volatile) at the pointee, at an inner pointer level,
// or at both with const propagated outward; a C-style cast may still perform a
// const_cast-like removal. The constexpr cases pin that the newly validated
// casts keep their value and still evaluate at compile time.

struct QualificationRecord {
	int value;
};

constexpr int pointeeConstCast() {
	int value = 7;
	int* pointer = &value;
	const int* qualified = static_cast<const int*>(pointer);
	return *qualified;
}

constexpr int nestedConstCast() {
	int value = 11;
	int* pointer = &value;
	int** plain = &pointer;
	const int* const* qualified = static_cast<const int* const*>(plain);
	return **qualified;
}

constexpr int innerPointerConstCast() {
	int value = 13;
	int* pointer = &value;
	int** plain = &pointer;
	int* const* qualified = static_cast<int* const*>(plain);
	return **qualified;
}

int recordPointerCast() {
	QualificationRecord record{17};
	QualificationRecord* pointer = &record;
	const QualificationRecord* const* qualified = static_cast<const QualificationRecord* const*>(&pointer);
	return (*qualified)->value;
}

static_assert(pointeeConstCast() == 7);
static_assert(nestedConstCast() == 11);
static_assert(innerPointerConstCast() == 13);

int main() {
	int value = 23;
	int* pointer = &value;
	const int* const_added = static_cast<const int*>(pointer);
	volatile int* volatile_added = static_cast<volatile int*>(pointer);
	const volatile int* both_added = static_cast<const volatile int*>(pointer);
	int** plain = &pointer;
	int* const* inner_const_added = static_cast<int* const*>(plain);
	int* volatile* inner_volatile_added = static_cast<int* volatile*>(plain);

	// [expr.cast]/4: a C-style cast may perform a const_cast-like conversion,
	// so these stay accepted even though static_cast rejects them.
	const int** deep_const = (const int**)plain;
	int* const* inner_const = nullptr;
	int** stripped = (int**)(inner_const);
	const int* pointee_const = nullptr;
	int* pointee_stripped = (int*)(pointee_const);

	(void)const_added;
	(void)volatile_added;
	(void)both_added;
	(void)inner_const_added;
	(void)inner_volatile_added;
	(void)deep_const;
	(void)stripped;
	(void)pointee_stripped;
	return pointeeConstCast() + nestedConstCast() + innerPointerConstCast() + recordPointerCast() - 48;
}
