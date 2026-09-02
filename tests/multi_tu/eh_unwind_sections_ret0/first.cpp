#include "shared.h"

// The first object's unwind section has no personality or LSDA. It must not
// terminate the linked unwind table before the other objects' FDEs.
void relayAcrossSections(int kind) {
	throwAcrossSections(kind);
}
