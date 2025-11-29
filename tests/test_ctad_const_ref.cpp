template <typename T>
struct RefBox {
    const T& ref;
};

// Deduction guide that binds through a const-lvalue reference
template <typename T>
RefBox(const T& value) -> RefBox<T>;

int main() {
    int value = 9;
    RefBox box{value};
    value = 14;
    return box.ref - 9;
}
