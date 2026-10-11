struct ReferenceResult {
	int value;
	ReferenceResult& operator=(const ReferenceResult& other) {
		if (other.value > 0) {
			value = other.value;
			return *this;
		}
	}
};

short both_branches(bool condition) {
	if (condition) {
		return 7;
	} else {
		return 9;
	}
}

int constant_while() {
	while (2) {
		continue;
	}
}

float do_returns_once() {
	do {
		return 0.5f;
	} while (false);
}

double floating_loop() {
	while (0.5) {
		continue;
	}
}

int nested_rethrow() {
	try {
		try {
			throw 1;
		} catch (...) {
			throw;
		}
	} catch (...) {
		return 3;
	}
}

int caught_missing() {
	try {
		throw 1;
	} catch (...) {
	}
}

namespace OrdinaryName {
int main(bool condition) {
	if (condition) {
		return 11;
	}
}
}

struct OrdinaryMemberName {
	int main(bool condition) {
		if (condition) {
			return 13;
		}
	}
};

int main() {
	ReferenceResult first{1};
	ReferenceResult second{2};
	first = second;
	int visits = 0;
	auto partial_void = [&visits](bool condition) {
		if (condition) {
			return;
		}
		++visits;
	};
	partial_void(true);
	partial_void(false);
	OrdinaryMemberName member;
	if (both_branches(true) != 7 || both_branches(false) != 9 || do_returns_once() != 0.5f || first.value != 2 || visits != 1 ||
		OrdinaryName::main(true) != 11 || member.main(true) != 13) {
		return 1;
	}
}
