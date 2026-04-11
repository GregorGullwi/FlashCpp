namespace meta {

template <typename T, typename U = int>
struct box {
	static constexpr int value = sizeof(U) == 4 ? 42 : 0;
};

template <typename T>
using alias = box<T>;

}

int main() {
	return meta::alias<char>().value;
}
