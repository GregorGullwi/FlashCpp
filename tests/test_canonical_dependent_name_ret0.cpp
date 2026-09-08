// Dependent member identifiers remain distinct until substitution; the owner
// parameter's spelling does not change the selected member type.
struct Payload { short value; };
struct Types {
	using first = char;
	using second = double;
	struct Nested { using item = Payload; };
};
template<class T> struct Members {
	typename T::first first;
	typename T::second second;
	typename T::Nested::item nested;
};
template<class Renamed> struct OtherMembers {
	typename Renamed::second second;
	typename Renamed::first first;
};
int main() {
	Members<Types> a{};
	OtherMembers<Types> b{};
	a.first = 2;
	a.second = 3.5;
	a.nested.value = 7;
	b.first = 4;
	b.second = 5.5;
	return a.first + static_cast<int>(a.second) + a.nested.value +
		b.first + static_cast<int>(b.second) - 21;
}
