// Test using declarations with namespace scope operator

#include <cstddef>
#include <cstdio>

namespace std {
    using ::size_t;
    using ::FILE;
}

int main() {
    return 0;
}
