struct Data { int value = 42; }; int main() { Data a; Data b = a; Data c = b; return c.value; }
