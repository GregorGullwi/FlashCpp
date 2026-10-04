// Unary record-property traits must be answered from canonical facts in both
// constant evaluation and lazy C++20 constraints, including specializations.
struct EmptyRecord {};

struct DataRecord {
	int value;
};

struct NontrivialDestructor {
	~NontrivialDestructor() {}
};

struct RecordWithVirtualDestructor {
	virtual ~RecordWithVirtualDestructor() = default;
};

struct PrivateDestructor {
private:
	~PrivateDestructor() = default;
};

template <typename T>
struct EmptyRecordTemplate {};

template <typename T>
concept CanonicalEmptyRecord =
	__is_trivially_copyable(T) &&
	__is_trivial(T) &&
	__is_pod(T) &&
	__is_standard_layout(T) &&
	__is_aggregate(T) &&
	__is_empty(T) &&
	__is_destructible(T) &&
	__is_trivially_destructible(T) &&
	__is_nothrow_destructible(T) &&
	__has_trivial_destructor(T) &&
	!__has_virtual_destructor(T);

template <CanonicalEmptyRecord T>
int selectEmptyRecord(T*) { return 1; }
int selectEmptyRecord(...) { return 2; }

static_assert(CanonicalEmptyRecord<EmptyRecord>);
static_assert(CanonicalEmptyRecord<EmptyRecordTemplate<int>>);
static_assert(__is_trivially_copyable(DataRecord));
static_assert(__is_trivial(DataRecord));
static_assert(__is_pod(DataRecord));
static_assert(__is_standard_layout(DataRecord));
static_assert(__is_aggregate(DataRecord));
static_assert(!__is_empty(DataRecord));
static_assert(__is_destructible(DataRecord));
static_assert(__is_trivially_destructible(DataRecord));
static_assert(__is_nothrow_destructible(DataRecord));
static_assert(__has_trivial_destructor(DataRecord));
static_assert(!__has_virtual_destructor(DataRecord));
static_assert(!__is_trivially_copyable(NontrivialDestructor));
static_assert(!__is_trivial(NontrivialDestructor));
static_assert(!__is_pod(NontrivialDestructor));
static_assert(__is_standard_layout(NontrivialDestructor));
static_assert(!__is_aggregate(RecordWithVirtualDestructor));
static_assert(!__is_empty(RecordWithVirtualDestructor));
static_assert(__is_destructible(RecordWithVirtualDestructor));
static_assert(!__is_trivially_destructible(RecordWithVirtualDestructor));
static_assert(__is_nothrow_destructible(RecordWithVirtualDestructor));
static_assert(!__has_trivial_destructor(RecordWithVirtualDestructor));
static_assert(__has_virtual_destructor(RecordWithVirtualDestructor));
static_assert(!__is_destructible(PrivateDestructor));

int main() {
	EmptyRecord empty;
	EmptyRecordTemplate<int> specialized;
	DataRecord data{};
	int mismatches = 0;
	mismatches += selectEmptyRecord(&empty) != 1;
	mismatches += selectEmptyRecord(&specialized) != 1;
	mismatches += selectEmptyRecord(&data) != 2;
	return mismatches;
}
