// Regression: NTTP (non-type template parameter) must be accessible inside
// deferred constructor bodies when the class template has static members.
// Without the fix, the deferred constructor body replay left template_param_names
// empty for constructors, causing "Missing identifier: N" during re-parse.

template<int N>
struct Array {
	int data[N];
	static constexpr int size = N;

	Array() {
		for (int i = 0; i < N; i++) {
			data[i] = i * 2;
		}
	}

	int sum() const {
		int s = 0;
		for (int i = 0; i < N; i++) {
			s += data[i];
		}
		return s;
	}
};

template<int N, int M>
struct Grid {
	static constexpr int rows = N;
	static constexpr int cols = M;
	int data[N * M];

	Grid() {
		for (int i = 0; i < N * M; i++) {
			data[i] = i;
		}
	}

	int get(int r, int c) const { return data[r * M + c]; }
};

int main() {
	// Array<4>: data = {0,2,4,6}, sum = 12
	Array<4> a;
	if (Array<4>::size != 4) return 1;
	if (a.sum() != 12) return 2;

	// Grid<2,3>: rows=2, cols=3
	Grid<2, 3> g;
	if (Grid<2, 3>::rows != 2) return 3;
	if (Grid<2, 3>::cols != 3) return 4;
	if (g.get(1, 2) != 5) return 5;

	return 0;
}
