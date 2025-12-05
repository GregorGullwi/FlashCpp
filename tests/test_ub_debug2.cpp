// Test for UB - two overloads
extern "C" int printf(const char*, ...);

class Widget {
    int value_;
public:
    Widget(int v) { value_ = v; }
    int value() { return value_; }
};

void process_widget(Widget& w) {
    printf("lvalue: value=%d\n", w.value());
}

void process_widget(Widget&& w) {
    printf("rvalue: value=%d\n", w.value());
}

int main() {
    process_widget(Widget(42));
    return 0;
}
