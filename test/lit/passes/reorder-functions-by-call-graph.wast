;; RUN: foreach %s %t wasm-opt -all --reorder-functions-by-call-graph -S -o - | filecheck %s
;; RUN: foreach %s %t wasm-opt -all --reorder-functions-by-call-graph=hfsort -S -o - | filecheck %s --check-prefix=HFSORT
;; RUN: foreach %s %t wasm-opt -all --reorder-functions-by-call-graph=hfsort-estimated -S -o - | filecheck %s --check-prefix=ESTIMATED
;; RUN: echo '(module)' | not wasm-opt - --reorder-functions-by-call-graph=invalid -o /dev/null 2>&1 | filecheck %s --check-prefix=ERROR
;; ERROR: expected textbook, hfsort, or hfsort-estimated for reorder-functions-by-call-graph

(module
  ;; HFSORT: (import "env" "external" (func $external
  ;; HFSORT: (func $left
  ;; HFSORT: (func $shared
  ;; HFSORT: (func $entry
  ;; HFSORT: (func $right
  ;; HFSORT: (func $unused1
  ;; HFSORT: (func $unused2
  ;; CHECK: (import "env" "external" (func $external
  ;; CHECK: (func $unused1
  ;; CHECK: (func $entry
  ;; CHECK: (func $right
  ;; CHECK: (func $left
  ;; CHECK: (func $shared
  ;; CHECK: (func $unused2
  (import "env" "external" (func $external))
  (func $unused1)
  (func $shared (call $left))
  (func $left
    (call $external)
    (call $shared)
    (call $shared)
  )
  (func $unused2)
  (func $right (return_call $shared))
  (func $entry
    (call $right)
    (call $left)
    (call $entry)
  )
  (export "entry" (func $entry))
)

(module
  ;; HFSORT: (func $exported
  ;; HFSORT: (func $callee
  ;; HFSORT: (func $table_target
  ;; HFSORT: (func $unused
  ;; HFSORT: (func $start
  ;; CHECK: (func $table_target
  ;; CHECK: (func $unused
  ;; CHECK: (func $start
  ;; CHECK: (func $exported
  ;; CHECK: (func $callee
  (type $signature (func))
  (table 1 funcref)
  (elem (i32.const 0) $table_target)
  (func $table_target)
  (func $unused)
  (func $callee)
  (func $exported
    (call $callee)
    (call_indirect (type $signature) (i32.const 0))
  )
  (func $start (call $callee))
  (export "exported" (func $exported))
  (export "alias" (func $exported))
  (start $start)
)

(module
  ;; HFSORT: (func $first
  ;; HFSORT: (func $second
  ;; CHECK: (func $first
  ;; CHECK: (func $second
  (func $first (call $second))
  (func $second)
)

(module)

(module
  ;; HFSORT: (func $caller
  ;; HFSORT: (func $weak
  ;; HFSORT: (func $strong
  ;; HFSORT: (func $shared
  ;; CHECK: (func $caller
  ;; CHECK: (func $strong
  ;; CHECK: (func $shared
  ;; CHECK: (func $weak
  (func $weak)
  (func $shared)
  (func $strong
    (call $shared)
    (call $shared)
  )
  (func $caller
    (call $weak)
    (call $weak)
    (call $shared)
    (call $strong)
    (call $strong)
    (call $strong)
  )
)

(module
  ;; HFSORT: (func $shared_target
  ;; HFSORT: (func $caller0
  ;; HFSORT: (func $caller1
  ;; HFSORT: (func $caller2
  ;; HFSORT: (func $caller3
  ;; HFSORT: (func $caller4
  ;; HFSORT: (func $caller5
  ;; HFSORT: (func $caller6
  ;; HFSORT: (func $caller7
  ;; HFSORT: (func $caller8
  ;; HFSORT: (func $caller9
  (func $caller0 (call $shared_target))
  (func $shared_target)
  (func $caller1 (call $shared_target))
  (func $caller2 (call $shared_target))
  (func $caller3 (call $shared_target))
  (func $caller4 (call $shared_target))
  (func $caller5 (call $shared_target))
  (func $caller6 (call $shared_target))
  (func $caller7 (call $shared_target))
  (func $caller8 (call $shared_target))
  (func $caller9 (call $shared_target))
)

(module
  ;; ESTIMATED: (func $loop_entry
  ;; ESTIMATED: (func $loop_hot
  ;; ESTIMATED: (func $loop_cold
  (func $loop_cold)
  (func $loop_hot)
  (func $loop_entry (param $condition i32)
    (call $loop_cold)
    (call $loop_cold)
    (call $loop_cold)
    (loop $again
      (call $loop_hot)
      (br_if $again (local.get $condition))
    )
  )
  (export "loop_entry" (func $loop_entry))
)

(module
  ;; ESTIMATED: (func $hint_entry
  ;; ESTIMATED: (func $hint_false
  ;; ESTIMATED: (func $hint_true
  (func $hint_true)
  (func $hint_false)
  (func $hint_entry (param $condition i32)
    (@metadata.code.branch_hint "\00")
    (if (local.get $condition)
      (then (call $hint_true))
      (else (call $hint_false))
    )
  )
  (export "hint_entry" (func $hint_entry))
)

(module
  ;; ESTIMATED: (func $trap_entry
  ;; ESTIMATED: (func $trap_common
  ;; ESTIMATED: (func $trap_rare
  (func $trap_rare)
  (func $trap_common)
  (func $trap_entry (param $condition i32)
    (if (local.get $condition)
      (then
        (call $trap_rare)
        (call $trap_rare)
        (call $trap_rare)
        (unreachable)
      )
      (else (call $trap_common))
    )
  )
  (export "trap_entry" (func $trap_entry))
)

(module
  ;; ESTIMATED: (func $no_backedge_entry
  ;; ESTIMATED: (func $no_backedge_other
  ;; ESTIMATED: (func $no_backedge_target
  (func $no_backedge_other)
  (func $no_backedge_target)
  (func $no_backedge_entry
    (call $no_backedge_other)
    (call $no_backedge_other)
    (loop (call $no_backedge_target))
  )
  (export "no_backedge_entry" (func $no_backedge_entry))
)

(module
  ;; ESTIMATED: (func $propagation_entry
  ;; ESTIMATED: (func $hot_parent
  ;; ESTIMATED: (func $propagated_hot
  ;; ESTIMATED: (func $cold_parent
  ;; ESTIMATED: (func $propagated_cold
  (func $propagated_cold)
  (func $propagated_hot)
  (func $cold_parent (call $propagated_cold))
  (func $hot_parent (call $propagated_hot))
  (func $propagation_entry (param $condition i32)
    (call $cold_parent)
    (loop $again
      (call $hot_parent)
      (br_if $again (local.get $condition))
    )
  )
  (export "propagation_entry" (func $propagation_entry))
)

(module
  ;; ESTIMATED: (func $recursive_first
  ;; ESTIMATED: (func $recursive_second
  ;; ESTIMATED: (func $recursive_unreachable
  (func $recursive_first
    (call $recursive_first)
    (call $recursive_second)
  )
  (func $recursive_second (call $recursive_first))
  (func $recursive_unreachable)
  (export "recursive_first" (func $recursive_first))
)

(module
  ;; ESTIMATED: (func $hint_loop_entry
  ;; ESTIMATED: (func $hint_loop_other
  ;; ESTIMATED: (func $hint_loop_target
  (func $hint_loop_target)
  (func $hint_loop_other)
  (func $hint_loop_entry (param $condition i32)
    (call $hint_loop_other)
    (call $hint_loop_other)
    (loop $again
      (call $hint_loop_target)
      (@metadata.code.branch_hint "\00")
      (br_if $again (local.get $condition))
    )
  )
  (export "hint_loop_entry" (func $hint_loop_entry))
)

(module
  ;; ESTIMATED: (func $throw_entry
  ;; ESTIMATED: (func $throw_common
  ;; ESTIMATED: (func $throw_rare
  (tag $error)
  (func $throw_rare)
  (func $throw_common)
  (func $throw_entry (param $condition i32)
    (if (local.get $condition)
      (then
        (call $throw_rare)
        (call $throw_rare)
        (call $throw_rare)
        (throw $error)
      )
      (else (call $throw_common))
    )
  )
  (export "throw_entry" (func $throw_entry))
)

(module
  ;; ESTIMATED: (func $constant_entry
  ;; ESTIMATED: (func $constant_live
  ;; ESTIMATED: (func $constant_dead
  (func $constant_dead)
  (func $constant_live)
  (func $constant_entry
    (if (i32.const 0)
      (then
        (call $constant_dead)
        (call $constant_dead)
        (call $constant_dead)
      )
      (else (call $constant_live))
    )
    (return)
    (call $constant_dead)
  )
  (export "constant_entry" (func $constant_entry))
)

(module
  ;; ESTIMATED: (func $return_entry
  ;; ESTIMATED: (func $return_common
  ;; ESTIMATED: (func $return_other
  (func $return_other)
  (func $return_common)
  (func $return_entry (param $condition i32)
    (if (local.get $condition)
      (then
        (call $return_common)
        (call $return_common)
        (call $return_common)
        (return)
      )
      (else (call $return_other))
    )
  )
  (export "return_entry" (func $return_entry))
)

(module
  ;; ESTIMATED: (func $infinite_entry
  ;; ESTIMATED: (func $infinite_hot
  ;; ESTIMATED: (func $infinite_other
  (func $infinite_other)
  (func $infinite_hot)
  (func $infinite_entry (param $condition i32)
    (if (local.get $condition)
      (then
        (loop $again (call $infinite_hot) (br $again))
      )
      (else (call $infinite_other))
    )
  )
  (export "infinite_entry" (func $infinite_entry))
)

(module
  ;; ESTIMATED: (func $catch_entry
  ;; ESTIMATED: (func $catch_common
  ;; ESTIMATED: (func $catch_rare
  (import "env" "may_throw" (func $may_throw))
  (func $catch_rare)
  (func $catch_common)
  (func $catch_entry
    (try
      (do
        (call $may_throw)
        (call $catch_common)
      )
      (catch_all
        (call $catch_rare)
        (call $catch_rare)
        (call $catch_rare)
      )
    )
  )
  (export "catch_entry" (func $catch_entry))
)

(module
  ;; ESTIMATED: (func $global_parent
  ;; ESTIMATED: (func $global_child
  ;; ESTIMATED: (func $global_unused
  (global funcref (ref.func $global_parent))
  (func $global_unused)
  (func $global_child)
  (func $global_parent (call $global_child))
)

(module
  ;; ESTIMATED: (func $start_parent
  ;; ESTIMATED: (func $start_child
  ;; ESTIMATED: (func $start_unused
  (func $start_unused)
  (func $start_child)
  (func $start_parent (call $start_child))
  (start $start_parent)
)

(module
  ;; ESTIMATED: (func $table_parent
  ;; ESTIMATED: (func $table_child
  ;; ESTIMATED: (func $table_unused
  (func $table_unused)
  (func $table_child)
  (func $table_parent (call $table_child))
  (table 1 funcref)
  (elem (i32.const 0) $table_parent)
)

(module
  ;; ESTIMATED: (func $nested_entry
  ;; ESTIMATED: (func $nested_inner
  ;; ESTIMATED: (func $nested_outer
  (func $nested_outer)
  (func $nested_inner)
  (func $nested_entry (param $condition i32)
    (loop $outer
      (call $nested_outer)
      (call $nested_outer)
      (call $nested_outer)
      (loop $inner
        (call $nested_inner)
        (br_if $inner (local.get $condition))
      )
      (br_if $outer (local.get $condition))
    )
  )
  (export "nested_entry" (func $nested_entry))
)

(module
  ;; ESTIMATED: (func $early_exit_entry
  ;; ESTIMATED: (func $early_exit_hot
  ;; ESTIMATED: (func $early_exit_other
  (func $early_exit_other)
  (func $early_exit_hot)
  (func $early_exit_entry (param $condition i32)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (call $early_exit_other)
    (block $done
      (loop $again
        (call $early_exit_hot)
        (br_if $done (local.get $condition))
        (br $again)
      )
    )
  )
  (export "early_exit_entry" (func $early_exit_entry))
)

(module
  ;; ESTIMATED: (func $zero_entry
  ;; ESTIMATED: (func $zero_nonzero
  ;; ESTIMATED: (func $zero_equal
  (func $zero_equal)
  (func $zero_nonzero)
  (func $zero_entry (param $value i32)
    (if (i32.eqz (local.get $value))
      (then (call $zero_equal))
      (else (call $zero_nonzero))
    )
  )
  (export "zero_entry" (func $zero_entry))
)

(module
  ;; ESTIMATED: (func $mask_entry
  ;; ESTIMATED: (func $mask_equal
  ;; ESTIMATED: (func $mask_other
  (func $mask_equal)
  (func $mask_other)
  (func $mask_entry (param $value i32)
    (if (i32.eqz (i32.and (local.get $value) (i32.const 4)))
      (then (call $mask_equal))
      (else (call $mask_other))
    )
  )
  (export "mask_entry" (func $mask_entry))
)

(module
  ;; ESTIMATED: (func $float_entry
  ;; ESTIMATED: (func $float_other
  ;; ESTIMATED: (func $float_equal
  (func $float_equal)
  (func $float_other)
  (func $float_entry (param $value f64)
    (if (f64.eq (local.get $value) (f64.const 0))
      (then (call $float_equal))
      (else (call $float_other))
    )
  )
  (export "float_entry" (func $float_entry))
)

(module
  ;; ESTIMATED: (func $signed_entry
  ;; ESTIMATED: (func $signed_positive
  ;; ESTIMATED: (func $signed_negative
  (func $signed_negative)
  (func $signed_positive)
  (func $signed_entry (param $value i64)
    (if (i64.lt_s (local.get $value) (i64.const 0))
      (then (call $signed_negative))
      (else (call $signed_positive))
    )
  )
  (export "signed_entry" (func $signed_entry))
)

(module
  ;; ESTIMATED: (func $override_entry
  ;; ESTIMATED: (func $override_equal
  ;; ESTIMATED: (func $override_other
  (func $override_other)
  (func $override_equal)
  (func $override_entry (param $value i32)
    (@metadata.code.branch_hint "\01")
    (if (i32.eqz (local.get $value))
      (then (call $override_equal))
      (else (call $override_other))
    )
  )
  (export "override_entry" (func $override_entry))
)

(module
  ;; ESTIMATED: (func $switch_entry
  ;; ESTIMATED: (func $switch_common
  ;; ESTIMATED: (func $switch_rare
  (func $switch_rare)
  (func $switch_common)
  (func $switch_entry (param $index i32)
    (block $done
      (block $rare
        (block $common
          (br_table $common $common $common $common $common
                    $common $common $common $common $rare (local.get $index))
        )
        (call $switch_common)
        (br $done)
      )
      (call $switch_rare)
      (call $switch_rare)
    )
  )
  (export "switch_entry" (func $switch_entry))
)

(module
  ;; ESTIMATED: (func $constant_switch_entry
  ;; ESTIMATED: (func $constant_switch_common
  ;; ESTIMATED: (func $constant_switch_rare
  (func $constant_switch_rare)
  (func $constant_switch_common)
  (func $constant_switch_entry
    (block $done
      (block $rare
        (block $common
          (br_table $common $rare (i32.const 0))
        )
        (call $constant_switch_common)
        (br $done)
      )
      (call $constant_switch_rare)
      (call $constant_switch_rare)
    )
  )
  (export "constant_switch_entry" (func $constant_switch_entry))
)

(module
  ;; ESTIMATED: (func $default_switch_entry
  ;; ESTIMATED: (func $default_switch_common
  ;; ESTIMATED: (func $default_switch_rare
  (func $default_switch_rare)
  (func $default_switch_common)
  (func $default_switch_entry
    (block $done
      (block $common
        (block $rare
          (br_table $rare $common (i32.const -1))
        )
        (call $default_switch_rare)
        (call $default_switch_rare)
        (br $done)
      )
      (call $default_switch_common)
    )
  )
  (export "default_switch_entry" (func $default_switch_entry))
)

(module
  ;; ESTIMATED: (func $inside_entry
  ;; ESTIMATED: (func $inside_likely
  ;; ESTIMATED: (func $inside_unlikely
  (func $inside_unlikely)
  (func $inside_likely)
  (func $inside_entry (param $value i32)
    (loop $again
      (if (i32.eqz (local.get $value))
        (then (call $inside_unlikely))
        (else (call $inside_likely))
      )
      (br_if $again (local.get $value))
    )
  )
  (export "inside_entry" (func $inside_entry))
)

(module
  ;; ESTIMATED: (func $cold_loop_entry
  ;; ESTIMATED: (func $cold_loop_normal
  ;; ESTIMATED: (func $cold_loop_helper
  (func $cold_loop_helper)
  (func $cold_loop_normal)
  (func $cold_loop_entry (param $condition i32)
    (if (local.get $condition)
      (then
        (loop $again
          (call $cold_loop_helper)
          (br_if $again (local.get $condition))
        )
        (unreachable)
      )
      (else (call $cold_loop_normal))
    )
  )
  (export "cold_loop_entry" (func $cold_loop_entry))
)

(module
  ;; ESTIMATED: (func $nested_cold_entry
  ;; ESTIMATED: (func $nested_cold_normal
  ;; ESTIMATED: (func $nested_cold_helper
  (func $nested_cold_helper)
  (func $nested_cold_normal)
  (func $nested_cold_entry (param $condition i32)
    (if (local.get $condition)
      (then
        (loop $outer
          (loop $inner
            (call $nested_cold_helper)
            (br_if $inner (local.get $condition))
          )
          (br_if $outer (local.get $condition))
        )
        (unreachable)
      )
      (else (call $nested_cold_normal))
    )
  )
  (export "nested_cold_entry" (func $nested_cold_entry))
)