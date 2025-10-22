// Test switch statements with various patterns

enum Color {
    Red,
    Green,
    Blue,
    Yellow
};

enum class Status {
    Active,
    Inactive,
    Pending
};

// Test 1: Basic switch with break
int test_basic_switch(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result = 10;
            break;
        case 2:
            result = 20;
            break;
        case 3:
            result = 30;
            break;
        default:
            result = 99;
            break;
    }
    return result;
}

// Test 2: Switch with fall-through
int test_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 1:
        case 2:
        case 3:
            result = 123;
            break;
        case 4:
        case 5:
            result = 45;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

// Test 3: Switch with char values
int test_char_switch(char c) {
    int result = 0;
    switch (c) {
        case 'a':
            result = 1;
            break;
        case 'b':
            result = 2;
            break;
        case 'c':
            result = 3;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

// Test 4: Switch with enum values
int test_enum_switch(Color c) {
    int result = 0;
    switch (c) {
        case Red:
            result = 1;
            break;
        case Green:
            result = 2;
            break;
        case Blue:
            result = 3;
            break;
        case Yellow:
            result = 4;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

// Test 5: Switch with enum class (using static_cast)
int test_enum_class_switch(Status s) {
    int result = 0;
    switch (s) {
        case Status::Active:
            result = 100;
            break;
        case Status::Inactive:
            result = 200;
            break;
        case Status::Pending:
            result = 300;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

// Test 6: Switch with enum fall-through
int test_enum_fallthrough(Color c) {
    int result = 0;
    switch (c) {
        case Red:
        case Yellow:
            result = 10;
            break;
        case Green:
        case Blue:
            result = 20;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

// Test 7: C-style cast with enum to int
int test_c_cast_enum() {
    Color c = Red;
    int x = (int)c;
    return x;
}

// Test 8: C-style cast in switch
int test_c_cast_in_switch() {
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

// Test 9: static_cast with enum (for comparison)
int test_static_cast_enum() {
    Color c = Red;
    int x = static_cast<int>(c);
    return x;
}

