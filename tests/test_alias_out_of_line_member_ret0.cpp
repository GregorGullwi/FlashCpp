// Test: type alias resolution for out-of-line member definitions
// Validates fix for using Alias = Struct; Alias::member{value};

struct MyStruct {
    static int count;
    static int brace_val;
    static int assign_val;
    static int assign_cast_val;
	static int assign_func_val;
	static int get_func_val() { return 7; }
};

using Alias = MyStruct;

// Out-of-line definition using the type alias with parenthesized init
int Alias::count(42);
// Out-of-line definition using the type alias with brace init (non-zero)
int Alias::brace_val{7};
int Alias::assign_val = 7;
int Alias::assign_cast_val = static_cast<int>(7.0);
int Alias::assign_func_val = MyStruct::get_func_val();

int main() {
    if (Alias::count != 42) return 1;
    if (Alias::brace_val != 7) return 2;
    if (Alias::assign_val != 7) return 3;
    if (Alias::assign_cast_val != 7) return 4;
    if (Alias::assign_func_val != 7) return 5;
    return 0;
}
