// Test: friend declaration with qualified name should parse correctly
// The friend name should capture the full qualified name (e.g., "outer::Inner")
// not just the first segment ("outer")
namespace outer {
    struct Inner {
        int value;
    };
}

struct Host {
    friend outer::Inner;
    int x;
};

int main() {
    Host h;
    h.x = 42;
    return h.x;
}
