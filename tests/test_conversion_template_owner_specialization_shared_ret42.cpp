// C++20 [over.ics.rank]/3.3: the second standard conversion sequences order two
// user-defined sequences only when the selected conversion function is the same.
// Here both overloads reach the one conversion function template specialization
// declared in BaseConversion<int> through a deeper base path, and its defaulted
// template argument list is identical. The shared identity keeps the exact
// Target&& binding ahead of the const Target& binding; a path-dependent owner
// identity would instead make the call ambiguous.
struct Target {
	int value;
};

template<class T>
struct BaseConversion {
	template<class U = int>
	operator Target() const {
		return Target{42};
	}
};

struct IntermediateConversion : BaseConversion<int> {};

struct FinalConversion : IntermediateConversion {};

int select(Target&& target) {
	return target.value;
}

int select(const Target& target) {
	return target.value + 100;
}

int main() {
	return select(FinalConversion{}) == 42 ? 42 : 1;
}
