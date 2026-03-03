template<typename T>
struct Pair {
	T first;
	T second;
};

using Int2 = int[2];

int main() {
	Pair<Int2> v = {};
	return (sizeof(v) == sizeof(Int2) * 2) ? 0 : 1;
}
