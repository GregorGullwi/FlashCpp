template<typename T>
struct Base {
	char value;
};

template<typename T>
struct Derived : Base<T> {
	char derived_value;
};

struct Source {
	operator Derived<int>() const { return {}; }
};

struct BaseSelection {
	char value;
};

struct EllipsisSelection {
	char value[2];
};

struct UnrelatedSelection {
	char value[3];
};

struct PlainBase {
	char value;
};

struct PlainDerived : PlainBase {
	char derived_value;
};

struct PlainSource {
	operator PlainDerived() const { return {}; }
};

BaseSelection choose(Base<int>);
BaseSelection choose(PlainBase);
EllipsisSelection choose(...);
UnrelatedSelection choose_unrelated(Base<long>);
EllipsisSelection choose_unrelated(...);

static_assert(__is_same(decltype(choose(Source{})), BaseSelection));
static_assert(__is_same(decltype(choose(PlainSource{})), BaseSelection));
static_assert(__is_same(decltype(choose_unrelated(Source{})), EllipsisSelection));

int main() {
	return 0;
}
