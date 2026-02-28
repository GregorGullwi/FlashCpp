// Test operator overloading in template structs
// Covers compound assignment, comparison, and different type parameters
template<typename T>
struct Wrapper {
    T value;

    Wrapper& operator+=(const Wrapper& other) {
        value += other.value;
        return *this;
    }

    Wrapper& operator-=(const Wrapper& other) {
        value -= other.value;
        return *this;
    }

    bool operator==(const Wrapper& other) {
        return value == other.value;
    }

    bool operator!=(const Wrapper& other) {
        return value != other.value;
    }

    bool operator<(const Wrapper& other) {
        return value < other.value;
    }
};

int main() {
    Wrapper<int> a{100};
    Wrapper<int> b{5};

    int result = 0;

    // Test compound assignment on template
    a += b; // 105
    if (a.value == 105) result += 10;

    a -= b; // 100
    if (a.value == 100) result += 10;

    // Test comparison on template
    Wrapper<int> c{100};
    if (a == c) result += 5;
    if (a != b) result += 5;
    if (b < a) result += 5;

    // Test with different type parameter
    Wrapper<short> d{3};
    Wrapper<short> e{7};
    d += e;
    if (d.value == 10) result += 5;

    return result; // 40
}
