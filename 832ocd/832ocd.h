#ifndef OCD832_H
#define OCD832_H

enum dbg832_op {DBG832_NOP,DBG832_STATUS,DBG832_RUN,DBG832_SINGLESTEP,DBG832_STEPOVER,
		DBG832_READREG,DBG832_READFLAGS,DBG832_READ,DBG832_WRITE,
		DBG832_BREAKPOINT,DBG832_STOP,DBG832_RELEASE=255};

#define REG_TMP 8
#define REG_FLAGS 9

struct regfile
{
	int prevpc;
	int regs[8];
	int tmp;
	char c,z,cond,sign;
};

#endif

