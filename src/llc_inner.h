#include "tree.h"
#include "local.h"
#include "lower.h"

/// The llc metadata
typedef struct llc {
	/// Persistent metadata, used for recovery
	struct meta *meta;
	/// Lower allocator
	lower_t lower;
	/// Cpu-local data
	struct local *local;
	/// Length of local
	size_t cores;
	/// Array of tree entries
	_Atomic(tree_t) *trees;
	size_t trees_len;
} llc_t;
