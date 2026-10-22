#pragma once

/*!
 * @file switch_compat.h
 * libnx's <switch.h> pulls in a handful of type names that collide with this project's own,
 * unrelated types of the same name:
 *  - `u128`: switch/types.h typedefs it to __uint128_t; common_types.h's `struct u128` is a
 *    tagged union used pervasively for VU/vector code, not swappable for a scalar type.
 *  - `Semaphore`: switch/kernel/semaphore.h's real OS semaphore vs. game/system/IOP_Kernel.h's
 *    unrelated struct modeling the PS2 IOP's emulated semaphores.
 * Nothing in this codebase currently calls libnx's versions of these, so renaming them out of
 * the way via macro substitution during <switch.h>'s own parse (rather than renaming this
 * project's much more widely-used types) is the smaller diff. Add more names here if future
 * libnx usage collides the same way.
 *
 * Include this instead of <switch.h> directly anywhere both libnx and this project's own
 * headers are needed.
 */

#ifdef __SWITCH__
#define u128 nx_u128
#define Semaphore nx_Semaphore
#include <switch.h>
#undef u128
#undef Semaphore
#endif
