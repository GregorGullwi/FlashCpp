struct Primary {
	virtual int primary() {
		return 1;
	}
};

struct Secondary {
	virtual int target() {
		return 2;
	}
};

struct Derived : Primary, Secondary {
	int target() override {
		return 29;
	}
};

int main() {
	Derived derived;
	Secondary* secondary = &derived;
	return secondary->target();
}
