// Test basic enum
enum Color {
    Red,
    Green,
    Blue
};

int test_basic_enum() {
    Color c = Red;
    return c;  // Should return 0
}

// Test enum with explicit values
enum Status {
    Success = 0,
    Warning = 1,
    Error = 2,
    Critical = 10
};

int test_enum_explicit_values() {
    Status s = Critical;
    return s;  // Should return 10
}

// Test enum class (scoped enum)
enum class Direction {
    North,
    South,
    East,
    West
};

int test_enum_class() {
    Direction d = Direction::North;
    return static_cast<int>(d);  // Should return 0
}

// Test enum class with explicit values
enum class Priority : int {
    Low = 1,
    Medium = 5,
    High = 10
};

int test_enum_class_explicit() {
    Priority p = Priority::High;
    return static_cast<int>(p);  // Should return 10
}

// Test enum with underlying type
enum class Byte : unsigned char {
    Min = 0,
    Max = 255
};

int test_enum_underlying_type() {
    Byte b = Byte::Max;
    return static_cast<int>(b);  // Should return 255
}

// Test unscoped enum values are accessible without qualification
enum Result {
    Ok = 0,
    Fail = 1
};

int test_unscoped_access() {
    int x = Ok;     // Should work without Result::Ok
    int y = Fail;   // Should work without Result::Fail
    return x + y;   // Should return 1
}

// Test enum in variable declaration
enum Level {
    Beginner,
    Intermediate,
    Advanced
};

int test_enum_variable() {
    Level lvl = Intermediate;
    return lvl;  // Should return 1
}

// Test multiple enumerators with gaps
enum Flags {
    None = 0,
    Read = 1,
    Write = 2,
    Execute = 4,
    All = 7
};

int test_enum_flags() {
    Flags f = All;
    return f;  // Should return 7
}

int main() {
    int result = 0;
    
    result += test_basic_enum();              // 0
    result += test_enum_explicit_values();    // 10
    result += test_enum_class();              // 0
    result += test_enum_class_explicit();     // 10
    result += test_enum_underlying_type();    // 255
    result += test_unscoped_access();         // 1
    result += test_enum_variable();           // 1
    result += test_enum_flags();              // 7
    
    return result;  // Should return 284
}

