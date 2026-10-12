#pragma once

// Platform contract. Each OS implements it in its own folder (Win/, Linux/).

// -----------------------------------------------------------------------------
// Virtual memory
// -----------------------------------------------------------------------------

// Reserves address space without committing it. Returns NULL on failure.
function void *PlatformVMReserve(u64 size);
// Makes [ptr, ptr + size) readable/writable. Both must be page aligned.
function b32   PlatformVMCommit(void *ptr, u64 size);
// Releases a whole reservation obtained from PlatformVMReserve.
function void  PlatformVMRelease(void *ptr, u64 size);
function u64   PlatformVMPageSize(void);
