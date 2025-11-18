template <typename T>
struct PtrBox {
    const T* ptr;
};

// Deduction guide requiring pointer-aware template argument deduction
template <typename T>
PtrBox(T* value_ptr) -> PtrBox<T>;

int main() {
    int value = 21;
    PtrBox box{&value};
    value = 34;
    return *box.ptr - 34;
}
