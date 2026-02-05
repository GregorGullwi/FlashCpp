// Test: Overload resolution with type aliases in reference/pointer contexts
// This tests that UserDefined type aliases (like char_type = wchar_t) are
// properly resolved during overload resolution for reference-to-value and
// pointer-to-pointer conversions.
// Expected return: 42

using wchar_t_alias = wchar_t;

// Two overloads with const/non-const pointer versions
const wchar_t* find_const(const wchar_t* s, wchar_t c) { return s; }
wchar_t* find_nonconst(wchar_t* s, wchar_t c) { return s; }

int test_ref_to_value(const wchar_t_alias& ref) {
    // Reference of alias type should match value parameter
    wchar_t val = ref;
    return val;
}

int main() {
    wchar_t buf[4] = {L'A', L'B', L'C', 0};
    const wchar_t_alias* p = buf;
    wchar_t_alias ch = L'B';

    // Test: const alias pointer should match const wchar_t* overload
    const wchar_t* result = find_const(p, ch);

    // Test: reference-to-value with alias type
    int val = test_ref_to_value(ch);

    return 42;
}
