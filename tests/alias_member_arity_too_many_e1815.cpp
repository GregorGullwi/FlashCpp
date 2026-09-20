struct Owner { template <class T> using I = T; };
Owner::I<int, int> value = 0;
int main() { return value; }
