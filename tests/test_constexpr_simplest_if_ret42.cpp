// Simplest possible if-return test
constexpr int simple_if(bool b) {
    if (b) {
        return 1;
    }
    return 0;
}

static_assert(simple_if(true) == 1, "simple_if true failed");
static_assert(simple_if(false) == 0, "simple_if false failed");

int main() {
    return 42;
}
