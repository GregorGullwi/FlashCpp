template <class T, int Rows, int Columns>
using MatrixRef = T (&)[Rows][Columns];

MatrixRef<int, 0, 3> invalid_reference;

int main() {
	return 0;
}
