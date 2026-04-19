template<typename T>
const T& (max)(const T& left, const T& right) {
    return left > right ? left : right;
}

template<typename T>
const T& (min)(const T& left, const T& right) {
    return left < right ? left : right;
}

int main() {
    int a = max(1, 2);
    int b = min(1, 2);
    return a + b;
}