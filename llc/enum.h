#pragma once

//failure codes
enum {
	/// Success
	ERR_OK = 0,
	/// Not enough memory
	ERR_MEMORY = -1,
	/// Failed atomic operation, retry procedure
	ERR_RETRY = -2,
	/// Invalid address
	ERR_ADDRESS = -3,
	/// Allocator not initialized or initialization failed
	ERR_INITIALIZATION = -4,
	/// Corrupted allocator state
	ERR_CORRUPTION = -5,
};

//init mode
enum {
	/// Not persistent
	VOLATILE = 0,
	/// Persistent and try recovery
	RECOVER = 1,
	/// Overwrite the persistent memory
	OVERWRITE = 2,
};
