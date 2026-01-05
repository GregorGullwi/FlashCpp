// Test __has_trivial_destructor intrinsic
// Should return 1 (true) for int which has a trivial destructor

int main() {
    // Test __has_trivial_destructor with int (should be true)
    if (__has_trivial_destructor(int)) {
        return 1;
    }
    return 0;
}
