// Test for UB - single overload
extern "C" int printf(const char*, ...);

class Widget {
    int value_;
public:
    Widget(int v) { value_ = v; }
    int value() { return value_; }
};

void process_widget(Widget&& w) {
    printf("value=%d\n", w.value());
}

int main() {
    process_widget(Widget(42));
    return 0;
}
