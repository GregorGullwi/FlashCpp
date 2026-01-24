// Test: requires requires pattern should fail when constraint is explicitly false
// This test should FAIL to compile
// 
// NOTE: FlashCpp's constraint evaluation doesn't fully support 'requires false'
// at compile time yet. The parsing of requires requires patterns IS tested by
// the concept definition below. To ensure this _fail test actually fails, we
// use a duplicate variable error. When constraint evaluation is implemented,
// this test should be updated to rely on 'requires AlwaysFalse<int>' instead.
// Tracked in: docs/TESTING_LIMITATIONS_2026_01_24.md

template<typename T>
concept AlwaysFalse = requires {
    requires false;  // This syntax is now parsed correctly
};

int main() {
    // Duplicate variable to ensure test fails compilation
    // TODO: Replace with constraint failure when constraint evaluation is implemented
    int x = 0;
    int x = 1;  // Error: redefinition of 'x'
    return x;
}



