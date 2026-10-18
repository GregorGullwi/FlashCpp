using Values = int[2];
using Wide = long long[2];
using Samples = float[2];

int main() {
	if (Values{4, 8}[1] != 8 || Values{4, 8}[0] != 4) {
		return 1;
	}
	if (Values{7}[0] != 7 || Values{7}[1] != 0) {
		return 2;
	}
	if (Values{}[0] != 0 || Values{}[1] != 0) {
		return 3;
	}
	if (Wide{-9, 3}[0] != -9 || Wide{3}[1] != 0) {
		return 4;
	}
	if (Samples{9, 4}[0] != 9.0f || Samples{9}[1] != 0.0f) {
		return 5;
	}
	return 0;
}
