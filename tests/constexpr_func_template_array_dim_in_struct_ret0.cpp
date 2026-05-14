template<int N>
constexpr int getSize() { return N; }

struct MyStruct {
    int data[getSize<4>()];
};

int main() {
    MyStruct s;
    s.data[0] = 42;
    return s.data[0] - 42;  // Should return 0
}
