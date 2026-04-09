template <typename T>
struct size_of {
	static constexpr int value = sizeof(T);
};

int main() {
	// sizeof(int&) should be sizeof(int) == 4, not 8 (pointer size)
	return size_of<int&>::value;
}
