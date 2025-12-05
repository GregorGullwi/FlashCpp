class Widget { 
    int v; 
public: 
    Widget(int x) : v(x) {} 
};

void f(Widget&& w) {}

int main() {
    Widget w1(10);
    f((Widget&&)w1);
    return 0;
}
