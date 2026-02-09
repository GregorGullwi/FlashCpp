// Test: type alias resolution for out-of-line member definitions
// Validates fix for using Alias = Struct; Alias::member{value};

struct MyStruct {
    static int count;
    static int brace_val;
};

using Alias = MyStruct;

// Out-of-line definition using the type alias with parenthesized init
int Alias::count(42);
// Out-of-line definition using the type alias with brace init (non-zero)
int Alias::brace_val{7};

int main() {
    if (Alias::count != 42) return 1;
    if (Alias::brace_val != 7) return 2;
    return 0;
}
