[[noreturn]] void stop();
void stop() { throw 1; }

int direct_no_return() {
	stop();
}

int caught_no_return() {
	try {
		stop();
	} catch (...) {
		return 0;
	}
}

struct MemberStop {
	[[noreturn]] void fail() const { stop(); }
};

int member_no_return() {
	MemberStop object;
	object.fail();
}

template <typename T>
struct TemplateStop {
	[[noreturn]] void fail(T) const { stop(); }
};

int template_member_no_return() {
	TemplateStop<int> object;
	object.fail(1);
}

struct OperatorStop {
	[[noreturn]] int operator+(int) const { stop(); }
};

int operator_no_return() {
	OperatorStop object;
	object + 1;
}

using NoThrowFunction = int (*)() noexcept;

int invoke_noexcept(NoThrowFunction function) {
	try {
		function();
		return 0;
	} catch (...) {
	}
}

struct NoThrowMember {
	int value() const noexcept { return 4; }
};

int member_noexcept(const NoThrowMember& object) {
	try {
		return object.value();
	} catch (...) {
	}
}

struct ConversionStop {
	[[noreturn]] operator int() const { stop(); }
};

int conversion_operator_no_return() {
	ConversionStop object;
	int value = object;
}

struct ArrowTarget {
	int value;
};

struct ArrowStop {
	[[noreturn]] ArrowTarget* operator->() const { stop(); }
};

int arrow_operator_no_return() {
	ArrowStop object;
	object->value;
}

int caught_noreturn_missing() {
	try {
		stop();
	} catch (...) {
	}
}

int main() {
	return 0;
}
