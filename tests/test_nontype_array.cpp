// Test non-type template parameter with array
template<int N>
struct Array {
    int data[N];
};

int main() {
    Array<5> arr;
    return 0;
}