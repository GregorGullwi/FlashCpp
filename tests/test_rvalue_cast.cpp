int f(int&& rvalue) { return rvalue+ 1; }
int f(int& lvalue) { return lvalue + 1; }

int main() {
    int v(10);
    v = f((int&&)v);
    v = f((int&)v);
    return v;
}
