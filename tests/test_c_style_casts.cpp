// Test C-style casts

enum Color {
    Red,
    Green,
    Blue
};

enum class Status {
    Active,
    Inactive
};

// Test 1: Basic int to int cast
int test_int_to_int() {
    int x = 42;
    int y = (int)x;
    return y;
}

// Test 2: Enum to int cast
int test_enum_to_int() {
    Color c = Green;
    int x = (int)c;
    return x;
}

// Test 3: Int to enum cast
int test_int_to_enum() {
    int x = 2;
    Color c = (Color)x;
    return (int)c;
}

// Test 4: Enum class to int cast
int test_enum_class_to_int() {
    Status s = Status::Active;
    int x = (int)s;
    return x;
}

// Test 5: Nested casts
int test_nested_casts() {
    int x = 5;
    Color c = (Color)x;
    int y = (int)c;
    return y;
}

// Test 6: Cast in expression
int test_cast_in_expression() {
    Color c = Red;
    int result = (int)c + 10;
    return result;
}

// Test 7: Cast in switch
int test_cast_in_switch() {
    int x = 1;
    Color c = (Color)x;
    int result = 0;
    switch (c) {
        case Red:
            result = 1;
            break;
        case Green:
            result = 2;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

// Test 8: Multiple casts in one function
int test_multiple_casts() {
    int a = 1;
    int b = 2;
    Color c1 = (Color)a;
    Color c2 = (Color)b;
    int x = (int)c1;
    int y = (int)c2;
    return x + y;
}


int main() {
    return test_int_to_int();
}
