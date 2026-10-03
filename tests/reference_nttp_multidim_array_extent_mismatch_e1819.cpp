template <int (&R)[2][3]>
struct MatrixRef {};

int values[2][4];

MatrixRef<values> invalid;
