struct Record { long long payload; short tag; };
template<class Content> using Parameter = Content;
template<class Result, class Input> using Callback = Result (*)(Parameter<Input>) noexcept;
template<class Output, class Value> using Forwarded = Callback<Output, Value>;
template<class Yield, class Element> using Nested = Forwarded<Yield, Element>;

template<class First, class Second> using Swapped = Callback<Second, First>;

template<class First, class Second>
struct SwappedSource {
	operator Swapped<Second, First>() const { return nullptr; }
};

template<class ReturnValue, class Argument>
struct Source {
	operator Nested<ReturnValue, Argument>() const { return nullptr; }
};

int consume(long (*callback)(short)) { return callback == nullptr ? 0 : 1; }
int consumeRecord(Record (*callback)(int)) { return callback == nullptr ? 0 : 1; }

int main() {
	return consume(Source<long, short>{}) == 0 && consumeRecord(Source<Record, int>{}) == 0 && consume(SwappedSource<long, short>{}) == 0 ? 0 : 1;
}
