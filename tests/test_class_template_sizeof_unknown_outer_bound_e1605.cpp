// [dcl.array]/1, [expr.sizeof]/2: a class-template static member that applies
// sizeof to long long[][3] must see the omitted outer bound, not the inner
// long long[3] extent that would make sizeof equal 24.
template <typename T>
struct Probe {
	static constexpr int value = static_cast<int>(sizeof(T));
};

int value = Probe<long long[][3]>::value;

int main() {
	return value;
}
