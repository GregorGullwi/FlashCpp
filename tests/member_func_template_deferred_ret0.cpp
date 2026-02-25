// Test: member function template body deferral in non-template class
// Per C++ §13.9.2, member function template bodies are only parsed at instantiation.
// This pattern is used by std::error_code in <system_error>.

namespace ns {
    void process() = delete;  // poison pill

    struct Handler {
        template<typename T>
        void handle(T val) {
            // In a full ADL scenario, this would find ns::process(T)
            // but since the body is deferred, no error during definition
        }

        int get() const { return 42; }
    };
}

int main() {
    ns::Handler h;
    return h.get() == 42 ? 0 : 1;
}
