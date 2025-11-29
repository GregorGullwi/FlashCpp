struct MyStruct {
    static const int value = 42;
};

int main() {
    return MyStruct::value;
}