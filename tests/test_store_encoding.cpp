struct Mixed { char c = 1; short s = 2; int i = 4; long long ll = 8; }; int main() { Mixed m; Mixed n = m; return n.c + n.s + n.i + n.ll; }
