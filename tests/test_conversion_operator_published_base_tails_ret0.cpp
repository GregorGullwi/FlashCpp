template<class T> struct Base { T value; };
template<class T> struct Derived : Base<T> { short tag; };
template<class T> struct Virtual : virtual Base<T> {};

struct Source {
	operator Derived<long long>() const {
		Derived<long long> result{};
		result.value = 42;
		return result;
	}
};
struct ReferenceSource {
	Derived<short> object;
	operator Derived<short>&() { return object; }
};
struct ValueSource {
	operator Derived<short>() const {
		Derived<short> result{};
		result.value = 42;
		return result;
	}
};
struct VirtualSource { operator Virtual<long long>() const { return {}; } };
struct Selected { char marker[3]; };
struct Rejected { char marker[7]; };
Selected virtual_value(Base<long long>);
Selected virtual_reference(const Base<long long>&);
Rejected virtual_value(...);
Rejected virtual_reference(...);
Selected unrelated(const Base<short>&);
Rejected unrelated(...);
static_assert(sizeof(decltype(virtual_value(VirtualSource{}))) == sizeof(Selected));
static_assert(sizeof(decltype(virtual_reference(VirtualSource{}))) == sizeof(Selected));
static_assert(sizeof(decltype(unrelated(Source{}))) == sizeof(Rejected));

short read_value(Base<short> object) { return object.value; }
long long read_reference(const Base<long long>& object) { return object.value; }
long long read_rvalue(Base<long long>&& object) { return object.value; }
void write_reference(Base<short>& object) { object.value = 7; }

int main() {
	const Source source{};
	ReferenceSource reference_source{};
	write_reference(reference_source);
	int result = read_value(ValueSource{}) == 42 ? 0 : 1;
	result |= read_reference(source) == 42 ? 0 : 2;
	result |= read_rvalue(source) == 42 ? 0 : 4;
	result |= reference_source.object.value == 7 ? 0 : 8;
	return result;
}
