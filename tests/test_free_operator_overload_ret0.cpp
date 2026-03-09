// Test free-function binary operator overloads (C++20 [over.match.oper])
// operator+(Number, Number) as a free function (not a member)

struct Number {
    int value;
    Number(int v) : value(v) {}
};

// Free-function operator+
Number operator+(const Number& a, const Number& b) {
    return Number(a.value + b.value);
}

// Free-function operator==
bool operator==(const Number& a, const Number& b) {
    return a.value == b.value;
}

int main() {
    Number x(20);
    Number y(22);
    Number z = x + y;       // Should call free operator+
    if (z == Number(42)) {  // Should call free operator==
        return 0;
    }
    return 1;
}
