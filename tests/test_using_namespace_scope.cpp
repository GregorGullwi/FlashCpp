// Test using declarations with namespace scope operator

namespace std {
    using ::size_t;
    using ::FILE;
}

int main() {
    return 0;
}
