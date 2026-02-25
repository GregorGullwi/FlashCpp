// Test: template forward declaration followed by empty-body full definition
// An empty body is a valid full definition (not a forward declaration).

template<typename T>
struct Tag;  // forward declaration

template<typename T>
struct Tag {};  // full definition with empty body

int main() {
    Tag<int> t;
    (void)t;
    return 0;
}
