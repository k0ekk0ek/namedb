# Adaptive Radix Tree (ART) tailored for domain name data

**work-in-progress** prototype in-memory database for domain name data.

[The Adaptive Radix Tree][1] (ART) is a database for very efficient indexing
in main memory. Its lookup performance surpasses highly tuned, read-only
search trees, while supporting efficient insertions and deletions as well. At
the same time, ART is very space efficient and solves the problem of excessive
worst-case space consumption, which plagues most radix trees by adptively
choosing compact and efficient data structures for internal nodes. Even though
ART's performance is comparable to hash tables, it maintains the data in
sorted order, which enables additional operations like range scan and prefix
lookup.

This particular implementation, which borrows ideas from [libart][2] and
[NSD][3], adds two additional node sizes:

1. a node of size 32 to leverage 256-bit SIMD extensions in modern CPUs;
2. and a node of size 38 for nodes that store hostname keys exclusively

for better space efficiency. Worst-case space consumption is lowered for nodes
of size 48 and 256 by always converting uppercase letters to lowercase
(required for domain name lookups), and applying a specific key transformation
algorithm. Range scans are made convenient because the api uses the notion of
*paths*.

ART's sorted nature combined with efficient range scans, make two-way direct
memory references unnecessary. This further improves memory efficiency and
allows for [read-copy-update (RCU)][4] synchronization mechanisms.

[1]: http://www-db.in.tum.de/~leis/papers/ART.pdf
[2]: https://github.com/armon/libart
[3]: https://nlnetlabs.nl/projects/nsd/about/
[4]: https://en.wikipedia.org/wiki/Read-copy-update

> The code is incomplete, largely untested and (very likely) bug-ridden.
> Still, it captures the basic ideas pretty well.
