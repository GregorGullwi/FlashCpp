// Regression: global/static bindings of pointer type must be stored and
// loaded at full pointer width. The binding width used to come from the
// declaration's base-type size, truncating pointers on store
// (C++20 [dcl.ptr]/1, [basic.align]).
//
// NOTE: member stores through a global struct pointer (wp->tag = 7) still
// misbehave; that is a separate pre-existing gap tracked in
// docs/KNOWN_ISSUES.md.

int gv;
int* gp;

long lv[3];
long* lp;

int main() {
	gp = &gv;
	*gp = 42;
	if (*gp != 42) {
		return 1;
	}

	lp = &lv[0];
	lp[2] = 9L;
	if (lv[2] != 9L) {
		return 2;
	}

	// Pointer read-back through a second global must preserve the value.
	int** gpp = &gp;
	if (**gpp != 42) {
		return 4;
	}
	return 0;
}

