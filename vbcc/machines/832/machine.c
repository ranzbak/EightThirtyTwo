/*  Example backend for vbcc, it models a generic 32bit RISC or CISC
    CPU.
 
    Configurable at build-time are:
    - number of (32bit) general-purpose-registers
    - number of (64bit) floating-point-registers
    - number of (8bit) condition-code-registers
    - mechanism for stack-arguments (moving ot fixed sp)
 
    It allows to select as run-time-options:
    - two- or three-address code
    - memory operands or load-store-architecture
    - number of register-arguments
    - number of caller-save-registers
*/                                                                             

// FIXME - need to reduce unneccesary ea calculations - r5 often contains the target address +/- a small offset,
// find a way to prevent it being recalculated from scratch.

#include "supp.h"

static char FILE_[]=__FILE__;

/*  Public data that MUST be there.                             */

/* Name and copyright. */
char cg_copyright[]="vbcc generic code-generator adapted to 832 in baby steps V0.1b (c) in 2001 by Volker Barthelmann, 2019 AMR";

/*  Commandline-flags the code-generator accepts:
    0: just a flag
    VALFLAG: a value must be specified
    STRINGFLAG: a string can be specified
    FUNCFLAG: a function will be called
    apart from FUNCFLAG, all other versions can only be specified once */
int g_flags[MAXGF]={0,0,
		    VALFLAG,VALFLAG,VALFLAG,
		    0,0,
		    VALFLAG,VALFLAG,0};

/* the flag-name, do not use names beginning with l, L, I, D or U, because
   they collide with the frontend */
char *g_flags_name[MAXGF]={"three-addr","load-store",
			   "volatile-gprs","volatile-fprs","volatile-ccrs",
			   "imm-ind","gpr-ind",
			   "gpr-args","fpr-args","use-commons"};

/* the results of parsing the command-line-flags will be stored here */
union ppi g_flags_val[MAXGF];

/*  Alignment-requirements for all types in bytes.              */
zmax align[MAX_TYPE+1];

/*  Alignment that is sufficient for every object.              */
zmax maxalign;

/*  CHAR_BIT for the target machine.                            */
zmax char_bit;

/*  sizes of the basic types (in bytes) */
zmax sizetab[MAX_TYPE+1];

/*  Minimum and Maximum values each type can have.              */
/*  Must be initialized in init_cg().                           */
zmax t_min[MAX_TYPE+1];
zumax t_max[MAX_TYPE+1];
zumax tu_max[MAX_TYPE+1];

/*  Names of all registers. will be initialized in init_cg(),
    register number 0 is invalid, valid registers start at 1 */
char *regnames[MAXR+1];

/*  The Size of each register in bytes.                         */
zmax regsize[MAXR+1];

/*  a type which can store each register. */
struct Typ *regtype[MAXR+1];

/*  regsa[reg]!=0 if a certain register is allocated and should */
/*  not be used by the compiler pass.                           */
int regsa[MAXR+1];

/*  Specifies which registers may be scratched by functions.    */
int regscratch[MAXR+1];

/* specifies the priority for the register-allocator, if the same
   estimated cost-saving can be obtained by several registers, the
   one with the highest priority will be used */
int reg_prio[MAXR+1];

/* an empty reg-handle representing initial state */
struct reg_handle empty_reg_handle={0,0};

/* Names of target-specific variable attributes.                */
char *g_attr_name[]={"__interrupt",0};


/****************************************/
/*  Private data and functions.         */
/****************************************/

#define THREE_ADDR (g_flags[0]&USEDFLAG)
#define LOAD_STORE (g_flags[1]&USEDFLAG)
#define VOL_GPRS   ((g_flags[2]&USEDFLAG)?g_flags_val[2].l:2)
#define VOL_FPRS   ((g_flags[3]&USEDFLAG)?g_flags_val[3].l:NUM_FPRS/2)
#define VOL_CCRS   ((g_flags[4]&USEDFLAG)?g_flags_val[4].l:NUM_CCRS/2)
#define IMM_IND    ((g_flags[5]&USEDFLAG)?1:0)
#define GPR_IND    ((g_flags[6]&USEDFLAG)?2:0)
#define GPR_ARGS   ((g_flags[7]&USEDFLAG)?g_flags_val[7].l:0)
#define FPR_ARGS   ((g_flags[8]&USEDFLAG)?g_flags_val[8].l:0)
#define USE_COMMONS (g_flags[9]&USEDFLAG)


/* alignment of basic data-types, used to initialize align[] */
static long malign[MAX_TYPE+1]=  {1,1,2,4,4,4,4,8,8,1,4,1,1,1,4,1};
/* sizes of basic data-types, used to initialize sizetab[] */
static long msizetab[MAX_TYPE+1]={1,1,2,4,4,8,4,8,8,0,4,0,0,0,4,0};

/* used to initialize regtyp[] */
static struct Typ ltyp={LONG},ldbl={DOUBLE},lchar={CHAR};

/* macros defined by the backend */
static char *marray[]={"__section(x)=__vattr(\"section(\"#x\")\")",
		       "__GENERIC__",
		       0};

/* special registers */
static int pc;                     /*  Program counter                     */
static int sp;                     /*  Stackpointer                        */
static int t1,t2;                  /*  temporary gprs */
static int f1,f2,f3;               /*  temporary fprs */

// int reg_stackrel[LAST_GPR+1];	/* Register currently contains a pointer to an item on the stack */
// int reg_stackoffset[LAST_GPR+1];	/* Offset of stack item */


#define dt(t) (((t)&UNSIGNED)?udt[(t)&NQ]:sdt[(t)&NQ])
static char *sdt[MAX_TYPE+1]={"??","c","s","i","l","ll","f","d","ld","v","p"};
static char *udt[MAX_TYPE+1]={"??","uc","us","ui","ul","ull","f","d","ld","v","p"};

/* sections */
#define DATA 0
#define BSS 1
#define CODE 2
#define RODATA 3
#define SPECIAL 4

static long stack;
static int stack_valid;
static int section=-1,newobj;
static char *codename="\t.text\n",
  *dataname="\t.data\n",
  *bssname="",
  *rodataname="\t.section\t.rodata\n";

/* return-instruction */
//static char *ret;

/* label at the end of the function (if any) */
static int exit_label;

/* assembly-prefixes for labels and external identifiers */
static char *labprefix="l",*idprefix="_";

#if FIXED_SP
/* variables to calculate the size and partitioning of the stack-frame
   in the case of FIXED_SP */
static long frameoffset,pushed,maxpushed,framesize;
#else
/* variables to keep track of the current stack-offset in the case of
   a moving stack-pointer */
static long notpopped,pushed,dontpop,stackoffset,maxpushed;
#endif

static long localsize,rsavesize,argsize;

static void emit_obj(FILE *f,struct obj *p,int t);
static void emit_prepobj(FILE *f,struct obj *p,int t,int reg);
static void emit_prepobjtotemp(FILE *f,struct obj *p,int t,int reg);
//static void emit_destobj(FILE *f,struct obj *p,int t);
static void emit_objtotemp(FILE *f,struct obj *p,int t);

/* calculate the actual current offset of an object relativ to the
   stack-pointer; we use a layout like this:
   ------------------------------------------------
   | arguments to this function                   |
   ------------------------------------------------
   | return-address [size=4]                      |
   ------------------------------------------------
   | caller-save registers [size=rsavesize]       |
   ------------------------------------------------
   | local variables [size=localsize]             |
   ------------------------------------------------
   | arguments to called functions [size=argsize] |
   ------------------------------------------------
   All sizes will be aligned as necessary.
   In the case of FIXED_SP, the stack-pointer will be adjusted at
   function-entry to leave enough space for the arguments and have it
   aligned to 16 bytes. Therefore, when calling a function, the
   stack-pointer is always aligned to 16 bytes.
   For a moving stack-pointer, the stack-pointer will usually point
   to the bottom of the area for local variables, but will move while
   arguments are put on the stack.

   This is just an example layout. Other layouts are also possible.
*/


static void push(long l)
{
  stackoffset-=l;
  if(stackoffset<maxpushed) 
    maxpushed=stackoffset;
  if(-maxpushed>stack)
    stack=-maxpushed;
}

static void pop(long l)
{
  stackoffset+=l;
}


static long real_offset(struct obj *o)
{
  long off=zm2l(o->v->offset);
  if(off<0){
    /* function parameter */
    off=localsize+rsavesize+4-off-zm2l(maxalign);
  }

#if FIXED_SP
  off+=argsize;
#else
  off+=stackoffset;
#endif
  off+=zm2l(o->val.vmax);
  return off;
}

/*  Initializes an addressing-mode structure and returns a pointer to
    that object. Will not survive a second call! */
static struct obj *cam(int flags,int base,long offset)
{
  static struct obj obj;
  static struct AddressingMode am;
  obj.am=&am;
//  am.flags=flags;
//  am.base=base;
//  am.offset=offset;
  return &obj;
}

/* changes to a special section, used for __section() */
static int special_section(FILE *f,struct Var *v)
{
  char *sec;
  if(!v->vattr) return 0;
  sec=strstr(v->vattr,"section(");
  if(!sec) return 0;
  sec+=strlen("section(");
  emit(f,"\t.section\t");
  while(*sec&&*sec!=')') emit_char(f,*sec++);
  emit(f,"\n");
  if(f) section=SPECIAL;
  return 1;
}

static void load_address_to_temp(FILE *f,int r,struct obj *o,int type)
/*  Generates code to load the address of a variable into register r.   */
{
  emit(f,"\t\t\t\t//FIXME - load_address\n");
}

/* generate code to load the address of a variable into register r */
static void load_address(FILE *f,int r,struct obj *o,int type)
/*  Generates code to load the address of a variable into register r.   */
{
  emit_prepobj(f,o,type,r);
  if(o->v->storage_class==REGISTER){
    emit(f,"#FIXME - register!\n");
  }
}

/* Generates code to load a memory object into temp.  Returns 1 if code was emitted, 0 if there's no need. */
static int load_temp(FILE *f,int r,struct obj *o,int type)
{
  emit(f,"\t\t\t\t\t// (load_temp)");
  type&=NU;
  if(o->flags&VARADR){
    emit(f," FIXME - check varadr - should we be dereferencing this?\n");
    emit_prepobj(f,o,type,t1);
    emit(f,"\tmt\t%s\n",regnames[t1]);
  }else{
    if((o->flags&(REG|DREFOBJ))==REG&&o->reg==r)
    {
      emit(f," nop\n");
      return(0);
    }
    emit(f," not varadr\n");
    emit_objtotemp(f,o,type);
  }
  return(1);
}

/* Generates code to load a memory object into register r. tmp is a
   general purpose register which may be used. tmp can be r. */

static void load_reg(FILE *f,int r,struct obj *o,int type)
{
  if(load_temp(f,r,o,type))
  {
    emit(f,"\tmr\t%s\n",regnames[r]);
//    reg_stackrel[r]=0;
  }
}

/*  Generates code to store register r into memory object o. */
static void store_reg(FILE *f,int r,struct obj *o,int type)
{
  type&=NQ;
  emit_prepobjtotemp(f,o,type,t2);
  emit(f,"\tstmpdec\t%s\n",regnames[r]);
}

/*  Generates code to store temp register r into memory object o. */
static void store_temp(FILE *f,int r,struct obj *o,int type)
{
  type&=NQ;
  emit_prepobj(f,o,type,t2);
  emit(f,"\tst\t%s\n",regnames[r],regnames[t2]);
}

/*  Yields log2(x)+1 or 0. */
static long pof2(zumax x)
{
  zumax p;int ln=1;
  p=ul2zum(1L);
  while(ln<=32&&zumleq(p,x)){
    if(zumeqto(x,p)) return ln;
    ln++;p=zumadd(p,p);
  }
  return 0;
}

static struct IC *preload(FILE *,struct IC *);

static void function_top(FILE *,struct Var *,long);
static void function_bottom(FILE *f,struct Var *,long);

#define isreg(x) ((p->x.flags&(REG|DREFOBJ))==REG)
#define isconst(x) ((p->x.flags&(KONST|DREFOBJ))==KONST)

static int q1reg,q2reg,zreg;

static char *ccs[]={"EQ","NEQ","SLT","GE","LE","SGT","EX",""};
static char *logicals[]={"or","xor","and"};
static char *arithmetics[]={"shl","asr","add","sub","mul","divw","mod"};

/* Does some pre-processing like fetching operands from memory to
   registers etc. */
static struct IC *preload(FILE *f,struct IC *p)
{
  int r;

  if(isreg(q1))
    q1reg=p->q1.reg;
  else
    q1reg=0;

  if(isreg(q2))
    q2reg=p->q2.reg;
  else
    q2reg=0;

  if(isreg(z)){
    zreg=p->z.reg;
  }else{
    if(ISFLOAT(ztyp(p)))
      zreg=f1;
    else
      zreg=t1;
  }

  return p;
}

/* save the result (in temp) into p->z */
void save_temp(FILE *f,struct IC *p)
{
  emit(f,"\t\t\t\t\t// (save temp) ");
  if(isreg(z)){
    emit(f,"isreg\n");
//    if(p->z.reg!=zreg)
      emit(f,"\tmr\t%s\n",regnames[p->z.reg]);
//      reg_stackrel[p->z.reg]=0;
  }else if ((p->z.flags&DREFOBJ) && (p->z.flags&REG)){
    emit(f,"store reg\n");
    emit(f,"\tst\t%s\n",regnames[p->z.reg]);
  } else {
    emit(f,"store prepped reg\n");
    emit(f,"\tst\t%s\n",regnames[t2]);
  }
  emit(f,"\t\t\t\t//save_temp done\n");
}

/* save the result (in zreg) into p->z */
void save_result(FILE *f,struct IC *p)
{
  emit(f,"\t\t\t\t\t// (save result) ");
  if((p->z.flags&(REG|DREFOBJ))==DREFOBJ&&!p->z.am){
    emit(f,"deref\n");
    p->z.flags&=~DREFOBJ;
    load_reg(f,t2,&p->z,POINTER);
    p->z.reg=t2;
    p->z.flags|=(REG|DREFOBJ);
  }
  if(isreg(z)){
    emit(f,"isreg\n");
    if(p->z.reg!=zreg)
      emit(f,"\tmt\t%s\n\tmr\t%s\n",regnames[zreg],regnames[p->z.reg]);
  }else{
    emit(f,"store reg\n");
    store_reg(f,zreg,&p->z,ztyp(p));
  }
}

#include "tempregs.c"

/*  Test if there is a sequence of FREEREGs containing FREEREG reg.
    Used by peephole. */
static int exists_freereg(struct IC *p,int reg)
{
  while(p&&(p->code==FREEREG||p->code==ALLOCREG)){
    if(p->code==FREEREG&&p->q1.reg==reg) return 1;
    p=p->next;
  }
  return 0;
}


/* generates the function entry code */
static void function_top(FILE *f,struct Var *v,long offset)
{
  int i;
  int regcount=0;
  emit(f,"\t//registers used:\n");
  for(i=FIRST_GPR;i<=LAST_GPR;++i)
  {
    emit(f,"\t\t//r%d: %s\n",i-FIRST_GPR,regused[i]?"yes":"no");
    if(regused[i] && (i>FIRST_GPR) && (i<=LAST_GPR-3))
      ++regcount;
  }
  rsavesize=0;
  if(!special_section(f,v)&&section!=CODE){emit(f,codename);if(f) section=CODE;} 
  if(v->storage_class==EXTERN){
    if((v->flags&(INLINEFUNC|INLINEEXT))!=INLINEFUNC)
      emit(f,"\t.global\t%s%s\n",idprefix,v->identifier);
    emit(f,"%s%s:\n",idprefix,v->identifier);
  }else
    emit(f,"%s%ld:\n",labprefix,zm2l(v->offset));

  if(regcount<3)
  {
    emit(f,"\tstdec\t%s\n",regnames[sp]);
    for(i=FIRST_GPR+1;i<=LAST_GPR-3;++i)
    {
      if(regused[i])
      {
        emit(f,"\tmt\t%s\n\tstdec\t%s\n",regnames[i],regnames[sp]);
        rsavesize+=4;
      }
    }
  }
  else
  {
    emit(f,"\texg\t%s\n\tstmpdec\t%s\n",regnames[sp],regnames[sp]);
    for(i=FIRST_GPR+1;i<=LAST_GPR-3;++i)
    {
      if(regused[i])
      {
        emit(f,"\tstmpdec\t%s\n",regnames[i]);
        rsavesize+=4;
      }
    }
    emit(f,"\texg\t%s\n",regnames[sp]);
  }

  // FIXME - Allow the stack to float, in the hope that we can use stdec to adjust it.

  if(offset==4)
	emit(f,"\tstdec\tr6\t// quickest way to decrement sp by 4\n");
  else if(offset)
  {
	emit_constanttotemp(f,offset);
	emit(f,"\tsub\t%s\n",regnames[sp]);
  }
}


/* generates the function exit code */
static void function_bottom(FILE *f,struct Var *v,long offset)
{
  int i;

  int regcount=0;
  for(i=FIRST_GPR+1;i<=LAST_GPR-3;++i)
  {
    if(regused[i])
      ++regcount;
  }

  if(regcount<3)
  {
    if(offset==4)
      emit(f,"\tldinc\t%s\t// quickest way to add 4 to sp\n",regnames[sp]);
    else if(offset)
    {
      emit_constanttotemp(f,offset);
      emit(f,"\tadd\t%s\n",regnames[sp]);
    }

    for(i=FIRST_GPR+1;i<=LAST_GPR-3;++i)
    {
      if(regused[i])
        emit(f,"\tldinc\t%s\n\tmr\t%s\n",regnames[sp],regnames[i]);
    }
    emit(f,"\tldinc\t%s\n\tmr\t%s\n\n",regnames[sp],regnames[pc]);
  }
  else
  {
    if(offset==4)
      emit(f,"\tldinc\t%s\t// quickest way to add 4 to sp\n",regnames[sp]);
    else if(offset)
    {
      emit_constanttotemp(f,offset);
      emit(f,"\taddt\t%s\n",regnames[sp]);
    }
    else
      emit(f,"\texg\t%s\n",regnames[sp]);
    for(i=FIRST_GPR+1;i<=LAST_GPR-3;++i)
    {
      if(regused[i])
        emit(f,"\tltmpinc\t%s\n",regnames[i]);
    }
    emit(f,"\tltmpinc\t%s\n\texg\t%s\n\tmr\t%s\n",regnames[sp],regnames[sp],regnames[pc]);
  }
}

/****************************************/
/*  End of private data and functions.  */
/****************************************/

/*  Does necessary initializations for the code-generator. Gets called  */
/*  once at the beginning and should return 0 in case of problems.      */
int init_cg(void)
{
  int i;
  /*  Initialize some values which cannot be statically initialized   */
  /*  because they are stored in the target's arithmetic.             */
  maxalign=l2zm(4L);
  char_bit=l2zm(8L);
  stackalign=l2zm(4);

  for(i=0;i<=MAX_TYPE;i++){
    sizetab[i]=l2zm(msizetab[i]);
    align[i]=l2zm(malign[i]);
  }

  regnames[0]="noreg";
  for(i=FIRST_GPR;i<=LAST_GPR;i++){
    regnames[i]=mymalloc(10);
    sprintf(regnames[i],"r%d",i-FIRST_GPR);
    regsize[i]=l2zm(4L);
    regtype[i]=&ltyp;
    regsa[i]=0;
  }
  for(i=FIRST_FPR;i<=LAST_FPR;i++){
    regnames[i]=mymalloc(10);
    sprintf(regnames[i],"fpr%d",i-FIRST_FPR);
    regsize[i]=l2zm(8L);
    regtype[i]=&ldbl;
  }
  for(i=FIRST_CCR;i<=LAST_CCR;i++){
    regnames[i]=mymalloc(10);
    sprintf(regnames[i],"ccr%d",i-FIRST_CCR);
    regsize[i]=l2zm(1L);
    regtype[i]=&lchar;
  }

  /*  Use multiple ccs.   */
  multiple_ccs=0;

  /*  Initialize the min/max-settings. Note that the types of the     */
  /*  host system may be different from the target system and you may */
  /*  only use the smallest maximum values ANSI guarantees if you     */
  /*  want to be portable.                                            */
  /*  That's the reason for the subtraction in t_min[INT]. Long could */
  /*  be unable to represent -2147483648 on the host system.          */
  t_min[CHAR]=l2zm(-128L);
  t_min[SHORT]=l2zm(-32768L);
  t_min[INT]=zmsub(l2zm(-2147483647L),l2zm(1L));
  t_min[LONG]=t_min(INT);
  t_min[LLONG]=zmlshift(l2zm(1L),l2zm(63L));
  t_min[MAXINT]=t_min(LLONG);
  t_max[CHAR]=ul2zum(127L);
  t_max[SHORT]=ul2zum(32767UL);
  t_max[INT]=ul2zum(2147483647UL);
  t_max[LONG]=t_max(INT);
  t_max[LLONG]=zumrshift(zumkompl(ul2zum(0UL)),ul2zum(1UL));
  t_max[MAXINT]=t_max(LLONG);
  tu_max[CHAR]=ul2zum(255UL);
  tu_max[SHORT]=ul2zum(65535UL);
  tu_max[INT]=ul2zum(4294967295UL);
  tu_max[LONG]=t_max(UNSIGNED|INT);
  tu_max[LLONG]=zumkompl(ul2zum(0UL));
  tu_max[MAXINT]=t_max(UNSIGNED|LLONG);
  
  /*  Reserve a few registers for use by the code-generator.      */
  /*  This is not optimal but simple.                             */
  pc=FIRST_GPR+7;
  sp=FIRST_GPR+6;
  t1=FIRST_GPR+5; // build source address here
  t2=FIRST_GPR+4; // build dest address here - mark
//  f1=FIRST_FPR;
//  f2=FIRST_FPR+1;

  for(i=FIRST_GPR;i<=LAST_GPR-VOL_GPRS;i++)
    regscratch[i]=0;
  for(i=FIRST_FPR;i<=LAST_FPR-VOL_FPRS;i++)
    regscratch[i]=0;
  for(i=FIRST_CCR;i<=LAST_CCR-VOL_CCRS;i++)
    regscratch[i]=0;

  regsa[FIRST_GPR]=1;	// Allocate the return register
  regsa[t1]=1;
//  regsa[t2]=1;
  regsa[sp]=1;
  regsa[pc]=1;
  regscratch[t1]=0;
//  regscratch[t2]=0;
  regscratch[sp]=0;
  regscratch[pc]=0;
//regscratch[t2]=0;
//  regscratch[f1]=regscratch[f2]=0;
//  regscratch[sp]=0;

  target_macros=marray;


  return 1;
}

void init_db(FILE *f)
{
}

int freturn(struct Typ *t)
/*  Returns the register in which variables of type t are returned. */
/*  If the value cannot be returned in a register returns 0.        */
/*  A pointer MUST be returned in a register. The code-generator    */
/*  has to simulate a pseudo register if necessary.                 */
{
  if(ISFLOAT(t->flags)) 
    return 0;
  if(ISSTRUCT(t->flags)||ISUNION(t->flags)) 
    return 0;
  if(zmleq(szof(t),l2zm(4L))) 
    return FIRST_GPR;
  else
    return 0;
}

int reg_pair(int r,struct rpair *p)
/* Returns 0 if the register is no register pair. If r  */
/* is a register pair non-zero will be returned and the */
/* structure pointed to p will be filled with the two   */
/* elements.                                            */
{
  return 0;
}

/* estimate the cost-saving if object o from IC p is placed in
   register r */
int cost_savings(struct IC *p,int r,struct obj *o)
{
  int c=p->code;
  if(o->flags&VKONST){
    if(o==&p->q1&&p->code==ASSIGN&&(p->z.flags&DREFOBJ))
      return 1;
    else
      return 1;
  }
  if(o->flags&DREFOBJ)
    return 1;
  if(c==SETRETURN&&r==p->z.reg&&!(o->flags&DREFOBJ)) return 1;
  if(c==GETRETURN&&r==p->q1.reg&&!(o->flags&DREFOBJ)) return 1;
  return 1;
}

int regok(int r,int t,int mode)
/*  Returns 0 if register r cannot store variables of   */
/*  type t. If t==POINTER and mode!=0 then it returns   */
/*  non-zero only if the register can store a pointer   */
/*  and dereference a pointer to mode.                  */
{
  if(r==0)
    return 0;
  t&=NQ;
  if(t==0&&r>=FIRST_CCR&&r<=LAST_CCR)
    return 1;
  if(ISFLOAT(t)&&r>=FIRST_FPR&&r<=LAST_FPR)
    return 1;
  if(t==POINTER&&r>=FIRST_GPR&&r<=LAST_GPR)
    return 1;
  if(t>=CHAR&&t<=LONG&&r>=FIRST_GPR&&r<=LAST_GPR)
    return 1;
  return 0;
}

int dangerous_IC(struct IC *p)
/*  Returns zero if the IC p can be safely executed     */
/*  without danger of exceptions or similar things.     */
/*  vbcc may generate code in which non-dangerous ICs   */
/*  are sometimes executed although control-flow may    */
/*  never reach them (mainly when moving computations   */
/*  out of loops).                                      */
/*  Typical ICs that generate exceptions on some        */
/*  machines are:                                       */
/*      - accesses via pointers                         */
/*      - division/modulo                               */
/*      - overflow on signed integer/floats             */
{
  int c=p->code;
  if((p->q1.flags&DREFOBJ)||(p->q2.flags&DREFOBJ)||(p->z.flags&DREFOBJ))
    return 1;
  if((c==DIV||c==MOD)&&!isconst(q2))
    return 1;
  return 0;
}

int must_convert(int o,int t,int const_expr)
/*  Returns zero if code for converting np to type t    */
/*  can be omitted.                                     */
/*  On the PowerPC cpu pointers and 32bit               */
/*  integers have the same representation and can use   */
/*  the same registers.                                 */
{
  int op=o&NQ,tp=t&NQ;
  if((op==INT||op==LONG||op==POINTER)&&(tp==INT||tp==LONG||tp==POINTER))
    return 0;
  if(op==DOUBLE&&tp==LDOUBLE) return 0;
  if(op==LDOUBLE&&tp==DOUBLE) return 0;
  return 1;
}

void gen_ds(FILE *f,zmax size,struct Typ *t)
/*  This function has to create <size> bytes of storage */
/*  initialized with zero.                              */
{
  if(newobj&&section!=SPECIAL)
    emit(f,"%ld\n",zm2l(size));
  else
    emit(f,"\t.space\t%ld\n",zm2l(size));
  newobj=0;
}

void gen_align(FILE *f,zmax align)
/*  This function has to make sure the next data is     */
/*  aligned to multiples of <align> bytes.              */
{
  if(zm2l(align)>1) emit(f,"\t.align\t2\n");
}

void gen_var_head(FILE *f,struct Var *v)
/*  This function has to create the head of a variable  */
/*  definition, i.e. the label and information for      */
/*  linkage etc.                                        */
{
  int constflag;char *sec;
  if(v->clist) constflag=is_const(v->vtyp);
  if(v->storage_class==STATIC){
    if(ISFUNC(v->vtyp->flags)) return;
    if(!special_section(f,v)){
      if(v->clist&&(!constflag||(g_flags[2]&USEDFLAG))&&section!=DATA){emit(f,dataname);if(f) section=DATA;}
      if(v->clist&&constflag&&!(g_flags[2]&USEDFLAG)&&section!=RODATA){emit(f,rodataname);if(f) section=RODATA;}
      if(!v->clist&&section!=BSS){emit(f,bssname);if(f) section=BSS;}
    }
    if(v->clist||section==SPECIAL){
      gen_align(f,falign(v->vtyp));
      emit(f,"%s%ld:\n",labprefix,zm2l(v->offset));
    }else
      emit(f,"\t.lcomm\t%s%ld,",labprefix,zm2l(v->offset));
    newobj=1;
  }
  if(v->storage_class==EXTERN){
    emit(f,"\t.globl\t%s%s\n",idprefix,v->identifier);
    if(v->flags&(DEFINED|TENTATIVE)){
      if(!special_section(f,v)){
	if(v->clist&&(!constflag||(g_flags[2]&USEDFLAG))&&section!=DATA){emit(f,dataname);if(f) section=DATA;}
	if(v->clist&&constflag&&!(g_flags[2]&USEDFLAG)&&section!=RODATA){emit(f,rodataname);if(f) section=RODATA;}
	if(!v->clist&&section!=BSS){emit(f,bssname);if(f) section=BSS;}
      }
      if(v->clist||section==SPECIAL){
	gen_align(f,falign(v->vtyp));
        emit(f,"%s%s:\n",idprefix,v->identifier);
      }else
        emit(f,"\t.global\t%s%s\n\t.%scomm\t%s%s,",idprefix,v->identifier,(USE_COMMONS?"":"l"),idprefix,v->identifier);
      newobj=1;
    }
  }
}

void gen_dc(FILE *f,int t,struct const_list *p)
/*  This function has to create static storage          */
/*  initialized with const-list p.                      */
{
  if(!p->tree){
    switch(t&NQ)
    {
      case CHAR:
        emit(f,"\t.byte\t");
        break;
      case SHORT:
        emit(f,"\t.short\t");
        break;
      case LONG:
      case INT:
      case MAXINT:
      case POINTER:
        emit(f,"\t.int\t");
        break;
      default:
        emit(f,"#FIXME - unsupported type\n");
    }
    emitval(f,&p->val,t&NU);

#if 0
    if(ISFLOAT(t)){
      /*  auch wieder nicht sehr schoen und IEEE noetig   */
      unsigned char *ip;
      ip=(unsigned char *)&p->val.vdouble;
      emit(f,"0x%02x%02x%02x%02x",ip[0],ip[1],ip[2],ip[3]);
      if((t&NQ)!=FLOAT){
	emit(f,",0x%02x%02x%02x%02x",ip[4],ip[5],ip[6],ip[7]);
      }
    }else{
      emitval(f,&p->val,t&NU);
    }
#endif
  }else{
        emit(f,"#FIXME - declare from tree\n");
//    emit_obj(f,&p->tree->o,t&NU);
  }
  emit(f,"\n");newobj=0;
}


/*  The main code-generation routine.                   */
/*  f is the stream the code should be written to.      */
/*  p is a pointer to a doubly linked list of ICs       */
/*  containing the function body to generate code for.  */
/*  v is a pointer to the function.                     */
/*  offset is the size of the stackframe the function   */
/*  needs for local variables.                          */

void gen_code(FILE *f,struct IC *p,struct Var *v,zmax offset)
/*  The main code-generation.                                           */
{
  static int idemp=0;
  int c,t,i;
  struct IC *m;
  argsize=0;
  // if(DEBUG&1) 
  printf("gen_code() - stackframe %d bytes\n",offset);
  for(c=1;c<=MAXR;c++)
    regs[c]=regsa[c];
  maxpushed=0;

//  for(c=FIRST_GPR;c<=LAST_GPR;++c)
//    reg_stackrel[c]=0;

  if(!idemp)
  {
    emit(f,"#include \"assembler.pp\"\n\n");
    idemp=1;
  }


  for(m=p;m;m=m->next){
    c=m->code;t=m->typf&NU;
    if(c==ALLOCREG) {regs[m->q1.reg]=1;continue;}
    if(c==FREEREG) {regs[m->q1.reg]=0;continue;}

    /* convert MULT/DIV/MOD with powers of two */
    if((t&NQ)<=LONG&&(m->q2.flags&(KONST|DREFOBJ))==KONST&&(t&NQ)<=LONG&&(c==MULT||((c==DIV||c==MOD)&&(t&UNSIGNED)))){
      eval_const(&m->q2.val,t);
      i=pof2(vmax);
      if(i){
        if(c==MOD){
          vmax=zmsub(vmax,l2zm(1L));
          m->code=AND;
        }else{
          vmax=l2zm(i-1);
          if(c==DIV) m->code=RSHIFT; else m->code=LSHIFT;
        }
        c=m->code;
	gval.vmax=vmax;
	eval_const(&gval,MAXINT);
	if(c==AND){
	  insert_const(&m->q2.val,t);
	}else{
	  insert_const(&m->q2.val,INT);
	  p->typf2=INT;
	}
      }
    }
#if FIXED_SP
    if(c==CALL&&argsize<zm2l(m->q2.val.vmax)) argsize=zm2l(m->q2.val.vmax);
#endif
  }

  for(c=1;c<=MAXR;c++){
    if(regsa[c]||regused[c]){
      BSET(regs_modified,c);
    }
  }

  localsize=(zm2l(offset)+3)/4*4;
#if FIXED_SP
  /*FIXME: adjust localsize to get an aligned stack-frame */
#endif

  function_top(f,v,localsize);

#if FIXED_SP
  pushed=0;
#endif

  for(;p;p=p->next){
    c=p->code;t=p->typf;
    if(c==NOP) {p->z.flags=0;continue;}
    if(c==ALLOCREG) {
      emit(f,"\t\t\t\t// allocreg %s\n",regnames[p->q1.reg]);
      regs[p->q1.reg]=1;continue;
    }
    if(c==FREEREG) {
      emit(f,"\t\t\t\t// freereg %s\n",regnames[p->q1.reg]);
      regs[p->q1.reg]=0;continue;
    }
    if(c==LABEL) {
        int i;
        emit(f,"%s%d: # \n",labprefix,t);
//        for(i=FIRST_GPR;i<=LAST_GPR;++i) // Can't carry register contexts across labels.
//          reg_stackrel[i]=0;
	continue;
    }

    // OK
    if(c==BRA){
      if(0/*t==exit_label&&framesize==0*/)
	function_bottom(f,v,localsize);
      else
        emit_pcreltotemp(f,labprefix,t);
        emit(f,"\tadd\t%s\n",regnames[pc]);
      continue;
    }

    // OK
    if(c>=BEQ&&c<BRA){
      printf("cond\n");
      emit(f,"\tcond\t%s\n",ccs[c-BEQ]);
      emit(f,"\t\t\t\t\t//conditional branch ");
      emit_pcreltotemp(f,labprefix,t);
      emit(f,"\tadd\tr7\n");
      continue;
    }

    // Investigate - but not currently seeing it used.
    if(c==MOVETOREG){
      emit(f,"\t\t\t\t\t//movetoreg\n");
      load_reg(f,p->z.reg,&p->q1,regtype[p->z.reg]->flags);
      continue;
    }

    // Investigate - but not currently seeing it used.
    if(c==MOVEFROMREG){
      emit(f,"\t\t\t\t\t//movefromreg\n");
      store_reg(f,p->z.reg,&p->q1,regtype[p->z.reg]->flags);
      continue;
    }

    // Reject types we can't handle - anything beyond a pointer and chars with more than 1 byte.
    if((c==ASSIGN||c==PUSH)&&((t&NQ)>POINTER||((t&NQ)==CHAR&&zm2l(p->q2.val.vmax)!=1))){
      ierror(0);
    }

    p=preload(f,p); // Setup zreg, etc.

    c=p->code;

    if(c==SUBPFP) c=SUB;
    if(c==ADDI2P) c=ADD;
    if(c==SUBIFP) c=SUB;

    // Investigate - but not currently seeing it used.
    // Sign extension of a register involved moving to temp, extb or exth, move to dest
    // 
    if(c==CONVERT){
      emit(f,"\t\t\t\t\t//FIXME convert\n");
	printf("convert\n");
      if(ISFLOAT(q1typ(p))||ISFLOAT(ztyp(p))) ierror(0);
      if(sizetab[q1typ(p)&NQ]<sizetab[ztyp(p)&NQ]){
	if(q1typ(p)&UNSIGNED)
	  emit(f,"\tzext.%s\t%s\n",dt(q1typ(p)),regnames[zreg]);
	else
	  emit(f,"\tsext.%s\t%s\n",dt(q1typ(p)),regnames[zreg]);
      }
      save_result(f,p);
      continue;
    }

    // Investigate - Still to be implemented
    if(c==KOMPLEMENT){
      emit(f,"\t\t\t\t\t//comp\n");
	printf("comp\n");
      load_reg(f,zreg,&p->q1,t);
      emit(f,"\tcpl.%s\t%s\n",dt(t),regnames[zreg]);
      save_result(f,p);
      continue;
    }

    // May not need to actually load the register here - certainly check before emitting code.
    if(c==SETRETURN){
      emit(f,"\t\t\t\t\t//setreturn\n");
      load_reg(f,p->z.reg,&p->q1,t);
      BSET(regs_modified,p->z.reg);
      continue;
    }

    // Investigate - May not be needed for register mode?
    if(c==GETRETURN){
      emit(f,"\t\t\t\t\t// (getreturn)");
      if(p->q1.reg){
//        emit(f," reg\n");
//        emit(f,"\tmt\t%s\n",regnames[p->q1.reg]);
        zreg=p->q1.reg;
	save_result(f,p);
      }else
      {
	emit(f," not reg\n");
        p->z.flags=0;
      }
      continue;
    }

    // OK - figure out what the bvunite stuff is all about.
    if(c==CALL){
      int reg;
      emit(f,"\t\t\t\t\t//call\n");
      if((p->q1.flags&(VAR|DREFOBJ))==VAR&&p->q1.v->fi&&p->q1.v->fi->inline_asm){
        emit_inline_asm(f,p->q1.v->fi->inline_asm);
      }else{
	/* FIXME - deal with different object types here */
        if(p->q1.v->storage_class==STATIC){
          emit_pcreltotemp(f,labprefix,zm2l(p->q1.v->offset));
          emit(f,"\tadd\t%s\n",regnames[pc]);
        }else{
          emit_externtotemp(f,p->q1.v->identifier);
          emit(f,"\texg\t%s\n",regnames[pc]);
        }
        emit_constanttotemp(f,pushedargsize(p));
        emit(f,"\tadd\t%s\n",regnames[sp]);
	emit(f,"\n");
      }
      /*FIXME*/
#if FIXED_SP
      pushed-=zm2l(p->q2.val.vmax);
#endif
      if((p->q1.flags&(VAR|DREFOBJ))==VAR&&p->q1.v->fi&&(p->q1.v->fi->flags&ALL_REGS)){
	bvunite(regs_modified,p->q1.v->fi->regs_modified,RSIZE);
      }else{
	int i;
	for(i=1;i<=MAXR;i++){
	  if(regscratch[i]) BSET(regs_modified,i);
	}
      }
      continue;
    }

    if(c==ASSIGN||c==PUSH){
      if(t==0) ierror(0);

      // Basically OK - not used very much.  Perhaps don't use a fixed stackframe?
      if(c==PUSH){
        emit(f,"\t\t\t\t\t// (a/p push)\n");
	printf("push\n");

/* FIXME - need to take dt into account */
	emit(f,"\t\t\t\t\t// a: pushed %ld, regnames[sp] %s\n",pushed,regnames[sp]);
	emit_objtotemp(f,&p->q1,t);
	emit(f,"\tstdec\t%s\n",regnames[sp]);
	pushed+=zm2l(p->q2.val.vmax);
	continue;
      }

      // Need to special case writing register to memory using addt and stt
      if(c==ASSIGN){
	// FIXME - have to deal with arrays and structs, not just elementary types
#if 0
	if((p->q1.flags&REG)&&(p->z.flags&VAR) && (p->z.v->storage_class==AUTO))
	{
		emit(f,"\t\t\t\t\t// (assign - reg to auto)\n");
		emit_constanttotemp(f,real_offset(&p->z));
		emit(f,"\taddt\tr6\n\tstt\t%s\n",regnames[p->q1.reg]);
	}
	else
	if((p->q1.flags&REG)&&(p->z.flags&VAR) && (p->z.v->storage_class==REGISTER))
	{
		emit(f,"\t\t\t\t\t// (assign - reg to reg)\n");
		emit_constanttotemp(f,real_offset(&p->z));
		emit(f,"\tmt\t%s\n\tmr\t%s\n",regnames[p->q1.reg],regnames[p->z.reg]);
	}
	else
#endif
	{
		emit(f,"\t\t\t\t\t// (a/p assign)\n");
		emit_prepobj(f,&p->z,t,t2);
		load_temp(f,zreg,&p->q1,t);
		save_temp(f,p);
	}
      }
      continue;

    }

    // Not yet seen it used.
    if(c==ADDRESS){
	emit(f,"\t\t\t\t\t// (address)\n");
      load_address(f,zreg,&p->q1,POINTER);
      save_result(f,p);
      continue;
    }

    // OK
    if(c==MINUS){
      emit(f,"\t\t\t\t\t// (minus)\n");
      load_reg(f,zreg,&p->q1,t);
      emit(f,"\tli\t0\n\texg %s\n\tsub %s\n",regnames[zreg],regnames[zreg]);
      save_result(f,p);
      continue;
    }

    // Compare - replace with subt?  Probably not all that useful
    // Revisit
    if(c==TEST){
      emit(f,"\t\t\t\t\t// (test)\n");
      emit_objtotemp(f,&p->q1,t); // Only need Z flag - moving to temp should be enough.
      continue;
    }

    // Compare - replace with subt?  Probably not all that useful
    // Revisit
    if(c==COMPARE){
	printf("compare\n");
	// FIXME - determine if q2 is a register, if not move to reg, move q1 to temp, compare.
      emit(f,"\t\t\t\t\t// (compare)");
      emit_objtotemp(f,&p->q1,t);
      emit(f,"\tmr\t%s\n",regnames[t2]);
//      reg_stackrel[t2]=0;
      emit_objtotemp(f,&p->q2,t);
      emit(f,"\tcmp\t%s\n",regnames[t2]);
      continue;
    }

    // Bitwise operations - again check on operand marshalling
    if((c>=OR&&c<=AND)||(c>=LSHIFT&&c<=MOD)){
	// FIXME - need to deal with loading both operands here.
        emit(f,"\t\t\t\t\t// (bitwise) ");
        emit(f,"loadreg\n");
	emit_objtotemp(f,&p->q1,t);
	emit(f,"\tmr\t%s\n",regnames[zreg]);
//	reg_stackrel[zreg]=0;
//	emit_prepobj(f,&p->z,t,t2);
	emit_objtotemp(f,&p->q2,t);
      if(c>=OR&&c<=AND)
	emit(f,"\t%s\t%s\n",logicals[c-OR],regnames[zreg]);
      else
	emit(f,"\t%s\t%s\n",arithmetics[c-LSHIFT],regnames[zreg]);
      save_result(f,p);
      continue;
    }
    pric2(stdout,p);
    ierror(0);
  }
  function_bottom(f,v,localsize);
  if(stack_valid){
    if(!v->fi) v->fi=new_fi();
    v->fi->flags|=ALL_STACK;
    v->fi->stack1=stack;
  }
  printf("done\n");
  emit(f,"# stacksize=%lu%s\n",zum2ul(stack),stack_valid?"":"+??");
}

int shortcut(int code,int typ)
{
  return 0;
}

int reg_parm(struct reg_handle *m, struct Typ *t,int vararg,struct Typ *d)
{
  int f;
  f=t->flags&NQ;
  if(f<=LONG||f==POINTER){
    if(m->gregs>=GPR_ARGS)
      return 0;
    else
      return FIRST_GPR+3+m->gregs++;
  }
  if(ISFLOAT(f)){
    if(m->fregs>=FPR_ARGS)
      return 0;
    else
      return FIRST_FPR+2+m->fregs++;
  }
  return 0;
}

int handle_pragma(const char *s)
{
}
void cleanup_cg(FILE *f)
{
}
void cleanup_db(FILE *f)
{
  if(f) section=-1;
}
