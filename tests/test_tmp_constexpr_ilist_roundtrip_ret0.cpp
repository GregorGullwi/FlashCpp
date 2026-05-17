namespace std {
template <typename T>
class initializer_list {
public:
    const T* first_;
    const T* last_;

    constexpr initializer_list(const T* f, const T* l) noexcept : first_(f), last_(l) {}
    constexpr const T* begin() const noexcept { return first_; }
    constexpr const T* end() const noexcept { return last_; }
};
}

struct Rec { int x; };

struct PtrBox {
    const Rec* p;
    constexpr PtrBox(const Rec* q) : p(q) {}
    constexpr const Rec* get() const { return p; }
};

constexpr int firstX(std::initializer_list<Rec> values) {
    PtrBox b(values.begin());
    const Rec* p = b.get();
    return p->x;
}

int main() {
    constexpr int v = firstX({{9}, {10}});
    static_assert(v == 9);
    return v - 9;
}
