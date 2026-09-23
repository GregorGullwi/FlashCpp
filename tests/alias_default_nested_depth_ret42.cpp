// Exercise nested alias template-ids under the normal process stack.
template<class T = int> using J = T;
using Deep = J<J<J<J<J<J<J<J<J<J<J<J<J<J<J<J<J<>>>>>>>>>>>>>>>>>;
Deep value = 42;
int main() { return value; }
