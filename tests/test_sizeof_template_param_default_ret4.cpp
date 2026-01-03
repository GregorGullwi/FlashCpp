// Test: non-type template parameter default
template<typename T, int N = 4>
struct test {
	char data[N];
};

int main() {
	test<int> t;  // N uses default value 4
	return sizeof(t.data);  // Should return 4
}
