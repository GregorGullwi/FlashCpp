template <int (&R)[3]>
struct Ref {};

int values[4];

Ref<values> invalid;
