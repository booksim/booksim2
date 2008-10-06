#ifndef _GLOBALS_HPP_
#define _GLOBALS_HPP_
#include <string>

/*all declared in main.cpp*/


extern bool _print_activity;

extern int gK;
extern int gN;
extern int gC;


extern int realgk;
extern int realgn;

extern int gNodes;


extern int xrouter;
extern int yrouter;
extern int xcount ;
extern int ycount;

extern bool _trace;

extern bool _use_read_write;

extern double gBurstAlpha;
extern double gBurstBeta;

/*number of flits per packet, set by the configuration file*/
extern int    gConstPacketSize;

extern int *gNodeStates;

extern string watch_file;
#endif
