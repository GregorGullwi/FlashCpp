struct SmallResult {
	short value;
};

int branch_missing(bool return_value) {
	if (return_value) {
		return 17;
	}
}

long switch_missing(unsigned int value) {
	switch (value) {
	case 0: return 23;
	case 1: return 29;
	}
}

int switch_all_paths(short value) {
	switch (value) {
	case 0: return 31;
	default: return 32;
	}
}

int loop_missing(bool leave) {
	while (leave) {
		return 31;
	}
}

int break_missing(bool break_loop) {
	while (true) {
		if (break_loop) {
			break;
		}
		return 37;
	}
}

int goto_bypasses_return() {
	goto end;
	return 41;
end:
	;
}

int goto_cycle_missing(bool leave_cycle) {
again:
	if (leave_cycle) {
		goto end;
	}
	goto again;
end:
	;
}

int infinite_loop() {
	for (;;) {
		continue;
	}
}

SmallResult aggregate_missing(bool return_value) {
	if (return_value) {
		return {43};
	}
}

template<typename T>
T template_missing(bool return_value, T value) {
	if (return_value) {
		return value;
	}
}

int lambda_partial(bool return_value) {
	auto function = [return_value]() -> int {
		if (return_value) {
			return 47;
		}
	};
	return function();
}

int lambda_all_paths_return(bool return_value) {
	auto function = [return_value]() -> int {
		if (return_value) {
			return 53;
		}
		return 59;
	};
	return function();
}

int throw_only(bool should_throw) {
	if (should_throw) {
		throw 1;
	}
	throw 2;
}

int rethrow_only() {
	try {
		throw 1;
	} catch (...) {
		throw;
	}
}

void* void_pointer_return_missing(bool return_value) {
	if (return_value) {
		return nullptr;
	}
}

int main() {
	if (branch_missing(true) != 17 || switch_missing(0) != 23 || switch_all_paths(1) != 32 || loop_missing(true) != 31 ||
		break_missing(false) != 37 || aggregate_missing(true).value != 43 ||
		template_missing(true, 61) != 61 || lambda_partial(true) != 47 || lambda_all_paths_return(false) != 59) {
		return 1;
	}
	// C++20 gives the global main an implicit return 0.
}
