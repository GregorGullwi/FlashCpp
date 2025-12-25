// Test: Phase 2 - Unified qualified identifier parser validation
// Simple test to verify the new unified parser works

namespace outer {
    namespace inner {
        template<typename T>
        int getValue() {
            return 15;
        }
    }
}

int main() {
    return outer::inner::getValue<int>();
}
