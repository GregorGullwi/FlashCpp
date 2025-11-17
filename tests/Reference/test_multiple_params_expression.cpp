// Test multiple non-type template parameters used in the same expression
// This tests the static token bug fix - previously all params shared same token

template<int N, int M>
struct Matrix {
    int rows[N];
    int cols[M];
};

int main() {
    Matrix<3, 4> mat;
    return 0;
}
