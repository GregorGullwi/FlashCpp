struct Record {
	long long value;
};

template <class Value>
using Parameter = Value;

template <class Argument>
using Callback = long (*)(Parameter<Argument>);

template <class Element>
using ChainedCallback = Callback<Element>;

struct IntSelection { char value; };
struct ShortSelection { char value[2]; };
struct RecordSelection { char value[3]; };

IntSelection choose(long (*)(int)) { return {1}; }
ShortSelection choose(long (*)(short)) { return {{2, 3}}; }
RecordSelection choose(long (*)(Record)) { return {{4, 5, 6}}; }
long long choose(...) { return 7; }

template <class SourceType>
struct Source {
	operator ChainedCallback<SourceType>() const { return nullptr; }
};

static_assert(sizeof(decltype(choose(Source<int>{}))) == sizeof(IntSelection));
static_assert(sizeof(decltype(choose(Source<short>{}))) == sizeof(ShortSelection));
static_assert(sizeof(decltype(choose(Source<Record>{}))) == sizeof(RecordSelection));
static_assert(sizeof(decltype(choose(Source<double>{}))) == sizeof(long long));

int main() {
	return sizeof(decltype(choose(Source<int>{}))) == sizeof(IntSelection) &&
		sizeof(decltype(choose(Source<short>{}))) == sizeof(ShortSelection) &&
		sizeof(decltype(choose(Source<Record>{}))) == sizeof(RecordSelection) ? 0 : 1;
}
