// GOAL Runtime assembly functions. These exist only in the arm64 version of GOAL.
// - https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms#Pass-arguments-to-functions-correctly
// - https://en.wikipedia.org/wiki/Calling_convention#ARM_(A64)
// - https://student.cs.uwaterloo.ca/~cs452/docs/rpi4b/aapcs64.pdf
// - s16-s31 (d8-d15, q4-q7) must be preserved
// - s0-s15 (d0-d7, q0-q3) and d16-d31 (q8-q15) do not need to be preserved
// - https://devblogs.microsoft.com/oldnewthing/20220728-00/?p=106912
// - https://courses.cs.washington.edu/courses/cse469/19wi/arm64.pdf
//
// NOTE: comments in this file use `//`, not the `;;`/`;` this file originally used - GNU
// binutils `as` (used by Linux/devkitA64 cross builds) treats `;` as a statement separator for
// AArch64, not a comment leader, unlike clang's integrated assembler (macOS) which tolerates
// both. `//` is the portable AArch64 comment syntax both assemblers accept.

.text

// Call C++ code on arm64 systems, from GOAL.
// Following the macOS documentation which mostly aligns with standard arm64
.global _arg_call_arm64
.align 4
_arg_call_arm64:
  // Pop the target function pointer the caller pushed with `str x9, [sp, #-16]!` right before
  // branching (not calling - LR is untouched, see kscheme.cpp's emit_arm64_c_call_trampoline())
  // into this function - this MUST happen before `sp` is touched by anything else below. The
  // original order here (`stp x29,x30,[sp,#-16]!` first, this `ldr` second) read back the just-
  // stored x29 instead of the caller's pushed value - confirmed as a real bug this way: every
  // built-in-type trampoline made via make_function_from_c_systemv() jumped through a garbage
  // "target" on real ARM64/Android hardware until this was reordered.
  ldr x8, [sp], #16
  stp	x29, x30, [sp, #-16]!
  mov	x29, sp

  // Putting an exclamation point after the close-bracket
  // means that the calculated effective address is written back to the base register. (pre-indexing)
  stp q15, q14, [sp, #-32]!
  stp q13, q12, [sp, #-32]!
  stp q11, q10, [sp, #-32]!
  stp q9, q8, [sp, #-32]!

  blr x8
  // BUG (found this session): GOAL's own ARM64 codegen reads a called function's return value out
  // of X8, not X0 (`Register.cpp`: `info.m_gpr_ret_reg = X8` - "playing RAX's role", a GOAL-side
  // convention, not real AAPCS64) - every GOAL-compiled function already ends with its own
  // "mov x8, <result>" before `ret` to honor this, but a real C++ function called through this
  // trampoline returns its value in the *real* AAPCS64 X0, which this trampoline never copied into
  // X8, so the GOAL caller read whatever stale garbage X8 already held instead of the real return
  // value. Confirmed on real hardware: this made `alloc_heap_object`
  // (`game/kernel/jak1/kscheme.cpp`, wired up as the "object" type's generic `new` method) appear
  // to always return null to GOAL callers even though it was actually allocating memory
  // successfully (verified with a temporary native fprintf inside `alloc_from_heap` showing a real,
  // non-null allocation happening every time) - this session's fifth and final real ARM64 bug,
  // and probably the reason `(new 'global ...)`-style allocations looked broken from the GOAL side
  // for anything beyond the trivial cases exercised earlier in boot.
  mov x8, x0

  ldp q9, q8, [sp], #32
  ldp q11, q10, [sp], #32
  ldp q13, q12, [sp], #32
  ldp q15, q14, [sp], #32

  ldp	x29, x30, [sp], #16
  ret


// Call C++ code on arm64 systems, from GOAL.
//
// Put arguments on the stack and put a pointer to this array in the first arg.
// this function pushes all 8 OpenGOAL registers into a stack array.
// then it calls the function pointed to by x0 (RAX in x86) with a pointer to this array.
// it returns the return value of the called function.
.global _stack_call_arm64
.align 4
_stack_call_arm64:
  // Same pop-before-frame-setup fix as _arg_call_arm64 above, same reason.
  ldr x8, [sp], #16
  stp	x29, x30, [sp, #-16]!
  mov	x29, sp

  stp q15, q14, [sp, #-32]!
  stp q13, q12, [sp, #-32]!
  stp q11, q10, [sp, #-32]!
  stp q9, q8, [sp, #-32]!

  // create stack array of arguments
  // arg 7 (R11 in x86)
  // arg 6 (R10 in x86)
  // arg 5 (R8 in x86)
  // arg 4 (R8 in x86)
  // arg 3 (RCX in x86)
  // arg 2 (RDX in x86)
  // arg 1 (RSI in x86)
  // arg 0 (RDI in x86)
  //
  // BUG (found this session): `STP Xt1, Xt2, [Xn, #-16]!` stores Xt1 at the *lower* address
  // ([Xn-16]) and Xt2 at the *higher* one ([Xn-8]) - so `stp x1, x0, ...` here put x1 before x0 in
  // memory, and likewise x3 before x2 and x5 before x4 and x7 before x6, leaving the resulting
  // array as [x1, x0, x3, x2, x5, x4, x7, x6] instead of the natural [x0, x1, x2, ..., x7] the
  // x86-64 version of this same trampoline produces (`asm_funcs_x86_64.asm`'s `_stack_call_systemv`
  // pushes r11..rdi in that order, and `push` only ever decrements-then-stores a single register,
  // so it can't have this pairwise-swap problem - only this manually-paired ARM64 `stp` version
  // could). Confirmed on real hardware: this silently swapped `format_impl_jak1`
  // (`game/kernel/jak1/kprint.cpp`)'s first two arguments (`args[0]` = dest, `args[1]` = format
  // string) whenever `format`/`fmt`/etc. were called with any argument at all (2 fixed args + at
  // least 1 vararg is exactly when x0/x1 stop being the *last* pair pushed and start being pushed
  // alongside x2/x3 etc. - actually every real call hits this, since dest+format-string are
  // *always* x0/x1) - `format_impl_jak1` read the format string's own heap offset as `dest` and
  // (usually) `0` as the format string pointer, corrupting control flow into a native SIGSEGV
  // (fault address `0x4`, a small-offset null-shaped dereference) instead of ever printing
  // anything. This is almost certainly why every `format`/print-based GOAL debug technique this
  // whole multi-session effort has silently failed to produce output on real ARM64 hardware -
  // *not* the same bug as the earlier X19-vs-X0 `args` pointer fix directly above, which fixed a
  // real but different problem (the pointer itself, not what it pointed *at*).
  stp x6, x7, [sp, #-16]!
  stp x4, x5, [sp, #-16]!
  stp x2, x3, [sp, #-16]!
  stp x0, x1, [sp, #-16]!

  // set first argument - AAPCS64's first integer arg register is X0, not X19 (this was a real
  // bug: format_impl_jak1(uint64_t* args) etc. read their `args` pointer from X0 per the real
  // calling convention, but got whatever stale value X0 already held - X0's own incoming value
  // was already saved to the stack above (the `stp x1, x0, ...` below), so it's safe to clobber
  // here. Confirmed as the real cause of a SIGSEGV in format_impl_jak1 reading a bogus `args`
  // pointer on real ARM64 hardware - see the development notes.
  mov x0, sp
  // call function
  blr x8
  // BUG (found this session): same GOAL-return-value-is-X8-not-X0 issue as `_arg_call_arm64`
  // above - copy the real AAPCS64 return value into X8 before it gets clobbered by the argument
  // restores below (which do legitimately need X0 back to its original, pre-call value first).
  mov x8, x0
  // restore arguments - order matches the (now-fixed) store order above
  ldp x0, x1, [sp], #16
  ldp x2, x3, [sp], #16
  ldp x4, x5, [sp], #16
  ldp x6, x7, [sp], #16

  ldp q9, q8, [sp], #32
  ldp q11, q10, [sp], #32
  ldp q13, q12, [sp], #32
  ldp q15, q14, [sp], #32

  ldp	x29, x30, [sp], #16
  // return!
  ret

// Call c++ code through mips2c.
// GOAL will call a dynamically generated trampoline (see LinkedFunctionTable::reg() in
// game/mips2c/mips2c_table.cpp). Unlike x86 (which is reached via `jmp` and relies on the real
// caller's return address still sitting on the stack, since `jmp` never pushes one of its own),
// the trampoline here reaches this function via `br` - which leaves the real caller's return
// address in X30 (LR), completely untouched by the jump. So this function just needs to preserve
// X30 across its own internal `blr` below (the `stp`/`ldp` of x29/x30 immediately does that) and
// `ret` at the end to return to the true original caller - no stack-based return-address tricks
// needed at all. The trampoline hands us the two things it would otherwise have had to stash on
// the stack directly in registers instead: X9 = the native exec() function pointer to call, X8 =
// the GOAL stack size to reserve. Both are chosen specifically because nothing below touches them
// until they're needed (X0-X7 in particular must stay exactly as the real GOAL caller set them up
// - they're GOAL's own argument registers, read into the MIPS execution context below).
.global _mips2c_call_arm64
.align 4
_mips2c_call_arm64:
  stp	x29, x30, [sp, #-16]!
  mov	x29, sp

  // first, save quadword registers
  stp q15, q14, [sp, #-32]!
  stp q13, q12, [sp, #-32]!
  stp q11, q10, [sp, #-32]!
  stp q9, q8, [sp, #-32]!

  // NOTE - in x86 the 2 special registers are saved (R10 and R11)
  // we don't need to do that in ARM64, there are plenty of registers to work with

  // oof
  sub sp, sp, 1280
  str x0, [sp, #+64] // arg 0 (RDI in x86) and
  str x1, [sp, #+80] // arg 1 (RSI in x86)
  str x2, [sp, #+96] // arg 2 (RDX in x86) and arg 3 (RCX in x86)
  str x3, [sp, #+112] // arg 2 (RDX in x86) and arg 3 (RCX in x86)
  str x4, [sp, #+128] // arg 4 (R8 in x86) and arg 5 (R8 in x86)
  str x5, [sp, #+144] // arg 4 (R8 in x86) and arg 5 (R8 in x86)
  str x6, [sp, #+160] // arg 6 (R10 in x86) and arg 7 (R11 in x86)
  str x7, [sp, #+176] // arg 6 (R10 in x86) and arg 7 (R11 in x86)
  str x20, [sp, #+352] // s6 (pp) (R13 in x86) and s7 (st) (R14 in x86)
  str x21, [sp, #+368] // s6 (pp) (R13 in x86) and s7 (st) (R14 in x86)

  mov x0, sp // move the stack pointer to arg 0
  sub x0, x0, x22 // R15 is a "special" offset TODO - whats special about it?
  str x0, [sp, #+464] // mip2c code's MIPS stack

  mov x0, sp // move the stack pointer to the new position

  sub sp, sp, x8 // allocate space on the stack for GOAL fake stack
  str x8, [sp, #-16]!

  // TEMP DEBUG: record exactly what we're about to call and bump a call counter, so a crash
  // handler dump can show which exec() address (and how many mips2c calls in) actually faulted.
  // x17 is free here (linker scratch, unused elsewhere in this function).
  adrp x17, :got:g_mips2c_debug_last_exec
  ldr x17, [x17, :got_lo12:g_mips2c_debug_last_exec]
  str x9, [x17]
  adrp x17, :got:g_mips2c_debug_call_count
  ldr x17, [x17, :got_lo12:g_mips2c_debug_call_count]
  ldr x16, [x17]
  add x16, x16, 1
  str x16, [x17]

  // x0 is already the context pointer (set at "mov x0, sp" above) - AAPCS64's own first-argument
  // register, exactly what execute(void* ctxt) expects. x9 (untouched since the trampoline set it)
  // holds the real native exec() address to call.
  blr x9 // call!

  // TEMP DEBUG: record that we made it back here at all (this write itself can't be what's
  // faulting - if the crash handler ever reports this address in g_mips2c_debug_last_ret, the
  // fault happened strictly after this point, not on the `blr` or the return from it).
  adrp x17, :got:g_mips2c_debug_last_ret
  ldr x17, [x17, :got_lo12:g_mips2c_debug_last_ret]
  str x30, [x17]

  // unallocate
  ldr x8, [sp], #16
  add sp, sp, x8

  ldr x8, [sp, #+32]

  add sp, sp, 1280 // reset the stackpointer back

  ldp q9, q8, [sp], #32
  ldp q11, q10, [sp], #32
  ldp q13, q12, [sp], #32
  ldp q15, q14, [sp], #32

  // No extra stack cleanup needed here (unlike the old design this replaced): the trampoline never
  // pushes anything onto the stack in the first place - it hands us exec()/stack_size in X9/X8
  // directly and reaches us via a plain `br`, so `sp` right now is exactly back to where it was
  // when this function's own entry `stp x29, x30, [sp, #-16]!` ran.
  ldp	x29, x30, [sp], #16
  ret

// The _call_goal_asm function is used to call a GOAL function from C.
// It calls on the parent stack, which is a bad idea if your stack is not already a GOAL stack.
// It supports up to 3 arguments and a return value.
// This should be called with the arguments:
// - first goal arg
// - second goal arg
// - third goal arg
// - address of function to call
// - address of the symbol table
// - GOAL memory space offset
.global _call_goal_asm_arm64
.align 4
_call_goal_asm_arm64:
  stp	x29, x30, [sp, #-16]!
  mov	x29, sp
  // saved registers we need to modify for GOAL should be preserved
  // ARM64 requires 16-byte stack pointer alignment
  stp x20, x21, [sp, #-16]!
  str x22, [sp, #-16]!
  // Native callers preserve V8-V15. GOAL's unallocated V14/V15 scratch
  // registers are changed by masked vector math and shuffles. (AI-assisted)
  stp q8, q9, [sp, #-32]!
  stp q10, q11, [sp, #-32]!
  stp q12, q13, [sp, #-32]!
  stp q14, q15, [sp, #-32]!

  // x0 - first arg
  // x1 - second arg
  // x2 - third arg
  // x3 - function pointer
  // x4 - st (goes in x20 and x21)
  // x5 - off (goes in x22)

  // set GOAL process
  mov x20, x4
  // symbol table
  mov x21, x4
  // offset
  mov x22, x5
  // call GOAL by function pointer
  blr x3
  mov x0, x8

  // restore saved registers.
  ldp q14, q15, [sp], #32
  ldp q12, q13, [sp], #32
  ldp q10, q11, [sp], #32
  ldp q8, q9, [sp], #32
  ldr x22, [sp], #16
  ldp x20, x21, [sp], #16
  ldp	x29, x30, [sp], #16
  ret

.global _call_goal8_asm_arm64
.align 4
_call_goal8_asm_arm64:
  stp	x29, x30, [sp, #-16]!
  mov	x29, sp
  // saved registers we need to modify for GOAL should be preserved
  // ARM64 requires 16-byte stack pointer alignment
  stp x20, x21, [sp, #-16]!
  str x22, [sp, #-16]!
  // Same native/GOAL vector convention boundary as the three-argument entry. (AI-assisted)
  stp q8, q9, [sp, #-32]!
  stp q10, q11, [sp, #-32]!
  stp q12, q13, [sp, #-32]!
  stp q14, q15, [sp, #-32]!

  // x0 - first arg (func)
  // x1 - second arg (arg array)
  // x2 - third arg  (0)
  // x3 - pp (goes in r13)
  // x4  - st (goes in r14)
  // x5  - off (goes in r15)

  // set GOAL function pointer
  mov x20, x3
  // st
  mov x21, x4
  // offset
  mov x22, x5
  // move function to temp
  mov x8, x0
  // extract arguments
  ldr x0, [x1]  // 0
  ldr x2, [x1, #+16] // 2
  ldr x3, [x1, #+24] // 3
  ldr x4, [x1, #+32]  // 4
  ldr x5, [x1, #+40]  // 5
  ldr x6, [x1, #+48] // 6
  ldr x7, [x1, #+56]  // 7
  ldr x1, [x1, #+8] // 1 (do this last)
  // call GOAL by function pointer
  blr x8
  mov x0, x8

  // retore registers.
  ldp q14, q15, [sp], #32
  ldp q12, q13, [sp], #32
  ldp q10, q11, [sp], #32
  ldp q8, q9, [sp], #32
  ldr x22, [sp], #16
  ldp x20, x21, [sp], #16
  ldp	x29, x30, [sp], #16
  ret

// Call goal, but switch stacks.
.global _call_goal_on_stack_asm_arm64
.align 4
_call_goal_on_stack_asm_arm64:
  stp	x29, x30, [sp, #-16]!
  mov	x29, sp
  // x0 - stack pointer
  // x1 - unused
  // x2 - unused
  // x3 - function pointer
  // x4  - st (goes in x21 and x20)
  // x5  - offset (goes in x22)

  // saved registers we need to modify for GOAL should be preserved
  // ARM64 requires 16-byte stack pointer alignment
  stp x20, x21, [sp, #-16]!
  stp x22, x23, [sp, #-16]!
  // Save on the native stack; restore only after switching back to it. (AI-assisted)
  stp q8, q9, [sp, #-32]!
  stp q10, q11, [sp, #-32]!
  stp q12, q13, [sp, #-32]!
  stp q14, q15, [sp, #-32]!
  // Stash the current (C++) stack pointer in a callee-saved register (x23, not sp itself - you
  // cannot directly store/load the `sp` register in arm64), NOT on the stack: once `sp` is
  // switched to the GOAL stack below, this same C++ stack becomes unreachable through `sp` - a
  // `[sp]`-relative pop after the call would read from the *GOAL* stack's current position, not
  // this one, silently reading garbage/zero instead of the real saved value. x23 is callee-saved
  // (AAPCS64) and already treated as such by GOAL's own register convention
  // (RegisterInfo::m_saved_gprs, goalc/emitter/Register.cpp), so it survives the `blr` below
  // whether or not the called GOAL function itself uses it. Confirmed as a real bug this way: the
  // stack-relative version crashed real ARM64/Android hardware with SIGSEGV at sp==0 after
  // popping a zeroed/garbage value here.
  mov x23, sp

  // switch to new stack
  mov sp, x0

  mov x20, x4 // set GOAL function pointer
  mov x21, x4 // symbol table
  mov x22, x5 // offset
  // call GOAL by function pointer
  blr x3
  mov x0, x8

  // restore the real (C++) stack pointer directly from the register we stashed it in above -
  // *not* by popping from `sp`, which right now still points into the GOAL stack the callee left
  // it at.
  mov sp, x23
  ldp q14, q15, [sp], #32
  ldp q12, q13, [sp], #32
  ldp q10, q11, [sp], #32
  ldp q8, q9, [sp], #32
  ldp x22, x23, [sp], #16
  ldp x20, x21, [sp], #16
  ldp	x29, x30, [sp], #16
  ret
