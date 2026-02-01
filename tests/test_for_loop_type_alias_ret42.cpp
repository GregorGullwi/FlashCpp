// Test for-loop initialization with type alias.

using size_t = unsigned long long;

int main() {
    int sum = 0;
    for (size_t i = 0; i < 6; ++i) {
        sum += 7;
    }
    return sum;
}
