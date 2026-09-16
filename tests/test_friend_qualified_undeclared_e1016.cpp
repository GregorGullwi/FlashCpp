// A qualified friend class declaration must name a previously declared class
// or class template ([class.friend]/3). An undeclared qualified non-template
// class must be rejected rather than silently granted friendship. An
// unqualified friend class name, by contrast, may declare a new class in the
// enclosing namespace and stays accepted.
namespace ns {
struct Present {};
}

struct Host {
	friend struct ns::Nope;
	int x = 1;
};
