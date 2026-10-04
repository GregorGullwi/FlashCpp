// Neither direct NTTP pattern is more specialized for Buffer<1, 1>, so
// target-typed address resolution must diagnose ambiguity.
template<int First, int Second>
struct Buffer {};

struct Owner {
	template<int Value>
	int select(Buffer<Value, 1>);

	template<int Value>
	int select(Buffer<1, Value>);
};

int main() {
	auto selected = static_cast<int (Owner::*)(Buffer<1, 1>)>(
		&Owner::select);
	return 0;
}
