struct Base {
int& ref;

Base(int& value) : ref(value) {}
};

struct Derived : Base {
Derived(int& value) : Base(value) {}

void assignBase(int value) {
Base::ref = value;
}
};

struct Holder {
int value;
int& ref;

Holder(int& alias) : value(0), ref(alias) {}

void assign(int new_value) {
Holder::ref = new_value;
}
};

int main() {
int a = 10;
Holder holder(a);
holder.assign(21);
if (a != 21) {
return 1;
}

int b = 35;
Derived derived(b);
derived.assignBase(42);
return b - 42;
}
