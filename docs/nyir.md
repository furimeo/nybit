// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)

# Nybit Intermediate Representation (NyIR)

NyIR is the textual intermediate representation used by the Nybit compiler infrastructure. It is a static single assignment (SSA) IR designed for low-level code generation, optimization, and JIT execution.

## Syntax Overview

A NyIR module consists of top-level declarations separated by `;;`:

```
@function name(params) -> ret_type;
.block;
    %value = opcode operands;
    @return %value;
;;
```

### Top-Level Declarations

#### Functions

```
@function name(param_list) -> ret_type;
```

Parameters use `%name: type` syntax. The return type is mandatory.

```
@function add(%a: i32, %b: i32) -> i32;
.entry;
    %r = add %a, %b;
    @return %r;
;;
```

#### Globals

Globals are module-level variables. `@readonly` marks them as constant data.

```
@global @readonly @answer: i32 = 42;
@global @counter: i32 = 0;
@global @buffer: i32;
```

A global without an initializer is placed in BSS (zero-initialized).

#### Type Declarations

Struct types are defined with `@type`:

```
@type Pair = struct { a: i32, b: i32 };
@type Big = struct { a: i64, b: i64, c: i64 };
@type Vec3 = struct { x: f32, y: f32, z: f32 };
```

### Types

| Keyword | Size | Description |
|---------|------|-------------|
| `void` | 0 | No value |
| `i8` | 1 | Signed 8-bit integer |
| `i16` | 2 | Signed 16-bit integer |
| `i32` | 4 | Signed 32-bit integer |
| `i64` | 8 | Signed 64-bit integer |
| `f32` | 4 | 32-bit float |
| `f64` | 8 | 64-bit double |
| `ptr` | 8 | Pointer |
| `bool` | 1 | Boolean |

### Blocks

Functions contain one or more basic blocks. The first block must be named `.entry`. Block labels start with `.`:

```
.entry;
    ...
.else;
    ...
```

### SSA Values

All values are in SSA form, prefixed with `%`. Each value is assigned exactly once.

## Instruction Reference

### Constants

```
%v = const 42;
%f = const 3.14;
```

### Arithmetic

| Opcode | Signed/Unsigned | Description |
|--------|-----------------|-------------|
| `add` | n/a | Integer addition |
| `sub` | n/a | Integer subtraction |
| `mul` | n/a | Integer multiplication |
| `div.s` / `div` | signed | Signed integer division |
| `div.u` | unsigned | Unsigned integer division |
| `rem.s` / `rem` | signed | Signed remainder |
| `rem.u` | unsigned | Unsigned remainder |
| `neg` | n/a | Negation |

### Floating-Point Arithmetic

| Opcode | Description |
|--------|-------------|
| `fadd` | Float addition |
| `fsub` | Float subtraction |
| `fmul` | Float multiplication |
| `fdiv` | Float division |
| `frem` | Float remainder |
| `fneg` | Float negation |

### Bitwise Operations

```
%r = and %a, %b;
%r = or %a, %b;
%r = xor %a, %b;
%r = not %a;
%r = shl %a, %b;
%r = shr %a, %b;
%r = sar %a, %b;
%r = rotl %a, %b;
%r = rotr %a, %b;
```

### Comparisons

Integer comparisons produce `i1` results (0 or 1):

| Opcode | Description |
|--------|-------------|
| `cmp.eq` | Equal |
| `cmp.ne` | Not equal |
| `cmp.lt.s` / `cmp.lt` | Less than (signed) |
| `cmp.lt.u` | Less than (unsigned) |
| `cmp.le.s` / `cmp.le` | Less or equal (signed) |
| `cmp.le.u` | Less or equal (unsigned) |
| `cmp.gt.s` / `cmp.gt` | Greater than (signed) |
| `cmp.gt.u` | Greater than (unsigned) |
| `cmp.ge.s` / `cmp.ge` | Greater or equal (signed) |
| `cmp.ge.u` | Greater or equal (unsigned) |

Floating-point comparisons:

| Opcode | Description |
|--------|-------------|
| `fcmp.eq` | Float equal |
| `fcmp.ne` | Float not equal |
| `fcmp.lt` | Float less than |
| `fcmp.le` | Float less or equal |
| `fcmp.gt` | Float greater than |
| `fcmp.ge` | Float greater or equal |

### Control Flow

#### Branch (unconditional)

```
@branch .target_block;
```

#### Conditional Branch

```
@branch_if %cond, .true_block, .false_block;
```

#### Return

```
@return %value;
@return;
```

#### Trap

```
@trap;
```

#### Select

```
%r = select %cond, %true_val, %false_val;
```

### Memory Operations

#### Global Address

```
%p = global_addr @global_name;
```

#### Load

```
%v = load %ptr;
```

#### Store

```
store %ptr, %value;
```

#### Stack Operations

```
%slot = stack_slot;
%addr = stack_addr %slot;
```

### Function Calls

#### Direct Call

```
%r = call @function_name, %arg1, %arg2;
```

#### Indirect Call

```
%r = call_indirect %fn_ptr, %arg1, %arg2;
```

### Type Conversions

| Opcode | Description |
|--------|-------------|
| `cast` | Generic cast |
| `extend` | Zero extend |
| `truncate` | Truncate to smaller type |
| `bitcast` | Reinterpret bits |
| `sext` | Sign extend |
| `zext` | Zero extend |
| `fext` | Float extend (f32 to f64) |
| `ftrunc` | Float truncate (f64 to f32) |
| `sitofp` | Signed int to float |
| `uitofp` | Unsigned int to float |
| `fptosi` | Float to signed int |
| `fptoui` | Float to unsigned int |

### Atomic Operations

```
%v = atomic_load %ptr;
atomic_store %ptr, %value;
%old = atomic_rmw %ptr, %new;
%ok = atomic_cmpxchg %ptr, %expected, %desired;
fence;
```

## Complete Examples

### Example 1: Simple Arithmetic

```
@function main() -> i32;
.entry;
    %a = const 40;
    %b = const 2;
    %r = add %a, %b;
    @return %r;
;;
```

Compiles to an executable that exits with code 42.

### Example 2: Function Calls

```
@function helper() -> i32;
.entry;
    %v = const 42;
    @return %v;
;;
@function main() -> i32;
.entry;
    %r = call @helper;
    @return %r;
;;
```

### Example 3: Control Flow (Absolute Value)

```
@function abs(%x: i32) -> i32;
.entry;
    %zero = const 0;
    %cond = cmp.lt.s %x, %zero;
    @branch_if %cond, .negative, .positive;
.negative;
    %r1 = neg %x;
    @return %r1;
.positive;
    @return %x;
;;
```

### Example 4: Globals

```
@global @readonly @base: i32 = 20;
@global @accum: i32 = 12;

@function compute() -> i32;
.entry;
    %p_base = global_addr @base;
    %base = load %p_base;
    %p_accum = global_addr @accum;
    %accum = load %p_accum;
    %sum = add %base, %accum;
    %c10 = const 10;
    %total = add %sum, %c10;
    %p_temp = global_addr @accum;
    store %p_temp, %total;
    %res = load %p_temp;
    @return %res;
;;
```

### Example 5: Struct Types and Aggregate ABI

```
@type Pair = struct { a: i32, b: i32 };

@function id_pair(%p: Pair) -> Pair;
.entry;
    @return %p;
;;

@function forward_pair(%p: Pair) -> Pair;
.entry;
    %r = call @id_pair, %p;
    @return %r;
;;
```

On AArch64, `Pair` (8 bytes) is classified as `GPR_AGG` and passed in a single GPR.

### Example 6: Large Aggregate (sret)

```
@type Big = struct { a: i64, b: i64, c: i64 };

@function make_big() -> Big;
.entry;
    %p = global_addr @some_big;
    %v = load %p;
    @return %v;
;;
```

`Big` (24 bytes) exceeds the 16-byte GPR limit and is passed via the sret buffer (X8 on AArch64).

### Example 7: HFA (Homogeneous Floating-point Aggregate)

```
@type Vec3 = struct { x: f32, y: f32, z: f32 };

@function transform(%v: Vec3) -> Vec3;
.entry;
    @return %v;
;;
```

`Vec3` is classified as `HFA` with 3 float members, passed in V0-V2 on AArch64.

### Example 8: Register Exhaustion

```
@type Pair = struct { a: i32, b: i32 };

@function exhaust(%a1: i64, %a2: i64, %a3: i64, %a4: i64,
                   %a5: i64, %a6: i64, %a7: i64, %a8: i64,
                   %p1: Pair, %p2: Pair) -> Pair;
.entry;
    @return %p1;
;;
```

After 8 GPR arguments exhaust X0-X7, remaining aggregate arguments spill to the stack.

### Example 9: Mixed Arguments

```
@type Pair = struct { a: i32, b: i32 };
@type Big = struct { a: i64, b: i64, c: i64 };

@function mixed(%x: i32, %p: Pair, %y: i64, %b: Big) -> Pair;
.entry;
    @return %p;
;;
```

### Example 10: Loops (via Branch)

```
@function sum_to(%n: i32) -> i32;
.entry;
    %zero = const 0;
    %one = const 1;
    %acc = const 0;
    @branch .loop;
.loop;
    %cond = cmp.lt.s %acc, %n;
    @branch_if %cond, .body, .done;
.body;
    %next = add %acc, %one;
    @branch .loop;
.done;
    @return %acc;
;;
```

## CLI Usage

```bash
# Compile to executable (default target)
nybit input.ny -o output

# Compile to object file
nybit input.ny --target aarch64 --emit-obj -o output.o

# Emit assembly text
nybit input.ny --target aarch64 --emit-x86 -o output.s

# Link object files
nybit link --target=elf64-aarch64 --entry=main input.o -o output

# Compile to executable (explicit target)
nybit input.ny --target elf64-aarch64 -o output
```

## IR Validation

The Nybit validator enforces:
- SSA dominance: every value is defined before use
- Phi node invariants: phi inputs match predecessor block count
- Type consistency: operand types match instruction expectations
- Block termination: every block ends with a terminator (`@return`, `@branch`, `@branch_if`, `@trap`)

Invalid IR produces diagnostics with file, line, and column information.
