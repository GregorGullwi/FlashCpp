template <int (&R)[3]>
struct Ref {};

int value;

Ref<value> invalid;
