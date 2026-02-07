// Test: Template member constructor in full specialization
// Validates parsing of template<typename U> CtorName(U) in full specializations
template<typename T>
struct Alloc {
    T data;
};

template<>
struct Alloc<void> {
    double data;
    template<typename U>
    Alloc(U x) noexcept { }
};

int main() {
    return sizeof(Alloc<int>);  // 4 (sizeof int)
}
