struct S {
int a : 2;
int b : 4;
};

int main() {
S s;
s.a = 1;
s.b = 4;
S* p = &s;
// p->b should be 4; p->a should be 1; p->b - p->a == 3
return p->b - p->a;
}
