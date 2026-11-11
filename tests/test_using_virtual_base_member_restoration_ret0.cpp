struct Base { int value; };
struct Derived : virtual Base { using Base::value; };
int main() { Derived value{}; value.value = 7; return value.value == 7 ? 0 : 1; }
