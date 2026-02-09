// Test: type alias resolution for out-of-line member definitions
// Validates fix for using Alias = Struct; Alias::member{};

struct MyStruct {
    struct Inner { int value; };
    static Inner data;
    static int count;
};

using Alias = MyStruct;

// Out-of-line definition using the type alias
Alias::Inner Alias::data{};
int Alias::count(42);

int main() {
    return Alias::count == 42 ? 0 : 1;
}
