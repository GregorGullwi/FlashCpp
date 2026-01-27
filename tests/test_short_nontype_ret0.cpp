template<short S>
struct ShortParam {
    short value = S;
};

// Expected return: 0
int main() {
    ShortParam<42> sp;
    return sp.value == 42 ? 0 : 1;
}