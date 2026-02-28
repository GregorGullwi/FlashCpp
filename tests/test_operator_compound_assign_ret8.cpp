// Test compound assignment operator overloading (+=, -=, *=, /=, %=, &=, |=, ^=)
struct Number {
    int value;

    Number& operator+=(const Number& other) {
        value += other.value;
        return *this;
    }
    Number& operator-=(const Number& other) {
        value -= other.value;
        return *this;
    }
    Number& operator*=(const Number& other) {
        value *= other.value;
        return *this;
    }
    Number& operator/=(const Number& other) {
        value /= other.value;
        return *this;
    }
    Number& operator%=(const Number& other) {
        value %= other.value;
        return *this;
    }
    Number& operator&=(const Number& other) {
        value &= other.value;
        return *this;
    }
    Number& operator|=(const Number& other) {
        value |= other.value;
        return *this;
    }
    Number& operator^=(const Number& other) {
        value ^= other.value;
        return *this;
    }
};

int main() {
    Number a{100};
    Number b{7};

    a += b; // 100 + 7 = 107
    int r1 = a.value;

    a -= b; // 107 - 7 = 100
    int r2 = a.value;

    a *= b; // 100 * 7 = 700
    int r3 = a.value;

    a /= b; // 700 / 7 = 100
    int r4 = a.value;

    a %= b; // 100 % 7 = 2
    int r5 = a.value;

    a |= b; // 2 | 7 = 7
    int r6 = a.value;

    a &= b; // 7 & 7 = 7
    int r7 = a.value;

    a ^= b; // 7 ^ 7 = 0
    int r8 = a.value;

    // Count how many operations gave the correct result
    int result = (r1 == 107) + (r2 == 100) + (r3 == 700) + (r4 == 100) +
                 (r5 == 2) + (r6 == 7) + (r7 == 7) + (r8 == 0);
    return result; // 8 if all correct
}
