// Test that rethrow (throw;) correctly propagates exceptions with type info preserved.
// This exercises __cxa_rethrow internally.

int g_caught_value = -1;

void rethrow_it() {
    try {
        throw 99;
    } catch (...) {
        throw;  // rethrow - uses __cxa_rethrow
    }
}

int main() {
    try {
        rethrow_it();
    } catch (int i) {
        g_caught_value = i;
    }
    return g_caught_value == 99 ? 0 : 1;
}
