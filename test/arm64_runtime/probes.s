.text
.global probe_goal_call
.type probe_goal_call, %function
probe_goal_call:
  stp x29, x30, [sp, #-16]!
  stp x19, x20, [sp, #-16]!
  stp x21, x22, [sp, #-16]!
  stp q8, q9, [sp, #-32]!
  stp q10, q11, [sp, #-32]!
  stp q12, q13, [sp, #-32]!
  stp q14, q15, [sp, #-32]!
  mov x19, x2
  mov x20, x3
  mov x21, x4
  mov x22, x5
  mov x16, x0
  ldp q8, q9, [x19]
  ldp q10, q11, [x19, #32]
  ldp q12, q13, [x19, #64]
  ldp q14, q15, [x19, #96]
  ldr x0, [x1]
  ldp x2, x3, [x1, #16]
  ldp x4, x5, [x1, #32]
  ldp x6, x7, [x1, #48]
  ldr x1, [x1, #8]
  mov x8, x0  // Seed the GOAL return register for the nothing/zero tests. (AI-assisted)
  blr x16
  mov x0, x8
  stp q8, q9, [x19]
  stp q10, q11, [x19, #32]
  stp q12, q13, [x19, #64]
  stp q14, q15, [x19, #96]
  ldp q14, q15, [sp], #32
  ldp q12, q13, [sp], #32
  ldp q10, q11, [sp], #32
  ldp q8, q9, [sp], #32
  ldp x21, x22, [sp], #16
  ldp x19, x20, [sp], #16
  ldp x29, x30, [sp], #16
  ret

.global probe_goal_return
.type probe_goal_return, %function
probe_goal_return:
  mov x0, #0x1234
  mov x8, #0x5678
  ret

// Calls real compiled GOAL tail-jump code with a distinct continuation. (AI-assisted)
.global probe_stack_return
.type probe_stack_return, %function
probe_stack_return:
  stp x19, x30, [sp, #-16]!
  mov x19, sp
  mov x16, x0
  adr x0, .Lstack_return_target
  adr x1, .Lstack_return_continuation
  blr x16
  // A plain BR retains this LR and incorrectly bypasses the chosen continuation.
  mov x8, #13
  b .Lstack_return_cleanup
.Lstack_return_continuation:
  mov x8, #99
.Lstack_return_cleanup:
  mov sp, x19
  mov x0, x8
  ldp x19, x30, [sp], #16
  ret
.Lstack_return_target:
  // The target uses the compiler's normal GOAL prologue/epilogue convention.
  str x30, [sp, #-16]!
  mov x8, #42
  ldr x30, [sp], #16
  ret

// Exercise a compiled uint128 parameter/return with caller-saved vectors clobbered. (AI-assisted)
.global probe_wide_return
.type probe_wide_return, %function
probe_wide_return:
  stp x19, x30, [sp, #-16]!
  mov x19, x2
  mov x16, x0
  ldr q1, [x1]
  adr x0, .Lwide_callback
  sub x0, x0, x22  // GOAL function pointers are offsets from the memory base.
  blr x16
  str q0, [x19]
  ldp x19, x30, [sp], #16
  ret
.Lwide_callback:
  movi v0.16b, #0xaa
  movi v1.16b, #0xbb
  movi v2.16b, #0xcc
  movi v3.16b, #0xdd
  movi v4.16b, #0xee
  movi v5.16b, #0xff
  movi v6.16b, #0x11
  movi v7.16b, #0x22
  mov x8, #7
  ret

// Seed C ABI preserved vector registers around a real C-to-GOAL entry point.
// Existing probe_goal_call tests the opposite boundary. (AI-assisted)
.global probe_c_call
.type probe_c_call, %function
probe_c_call:
  stp x19, x30, [sp, #-16]!
  stp q8, q9, [sp, #-32]!
  stp q10, q11, [sp, #-32]!
  stp q12, q13, [sp, #-32]!
  stp q14, q15, [sp, #-32]!
  mov x19, x2
  mov x16, x0
  ldp q8, q9, [x19]
  ldp q10, q11, [x19, #32]
  ldp q12, q13, [x19, #64]
  ldp q14, q15, [x19, #96]
  ldp x4, x5, [x1, #32]
  ldp x2, x3, [x1, #16]
  ldp x0, x1, [x1]
  blr x16
  stp q8, q9, [x19]
  stp q10, q11, [x19, #32]
  stp q12, q13, [x19, #64]
  stp q14, q15, [x19, #96]
  ldp q14, q15, [sp], #32
  ldp q12, q13, [sp], #32
  ldp q10, q11, [sp], #32
  ldp q8, q9, [sp], #32
  ldp x19, x30, [sp], #16
  ret

.global probe_goal_scratch_return
.type probe_goal_scratch_return, %function
probe_goal_scratch_return:
  // GOAL's masked vector/shuffle emitters use these reserved scratch registers.
  movi v14.16b, #0xaa
  movi v15.16b, #0xbb
  mov x8, #0x5678
  ret

.section .note.GNU-stack,"",%progbits
