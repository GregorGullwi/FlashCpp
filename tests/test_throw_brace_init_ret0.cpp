// Test throw with brace initialization
// Pattern from <any> header
class bad_any_cast {
public:
    virtual const char* what() const noexcept { return "bad"; }
};

inline void throw_it() {
    throw bad_any_cast{};
}

int main() { return 0; }
