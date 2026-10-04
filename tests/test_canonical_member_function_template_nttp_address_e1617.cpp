// The direct NTTP pattern is more specialized than the generic Type overload;
// access checking must diagnose the selected private declaration.
template<int Value>
struct Buffer {};

struct Owner {
public:
	template<class Type>
	int select(Type);

private:
	template<int Value>
	int select(Buffer<Value>);
};

int main() {
	auto selected = static_cast<int (Owner::*)(Buffer<5>)>(&Owner::select);
	return 0;
}
