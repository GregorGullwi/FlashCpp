// Test that sizeof...() works in template member functions
// Previously this caused a StringBuilder leak that triggered
// SIGABRT and crash handler deadlock

template<typename... Types>
struct TypePack {
    static constexpr int size() {
        return sizeof...(Types);
    }
};

int main() {
    return TypePack<int, float, double>::size();  // returns 3
}
