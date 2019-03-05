#pragma once
// helper code for handling the various hardware types

typedef unsigned short Word;
typedef unsigned char Byte;

typedef Byte (*readFunc)(Word, bool);
typedef void (*writeFunc)(Word, Byte);
typedef void (*unconfigFunc)(readFunc*, writeFunc*, readFunc*, writeFunc*);

Byte readZero(Word /*addr*/, bool /* rmw */);
void writeZero(Word /*addr*/, Byte /*data*/);

// For each program type they fill in an array for reads and writes
// via a "configureXXX()" function, which returns a pointer to a cleanup function.
// If it returns NULL, the configuration failed. Maintain a list of the cleanup
// functions and drain the list before re-configuring anew, otherwise memory
// allocations will repeat themselves. This permits reconfiguration without
// extra memory being allocated.
