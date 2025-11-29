int main() {
    bool boolean_expression = true;
    int* null_obj = nullptr;
    int* non_null_obj = new int(42);

    // Test double negation with logical OR
    if (!!boolean_expression || !!null_obj) {
        return 1;  // Should return 1 since boolean_expression is true
    }

    // Test with null object
    if (!!null_obj || !!non_null_obj) {
        return 2;  // Should return 2 since non_null_obj is not null
    }

    // Test with both false
    boolean_expression = false;
    null_obj = nullptr;
    if (!!boolean_expression || !!null_obj) {
        return 3;  // Should not reach here
    }

    return 0;  // All tests passed
}