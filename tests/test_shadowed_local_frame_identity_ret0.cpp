// A local variable that shadows an outer declaration of the same spelling must
// get its own frame slot, and every reference must name the frame of the
// declaration that is visible at that point. The original defect collapsed both
// declarations onto one slot, so the inner object overwrote the still-live
// outer one.
//
// Each block checks one accepted shape and returns a distinct code on failure,
// so a wrong read is identified by the exit status alone.

struct Pair {
	int first;
	short second;
};

struct Counter {
	int value;
	int copiedBump() {
		auto twice = [*this]() mutable {
			value += 5;
			return value;
		};
		return twice() + twice() + value;
	}
};

int addThroughRef(int& target, int value) {
	target += value;
	return target;
}

int main() {
	// Sequential sibling blocks: the outer value must survive the inner block.
	{
		int total = 0;
		int sample = 1000;
		{
			int sample = 2000;
			total += sample;
		}
		total += sample;
		if (total != 3000) return 11;
	}

	// Nested repeated shadowing: only the innermost declaration is live.
	{
		int total = 0;
		int depth = 1;
		{
			int depth = 2;
			{
				int depth = 3;
				total += depth;
			}
			total += depth;
		}
		total += depth;
		if (total != 6) return 12;
	}

	// Loop variables of sequential loops share a spelling.
	{
		int total = 0;
		for (int index = 0; index < 3; ++index) {
			total += index;
		}
		for (int index = 10; index < 13; ++index) {
			total += index;
		}
		if (total != 36) return 13;
	}

	// Aggregate initialization, member stores, member reads and a receiver
	// address all have to agree on the shadowed object's frame.
	{
		int total = 0;
		int probe = 7;
		{
			Pair pair = {8, 9};
			total += pair.first + pair.second;
		}
		total += probe;
		if (total != 24) return 14;
	}

	// Reference argument binding through a shadowed local.
	{
		int total = 0;
		int slot = 5;
		{
			int slot = 40;
			addThroughRef(slot, 2);
			total += slot;
		}
		total += slot;
		if (total != 47) return 15;
	}

	// Member function call through a shadowed receiver, plus a copied-this
	// lambda whose body mutates its own copy.
	{
		int total = 0;
		Counter counter = {1};
		{
			Counter counter = {10};
			total += counter.copiedBump();
		}
		total += counter.value;
		if (total != 46) return 16;
	}

	// Catch parameters of sibling handlers may share a spelling.
	{
		int total = 0;
		try {
			throw 11;
		} catch (int error) {
			total += error;
		}
		try {
			throw 22;
		} catch (int error) {
			total += error;
		}
		if (total != 33) return 17;
	}

	// The outer declaration must still be usable after all inner blocks.
	{
		int total = 0;
		int linger = 500;
		{
			int linger = 600;
			(void)linger;
		}
		total += linger;
		if (total != 500) return 18;
	}

	return 0;
}