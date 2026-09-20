struct Owner { template <class T> using I = T; };
Owner::I<> value = 0;
int main() { return value; }
