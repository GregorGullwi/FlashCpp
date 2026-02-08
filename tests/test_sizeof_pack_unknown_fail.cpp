// Test that sizeof... with an unknown pack name fails compilation.
// The identifier 'unknown_pack' is not a parameter pack, so sizeof...(unknown_pack)
// should produce a compile error.

template<typename T>
int bad_sizeof() {
    if constexpr (sizeof...(unknown_pack) == 0) {
        return 0;
    } else {
        return 1;
    }
}

int main() {
    return bad_sizeof<int>();
}
