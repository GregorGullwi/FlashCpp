namespace meta {

template <typename T, typename U = int>
struct box {
	static constexpr int value = sizeof(U) == 4 ? 42 : 0;
};

}

int main() {
	return meta::box<char>::value;
}
