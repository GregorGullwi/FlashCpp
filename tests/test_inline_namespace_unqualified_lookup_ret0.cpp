// Regression test: unqualified lookup inside a reopened parent namespace must
// find symbols that live in an inline child namespace.

namespace ns {
inline namespace v1 {
int helper() { return 7; }
}
}

namespace ns {
// Reopen ns (without repeating 'inline').  Unqualified 'helper' must
// still resolve because v1 is inline in ns.
int via_helper() { return helper(); }
}

int main() {
if (ns::via_helper() != 7) return 1;
return 0;
}
