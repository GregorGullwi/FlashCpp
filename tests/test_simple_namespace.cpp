namespace A {
    int func() { return 42; }
}
int main() {
    int x = A::func();
    return x;
}
