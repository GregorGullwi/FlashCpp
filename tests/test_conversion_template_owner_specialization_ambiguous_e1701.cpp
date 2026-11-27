// C++20 [over.ics.rank]/3.3: two user-defined conversion sequences are ordered
// by their second standard conversion sequence only when they contain the same
// conversion function or constructor. Both bases carry a clone of one conversion
// function template, and the defaulted template argument list is identical, but
// the declaring class specializations differ: BaseConversion<int>::operator int&
// is a different conversion function from
// BaseConversion<const int>::operator const int&. The exact int& binding and the
// exact const int& binding therefore may not be ordered against each other, so
// the call is ambiguous. Keying only the member pattern would merge them and
// wrongly rank the int& candidate ahead.
template<class T>
struct BaseConversion {
	template<class U = int>
	operator T&() const {
		static T value{37};
		return value;
	}
};

struct TwoBaseConversion : BaseConversion<int>, BaseConversion<const int> {};

int choose(int& value) {
	return value;
}

int choose(const int& value) {
	return value + 1;
}

int main() {
	return choose(TwoBaseConversion{});
}
