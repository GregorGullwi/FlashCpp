// Interleaved pointer and array parameters must have distinct external symbols.
void shape(int (*p)[3]) {}
void shape(int (*(*p)[3])[4]) {}

void pointer_cv(int* const (*p)[3]) {}
void pointer_cv(int (*const p)[3][4]) {}
