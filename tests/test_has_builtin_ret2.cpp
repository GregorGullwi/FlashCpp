// Test __has_builtin preprocessor macro support
// This test verifies that FlashCpp correctly implements __has_builtin()
// which is required for standard library headers like <type_traits>

#if __has_builtin(__is_void)
int has_is_void = 1;
#else
int has_is_void = 0;
#endif

#if __has_builtin(__is_same)
int has_is_same = 1;
#else
int has_is_same = 0;
#endif

#if __has_builtin(__not_a_real_builtin)
int has_fake = 1;
#else
int has_fake = 0;
#endif

int main() {
    // has_is_void and has_is_same should be 1, has_fake should be 0
    // So the return value should be 1 + 1 + 0 = 2
    return has_is_void + has_is_same + has_fake;
}
