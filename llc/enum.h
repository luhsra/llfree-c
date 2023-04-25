#pragma once

//failcodes
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

  //Expectes in cas operation did not match
  ERR_CANCEL = -6,
};

//init mode
enum {
    /// Not persistent
    Volatile = 0,
    /// Persistent and try recovery
    Recover = 1,
    /// Overwrite the persistent memory
    Overwrite = 1,
};