// Test non-type template parameter
template<int N>
int multiply_by_n(int x) {
    return x * N;
}

int main() {
    return multiply_by_n<2>(5);
}

