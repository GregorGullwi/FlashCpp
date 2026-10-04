// A leading fixed parameter makes this pack overload more specialized for
// the target member-function pointer type, so address resolution selects it
// and rejects access to its private declaration.
struct Marker {};

struct Owner {
public:
	template<class... Types>
	int select(Types...);

private:
	template<class First, class... Rest>
	int select(First, Rest...);
};

int main() {
	auto selected = static_cast<int (Owner::*)(int, Marker, char)>(
		&Owner::select);
	return 0;
}
