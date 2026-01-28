// Test constructor call before cast

class Widget {
    int value_;
public:
    Widget(int v) {
        value_ = v;
    }
    
    int value() { return value_; }
};

void process_widget(Widget&& w) {}

int main() {
    Widget w1(10);
    process_widget(Widget(42));    // Constructor call first
    Widget&& r = (Widget&&)w1;  // Cast second
    return 0;
}
