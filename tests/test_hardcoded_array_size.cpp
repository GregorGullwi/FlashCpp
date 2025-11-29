template<int N>
struct Array {
    int data[10];  // Hardcoded size to bypass the bug
};

int main() {
    Array<5> arr;
    return 0;
}
