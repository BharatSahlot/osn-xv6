# OSN Assignment 4

xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern RISC-V multiprocessor using ANSI C.

## Features Implemented

### Syscalls Implemented

All syscalls are declared in `kernel/syscall.c` with the `extern` keyword. All syscalls have a corresponding syscall number. This is defined by a macro in `kernel/syscall.h` and has the prefix `SYS_`. Using the syscall number they must be added in the arrays, `syscalls`, `sysnames` and `sysargs`, which hold the function pointer, syscall name and number of arguement the syscall expects.

`xv6` follows the convention, that all syscalls have `sys_` as a prefix. All syscalls must also be declared in `user/user.h` without the `sys_` prefix. Then they must be added to `usys.pl` using the `entry()` function. `usys.pl` which is a perl script, then generates `.S` file for all the calls of `entry()`. It defines `call` from `user.h` and calls `sys_call` using the syscall number `SYS_call` if `entry("call")` was called. This way programmers can invoke syscalls.

Syscalls are treated as a trap. Whenever a trap occurs the kernel finds its source, and if it is a syscall then it calls `syscall(void)` function, which reads the syscall number from the processes `a7` register.
Then it looks up in the `syscall` array to get the calls function pointer. Which is then invoked. All the arguements to a syscall are stored in the `a0` to `a6` registers.

Some syscalls only have to part of the kernel when the kernel is compiled for a specific scheduling algorithm. This is achieved using `Makefile`. In the `Makefile` we copy the `usys.pl` to `temp.pl` and append entries for whichever syscall we need to compile for this scheduling. Then we run `temp.pl` instead of `usys.pl`.

#### trace

Implemented syscall `trace` which turns on the tracing mode for a process. During tracing for all syscalls called and specified in the mask, their arguement list and return value is printed.

`trace` syscall can be found in `kernel/sysproc.c`. Each process can have its own trace mask. This mask is set by the `trace` syscall.

In the `syscall(void)` function in `kernel/syscall.c`, we just check if the current syscall is specified in this processes trace mask. If it is, we print its information.

#### sigalarm and sigreturn

Implemented syscall `sigalarm` which sets an alarm flag, an alarm interval `n` and an alarm handler `fn` to be called for the process at every `n` ticks.

Implemented syscall `sigreturn` which restores the state of the process to before the last alarm handler was called. It is supposed to be called at the end of the handler by the handler.

#### settickets (LBS)

Implemented syscall `settickets` which sets the number of tickets of a process to the given value. It can be used to increase or decrease the probability of a process being scheduled in LBS.

##### set_priority (PBS)

Implemented syscall `set_priority` which resets the `niceness` and sets the `priority` of a given process. The `niceness` and `priority` are used by priority based scheduling for scheduling. Also preempts the current process in case the set priority is higher.

### Programs Written
- **strace:** `strace <mask> <command>`, executes command `command` and traces all syscalls specified in the mask `mask`.
- **setpriority:** `setpriority <priority> <pid>`, executes command `command` and traces all syscalls specified in the mask `mask`.

### Scheduling Algorithms Implemented

Our implementation of `xv6` supports different scheduling algorithms. Only one scheduling algorithm can be active at a time, and can be set at compile time setting the Makefile variable `SCHEDULER`. For example, to compile for `MLFQ`, `make qemu SCHEDULER=MLFQ`.

#### First come first serve scheduling (FCFS)

Each time a new process is started, we store the number of ticks till then. The `scheduler` function selects process with the least start time. This is a non-preemptive scheduling i.e. a process keeps on running until it goes to sleep or exits.

#### Lottery based scheduling (LBS)

#### Priority based scheduling (PBS)

Each process has a static priority (default 60) and a niceness value (default 5), which is used for choosing the next process to schedule. This is also non-preemptive.

`niceness` is calculated everytime a process wakes up or goes to sleep. `set_priority` resets the niceness to `5`.

Each process stores the variables:

- `stick`: Tick number when the process was created.

- `tickls`: Tick number when the process was last scheduled.

- `tickslp`: Number of ticks while the process was in sleep state. Calculated in `kill()` and `wakeup()` using `ticks - tickls`, where `ticks` is the current tick number. And then `tickls` is set to `ticks`.

- `tickrng`: Number of ticks while the process was in running or runnable state. Calculated in `sleep()` by `ticks - tickls`, where `ticks` is the current tick number. And then `tickls` is set to `ticks`.

- `niceness`: Calculated everytime `tickslp` or `tickrng` is changed. Formula is $\text{Int}({\frac{Ticks\ spent\ in\ (sleeping)\ state}{Ticks\ spent\ in\ (running\ +\ sleeping)\ state}} * 10)$.

- `priority`: Static priority of the process, defaults to `60`. Can be changed using `set_priority` syscall.

- `nscheduled`: Number of times the process has been scheduled. This is used to break ties while scheduling.

The algorithm computes the dynamic priority of the processes and then chooses the process with the highest priority. In case of ties, the process scheduled the least number of times is selected. If the tie still remains then the process most recently created process is selected.

The dynamic priority is computed using,
$$
DP = \max{(0, \min{(SP\ -\ \text{niceness}\ +\ 5, 100)})}.
$$

Code for scheduling and computing priority is in `kernel/proc.c`.

#### Multi-level feedback queue scheduling (MLFQ)

MLFQ scheduling, stores processes in multiple queues. The last queue uses round robin scheduling, which the others use priority based scheduling.

MLFQ scheduling always runs the processes in the highest priority queue first. It is a premptive scheduling algorithm, so on a click tick if it finds that a process with higher priority is waiting to be scheduled then it will preempt the current process.

The number of queues is controlled by the macro `NQUEUE` which defaults to $5$. New processes always get pushed to the highest priority queue, i.e. `0` queue.

Each queue has different number of ticks a process can run for at a time before it is preempted and gets shifted to the next lower priority queue. If a process is in queue $i$ and it uses $2^i$ ticks, then it gets pushed to the end of queue $i + 1$. Unless it is the last queue, in that case it just gets pushed to the end of the $i$ queue.

If a process voluntarily relinquishes control of the CPU before it uses all its available ticks, then it stays in the current queue. This can be exploited by a process, so that the process can stay in a higher priority queue for a long time. Using `sigalarm` a process can periodically go to sleep and come back quickly to reset its tick count and stay in the same queue.

MLFQ scheduling also implements aging of processes. Every process has a tick count for the amount of ticks it has spent waiting in the queue.
If this exceeds a certain value(set by the macro `MAX_WAIT_TIME`), then the process gets pushed to the end of a higher priority queue.
This helps prevent starvation.

#### Benchmark


| Algorithm | CPU Count | Avg. Runtime | Avg. Waitime |
| - | - | - | - |
| RR | 1 | 23 | 193 |
| FCFS | 1 | 22 | 156 |
| PBS | 1 | 22 | 157 |
| LBS | 1 | 22 | 190 |
| MLFQ | 1 | 22 | 186 |
|  |  |  |  |
| RR | 2 | 21 | 135 |
| FCFS | 2 | 24 | 119 |
| PBS | 2 | 24 | 119 |
| LBS | 2 | 20 | 136 |
| MLFQ | 2 | 22 | 127 |

## ACKNOWLEDGMENTS

xv6 is inspired by John Lions's Commentary on UNIX 6th Edition (Peer
to Peer Communications; ISBN: 1-57398-013-7; 1st edition (June 14,
2000)).  See also https://pdos.csail.mit.edu/6.1810/, which provides
pointers to on-line resources for v6.

The following people have made contributions: Russ Cox (context switching,
locking), Cliff Frey (MP), Xiao Yu (MP), Nickolai Zeldovich, and Austin
Clements.

We are also grateful for the bug reports and patches contributed by
Takahiro Aoyagi, Silas Boyd-Wickizer, Anton Burtsev, carlclone, Ian
Chen, Dan Cross, Cody Cutler, Mike CAT, Tej Chajed, Asami Doi,
eyalz800, Nelson Elhage, Saar Ettinger, Alice Ferrazzi, Nathaniel
Filardo, flespark, Peter Froehlich, Yakir Goaron, Shivam Handa, Matt
Harvey, Bryan Henry, jaichenhengjie, Jim Huang, Matúš Jókay, John
Jolly, Alexander Kapshuk, Anders Kaseorg, kehao95, Wolfgang Keller,
Jungwoo Kim, Jonathan Kimmitt, Eddie Kohler, Vadim Kolontsov, Austin
Liew, l0stman, Pavan Maddamsetti, Imbar Marinescu, Yandong Mao, Matan
Shabtay, Hitoshi Mitake, Carmi Merimovich, Mark Morrissey, mtasm, Joel
Nider, Hayato Ohhashi, OptimisticSide, Harry Porter, Greg Price, Jude
Rich, segfault, Ayan Shafqat, Eldar Sehayek, Yongming Shen, Fumiya
Shigemitsu, Cam Tenny, tyfkda, Warren Toomey, Stephen Tu, Rafael Ubal,
Amane Uehara, Pablo Ventura, Xi Wang, WaheedHafez, Keiichi Watanabe,
Nicolas Wolovick, wxdao, Grant Wu, Jindong Zhang, Icenowy Zheng,
ZhUyU1997, and Zou Chang Wei.


The code in the files that constitute xv6 is
Copyright 2006-2022 Frans Kaashoek, Robert Morris, and Russ Cox.

## ERROR REPORTS

Please send errors and suggestions to Frans Kaashoek and Robert Morris
(kaashoek,rtm@mit.edu).  The main purpose of xv6 is as a teaching
operating system for MIT's 6.1810, so we are more interested in
simplifications and clarifications than new features.

## BUILDING AND RUNNING XV6

You will need a RISC-V "newlib" tool chain from
https://github.com/riscv/riscv-gnu-toolchain, and qemu compiled for
riscv64-softmmu.  Once they are installed, and in your shell
search path, you can run "make qemu".
