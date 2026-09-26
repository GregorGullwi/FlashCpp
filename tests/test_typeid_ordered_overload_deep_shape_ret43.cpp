// Deep ordered pointer/array shapes are planned structurally without flattening the declarator.
int (*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*ordered_pointer)[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2])[2] = nullptr;
int choose_pointer(void*) { return 43; }
int choose_pointer(long) { return 7; }
int main() { return choose_pointer(ordered_pointer); }
