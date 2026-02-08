// Test: Global-scope-qualified multi-segment types (::Namespace::Type)
// Verifies that type_name_token carries the full qualified name, not just
// the first or last segment.
namespace ns {
    struct Helper {
        int val;
    };
}

void init(::ns::Helper& h, int v) {
    h.val = v;
}

int main() {
    ns::Helper h;
    h.val = 0;
    init(h, 42);
    return h.val;
}
