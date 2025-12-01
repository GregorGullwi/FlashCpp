template<typename T>
T max(T a, T b);

template<>
int max<int>(int a, int b) {
    return a > b ? a : b;
}

int main() {
    return max<int>(3, 5);
}
