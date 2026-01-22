// Test friend defaulted comparison operator
// Pattern from bits/exception_ptr.h
struct Test {
    int x;
    friend bool operator==(const Test&, const Test&) noexcept = default;
};

int main() { return 0; }
