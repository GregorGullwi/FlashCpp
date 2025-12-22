// Template instantiation test - declarations with definitions
template<typename T>
T max(T a, T b) {
    if (a > b) return a;
    return b;
}

int main() {
    // This should trigger template instantiation
    int x = max(3, 5);
    return x;
}

