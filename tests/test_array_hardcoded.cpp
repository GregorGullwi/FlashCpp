template<int N>
struct Array {
    int data[5];  // Hardcoded for now
};

int main() {
    Array<5> arr;
    arr.data[0] = 42;
    return arr.data[0];
}
