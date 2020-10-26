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
4) Additional features.
5) Integration within Tarantool.

### Prerequisites

This section describes additional changes in LuaJIT required to feature
implementation.

There are two different representation of functions in LuaJIT: the function's
prototype (`GCproto`) and the function's closure (`GCfunc`). The closures are
represented as `GCfuncL` and `GCfuncC` for Lua and C closures correspondingly.
Also LuaJIT has special function's type aka Fast Function. It is used for
specially optimized standard Lua libraries functions.

For Lua functions for profile events we had to determine line number of the
function definition and corresponding `GCproto` address. For C functions only
address will be enough. If Fast function is called from Lua function we had to
report the Lua function for more meaningful output. Otherwise report the C
function.

So we need to know in what type of function CALL/RETURN virtual machine (VM)
is. LuaJIT has already determined C function execution VM state but neither
Fast functions nor Lua function. So corresponding VM states will be added.

To determine currently allocating coroutine (that may not be equal to currently
executed) new field will be added to `global_State` structure named `mem_L`
kept coroutine address.

There is the static function (`lj_debug_getframeline`) returned line number for
current `BCPos` in `lj_debug.c` already. It will be added to debug module API
to be used in memory profiler.

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
** prof-id        := <ULEB128>
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
**           to 1 second.
**  * F    : 0 for regular events, 1 for epilogue's *F*inal header
**           (if F is set to 1, all other bits are currently ignored)
*/
```

It iss enough to know the address of LUA/C function to determine it. Symbolic
table (symtab) dumps at start of profiling to avoid determine and write line
number of Lua code and corresponding chunk of code each time, when memory event
happens. Each line contains the address, Lua chunk definition as filename and
line number of the function's declaration. This symbols table has the following
format described at
<ljp_symtab.h>:

```c
/*
** symtab format:
**
** symtab         := prologue sym*
** prologue       := 'l' 'j' 's' version reserved vm-addr
** version        := <BYTE>
** reserved       := <BYTE> <BYTE> <BYTE>
** vm-addr        := <ULEB128>
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

As you can see the most part of data is saved in
[ULEB128](https://en.wikipedia.org/wiki/LEB128) format.
So when memory profiling starts default allocation function is replaced by the
new allocation function as additional wrapper to write inspected profiling
events. When profiler stops old allocation function is substituted back.

Extended functions to control profiler are added to <lmisclib.h>.
Profiler is configured by this options structure:

```c
/* Profiler options. */
struct luam_Prof_options {
  /* Options for the profile writer and final callback. */
  void *arg;
  /*
  ** Writer function for profile events.
  ** Should return amount of written bytes on success or zero in case of error.
  */
  size_t (*writer)(const void *data, size_t len, void *arg);
  /*
  ** Callback on profiler stopping. Required for correctly cleaning
  ** at vm shoutdown when profiler still running.
  ** Returns zero on success.
  */
  int (*on_stop)(void *arg);
};
```

This options are saved inside the profiler till it is running. Argument (`arg`)
for them will be passed to all writers and callbacks to be used as a context of
profiler. `on_stop` callback determines destroyer for `arg` that is called when
profiler shutdowns. `writer` function copies data from internal profiler buffer
wherever you want. Data can be written to the file, sending to the pipe or
socket, writing to memory to be parsed later. If the writer function returns
zero, profiler takes it as a error, remembers it and will notice about that as
it will stopped. Otherwise returned value described how many bytes was
successfully written as intended.

Profiler is started by with:
```c
/*
** Starts profiling. Returns LUAM_PROFILE_SUCCESS on success and one of
** LUAM_PROFILE_ERR* codes otherwise. Destroyer is not called in case of
** LUAM_PROFILE_ERR*.
*/
LUAMISC_API int luaM_memprof_start(lua_State *L,
				   const struct luam_Prof_options *opt);
```

Profiler may fail to start with one of the `LUAM_PROFILE_ERR*` statuses that
declares as:
```c
/* Profiler public API. */
#define LUAM_PROFILE_SUCCESS 0
#define LUAM_PROFILE_ERR     1
#define LUAM_PROFILE_ERRMEM  2
#define LUAM_PROFILE_ERRIO   3
```

This may happen in several cases as when you call profiler with bad arguments
(`writer` or `on_stop` callback is undefined) or there is no enough memory to
allocate corresponding buffer or profiler is already running or it fails to
write the symtab or the prologue at start.
As written above memory profiler does not call the destroyer for your argument.
If it is failed to start -- it should be done by a caller.

As soon as you want to finish profiling, call the corresponding function:

```c
/*
** Stops profiling. Returns LUAM_PROFILE_SUCCESS on success and one of
** LUAM_PROFILE_ERR* codes otherwise. If writer() function returns zero
** on call at buffer flush, or on_stop() callback returns non-zero
** value, returns LUAM_PROFILE_ERRIO.
*/
LUAMISC_API int luaM_memprof_stop(const lua_State *L);
```

This function flushes all data from the internal buffer to writer, sends final
event header and returns back the substituted allocation function if profiling
was started from the same Lua state. Otherwise does nothing and returns
`LUAM_PROFILE_ERR`. Also this error returns if profiler is not running at the
moment of call. `LUAM_PROFILE_ERRIO` returns if there were some problems at
buffer write or `on_stop` callback has returned bad status.

And the last one function that checks is profiler running or not:

```c
/* Check that profiler is running. */
LUAMISC_API bool luaM_memprof_isrunning(void);
```

If you want to build LuaJIT without memory profiler you should build it with
`-DLUAJIT_DISABLE_MEMPROF`. If it is disabled `luaM_memprof_stop()` and
`luaM_memprof_start` always returns `LUAM_PROFILE_ERR` and
`luaM_memprof_isrunning()` always returns `false`.

Memory profiler is expected to be thread safe, so it has corresponding
lock/unlock at internal mutex whenever you call `luaM_memprof_*`. If you want
to build LuaJIT without thread safety use `-DLUAJIT_DISABLE_THREAD_SAFE`.

There are also complements introduced for Lua space in builtin `misc` library.
Starting profiler from Lua is quite simple:

```lua
local started = misc.memprof.start(fname)
```
Where `fname` is name of the file where profile events are written. This
function is just a wrapper to `luaM_memprof_start()`. Writer for this function
perform `fwrite()` for each call. Final callback calls `fclose()` at the end of
profiling. If it is impossible to open a file for writing or
`luaM_memprof_start()` returns error status the function returns `false` value.
Otherwise returns `true`.

For stopping or checking that profile is running from Lua space use
`misc.memprof.stop()` and `misc.memprof.is_running()` correspondingly.

If there is any error occurred at profiling stopping (bad returned status from
`luaM_memprof_stop()` or an error when file descriptor was closed)
`memprof.stop()` returns `false`. Returns `true` otherwise.

### Reading and displaying saved data

Binary data can be read by `lj-parse-memprof` utility. It parses binary format
provided from memory profiler and render it in human-readable format.

The usage is very simple:
```
$ ./lj-parse-memprof --help
lj-parse-memprof - parser of the memory usage profile collected
                     with LuaJIT's memprof.

SYNOPSIS

lj-parse-memprof memprof.bin [options]

Supported options are:

  --help                            Show this help and exit
```

Plain text of profiled info has the following format:
```
@<filename>:<function_line>, line <line where event was detected>: <number of events>	<allocated>	<freed>
```

It looks like:
```
$ ./lj-parse-memprof memprof.bin
ALLOCATIONS
@test_memprof.lua:0, line 10: 5	1248	0
...
INTERNAL: 2511	203000	0
...
@test_memprof.lua:0, line 8: 455	25646897	0
@test_memprof.lua:0, line 14: 260	64698	0
...
@test_memprof.lua:0, line 6: 1	72	0

REALLOCATIONS
INTERNAL: 43	3556160	7100608
	Overrides:
		...
		@test_memprof.lua:0, line 8
		INTERNAL

@test_memprof.lua:0, line 8: 35	4668416	2334208
	Overrides:
		@test_memprof.lua:0, line 8
		INTERNAL

...

@test_memprof.lua:0, line 10: 7	8240	4176
	Overrides:
		@test_memprof.lua:0, line 10

DEALLOCATIONS
INTERNAL: 3504541	0	592186069
	Overrides:
		...
		@test_memprof.lua:0, line 8
		INTERNAL

...

@test_memprof.lua:0, line 14: 9	0	28592
	Overrides:
		@test_memprof.lua:0, line 10
		@test_memprof.lua:0, line 14
		@test_memprof.lua:0, line 8

@test_memprof.lua:0, line 8: 1	0	4096
```

Where `INTERNAL` means that this allocations are caused by internal LuaJIT
structures. Note that events are sorted from the most often to the least.

### Additional features

This part describes two features: approximately time of profiling execution and
full dumping of Lua objects information.

#### Duration of memory profiler running

Memory profiler in [LuaVela](https://github.com/luavela/luavela) also provides
user to determine _approximately_ time to be run. For this feature a little bit
more improvements of LuaJIT should be made.

This adds signal-based timer modules inside LuaJIT sources. They provide API
to create and to manage timers that sent signal approximately each nanosecond.
Signal handler for corresponding signal will increment tick counter. When
amount of ticks between two different allocation events is more or equal of 1
second (within the accuracy of 1 micro second) the bit for timer events is set
in event header.

This feature is available only for Linux, FreeBSD or macOS systems for the
nonce. For FreeBSD or macOS `dispatch_source_set_timer()` is used instead
`timer_create()`. For these systems `struct luam_Prof_options` is extended by
`uint64_t dursec` field (zero by defaults means infinite profiling) defines
duration of profiling in seconds and `int signo` field (`SIGPROF` by default if
this field equals zero at function's call) to override sigaction for the
signal with the new one.

For these systems `struct luam_Prof_options` is extended by `uint64_t dursec`
field that defines duration of profiling in seconds and `int signo` field to
override sigaction for the signal with the new one. If `dursec` equals zero it
means infinite profiling. `signo` field sets to `SIGPROF` as the default value
if it equals zero at function's call.

Lua interface updates as follows:

```lua
local started = misc.memprof.start(fname, interval)
```

If interval is not defined or equals 0 profile will run unless
`misc.memprof.stop()` is called. Default `signo` (`SIGPROF`) for timer action
is used.

Profiler utility `lj-parce-memprof` is extended by a new option named
`--heap-snap-interval`:
```
  --heap-snap-interval SEC          Report heap state each SEC seconds
```

It adds preamble to output with the following format:
```
@<filename>:<function_line>, line <line number> holds <amount of bytes> bytes
```

For example with `SEC = 1`:
```
===== HEAP SNAPSHOT, 1 seconds passed
@test_memprof.lua:0, line 13 holds 63503 bytes
INTERNAL holds 30568 bytes
...
@test_memprof.lua:0, line 19 holds 7741 bytes
...
@test_memprof.lua:0, line 11 holds 72 bytes

===== HEAP SNAPSHOT, 2 seconds passed
```

#### Dump of Lua universe

This feature allows you to dump information about all Lua objects inside Lua
universe to analyze it later. It is possible to determine object dependencies,
find top of the most/least huge objects, see upvalues for different functions
and so on. This function uses format like symtab stream but with more objects
in dump. It allows not to run full GC cycle, but traverse all object like the
full propagate phase does.

For C API dump function declared as:
```c
/*
** Traverse and dump Lua universe. Returns LUAM_PROFILE_SUCCESS on success and
** one of LUAM_PROFILE_ERR* codes otherwise. Destroyer is not called at the
** end.
*/
LUAMISC_API int luaM_memprof_dump(lua_State *L,
				   const struct luam_Prof_options *opt);
```

This function does not yield. It dumps full information and only then stops.
It ignores `dursec` and `signo` fields in `struct luam_Prof_options`.
`on_stop` callback is ignored too, resources should clear manually by a caller.

For Lua space this function provided by
```lua
local res = misc.memprof.dump(filename)
```

It returns `true` on success, `false` if an error is occurred.

Parsing utility is extended by additional option `--dump`:

```
  --dump                            Parse dump of whole Lua universe
```

It has the following output format:
```
===== MEMORY SNAPSHOT
@<object type> <object addr> holds <amount of bytes> bytes and linked with
	methatable => LJ_TTAB <metatable address> or NULL
	<link format>
```

There are several object types with non-zero memory usage:
- `LJ_TSTR`
- `LJ_TUPVAL`
- `LJ_TTHREAD`
- `LJ_TPROTO`
- `LJ_TFUNC`
- `LJ_TTRACE`
- `LJ_TCDATA`
- `LJ_TTAB`
- `LJ_TUDATA`

As far as `LJ_TSTR`, `LJ_TUDATA` and `LJ_TCDATA` do not reference to any other
objects they have empty link format.

Linked list describes dependencies for referenced objects. If referencer type
is `LJ_TTAB` link format looks like:
```
===== ARRAY PART
	[<array field number>] => <object type> <object addr>
	...
===== HASH PART
	<object type> <object addr> => <object type> <object addr>
```

And finally all other values contain a list of reference to other objects:
```
	<object type> <object addr>
```

### Tarantool integration

As far as Tarantool is a hight-performance application server and database it
may be not well suited to use writes to file using `fwrite()` or so on. So
Tarantool should use custom functions to start and to stop memory profiling.
When profiler starts the new thread (named `cord`) is also created.

It looks like:
```c
int
tmemprof_init(const char *filename)
{
	/* Initialize the state. */
	struct tmemprof_writer *wr = &tmemprof_writer_singleton;

	/* Init messages mempool and open file to write. */
	tmemprof_writer_create(wr, filename);

	/* Start TMEMPROF thread. */
	if (cord_costart(&wr->cord, "tmemprof", tmemprof_writer_f, NULL) != 0)
		return -1;

	return 0;
}
```
Where `tmem_writer_f()` creates new `"tmemprof"` endpoint, paired it with
`"tx"` thread and started its processing loop. After profiling is stopped it
destroys endpoints, unpaired cbus pipes and clear corresponding structures.

All writes of profiling data just copies info from LuaJIT internal buffer to
Tarantools buffer specially allocated for this needs. As far as Tarantool may
heavily load disks (e.g. when it is hight-load instance using vynil) may be it
shouldn't write to file directly. It may be reasonable to use dumps to memory
to be parsed later, pipes, sockets or so on in that cases.

After profiling stops, callback (that has been set on start) stop cbus loop at
`"tmemprof"` endpoint. All new added functions returns values likewise LuaJIT
original functions.
