class Widget {
    int value_;
public:
    Widget(int v) { value_ = v; }
    int value() { return value_; }
};

void process_widget(Widget&& w) {
}

int main() {
    Widget w1(10);
    // process_widget((Widget&&)w1);  // Commented out
    return 0;
}
