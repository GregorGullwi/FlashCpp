// Test postfix const qualifier with reference: Type const&
struct NonesuchBase {};

struct Nonesuch : NonesuchBase {
    ~Nonesuch() = delete;
    Nonesuch(Nonesuch const&) = delete;
    void operator=(Nonesuch const&) = delete;
};

int main() {
    return 0;
}
