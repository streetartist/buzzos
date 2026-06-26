# Procfs Status Files

BuzzOS mounts a small read-only pseudo filesystem at `/proc`. It exposes live
kernel status through normal file operations, so shell tools can inspect the
system without adding a new syscall for every diagnostic.

## Files

```text
/proc/tasks
/proc/threads
/proc/meminfo
/proc/net
/proc/sync
/proc/fds
/proc/mounts
```

`/proc/tasks` mirrors the process table format used by `ps`.

`/proc/threads` lists individual scheduler tasks, including threads that are
currently `READY`, `RUNNING`, `SLEEP`, or `BLOCKED`.

`/proc/meminfo` reports the physical page allocator's current view:

```text
page_size
managed_limit
managed_pages
used_pages
free_pages
```

`/proc/net` reports the current lightweight network state:

```text
driver
mac
ip
gateway
dns
dhcp
arp
tcp
tcp_rx_buffered
tcp_rx_dropped
tx_frames
rx_frames
arp_frames
ip_frames
icmp_packets
udp_packets
tcp_packets
dhcp_packets
dns_packets
```

The `tcp` line reports whether any TCP PCB is connected and the current open
PCB count, for example `tcp closed open 0`. `tcp_rx_buffered` and
`tcp_rx_dropped` expose the lightweight TCP demux receive queue state.

`/proc/sync` reports synchronization state:

```text
futex_waiters
SLOT TID ADDR WOKEN
```

The `syncstat` shell command prints the same file.

`/proc/fds` reports the current process fd-owner table:

```text
OWNER FD OF REFS FLAGS KIND NAME DETAIL
```

It includes stdio, currently open procfs files, and pipe endpoints with pipe
index, buffered byte count, reader count, and writer count. The `fdstat` shell
command prints the same file.

`/proc/mounts` lists the built-in mounts:

```text
/ ramfs
/dev devfs
/proc procfs
/fs minifs
```

## Usage

From the BuzzOS shell:

```text
ls /proc
cat /proc/tasks
cat /proc/threads
cat /proc/meminfo
cat /proc/fds
cat /proc/net
cat /proc/sync
cat /proc/mounts
fdstat
```

The files are generated when read. They cannot be created, removed, renamed, or
written, and they do not consume `/fs` space.
