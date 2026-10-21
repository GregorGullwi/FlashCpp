struct Record {
	long long value;
};

using LongCallback = long(short);
using SafeCallback = long(short) noexcept;
using RecordCallback = Record(short);
using WrongCallback = long(int);

namespace api {
	long qualified(short input) {
		return input + 5;
	}
}

struct Receiver {
	LongCallback* function_pointer;
	Receiver(LongCallback&& function) : function_pointer(&function) {}
};

static_assert(__is_constructible(LongCallback&&, LongCallback&));
static_assert(__is_constructible(LongCallback&, LongCallback&&));
static_assert(__is_constructible(LongCallback&&, SafeCallback&));
static_assert(__is_constructible(LongCallback&, SafeCallback&));
static_assert(__is_constructible(RecordCallback&&, RecordCallback&));
static_assert(__is_constructible(Receiver, LongCallback&));
static_assert(!__is_constructible(SafeCallback&&, LongCallback&));
static_assert(!__is_constructible(LongCallback&&, WrongCallback&));
static_assert(!__is_constructible(LongCallback&&, LongCallback*));
static_assert(!__is_constructible(LongCallback*&&, LongCallback*&));
static_assert(!__is_constructible(long&&, long&));
static_assert(!__is_constructible(Record&&, Record&));

long widen(short input) {
	return input;
}

long safe(short input) noexcept {
	return input + 17;
}

Record makeRecord(short input) {
	return {input};
}

template <class Result>
Result invoke(Result (&&callback)(short), short input) {
	return callback(input);
}

Record invoke_record(Record (&&callback)(short), short input) {
	return callback(input);
}

int select(LongCallback&&) {
	return 1;
}

int select(...) {
	return 2;
}

int main() {
	if (invoke<long>(widen, -7) != -7 || invoke<long>(safe, 4) != 21) return 1;
	Record result = invoke_record(makeRecord, 31);
	if (result.value != 31 || select(widen) != 1 || select(&widen) != 2 || select(api::qualified) != 1) return 2;
	Receiver receiver(widen);
	if (receiver.function_pointer != &widen) return 3;
	return invoke<long>(widen, 23) == 23 ? 0 : 4;
}
