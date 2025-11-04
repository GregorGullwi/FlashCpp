// Template instantiation test - declarations only
template<typename T>
T max(T a, T b);

int main() {
    // This should trigger template instantiation
    int x = max(3, 5);
    return x;
}

