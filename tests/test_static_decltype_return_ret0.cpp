// Test static decltype return type
// This was blocking <type_traits> at line 2372

struct TestStruct {
    template<typename T, typename U>
    static int test_func(int);

    template<typename T, typename U>
    static decltype(test_func<T, U>(0)) wrapper(int x) {
        return test_func<T, U>(x);
    }
};

int main() {
    return 0;
}
