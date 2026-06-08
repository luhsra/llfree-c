#pragma once

#include "llfree_types.h"

/// Unused functions and variables
#define ll_unused __attribute__((unused))

#ifdef __clang__
#define ll_warn_unused __attribute__((warn_unused_result))
#else
#define ll_warn_unused
#endif

#define ll_def_optional(ty, prefix, def)                                    \
	typedef struct prefix##_optional {                                  \
		bool present;                                               \
		ty value;                                                   \
	} prefix##_optional_t;                                              \
	static inline ll_unused prefix##_optional_t prefix##_some(ty value) \
	{                                                                   \
		return (prefix##_optional_t){ .present = true,              \
					      .value = value };             \
	}                                                                   \
	static inline ll_unused prefix##_optional_t prefix##_none(void)     \
	{                                                                   \
		return (prefix##_optional_t){ .present = false,             \
					      .value = def };               \
	}

ll_def_optional(size_t, ll, 0);

/// Unique identifier for a frame
typedef struct frame_id {
	uint64_t value;
} frame_id_t;
static inline ll_unused frame_id_t frame_id(uint64_t value)
{
	return (frame_id_t){ .value = value };
}
ll_def_optional(struct frame_id, frame_id, { 0 });

/// Unique identifier for a tree
typedef struct tree_id {
	size_t value;
} tree_id_t;
static inline ll_unused tree_id_t tree_id(size_t value)
{
	return (tree_id_t){ .value = value };
}
ll_def_optional(struct tree_id, tree_id, { 0 });

/// Opaque llfree allocator type
typedef struct llfree llfree_t;

enum : uint8_t {
	/// Success
	LLFREE_ERR_OK = 0,
	/// Not enough memory
	LLFREE_ERR_MEMORY = 1,
	/// Invalid argument
	LLFREE_ERR_ARGUMENT = 2,
	/// Allocator not initialized or initialization failed
	LLFREE_ERR_INIT = 3,
};

/// Result type for llfree_get: includes the class of the allocated frame
typedef struct ll_warn_unused llfree_result {
	/// Frame number, usually only valid if error == LLFREE_ERR_OK
	frame_id_t frame;
	/// Class of the allocated frame (may differ from requested due to demotion)
	uint8_t class;
	/// Error code, 0 if no error
	uint8_t error;
} llfree_result_t;

/// Create a successful result with the given frame and class
static inline llfree_result_t ll_unused llfree_ok(frame_id_t frame,
						  uint8_t class)
{
	return (llfree_result_t){ .frame = frame,
				  .class = class,
				  .error = LLFREE_ERR_OK };
}
static inline llfree_result_t ll_unused llfree_err(uint8_t err)
{
	return (llfree_result_t){ .frame = frame_id(0),
				  .class = 0,
				  .error = err };
}

/// Check if the result is ok (no error)
static inline bool ll_unused llfree_is_ok(llfree_result_t r)
{
	return r.error == LLFREE_ERR_OK;
}

/// Init modes
enum : uint8_t {
	/// Clear the allocator marking all frames as free
	LLFREE_INIT_FREE = 0,
	/// Clear the allocator marking all frames as allocated
	LLFREE_INIT_ALLOC = 1,
	/// Try recovering all frames from persistent memory
	LLFREE_INIT_RECOVER = 2,
	/// Assume the allocator is already initialized
	LLFREE_INIT_NONE = 4,
	/// The number of initialization modes
	LLFREE_INIT_MAX = 5,
};

/// Request for memory allocation, matching the Rust `Request` struct.
typedef struct llfree_request {
	/// Allocation order (frames = 1 << order)
	uint8_t order;
	/// Requested class
	uint8_t class;
	/// Within-class local index (e.g. core id),
	/// or ll_none() for global-only allocation.
	ll_optional_t local;
} llfree_request_t;

static inline llfree_request_t ll_unused llreq(uint8_t order, uint8_t class,
					       ll_optional_t local)
{
	return (llfree_request_t){ .order = order,
				   .class = class,
				   .local = local };
}

/// Policy result for tree class access
typedef enum {
	/// Trees of the requested class can be used (higher = better fit)
	LLFREE_POLICY_MATCH = 0,
	/// Can steal from target class without demoting it
	LLFREE_POLICY_STEAL = 1,
	/// Would demote the target tree to the requested class
	LLFREE_POLICY_DEMOTE = 2,
	/// Cannot use target class
	LLFREE_POLICY_INVALID = 3,
} llfree_policy_type_t;

typedef struct {
	llfree_policy_type_t type;
	/// Priority for MATCH: 0 = worst, UINT8_MAX = perfect/immediate
	uint8_t priority;
} llfree_policy_t;

/// Policy function: given requested and target class with free frames,
/// returns how the target tree can be used.
typedef llfree_policy_t (*llfree_policy_fn)(uint8_t requested, uint8_t target,
					    size_t free);

/// Per-class configuration: class identifier and local slot count.
/// Matches the Rust `(Class, usize)` entry in `Classing::classes`.
typedef struct llfree_class_conf {
	/// Class identifier
	uint8_t class;
	/// Number of local slots for this class (typically one per core)
	size_t count;
} llfree_class_conf_t;

/// Classing configuration for the allocator.
/// Matches the Rust `Classing` struct: each class has an independent local slot count.
typedef struct llfree_classing {
	/// Per-class configuration (class id + local slot count).
	/// classes[0..num_classes) are valid; slots are laid out flat:
	/// [class0_slot0..class0_slot(count0-1), class1_slot0..]
	llfree_class_conf_t classes[LLFREE_MAX_CLASSES];
	/// Number of valid entries in classes[]
	size_t num_classes;
	/// Default class for entirely free/new trees
	uint8_t default_class;
	/// Policy function for class matching
	llfree_policy_fn policy;
} llfree_classing_t;

/// Size of the required metadata
typedef struct llfree_meta_size {
	/// Volatile data.
	size_t llfree;
	/// CPU-local data.
	size_t local;
	/// Tree array.
	size_t trees;
	/// Lower children and bitfields (optionally persistent).
	size_t lower;
} llfree_meta_size_t;

/// Returns the size of the metadata buffers required for initialization.
/// Matches the Rust `metadata_size(classing, frames)` signature.
llfree_meta_size_t llfree_metadata_size(const llfree_classing_t *classing,
					size_t frames);

/// Size of the required metadata
typedef struct llfree_meta {
	/// CPU-local data.
	uint8_t *local;
	/// Tree array.
	uint8_t *trees;
	/// Lower children and bitfields (optionally persistent).
	uint8_t *lower;
} llfree_meta_t;

/// Allocate and initialize the data structures of the allocator.
///
/// `frames` is the number of frames to manage.
/// The `init` parameter is expected to be one of the `LLFREE_INIT_<..>` modes.
/// The `meta` buffers store the allocator state and must be at least as large
/// as reported by `llfree_metadata_size`.
/// The `classing` parameter configures class counts and the policy function.
llfree_result_t llfree_init(llfree_t *self, size_t frames, uint8_t init,
			    llfree_meta_t meta,
			    const llfree_classing_t *classing);

/// Returns the metadata
llfree_meta_t llfree_metadata(const llfree_t *self);
/// Returns the metadata size of an already-initialized allocator.
/// Useful for cleanup without needing the original classing struct.
llfree_meta_size_t llfree_metadata_size_of(const llfree_t *self);

/// Allocates a frame. Returns the frame and its actual class in the result.
/// The actual class may differ from request.class if demotion occurred.
/// If frame is present, allocates that specific frame (get_at behavior);
/// otherwise allocates any frame near the local slot's preferred location.
/// Set request.local to ll_none() for global-only allocation.
llfree_result_t llfree_get(llfree_t *self, frame_id_optional_t frame,
			   llfree_request_t request);
/// Frees a frame
llfree_result_t llfree_put(llfree_t *self, frame_id_t frame,
			   llfree_request_t request);

/// Unreserves all local reservations.
void llfree_drain(llfree_t *self);

/// Match conditions for llfree_change_tree.
typedef struct llfree_tree_match {
	/// Match a specific tree index (ll_none() for any tree).
	tree_id_optional_t id;
	/// Match a specific class (LLFREE_CLASS_NONE for any class).
	uint8_t class;
	/// Require at least this many free frames in the tree.
	size_t free;
} llfree_tree_match_t;

/// Tree operation for llfree_change_tree.
typedef enum llfree_tree_operation {
	/// Do not apply an operation.
	LLFREE_TREE_OP_NONE = 0,
	/// Online the tree and repopulate its free counter from lower stats.
	LLFREE_TREE_OP_ONLINE = 1,
	/// Offline the tree by setting its free counter to zero.
	LLFREE_TREE_OP_OFFLINE = 2,
} llfree_tree_operation_t;

/// Tree change for llfree_change_tree.
typedef struct llfree_tree_change {
	/// Change the class. Use LLFREE_CLASS_NONE to leave unchanged.
	uint8_t class;
	/// Operation to apply.
	llfree_tree_operation_t operation;
} llfree_tree_change_t;

/// Change a tree matching `matcher` according to `change`.
/// Fails if the tree does not match or is currently reserved.
llfree_result_t llfree_change_tree(llfree_t *self, llfree_tree_match_t matcher,
				   llfree_tree_change_t change);

/// Returns the total number of frames the allocator can allocate.
size_t llfree_frames(const llfree_t *self);

/// LLFree statistics
typedef struct ll_stats {
	size_t free_frames;
	size_t free_huge;
	size_t free_trees;
} ll_stats_t;

/// Per-class tree statistics
typedef struct ll_class_stats {
	size_t free_frames;
	size_t alloc_frames;
} ll_class_stats_t;

/// Tree statistics
typedef struct ll_tree_stats {
	size_t free_frames;
	size_t free_trees;
	/// Stats per class
	ll_class_stats_t classes[LLFREE_MAX_CLASSES];
} ll_tree_stats_t;

/// Returns the tree-level stats.
/// This is faster than llfree_stats as it doesn't scan the lower allocator, but may be less accurate.
ll_tree_stats_t llfree_tree_stats(const llfree_t *self);

/// Counts free frames accurately by scanning the lower allocator.
ll_stats_t llfree_stats(const llfree_t *self);
/// Returns the full stats for a frame at a given order.
ll_stats_t llfree_stats_at(const llfree_t *self, frame_id_t frame,
			   size_t order);

// == Debugging ==

/// Prints the allocators state for debugging with given Rust printer
void llfree_print_debug(const llfree_t *self,
			void (*writer)(void *, const char *), void *arg);

/// Prints detailed stats about the allocator state
void llfree_print(const llfree_t *self);
/// Validate the internal data structures
void llfree_validate(const llfree_t *self);

// == Example Classing ==

/// Simple 2-class policy (small=0, huge=1)
static inline llfree_policy_t ll_unused llfree_simple_policy(uint8_t requested,
							     uint8_t target,
							     size_t free)
{
	if (requested > target)
		return (llfree_policy_t){ LLFREE_POLICY_STEAL, 0 };
	if (requested < target)
		return (llfree_policy_t){ LLFREE_POLICY_DEMOTE, 0 };
	/* same class */
	if (free >= LLFREE_TREE_SIZE / 2)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, 1 };
	if (free >= LLFREE_TREE_SIZE / 64)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, UINT8_MAX };
	return (llfree_policy_t){ LLFREE_POLICY_MATCH, 0 };
}

/// Create a simple 2-class classing: class 0 for small frames, class 1 for huge.
/// Each class gets `cores` local slots (one per core).
static inline llfree_classing_t ll_unused llfree_classing_simple(size_t cores)
{
	llfree_classing_t t = { .num_classes = 2,
				.default_class = 1,
				.policy = llfree_simple_policy };
	t.classes[0] = (llfree_class_conf_t){ .class = 0, .count = cores };
	t.classes[1] = (llfree_class_conf_t){ .class = 1, .count = cores };
	return t;
}

/// Build a request for simple 2-class classing.
/// Maps (order, core) to the correct class and within-class local index.
static inline llfree_request_t ll_unused llfree_simple_request(size_t cores,
							       uint8_t order,
							       size_t core)
{
	if (order >= LLFREE_HUGE_ORDER)
		return llreq(order, 1, ll_some(core % cores));
	return llreq(order, 0, ll_some(core % cores));
}

// == More complex movable classing example ==

/// Simple movable policy (3 classes: immovable=0, movable=1, huge=2)
static inline llfree_policy_t ll_unused llfree_movable_policy(uint8_t requested,
							      uint8_t target,
							      size_t free)
{
	if (requested > target)
		return (llfree_policy_t){ LLFREE_POLICY_STEAL, 0 };
	if (requested < target)
		return (llfree_policy_t){ LLFREE_POLICY_DEMOTE, 0 };
	/* same class */
	if (free >= LLFREE_TREE_SIZE / 2)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, 1 };
	if (free >= LLFREE_TREE_SIZE / 64)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, UINT8_MAX };
	return (llfree_policy_t){ LLFREE_POLICY_MATCH, 2 };
}

/// Create a 3-class movable classing: class 0 immovable, class 1 movable, class 2 huge.
/// Each class gets `cores` local slots.
static inline llfree_classing_t ll_unused llfree_classing_movable(size_t cores)
{
	llfree_classing_t t = { .num_classes = 3,
				.default_class = 2,
				.policy = llfree_movable_policy };
	t.classes[0] = (llfree_class_conf_t){ .class = 0, .count = cores };
	t.classes[1] = (llfree_class_conf_t){ .class = 1, .count = cores };
	t.classes[2] = (llfree_class_conf_t){ .class = 2, .count = cores };
	return t;
}

/// Build a request for movable 3-class classing.
/// Maps (order, core, movable) to the correct class and within-class local index.
static inline llfree_request_t ll_unused llfree_movable_request(size_t cores,
								uint8_t order,
								size_t core,
								bool movable)
{
	if (order >= LLFREE_HUGE_ORDER)
		return llreq(order, 2, ll_some(core % cores));
	if (movable)
		return llreq(order, 1, ll_some(core % cores));
	return llreq(order, 0, ll_some(core % cores));
}
