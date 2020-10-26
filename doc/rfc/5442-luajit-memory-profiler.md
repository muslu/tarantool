# LuaJIT memory profiler

* **Status**: In progress
* **Start date**: 24-10-2020
* **Authors**: Sergey Kaplun @Buristan skaplun@tarantool.org,
               Igor Munkin @igormunkin imun@tarantool.org,
               Sergey Ostanevich @sergos sergos@tarantool.org
* **Issues**: [#5442](https://github.com/tarantool/tarantool/issues/5442)

## Summary

LuaJIT memory profiler is a toolchain for analysis of memory usage by user's
application.

## Background and motivation

Garbage collector (GC) is a curse of performance for most of Lua applications.
Memory usage of Lua application should be profiled to find out various
memory-unoptimized code blocks. If the application has memory leaks they can be
found with the profiler.

## Detailed design

The whole toolchain of memory profiling will be divided by several parts:
1) Prerequisites.
2) Recording information about memory usage and saving it.
3) Reading saved data and display it in human-readable format.

### Prerequisites

This section describes additional changes in LuaJIT required to feature
implementation. This version of LuaJIT memory profiler does not support
reporting allocations from traces. But trace code semantics should be totally
the same as for Lua interpreter. So profiling with `jit.off()` should be
enough.

There are two different representations of functions in LuaJIT: the function's
prototype (`GCproto`) and the function object so called closure (`GCfunc`).
The closures are represented as `GCfuncL` and `GCfuncC` for Lua and C closures
correspondingly. Also LuaJIT has special function's type aka Fast Function. It
is used for LuaJIT builtins.

Fast function allocation events always belong to the previous frame with
considering of tail call optimizations (TCO).

Assume we have the following Lua chunk named <test.lua>:

```
1  jit.off()
2  misc.memprof.start("memprof_new.bin")
3  local function append(str, rep)
4      return string.rep(str, rep)
5  end
6
7  local t = {}
8  for _ = 1, 1e5 do
9      table.insert(t,
10         append('q', _)
11     )
12 end
13 misc.memprof.stop()
```

Profilers output is like the follows:
```
ALLOCATIONS
@test.lua:0, line 10: 100007 5004638934      0
@test.lua:0, line 5: 1       40      0
@test.lua:0, line 7: 1       72      0
@test.lua:0, line 9: 1       48      0

REALLOCATIONS
@test.lua:0, line 9: 16      4194496 2097376
        Overrides:
                @test.lua:0, line 9

@test.lua:0, line 10: 12     262080  131040
        Overrides:
                @test.lua:0, line 10


DEALLOCATIONS
INTERNAL: 21    0       2463
@test.lua:0, line 10: 8      0       1044480
        Overrides:
                @test.lua:0, line 10
```

In Lua functions for profile events, we had to determine the line number of the
function definition and corresponding `GCproto` address. For C functions only
address will be enough. If Fast function is called from Lua function we had to
report the Lua function for more meaningful output. Otherwise report the C
function.

So we need to know in what type of function CALL/RETURN virtual machine (VM)
is. LuaJIT has already determined C function execution VM state but neither
Fast functions nor Lua function. So corresponding VM states will be added.

To determine currently allocating coroutine (that may not be equal to currently
executed) new field will be added to `global_State` structure named `mem_L`
kept coroutine address. This field sets at each reallocation to corresponding
`L` with which it was called.

There is the static function (`lj_debug_getframeline`) returned line number for
current `BCPos` in `lj_debug.c` already. It will be added to the debug module
API to be used in memory profiler.

### Information recording

Each allocate/reallocate/free is considered as a type of event that are
reported. Event stream has the following format:

```c
/*
** Event stream format:
**
** stream         := symtab memprof
** symtab         := see <ljp_symtab.h>
** memprof        := prologue event* epilogue
** prologue       := 'l' 'j' 'm' version reserved
** version        := <BYTE>
** reserved       := <BYTE> <BYTE> <BYTE>
** event          := event-alloc | event-realloc | event-free
** event-alloc    := event-header loc? naddr nsize
** event-realloc  := event-header loc? oaddr osize naddr nsize
** event-free     := event-header loc? oaddr osize
** event-header   := <BYTE>
** loc            := loc-lua | loc-c
** loc-lua        := sym-addr line-no
** loc-c          := sym-addr
** sym-addr       := <ULEB128>
** line-no        := <ULEB128>
** oaddr          := <ULEB128>
** naddr          := <ULEB128>
** osize          := <ULEB128>
** nsize          := <ULEB128>
** epilogue       := event-header
**
** <BYTE>   :  A single byte (no surprises here)
** <ULEB128>:  Unsigned integer represented in ULEB128 encoding
**
** (Order of bits below is hi -> lo)
**
** version: [VVVVVVVV]
**  * VVVVVVVV: Byte interpreted as a plain integer version number
**
** event-header: [FTUUSSEE]
**  * EE   : 2 bits for representing allocation event type (AEVENT_*)
**  * SS   : 2 bits for representing allocation source type (ASOURCE_*)
**  * UU   : 2 unused bits
**  * T    : Reserved. 0 for regular events, 1 for the events marked with
**           the timestamp mark. It is assumed that the time distance between
**           two marked events is approximately the same and is equal
**           to 1 second. Always zero for now.
**  * F    : 0 for regular events, 1 for epilogue's *F*inal header
**           (if F is set to 1, all other bits are currently ignored)
*/
```

It is enough to know the address of LUA/C function to determine it. Symbolic
table (symtab) dumps at start of profiling to avoid determine and write line
number of Lua code and corresponding chunk of code each time, when memory event
happens. Each line contains the address, Lua chunk definition as the filename
and line number of the function's declaration. This table of symbols has the
following format described at <ljp_symtab.h>:

```c
/*
** symtab format:
**
** symtab         := prologue sym*
** prologue       := 'l' 'j' 's' version reserved
** version        := <BYTE>
** reserved       := <BYTE> <BYTE> <BYTE>
** sym            := sym-lua | sym-final
** sym-lua        := sym-header sym-addr sym-chunk sym-line
** sym-header     := <BYTE>
** sym-addr       := <ULEB128>
** sym-chunk      := string
** sym-line       := <ULEB128>
** sym-final      := sym-header
** string         := string-len string-payload
** string-len     := <ULEB128>
** string-payload := <BYTE> {string-len}
**
** <BYTE>   :  A single byte (no surprises here)
** <ULEB128>:  Unsigned integer represented in ULEB128 encoding
**
** (Order of bits below is hi -> lo)
**
** version: [VVVVVVVV]
**  * VVVVVVVV: Byte interpreted as a plain numeric version number
**
** sym-header: [FUUUUUTT]
**  * TT    : 2 bits for representing symbol type
**  * UUUUU : 5 unused bits
**  * F     : 1 bit marking the end of the symtab (final symbol)
*/
```

So when memory profiling starts default allocation function is replaced by the
new allocation function as additional wrapper to write inspected profiling
events. When profiler stops old allocation function is substituted back.

Starting profiler from Lua is quite simple:
```lua
local started, err = misc.memprof.start(fname)
```
Where `fname` is name of the file where profile events are written. Writer for
this function perform `fwrite()` for each call retrying in case of `EINTR`.
Final callback calls `fclose()` at the end of profiling. If it is impossible to
open a file for writing or profiler fails to start, returns `nil` on failure
(plus an error message as a second result and a system-dependent error code as
a third result). Otherwise returns some true value.

Stopping profiler from Lua is simple too:
```lua
local stopped, err = misc.memprof.stop()
```

If there is any error occurred at profiling stopping (an error when file
descriptor was closed) `memprof.stop()` returns `nil` (plus an error message as
a second result and a system-dependent error code as a third result). Returns
`true` otherwise.

If you want to build LuaJIT without memory profiler, you should build it with
`-DLUAJIT_DISABLE_MEMPROF`. If it is disabled `misc.memprof.start()` and
`misc.memprof.stop()` always return `false`.

Memory profiler is expected to be thread safe, so it has a corresponding
lock/unlock at internal mutex whenever you call `luaM_memprof_*`. If you want
to build LuaJIT without thread safety use `-DLUAJIT_DISABLE_THREAD_SAFE`.

### Reading and displaying saved data

Binary data can be read by `lj-parse-memprof` utility. It parses the binary
format provided from memory profiler and render it in human-readable format.

The usage is very simple:
```
$ ./luajit-parse-memprof --help
luajit-parse-memprof - parser of the memory usage profile collected
                       with LuaJIT's memprof.

SYNOPSIS

luajit-parse-memprof [options] memprof.bin

Supported options are:

  --help                            Show this help and exit
```

Plain text of profiled info has the following format:
```
@<filename>:<function_line>, line <line where event was detected>: <number of events>	<allocated>	<freed>
```
See example above.

`INTERNAL` means that this allocations are caused by internal LuaJIT
structures. Note that events are sorted from the most often to the least.

`Overrides` means what allocation this reallocation overrides.

## Benchmarks

Benchmarks were taken from repo:
[LuaJIT-test-cleanup](https://github.com/LuaJIT/LuaJIT-test-cleanup).

Example of usage:
```bash
/usr/bin/time -f"array3d %U" ./luajit $BENCH_DIR/array3d.lua 300 >/dev/null
```

Benchmark results before and after the patch (less is better):

```
               | BEFORE | AFTER,memprof off | AFTER,memprof on
---------------+--------+-------------------+-----------------
array3d        |  0.22  |        0.20       |       0.21
binary-trees   |  3.32  |        3.33       |       3.94
chameneos      |  2.92  |        3.18       |       3.12
coroutine-ring |  0.99  |        1.00       |       0.99
euler14-bit    |  1.04  |        1.05       |       1.03
fannkuch       |  6.77  |        6.69       |       6.64
fasta          |  8.27  |        8.30       |       8.25
life           |  0.48  |        0.48       |       1.03
mandelbrot     |  2.69  |        2.70       |       2.75
mandelbrot-bit |  1.99  |        2.00       |       2.08
md5            |  1.57  |        1.61       |       1.56
nbody          |  1.35  |        1.38       |       1.33
nsieve         |  2.11  |        2.19       |       2.09
nsieve-bit     |  1.50  |        1.55       |       1.47
nsieve-bit-fp  |  4.40  |        4.63       |       4.44
partialsums    |  0.54  |        0.58       |       0.55
pidigits-nogmp |  3.48  |        3.50       |       3.47
ray            |  1.63  |        1.68       |       1.64
recursive-ack  |  0.19  |        0.22       |       0.20
recursive-fib  |  1.62  |        1.71       |       1.63
scimark-fft    |  5.78  |        5.94       |       5.69
scimark-lu     |  3.26  |        3.57       |       3.59
scimark-sor    |  2.34  |        2.35       |       2.33
scimark-sparse |  5.03  |        4.92       |       4.91
series         |  0.94  |        0.96       |       0.95
spectral-norm  |  0.96  |        0.96       |       0.95
```
