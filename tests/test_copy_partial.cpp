// Test constructor calls as expressions

extern "C" int printf(const char*, ...);

class Widget {
    int value_;
public:
    Widget(int v) {
        value_ = v;
        printf("Widget(%d) constructed\n", v);
    }
    
    Widget(Widget& other) {
        value_ = other.value_;
        printf("Widget copy from %d\n", other.value_);
    }
    
    Widget(Widget&& other) {
        value_ = other.value_;
        printf("Widget move from %d\n", other.value_);
    }
    
    int value() { return value_; }
};

void process_widget(Widget& w) {
    printf("process_widget(Widget&) value=%d\n", w.value());
}

void process_widget(Widget&& w) {
    printf("process_widget(Widget&&) value=%d\n", w.value());
}
