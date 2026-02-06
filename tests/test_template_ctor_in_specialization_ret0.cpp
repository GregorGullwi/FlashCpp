// Test: Template member constructor in full specialization
// Validates parsing of template<typename U> CtorName(U) in full specializations
template<typename T>
struct Alloc {};

template<>
struct Alloc<void> {
    template<typename U>
    Alloc(U x) noexcept { }
};

int main() {
    return 0;
}
