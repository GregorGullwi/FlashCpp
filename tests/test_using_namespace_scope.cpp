// Test using declarations with namespace scope operator

typedef unsigned long size_t;
struct __flashcpp_file;
typedef __flashcpp_file FILE;

namespace std {
    using ::size_t;
    using ::FILE;
}

int main() {
    FILE* f = nullptr;
    size_t n = 0;
    return (f == nullptr && n == 0) ? 0 : 1;
}
