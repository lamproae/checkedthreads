
/*--------------------------------------------------------------------*/
/*--- A checkedthreads data race detector.   checkedthreads_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is based on Lackey, an example Valgrind tool that does
   some simple program measurement and tracing.

   Lackey is Copyright (C) 2002-2012 Nicholas Nethercote
      njn@valgrind.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/


// This tool shows how to do some basic instrumentation.
//
// There are four kinds of instrumentation it can do.  They can be turned
// on/off independently with command line options:
//
// * --basic-counts   : do basic counts, eg. number of instructions
//                      executed, jumps executed, etc.
// * --detailed-counts: do more detailed counts:  number of loads, stores
//                      and ALU operations of different sizes.
// * --trace-mem=yes:   trace all (data) memory accesses.
// * --trace-superblocks=yes:   
//                      trace all superblock entries.  Mostly of interest
//                      to the Valgrind developers.
//
// The code for each kind of instrumentation is guarded by a clo_* variable:
// clo_basic_counts, clo_detailed_counts, clo_trace_mem and clo_trace_sbs.
//
// If you want to modify any of the instrumentation code, look for the code
// that is guarded by the relevant clo_* variable (eg. clo_trace_mem)
// If you're not interested in the other kinds of instrumentation you can
// remove them.  If you want to do more complex modifications, please read
// VEX/pub/libvex_ir.h to understand the intermediate representation.
//
//
// Specific Details about --trace-mem=yes
// --------------------------------------
// Lackey's --trace-mem code is a good starting point for building Valgrind
// tools that act on memory loads and stores.  It also could be used as is,
// with its output used as input to a post-mortem processing step.  However,
// because memory traces can be very large, online analysis is generally
// better.
//
// It prints memory data access traces that look like this:
//
//   I  0023C790,2  # instruction read at 0x0023C790 of size 2
//   I  0023C792,5
//    S BE80199C,4  # data store at 0xBE80199C of size 4
//   I  0025242B,3
//    L BE801950,4  # data load at 0xBE801950 of size 4
//   I  0023D476,7
//    M 0025747C,1  # data modify at 0x0025747C of size 1
//   I  0023DC20,2
//    L 00254962,1
//    L BE801FB3,1
//   I  00252305,1
//    L 00254AEB,1
//    S 00257998,1
//
// Every instruction executed has an "instr" event representing it.
// Instructions that do memory accesses are followed by one or more "load",
// "store" or "modify" events.  Some instructions do more than one load or
// store, as in the last two examples in the above trace.
//
// Here are some examples of x86 instructions that do different combinations
// of loads, stores, and modifies.
//
//    Instruction          Memory accesses                  Event sequence
//    -----------          ---------------                  --------------
//    add %eax, %ebx       No loads or stores               instr
//
//    movl (%eax), %ebx    loads (%eax)                     instr, load
//
//    movl %eax, (%ebx)    stores (%ebx)                    instr, store
//
//    incl (%ecx)          modifies (%ecx)                  instr, modify
//
//    cmpsb                loads (%esi), loads(%edi)        instr, load, load
//
//    call*l (%edx)        loads (%edx), stores -4(%esp)    instr, load, store
//    pushl (%edx)         loads (%edx), stores -4(%esp)    instr, load, store
//    movsw                loads (%esi), stores (%edi)      instr, load, store
//
// Instructions using x86 "rep" prefixes are traced as if they are repeated
// N times.
//
// Lackey with --trace-mem gives good traces, but they are not perfect, for
// the following reasons:
//
// - It does not trace into the OS kernel, so system calls and other kernel
//   operations (eg. some scheduling and signal handling code) are ignored.
//
// - It could model loads and stores done at the system call boundary using
//   the pre_mem_read/post_mem_write events.  For example, if you call
//   fstat() you know that the passed in buffer has been written.  But it
//   currently does not do this.
//
// - Valgrind replaces some code (not much) with its own, notably parts of
//   code for scheduling operations and signal handling.  This code is not
//   traced.
//
// - There is no consideration of virtual-to-physical address mapping.
//   This may not matter for many purposes.
//
// - Valgrind modifies the instruction stream in some very minor ways.  For
//   example, on x86 the bts, btc, btr instructions are incorrectly
//   considered to always touch memory (this is a consequence of these
//   instructions being very difficult to simulate).
//
// - Valgrind tools layout memory differently to normal programs, so the
//   addresses you get will not be typical.  Thus Lackey (and all Valgrind
//   tools) is suitable for getting relative memory traces -- eg. if you
//   want to analyse locality of memory accesses -- but is not good if
//   absolute addresses are important.
//
// Despite all these warnings, Lackey's results should be good enough for a
// wide range of purposes.  For example, Cachegrind shares all the above
// shortcomings and it is still useful.
//
// For further inspiration, you should look at cachegrind/cg_main.c which
// uses the same basic technique for tracing memory accesses, but also groups
// events together for processing into twos and threes so that fewer C calls
// are made and things run faster.
//
// Specific Details about --trace-superblocks=yes
// ----------------------------------------------
// Valgrind splits code up into single entry, multiple exit blocks
// known as superblocks.  By itself, --trace-superblocks=yes just
// prints a message as each superblock is run:
//
//  SB 04013170
//  SB 04013177
//  SB 04013173
//  SB 04013177
//
// The hex number is the address of the first instruction in the
// superblock.  You can see the relationship more obviously if you use
// --trace-superblocks=yes and --trace-mem=yes together.  Then a "SB"
// message at address X is immediately followed by an "instr:" message
// for that address, as the first instruction in the block is
// executed, for example:
//
//  SB 04014073
//  I  04014073,3
//   L 7FEFFF7F8,8
//  I  04014076,4
//  I  0401407A,3
//  I  0401407D,3
//  I  04014080,3
//  I  04014083,6


#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_options.h"
#include "pub_tool_machine.h"     // VG_(fnptr_to_fnentry)
#include "pub_tool_mallocfree.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_stacktrace.h"
#include <stdint.h>

/*------------------------------------------------------------*/
/*--- Command line options                                 ---*/
/*------------------------------------------------------------*/

/* Command line options controlling instrumentation kinds, as described at
 * the top of this file. */
static Bool clo_trace_mem       = True;
static Bool clo_print_commands  = False;

static Bool lk_process_cmd_line_option(Char* arg)
{
   if VG_BOOL_CLO(arg, "--print-commands", clo_print_commands) {}
   else
      return False;
   return True;
}

static void lk_print_usage(void)
{  
   VG_(printf)(
"    --print-commands=no|yes   print commands issued by the checkedtheads\n"
"                              runtime [no]\n"
   );
}

static void lk_print_debug_usage(void)
{  
   VG_(printf)(
"    (none)\n"
   );
}

/*------------------------------------------------------------*/
/*--- Stuff for --trace-mem                                ---*/
/*------------------------------------------------------------*/

#define MAX_DSIZE    512

typedef
   IRExpr 
   IRAtom;

typedef 
   enum { Event_Ir, Event_Dr, Event_Dw, Event_Dm }
   EventKind;

typedef
   struct {
      EventKind  ekind;
      IRAtom*    addr;
      Int        size;
   }
   Event;

/* Up to this many unnotified events are allowed.  Must be at least two,
   so that reads and writes to the same address can be merged into a modify.
   Beyond that, larger numbers just potentially induce more spilling due to
   extending live ranges of address temporaries. */
#define N_EVENTS 4

/* Maintain an ordered list of memory events which are outstanding, in
   the sense that no IR has yet been generated to do the relevant
   helper calls.  The SB is scanned top to bottom and memory events
   are added to the end of the list, merging with the most recent
   notified event where possible (Dw immediately following Dr and
   having the same size and EA can be merged).

   This merging is done so that for architectures which have
   load-op-store instructions (x86, amd64), the instr is treated as if
   it makes just one memory reference (a modify), rather than two (a
   read followed by a write at the same address).

   At various points the list will need to be flushed, that is, IR
   generated from it.  That must happen before any possible exit from
   the block (the end, or an IRStmt_Exit).  Flushing also takes place
   when there is no space to add a new event.

   If we require the simulation statistics to be up to date with
   respect to possible memory exceptions, then the list would have to
   be flushed before each memory reference.  That's a pain so we don't
   bother.

   Flushing the list consists of walking it start to end and emitting
   instrumentation IR for each event, in the order in which they
   appear. */

static Event events[N_EVENTS];
static Int   events_used = 0;

#define MAGIC 0x12345678
#define CONST_MAGIC "Valgrind command"
#define MAX_CMD 128

typedef struct {
    volatile uint32_t stored_magic;
    const char const_magic[16];
    volatile char payload[MAX_CMD];
} ct_cmd;

static ct_cmd* g_ct_last_cmd = 0; /* ignore writes to cmd */

/* 3-level page table; up to 2^36 pages of 2^12 bytes each,
   organized into levels of up to 2^12 entries each.
   should work fine for virtual addresses of up to 48 bits. */
#define PAGE_BITS 12
#define PAGE_SIZE (1<<PAGE_BITS)
#define L1_BITS 12
#define NUM_PAGES (1<<L1_BITS)
#define L2_BITS 12
#define NUM_L1_PAGETABS (1<<L2_BITS)
#define L3_BITS 12
#define NUM_L2_PAGETABS (1<<L3_BITS)

#define L2_PAGETAB(addr) ((((UInt)addr >> 24) >> 12) & 0xfff)
#define L1_PAGETAB(addr) (((UInt)addr >> 24) & 0xfff)
#define PAGE(addr) (((UInt)addr >> 12) & 0xfff)
#define BYTE_IN_PAGE(addr) ((UInt)addr & 0xfff)

typedef struct ct_page_ {
    /* 0 means "owned by none" (so is OK to access).
       the rest means "owned by i" (so is OK to access for i only.) */
    unsigned char owning_thread[PAGE_SIZE];
    /* we keep a linked a list of allocated pages so as to not have
       to traverse all indexes to find allocated pages. */
    struct ct_page_* prev_alloc_page;
    int index;
} ct_page;

typedef struct ct_pagetab_L1_ {
    ct_page* pages[NUM_PAGES];
    ct_page* last_alloc_page; /* head of allocated pages list */
    struct ct_pagetab_L1_* prev_alloc_pagetab_L1;
} ct_pagetab_L1;

typedef struct ct_pagetab_L2_ {
    ct_pagetab_L1* pagetabs_L1[NUM_L1_PAGETABS];
    ct_pagetab_L1* last_alloc_pagetab_L1;
    struct ct_pagetab_L2_* prev_alloc_pagetab_L2;
} ct_pagetab_L2;

typedef struct ct_pagetab_L3_ {
    ct_pagetab_L2* pagetabs_L2[NUM_L2_PAGETABS];
    ct_pagetab_L2* last_alloc_pagetab_L2;
} ct_pagetab_L3;

typedef struct ct_pagetab_stack_ {
    ct_pagetab_L3* pagetab_L3;
    struct ct_pagetab_stack_* next_stack_entry;
} ct_pagetab_stack;

static Bool g_ct_active = False;
static Int g_ct_curr_thread = 0;
static ct_pagetab_stack* g_ct_pagetab_stack;
/* FIXME: make a pointer and stuff */
static ct_pagetab_L3 g_ct_pagetab_L3 = {{0},0}; /* top of the pagetab stack */
static char* g_ct_stackbot = 0;
static char* g_ct_stackend = 0;

static ct_page* ct_get_page(Addr a)
{
    ct_pagetab_L3* pagetab_L3 = &g_ct_pagetab_L3;
    ct_pagetab_L2* pagetab_L2;
    ct_pagetab_L1* pagetab_L1;
    ct_page* page;

    UInt pt2_index = L2_PAGETAB(a);
    pagetab_L2 = pagetab_L3->pagetabs_L2[pt2_index];
    if(pagetab_L2 == 0) {
        pagetab_L2 = (ct_pagetab_L2*)VG_(calloc)("pagetab_L2", 1, sizeof(ct_pagetab_L2));
        pagetab_L2->prev_alloc_pagetab_L2 = pagetab_L3->last_alloc_pagetab_L2; 
        pagetab_L3->last_alloc_pagetab_L2 = pagetab_L2;
        pagetab_L3->pagetabs_L2[pt2_index] = pagetab_L2;
    }

    UInt pt1_index = L1_PAGETAB(a);
    pagetab_L1 = pagetab_L2->pagetabs_L1[pt1_index];
    if(pagetab_L1 == 0) {
        pagetab_L1 = (ct_pagetab_L1*)VG_(calloc)("pagetab_L1", 1, sizeof(ct_pagetab_L1));
        pagetab_L1->prev_alloc_pagetab_L1 = pagetab_L2->last_alloc_pagetab_L1;
        pagetab_L2->last_alloc_pagetab_L1 = pagetab_L1;
        pagetab_L2->pagetabs_L1[pt1_index] = pagetab_L1;
    }
    
    Int page_index = PAGE(a);
    page = pagetab_L1->pages[page_index];
    if(page == 0) {
        page = (ct_page*)VG_(calloc)("page", 1, sizeof(ct_page));
        page->prev_alloc_page = pagetab_L1->last_alloc_page;
        pagetab_L1->last_alloc_page = page;
        pagetab_L1->pages[page_index] = page;
    }
    return page;
}

static void ct_clear_pagetab(void)
{
    ct_pagetab_L3* pagetab_L3 = &g_ct_pagetab_L3;
    ct_pagetab_L2* pagetab_L2 = pagetab_L3->last_alloc_pagetab_L2;
    /* free all L2 pages */
    while(pagetab_L2) {
        ct_pagetab_L2* prev_pagetab_L2 = pagetab_L2->prev_alloc_pagetab_L2;
        /* free all L1 pages */
        ct_pagetab_L1* pagetab_L1 = pagetab_L2->last_alloc_pagetab_L1;
        while(pagetab_L1) {
            ct_pagetab_L1* prev_pagetab_L1 = pagetab_L1->prev_alloc_pagetab_L1;
            /* free all pages */
            ct_page* page = pagetab_L1->last_alloc_page;
            while(page) {
                ct_page* prev_page = page->prev_alloc_page;
                VG_(free)(page);
                page = prev_page;
            }
            /* free the L1 pagetab */
            VG_(free)(pagetab_L1);
            pagetab_L1 = prev_pagetab_L1;
        }
        /* free the L2 pagetab */
        VG_(free)(pagetab_L2);
        pagetab_L2 = prev_pagetab_L2;
    }
    VG_(memset)(pagetab_L3->pagetabs_L2, 0, sizeof(pagetab_L3->pagetabs_L2));
    pagetab_L3->last_alloc_pagetab_L2 = 0;
}

static char* ct_stack_end(void)
{
    ThreadId tid = VG_(get_running_tid)();
    return (char*)(VG_(thread_get_stack_max)(tid) - VG_(thread_get_stack_size)(tid));
}

static Bool ct_suppress(Addr addr)
{
    char* p = (char*)addr;
    /* ignore writes to the stack below stackbot (the point where the ct framework was entered.) */
    if(p >= g_ct_stackend && p < g_ct_stackbot) {
        return True;
    }
    if(p < g_ct_stackend) { /* perhaps the stack grew in the meanwhile? */
        g_ct_stackend = ct_stack_end();
        /* repeat the test */
        if(p >= g_ct_stackend && p < g_ct_stackbot) {
            return True;
        }
    }
    /* ignore writes to the command object. */
    if(p >= (char*)g_ct_last_cmd && p < (char*)(g_ct_last_cmd+1)) {
        return True;
    }
    return False;
}

static inline void ct_on_access(Addr base, SizeT size, Bool store)
{
    SizeT i;
    for(i=0; i<size; ++i) {
        Addr addr = base+i;
        ct_page* page = ct_get_page(addr);
        int index_in_page = BYTE_IN_PAGE(addr);
        int owner = page->owning_thread[index_in_page];
        if(owner && owner != g_ct_curr_thread) {
            if(!ct_suppress(addr)) {
                VG_(printf)("checkedthreads: error - thread %d accessed %p [%p,%d], owned by %d\n",
                        g_ct_curr_thread-1,
                        (void*)addr, (void*)base, (int)size,
                        owner-1);
                VG_(get_and_pp_StackTrace)(VG_(get_running_tid)(), 20);
                break;
            }
        }
        if(store) {
            /* update the owner */
            page->owning_thread[index_in_page] = g_ct_curr_thread;
        }
    }
}

static Bool ct_str_is(volatile const char* variable, const char* constant)
{
    int i=0;
    while(constant[i]) {
        if(variable[i] != constant[i]) {
            return False;
        }
        ++i;
    }
    return True;
}

static Addr ct_cmd_ptr(ct_cmd* cmd, int oft)
{
    return *(Addr*)&cmd->payload[oft];
}

static Int ct_cmd_int(ct_cmd* cmd, int oft)
{
    return *(volatile int32_t*)&cmd->payload[oft];
}

static void ct_process_command(ct_cmd* cmd)
{
    if(!ct_str_is(cmd->const_magic, CONST_MAGIC)) {
        return;
    }
    if(ct_str_is(cmd->payload, "begin_for")) {
        if(clo_print_commands) VG_(printf)("begin_for\n");
    }
    else if(ct_str_is(cmd->payload, "end_for")) {
        if(clo_print_commands) VG_(printf)("end_for\n");
        ct_clear_pagetab();
        g_ct_curr_thread = 0;
    }
    else if(ct_str_is(cmd->payload, "iter")) {
        if(clo_print_commands) VG_(printf)("iter %d\n", ct_cmd_int(cmd, 4));
        g_ct_active = True;
    }
    else if(ct_str_is(cmd->payload, "done")) {
        if(clo_print_commands) VG_(printf)("done %d\n", ct_cmd_int(cmd, 4));
        g_ct_active = False;
    }
    else if(ct_str_is(cmd->payload, "thrd")) {
        g_ct_curr_thread = ct_cmd_int(cmd, 4)+1;
    }
    else if(ct_str_is(cmd->payload, "stackbot")) {
        g_ct_stackbot = (char*)ct_cmd_ptr(cmd, 8);
        g_ct_stackend = ct_stack_end();
        if(clo_print_commands) VG_(printf)("stackbot %p [stackend %p]\n",
                (void*)g_ct_stackbot, (void*)g_ct_stackend);
    }
    else {
        VG_(printf)("checkedthreads: WARNING - unknown command!\n");
        VG_(get_and_pp_StackTrace)(VG_(get_running_tid)(), 20);
    }
    g_ct_last_cmd = cmd;
}

static VG_REGPARM(2) void trace_load(Addr addr, SizeT size)
{
    if(g_ct_active) {
        ct_on_access(addr, size, False);
    }
}

static inline void ct_on_store(Addr addr, SizeT size)
{
   ct_cmd* p = (ct_cmd*)addr;
   if(p->stored_magic == MAGIC) {
       ct_process_command(p);
   }
   if(g_ct_active) {
       ct_on_access(addr, size, True);
   }
}

static VG_REGPARM(2) void trace_store(Addr addr, SizeT size)
{
    ct_on_store(addr, size);
}

static VG_REGPARM(2) void trace_modify(Addr addr, SizeT size)
{
    ct_on_store(addr, size);
}


static void flushEvents(IRSB* sb)
{
   Int        i;
   Char*      helperName;
   void*      helperAddr;
   IRExpr**   argv;
   IRDirty*   di;
   Event*     ev;

   for (i = 0; i < events_used; i++) {

      ev = &events[i];

      helperAddr = NULL;
      
      // Decide on helper fn to call and args to pass it.
      switch (ev->ekind) {
         case Event_Ir: helperAddr = NULL; break;

         case Event_Dr: helperName = "trace_load";
                        helperAddr =  trace_load;   break;

         case Event_Dw: helperName = "trace_store";
                        helperAddr =  trace_store;  break;

         case Event_Dm: helperName = "trace_modify";
                        helperAddr =  trace_modify; break;
         default:
            tl_assert(0);
      }

      // Add the helper.
      if (helperAddr) {
          argv = mkIRExprVec_2( ev->addr, mkIRExpr_HWord( ev->size ) );
          di   = unsafeIRDirty_0_N( /*regparms*/2, 
                  helperName, VG_(fnptr_to_fnentry)( helperAddr ),
                  argv );
          addStmtToIRSB( sb, IRStmt_Dirty(di) );
      }
   }

   events_used = 0;
}

// original comment from Lackey follows; checkedthreads follows the advice
// and indeed doesn't add calls to trace_instr in flushEvents.

// WARNING:  If you aren't interested in instruction reads, you can omit the
// code that adds calls to trace_instr() in flushEvents().  However, you
// must still call this function, addEvent_Ir() -- it is necessary to add
// the Ir events to the events list so that merging of paired load/store
// events into modify events works correctly.
static void addEvent_Ir ( IRSB* sb, IRAtom* iaddr, UInt isize )
{
   Event* evt;
   tl_assert(clo_trace_mem);
   tl_assert( (VG_MIN_INSTR_SZB <= isize && isize <= VG_MAX_INSTR_SZB)
            || VG_CLREQ_SZB == isize );
   if (events_used == N_EVENTS)
      flushEvents(sb);
   tl_assert(events_used >= 0 && events_used < N_EVENTS);
   evt = &events[events_used];
   evt->ekind = Event_Ir;
   evt->addr  = iaddr;
   evt->size  = isize;
   events_used++;
}

static
void addEvent_Dr ( IRSB* sb, IRAtom* daddr, Int dsize )
{
   Event* evt;
   tl_assert(clo_trace_mem);
   tl_assert(isIRAtom(daddr));
   tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);
   if (events_used == N_EVENTS)
      flushEvents(sb);
   tl_assert(events_used >= 0 && events_used < N_EVENTS);
   evt = &events[events_used];
   evt->ekind = Event_Dr;
   evt->addr  = daddr;
   evt->size  = dsize;
   events_used++;
}

static
void addEvent_Dw ( IRSB* sb, IRAtom* daddr, Int dsize )
{
   Event* lastEvt;
   Event* evt;
   tl_assert(clo_trace_mem);
   tl_assert(isIRAtom(daddr));
   tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);

   // Is it possible to merge this write with the preceding read?
   lastEvt = &events[events_used-1];
   if (events_used > 0
    && lastEvt->ekind == Event_Dr
    && lastEvt->size  == dsize
    && eqIRAtom(lastEvt->addr, daddr))
   {
      lastEvt->ekind = Event_Dm;
      return;
   }

   // No.  Add as normal.
   if (events_used == N_EVENTS)
      flushEvents(sb);
   tl_assert(events_used >= 0 && events_used < N_EVENTS);
   evt = &events[events_used];
   evt->ekind = Event_Dw;
   evt->size  = dsize;
   evt->addr  = daddr;
   events_used++;
}


/*------------------------------------------------------------*/
/*--- Basic tool functions                                 ---*/
/*------------------------------------------------------------*/

static void lk_post_clo_init(void)
{
}

static
IRSB* lk_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn, 
                      VexGuestLayout* layout, 
                      VexGuestExtents* vge,
                      IRType gWordTy, IRType hWordTy )
{
   Int        i;
   IRSB*      sbOut;
   IRTypeEnv* tyenv = sbIn->tyenv;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   /* Set up SB */
   sbOut = deepCopyIRSBExceptStmts(sbIn);

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( sbOut, sbIn->stmts[i] );
      i++;
   }

   if (clo_trace_mem) {
      events_used = 0;
   }

   for (/*use current i*/; i < sbIn->stmts_used; i++) {
      IRStmt* st = sbIn->stmts[i];
      if (!st || st->tag == Ist_NoOp) continue;

      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_IMark:
            if (clo_trace_mem) {
               // WARNING: do not remove this function call, even if you
               // aren't interested in instruction reads.  See the comment
               // above the function itself for more detail.
               addEvent_Ir( sbOut, mkIRExpr_HWord( (HWord)st->Ist.IMark.addr ),
                            st->Ist.IMark.len );
            }
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_WrTmp:
            // Add a call to trace_load() if --trace-mem=yes.
            if (clo_trace_mem) {
               IRExpr* data = st->Ist.WrTmp.data;
               if (data->tag == Iex_Load) {
                  addEvent_Dr( sbOut, data->Iex.Load.addr,
                               sizeofIRType(data->Iex.Load.ty) );
               }
            }
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_Store:
            if (clo_trace_mem) {
               IRExpr* data  = st->Ist.Store.data;
               addEvent_Dw( sbOut, st->Ist.Store.addr,
                            sizeofIRType(typeOfIRExpr(tyenv, data)) );
            }
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_Dirty: {
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_CAS: {
            /* We treat it as a read and a write of the location.  I
               think that is the same behaviour as it was before IRCAS
               was introduced, since prior to that point, the Vex
               front ends would translate a lock-prefixed instruction
               into a (normal) read followed by a (normal) write. */
            Int    dataSize;
            IRType dataTy;
            IRCAS* cas = st->Ist.CAS.details;
            tl_assert(cas->addr != NULL);
            tl_assert(cas->dataLo != NULL);
            dataTy   = typeOfIRExpr(tyenv, cas->dataLo);
            dataSize = sizeofIRType(dataTy);
            if (cas->dataHi != NULL)
               dataSize *= 2; /* since it's a doubleword-CAS */
            if (clo_trace_mem) {
               addEvent_Dr( sbOut, cas->addr, dataSize );
               addEvent_Dw( sbOut, cas->addr, dataSize );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_LLSC: {
            IRType dataTy;
            if (st->Ist.LLSC.storedata == NULL) {
               /* LL */
               dataTy = typeOfIRTemp(tyenv, st->Ist.LLSC.result);
               if (clo_trace_mem)
                  addEvent_Dr( sbOut, st->Ist.LLSC.addr,
                                      sizeofIRType(dataTy) );
            } else {
               /* SC */
               dataTy = typeOfIRExpr(tyenv, st->Ist.LLSC.storedata);
               if (clo_trace_mem)
                  addEvent_Dw( sbOut, st->Ist.LLSC.addr,
                                      sizeofIRType(dataTy) );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_Exit:
            if (clo_trace_mem) {
               flushEvents(sbOut);
            }

            addStmtToIRSB( sbOut, st );      // Original statement

            break;

         default:
            tl_assert(0);
      }
   }

   if (clo_trace_mem) {
      /* At the end of the sbIn.  Flush outstandings. */
      flushEvents(sbOut);
   }

   return sbOut;
}

static void lk_fini(Int exitcode)
{
}

static void lk_pre_clo_init(void)
{
   VG_(details_name)            ("checkedthreads");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("a data race detector for the checkedthreads framework");
   VG_(details_copyright_author)(
      "Copyright (C) 2012-2013 by Yossi Kreinin (Yossi.Kreinin@gmail.com)");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);
   VG_(details_avg_translation_sizeB) ( 200 );

   VG_(basic_tool_funcs)          (lk_post_clo_init,
                                   lk_instrument,
                                   lk_fini);
   VG_(needs_command_line_options)(lk_process_cmd_line_option,
                                   lk_print_usage,
                                   lk_print_debug_usage);
}

VG_DETERMINE_INTERFACE_VERSION(lk_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                    checkedthreads_main.c ---*/
/*--------------------------------------------------------------------*/
