// Regression test: calling a member function on a temporary (brace-init or paren-init)
// should produce correct results. Previously triggered codegen error:
//   "Register call argument marked pass-by-address is not addressable"
// because ConstructorCallNode was not handled in the object-expression dispatch of
// generateMemberFunctionCallIr, and the struct's member function bodies were not
// generated because try_instantiate_class_template's result was discarded.

int temporary_method_calls;
int temporary_method_sum;

template <typename... Args>
struct Counter {
	int size() {
		++temporary_method_calls;
		temporary_method_sum += static_cast<int>(sizeof...(Args));
		return static_cast<int>(sizeof...(Args));
	}
};

struct Box {
	int value;
	explicit Box(int v) : value(v) {}
	int get() const { return value; }
	int visit() const {
		++temporary_method_calls;
		temporary_method_sum += value;
		return value;
	}
};

namespace sample {
using Box = ::Box;
}

using BoxAlias = Box;

template <typename T>
struct TemplatedBox {
	T value;
	explicit TemplatedBox(T v) : value(v) {}
	int visit() const {
		++temporary_method_calls;
		temporary_method_sum += static_cast<int>(value);
		return static_cast<int>(value);
	}
};

struct EmptyReceiver {
	void visit() const {
		++temporary_method_calls;
		temporary_method_sum += 3;
	}
};

struct NestedValue {
	int value;
	short delta;
	int get() const { return value; }
};

struct AggregateReceiver {
	NestedValue inner;
	int numbers[2];
	short shorts[2];
	constexpr int sum() const { return inner.value + inner.delta + numbers[0] + numbers[1] + shorts[0] + shorts[1]; }
};

using AggregateAlias = AggregateReceiver;

struct AggregateTypes {
	using Type = AggregateReceiver;
};

static_assert(AggregateAlias{.inner = {10, 2}, .shorts = {5, 6}}.sum() == 23);

int default_initializer_calls;
int nextDefaultValue() { ++default_initializer_calls; return 7; }

struct DefaultReceiver {
	int value = nextDefaultValue();
	int tail = 9;
	int sum() const { return value + tail; }
};

using DefaultAlias = DefaultReceiver;

struct ReceiverBase {
	int first;
	short second;
};

struct DerivedReceiver : ReceiverBase {
	int value;
	int sum() const { return first + second + value; }
};

struct ConstructedBase {
	int value;
	ConstructedBase(int v) : value(v) {}
	ConstructedBase(const ConstructedBase&) = delete;
};

struct DerivedConstructedReceiver : ConstructedBase {
	int extra;
	int sum() const { return value + extra; }
};

int member_constructor_calls;
struct ConstructedMember {
	int value;
	ConstructedMember() : value(21) { ++member_constructor_calls; }
};

struct MemberReceiver {
	ConstructedMember members[2];
	int sum() const { return members[0].value + members[1].value; }
};

template <typename T>
struct AggregateTemplate {
	T values[2];
	T sum() const { return values[0] + values[1]; }
};

template <typename T>
void visitDefaultTemporary() {
	T{}.visit();
}

Box makeBox(int v) { return Box(v); }

int main() {
 // Brace-init temporary: Template<T>{}
	if (Counter<int, int, int>{}.size() != 3)
		return 1;
	if (Counter<int>{}.size() != 1)
		return 2;
	if (Counter<int, int>{}.size() != 2)
		return 3;

 // Paren-init temporary: Template<T>()
	if (Counter<int, int, int>().size() != 3)
		return 4;

 // Function-return temporary
	if (makeBox(42).get() != 42)
		return 5;

	// These local declarations must remain declarations while temporary calls are statements.
	Box local_box(70);
	Box local_brace{71};
	Box(local_parenthesized){72};
	if (local_box.get() != 70 || local_brace.get() != 71 || local_parenthesized.get() != 72)
		return 6;
	if (AggregateReceiver{{10, 2}, {3, 4}, {5, 6}}.sum() != 30)
		return 9;
	if (AggregateAlias{{10, 2}, {3, 4}, {5, 6}}.sum() != 30)
		return 10;
	if (AggregateTypes::Type{{10, 2}, {3, 4}, {5, 6}}.sum() != 30)
		return 11;
	if (AggregateReceiver{.shorts = {5, 6}}.sum() != 11)
		return 12;
	if (AggregateAlias{.inner = {10, 2}, .shorts = {5, 6}}.sum() != 23)
		return 13;
	if (DefaultReceiver{2}.sum() != 11 || default_initializer_calls != 0)
		return 14;
	if (DefaultAlias{}.sum() != 16 || default_initializer_calls != 1)
		return 15;
	if (DerivedReceiver{{4, 5}, 6}.sum() != 15 || DerivedReceiver{}.sum() != 0)
		return 16;
	if (DerivedConstructedReceiver{{20}, 22}.sum() != 42)
		return 20;
	if (MemberReceiver{}.sum() != 42 || member_constructor_calls != 2)
		return 17;
	if (AggregateTemplate<short>{{20, 22}}.sum() != 42)
		return 18;

	if (NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{
		NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{
		NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{
		NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{NestedValue{
		NestedValue{NestedValue{NestedValue{NestedValue{1,0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}
		.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}
		.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}
		.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get(),0}.get() != 1)
		return 19;

	temporary_method_calls = 0;
	temporary_method_sum = 0;
	Box{10}.visit();
	Box(11).visit();
	sample::Box{12}.visit();
	BoxAlias{13}.visit();
	Counter<int, long>{}.size();
	Counter<char>().size();
	TemplatedBox<int>{14}.visit();
	TemplatedBox<long>(15).visit();
	EmptyReceiver{}.visit();
	visitDefaultTemporary<EmptyReceiver>();
	[[likely]] EmptyReceiver{}.visit();
	if (temporary_method_calls != 11 || temporary_method_sum != 87)
		return 7;

	return 0;
}
