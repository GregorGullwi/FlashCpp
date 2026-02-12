// Test that >> is correctly handled as closing angle brackets in template arguments
template<typename T>
struct vector {};

template<typename T>
struct hash {};

template<typename T>
struct allocator {};

// Use nested template arguments with >>
using TestType = hash<vector<int>>;

int main() {
    return 0;
}
