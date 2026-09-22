// A decayed ordered array whose element shape differs from the parameter is
// an ordinary no-match, not an abort in the flat projection guard.
int reject(int (*(*(*param))[9])[4]);

int main() {
	int (*(*(*rows)[2])[3])[4] = nullptr;
	reject(*rows);
	return 0;
}
