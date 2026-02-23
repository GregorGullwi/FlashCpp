struct S {
int a : 3;
int b : 5;
};

S g = {2, 4};

int main() {
g.a = 1;
g.b = 3;
// g.b should be 3; g.a should be 1; g.b - g.a == 2
return g.b - g.a;
}
