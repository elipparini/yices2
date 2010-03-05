/*
 * Context
 */

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "memalloc.h"
#include "yices_globals.h"

#include "context.h"
#include "eq_learner.h"
#include "simplex.h"
#include "idl_floyd_warshall.h"
#include "rdl_floyd_warshall.h"
#include "fun_solver.h"
#include "bvsolver.h"


#define TRACE 0
#define TEST_DL 0
#define TRACE_EQ_ABS 0

#if (TRACE || TEST_DL || TRACE_EQ_ABS)

#include <stdio.h>
#include <inttypes.h>
#include "solver_printer.h"
#include "term_printer.h"
#include "cputime.h"

#endif



/**************************
 *  UNION-FIND STRUCTURE  *
 *************************/

/*
 * Initialization:
 * - n = initial size
 * - ttbl = type table for all terms in this partition
 * If n = 0, nothing is allocated, arrays of default size are allocated
 * on the first addition.
 */
static void init_partition(partition_t *p, uint32_t n, type_table_t *ttbl) {
  if (n >= MAX_PARTITION_SIZE) {
    out_of_memory();
  }

  p->size = n;
  p->nelems = 0;
  p->types = ttbl;
  if (n == 0) {
    p->parent = NULL;
    p->type = NULL;
    p->rank = NULL;
  } else {
    p->parent = (term_t *) safe_malloc(n * sizeof(term_t));
    p->type = (type_t *) safe_malloc(n * sizeof(type_t));
    p->rank = (uint8_t *) safe_malloc(n * sizeof(uint8_t));
  }
}


/*
 * Delete: free arrays parent, type, and rank
 */
static void delete_partition(partition_t *p) {
  safe_free(p->parent);
  safe_free(p->type);
  safe_free(p->rank);
  p->parent = NULL;
  p->type = NULL;
  p->rank = NULL;
}


/*
 * Reset: empty the whole partition
 */
static inline void reset_partition(partition_t *p) {
  p->nelems = 0;
}

/*
 * Resize: make sure arrays are large enough for adding element t
 * and initialize parents[i] to NULL_TERM (INT32_MIN) for i = p->nelems to t
 * - do nothing if t < p->nelems;
 */
static void resize_partition(partition_t *p, term_t t) {
  uint32_t n;
  term_t i;

  n = p->size;
  if (n <= t) {
    if (n == 0) {
      n = DEF_PARTITION_SIZE;
    } else {
      n += n>>1; // try to increase by 50%
    }
    if (n <= t) n = t + 1;
    if (n >= MAX_PARTITION_SIZE) {
      out_of_memory();
    }

    p->size = n;
    p->parent = (term_t *) safe_realloc(p->parent, n * sizeof(term_t));
    p->type = (type_t *) safe_realloc(p->type, n * sizeof(type_t));
    p->rank = (uint8_t *) safe_realloc(p->rank, n * sizeof(uint8_t));
  }

  assert(t < p->size);

  for (i=p->nelems; i<=t; i++) {
    p->parent[i] = NULL_TERM;
  }
  p->nelems = i;
}


/*
 * Auxiliary function: add t to the partition.
 * - t must not be present in the partition.
 * - tau = type of t
 * - rnk = rank of t (should be either 0 or 255)
 */
static void partition_add(partition_t *p, term_t t, type_t tau, uint8_t rnk) {
  assert(t >= 0);

  if (t >= p->nelems) resize_partition(p, t);
  assert(p->parent[t] == NULL_TERM);
  p->parent[t] = t;
  p->type[t] = tau;
  p->rank[t] = rnk;
}


/*
 * Add uninterpreted term t to the partition in a singleton class
 * - tau must be the type of t
 */
static inline void partition_add_term(partition_t *p, term_t t, type_t tau) {
  partition_add(p, t, tau, 0);
}

/*
 * Add t to the partition in a singleton class and make
 * sure t will remain root of its class.
 */
static inline void partition_add_root(partition_t *p, term_t t, type_t tau) {
  partition_add(p, t, tau, 255);
}



/*
 * Find the root of t's class
 * - return NULL_TERM if t has not been added to any class
 */
static term_t partition_find(partition_t *p, term_t t) {
  term_t y, r;
  term_t *parent;

  assert(t >= 0);

  if (t >= p->nelems) return NULL_TERM;

  parent = p->parent;
  y = parent[t];
  if (y < 0 || y == t) return y;

  // find the root r
  for (;;) {
    r = parent[y];
    if (r == y) break;
    y = r;
  }

#if 1
  // path compression, starting from t
  do {
    y = parent[t];
    parent[t] = r;
    t = y;
  } while (t != r);
#endif

  return r;
}



/*
 * Check whether t is present and root of its class
 */
static inline bool term_is_root(partition_t *p, term_t t) {
  assert(t >= 0);
  return t < p->nelems && p->parent[t] == t;
}


/*
 * Get the type of the class of t
 * - t must be root of its class
 */
static inline type_t partition_class_type(partition_t *p, term_t t) {
  assert(term_is_root(p, t));
  return p->type[t];
}

/*
 * Check whether root t is frozen
 * - this means that t is not an uninterpreted term (and not assigned to true/false)
 */
static inline bool root_is_frozen(partition_t *p, term_t t) {
  assert(term_is_root(p, t));
  return p->rank[t] == 255;
}


/*
 * Freeze the class of t
 * - t must be the root of that class
 */
static inline void freeze_class(partition_t *p, term_t t) {
  assert(term_is_root(p, t));
  p->rank[t] = 255;
}


/*
 * Check whether the classes of x and y can be merged
 * - both x and y must be root and distinct
 */
static bool mergeable_classes(partition_t *p, term_t x, term_t y) {
  assert(term_is_root(p, x) && term_is_root(p, y) && x != y);

  if (p->rank[x] == 255) {
    /*
     * y := x is allowed if y is not frozen and type[x] <= type[y]
     */
    return p->rank[y] != 255 && is_subtype(p->types, p->type[x], p->type[y]);

  } else if (p->rank[y] == 255) {
    /*
     * x := y is allowed if x is not frozen and type[y] <= type[x]
     */
    return p->rank[x] != 255 && is_subtype(p->types, p->type[y], p->type[x]);
    
  } else {
    // both are variables of compatible types
    assert(compatible_types(p->types, p->type[x], p->type[y]));
    return true;
  }
}


/*
 * Merge the classes of x and y
 * - both must be root of their class and the classes must be mergeable
 */
static void partition_merge(partition_t *p, term_t x, term_t y) {
  uint8_t r_x, r_y;

  assert(mergeable_classes(p, x, y));

  r_x = p->rank[x];
  r_y = p->rank[y];

  assert(r_x != 255 || r_y != 255);

  if (r_x < r_y) {
    // y stays root
    p->parent[x] = y;
    p->type[y] = inf_type(p->types, p->type[x], p->type[y]);
  } else {
    // x stays root
    p->parent[y] = x;
    p->type[x] = inf_type(p->types, p->type[x], p->type[y]);
    if (r_x == r_y) {
      p->rank[x] = r_x + 1;
    }
  }
}



/*
 * Number of equalities encoded in the partition table
 */
static uint32_t partition_num_eqs(partition_t *p) {
  term_t t, r;
  uint32_t c;

  c = 0;
  for (t = 0; t<p->nelems; t++) {
    r = p->parent[t];
    if (r >= 0 && r != t) c++;
  }
  return c;
}



/****************
 *  PARAMETERS  *
 ***************/

/*
 * Map architecture id to theories word
 */
static const uint32_t const arch2theories[NUM_ARCH] = {
  0,                           //  CTX_ARCH_NOSOLVERS --> empty theory

  UF_MASK,                     //  CTX_ARCH_EG
  ARITH_MASK,                  //  CTX_ARCH_SPLX
  IDL_MASK,                    //  CTX_ARCH_IFW
  RDL_MASK,                    //  CTX_ARCH_RFW
  BV_MASK,                     //  CTX_ARCH_BV
  UF_MASK|FUN_MASK,            //  CTX_ARCH_EGFUN
  UF_MASK|ARITH_MASK,          //  CTX_ARCH_EGSPLX
  UF_MASK|BV_MASK,             //  CTX_ARCH_EGBV
  UF_MASK|ARITH_MASK|FUN_MASK, //  CTX_ARCH_EGFUNSPLX
  UF_MASK|BV_MASK|FUN_MASK,    //  CTX_ARCH_EGFUNBV
  ALLTH_MASK,                  //  CTX_ARCH_EGFUNSPLXBV

  IDL_MASK,                    //  CTX_ARCH_AUTO_IDL
  RDL_MASK,                    //  CTX_ARCH_AUTO_RDL
};


/*
 * Each architecture has a fixed set of solver components:
 * - the set of components is stored as a bit vector (on 8bits)
 * - this uses the following bit-masks
 * For the AUTO_xxx architecture, nothing is required initially,
 * so the bitmask is 0.
 */
#define EGRPH  0x1
#define SPLX   0x2
#define IFW    0x4
#define RFW    0x8
#define BVSLVR 0x10
#define FSLVR  0x20

static const uint8_t const arch_components[NUM_ARCH] = {
  0,                        //  CTX_ARCH_NOSOLVERS

  EGRPH,                    //  CTX_ARCH_EG
  SPLX,                     //  CTX_ARCH_SPLX
  IFW,                      //  CTX_ARCH_IFW
  RFW,                      //  CTX_ARCH_RFW
  BVSLVR,                   //  CTX_ARCH_BV
  EGRPH|FSLVR,              //  CTX_ARCH_EGFUN
  EGRPH|SPLX,               //  CTX_ARCH_EGSPLX
  EGRPH|BVSLVR,             //  CTX_ARCH_EGBV
  EGRPH|SPLX|FSLVR,         //  CTX_ARCH_EGFUNSPLX
  EGRPH|BVSLVR|FSLVR,       //  CTX_ARCH_EGFUNBV
  EGRPH|SPLX|BVSLVR|FSLVR,  //  CTX_ARCH_EGFUNSPLXBV

  0,                        //  CTX_ARCH_AUTO_IDL
  0,                        //  CTX_ARCH_AUTO_RDL
};


/*
 * Smt mode for a context mode
 */
static const smt_mode_t const core_mode[NUM_MODES] = {
  SMT_MODE_BASIC,       // one check
  SMT_MODE_BASIC,       // multichecks
  SMT_MODE_PUSHPOP,     // push/pop
  SMT_MODE_INTERACTIVE, // interactive
};


/*
 * Flags for a context mode
 */
static const uint32_t const mode2options[NUM_MODES] = {
  0,
  MULTICHECKS_OPTION_MASK,
  MULTICHECKS_OPTION_MASK|PUSHPOP_OPTION_MASK,
  MULTICHECKS_OPTION_MASK|PUSHPOP_OPTION_MASK|CLEANINT_OPTION_MASK,
};




/******************
 *  EMPTY SOLVER  *
 *****************/

/*
 * We need an empty theory solver for initializing
 * the core if the architecture is NOSOLVERS.
 */
static void donothing(void *solver) {  
}

static void null_backtrack(void *solver, uint32_t backlevel) {
}

static bool null_propagate(void *solver) {
  return true;
}

static fcheck_code_t null_final_check(void *solver) {
  return FCHECK_SAT;
}

static th_ctrl_interface_t null_ctrl = {
  donothing,        // start_internalization
  donothing,        // start_search
  null_propagate,   // propagate
  null_final_check, // final check
  donothing,        // increase_decision_level
  null_backtrack,   // backtrack
  donothing,        // push
  donothing,        // pop
  donothing,        // reset
};


// for the smt interface, nothing should be called since there are no atoms
static th_smt_interface_t null_smt = {
  NULL, NULL, NULL, NULL, NULL,
};




/****************************
 *  SIMPLEX SOLVER OPTIONS  *
 ***************************/

/*
 * Which version of the arithmetic solver is present
 */
bool context_has_idl_solver(context_t *ctx) {
  uint8_t solvers;
  solvers = arch_components[ctx->arch];
  return ctx->arith_solver != NULL && (solvers & IFW);
}

bool context_has_rdl_solver(context_t *ctx) {
  uint8_t solvers;
  solvers = arch_components[ctx->arch];
  return ctx->arith_solver != NULL && (solvers & RFW);
}

bool context_has_simplex_solver(context_t *ctx) {
  uint8_t solvers;
  solvers = arch_components[ctx->arch];
  return ctx->arith_solver != NULL && (solvers & SPLX);
}



/*
 * If the simplex solver already exists, the options are propagated.
 * Otherwise, they are recorded into the option flags. They will
 * be set up when the simplex solver is created.
 */
void enable_splx_eager_lemmas(context_t *ctx) {
  ctx->options |= SPLX_EGRLMAS_OPTION_MASK;
  if (context_has_simplex_solver(ctx)) {
    simplex_enable_eager_lemmas(ctx->arith_solver);
  }  
}

void disable_splx_eager_lemmas(context_t *ctx) {
  ctx->options &= ~SPLX_EGRLMAS_OPTION_MASK;
  if (context_has_simplex_solver(ctx)) {
    simplex_disable_eager_lemmas(ctx->arith_solver);
  }
}


void enable_splx_periodic_icheck(context_t *ctx) {
  ctx->options |= SPLX_ICHECK_OPTION_MASK;
  if (context_has_simplex_solver(ctx)) {
    simplex_enable_periodic_icheck(ctx->arith_solver);
  }
}

void disable_splx_periodic_icheck(context_t *ctx) {
  ctx->options &= ~SPLX_ICHECK_OPTION_MASK;
  if (context_has_simplex_solver(ctx)) {
    simplex_disable_periodic_icheck(ctx->arith_solver);
  }
}




/****************************
 *  COMPONENT CONSTRUCTION  *
 ***************************/

/*
 * Create and initialize the egraph
 * - the core must be created first
 */
static void create_egraph(context_t *ctx) {
  egraph_t *egraph;

  assert(ctx->egraph == NULL);

  egraph = (egraph_t *) safe_malloc(sizeof(egraph_t));
  init_egraph(egraph, ctx->types);
  ctx->egraph = egraph;
}


/*
 * Create and initialize the idl solver and attach it to the core
 * - there must be no other solvers and no egraph
 * - also initialize the core
 */
static void create_idl_solver(context_t *ctx) {
  idl_solver_t *solver;
  smt_mode_t cmode;

  assert(ctx->egraph == NULL && ctx->arith_solver == NULL && ctx->bv_solver == NULL &&
	 ctx->fun_solver == NULL && ctx->core != NULL);

  cmode = core_mode[ctx->mode];
  solver = (idl_solver_t *) safe_malloc(sizeof(idl_solver_t));
  init_idl_solver(solver, ctx->core, &ctx->gate_manager);
  init_smt_core(ctx->core, CTX_DEFAULT_CORE_SIZE, solver, idl_ctrl_interface(solver),
		idl_smt_interface(solver), cmode);
  idl_solver_init_jmpbuf(solver, &ctx->env);
  ctx->arith_solver = solver;
  ctx->arith = idl_arith_interface(solver);
}


/*
 * Create and initialize the rdl solver and attach it to the core.
 * - there must be no other solvers and no egraph
 * - also initialize the core
 */
static void create_rdl_solver(context_t *ctx) {
  rdl_solver_t *solver;
  smt_mode_t cmode;

  assert(ctx->egraph == NULL && ctx->arith_solver == NULL && ctx->bv_solver == NULL &&
	 ctx->fun_solver == NULL && ctx->core != NULL);

  cmode = core_mode[ctx->mode];
  solver = (rdl_solver_t *) safe_malloc(sizeof(rdl_solver_t));
  init_rdl_solver(solver, ctx->core, &ctx->gate_manager);
  init_smt_core(ctx->core, CTX_DEFAULT_CORE_SIZE, solver, rdl_ctrl_interface(solver),
		rdl_smt_interface(solver), cmode);
  rdl_solver_init_jmpbuf(solver, &ctx->env);
  ctx->arith_solver = solver;
  ctx->arith = rdl_arith_interface(solver);
}


/*
 * Create an initialize the simplex solver and attach it to the core
 * or to the egraph if the egraph exists.
 */
static void create_simplex_solver(context_t *ctx) {
  simplex_solver_t *solver;
  smt_mode_t cmode;

  assert(ctx->arith_solver == NULL && ctx->core != NULL);

  cmode = core_mode[ctx->mode];
  solver = (simplex_solver_t *) safe_malloc(sizeof(simplex_solver_t));
  init_simplex_solver(solver, ctx->core, &ctx->gate_manager, ctx->egraph, ctx->arith_manager);

  // set simplex options
  if (splx_eager_lemmas_enabled(ctx)) {
    simplex_enable_eager_lemmas(solver);
  }
  if (splx_periodic_icheck_enabled(ctx)) {
    simplex_enable_periodic_icheck(solver);
  }

  // row saving must be enabled unless we're in ONECHECK mode
  if (ctx->mode != CTX_MODE_ONECHECK) {
    simplex_enable_row_saving(solver);
  }

  if (ctx->egraph != NULL) {
    // attach the simplex solver as a satellite solver to the egraph
    egraph_attach_arithsolver(ctx->egraph, solver, simplex_ctrl_interface(solver),
			      simplex_smt_interface(solver), simplex_egraph_interface(solver),
			      simplex_arith_egraph_interface(solver));
  } else {
    // attach simplex to the core and initialize the core
    init_smt_core(ctx->core, CTX_DEFAULT_CORE_SIZE, solver, simplex_ctrl_interface(solver),
		  simplex_smt_interface(solver), cmode);
  }

  simplex_solver_init_jmpbuf(solver, &ctx->env);
  ctx->arith_solver = solver;
  ctx->arith = simplex_arith_interface(solver);
}




/*
 * Create IDL/SIMPLEX solver based on ctx->dl_profile
 */
static void create_auto_idl_solver(context_t *ctx) {
  dl_data_t *profile;
  int32_t sum_const;
  double atom_density;

  assert(ctx->dl_profile != NULL);
  profile = ctx->dl_profile;

  if (q_is_smallint(&profile->sum_const)) {
    sum_const = q_get_smallint(&profile->sum_const);
  } else {
    sum_const = INT32_MAX;
  }

  if (sum_const >= 1073741824) {
    // simplex rquired because of arithmetic overflow
    create_simplex_solver(ctx);
    ctx->arch = CTX_ARCH_SPLX;
  } else if (profile->num_vars >= 1000) {
    // too many variables for FW
    create_simplex_solver(ctx);
    ctx->arch = CTX_ARCH_SPLX;
  } else if (profile->num_vars <= 200 || profile->num_eqs == 0) {
    // use FW for now, until we've tested SIMPLEX more
    // 0 equalities usually means a scheduling problem
    // --flatten works better on IDL/FW
    create_idl_solver(ctx);
    ctx->arch = CTX_ARCH_IFW;
    enable_diseq_and_or_flattening(ctx);

  } else {

    // problem density
    if (profile->num_vars > 0) {
      atom_density = ((double) profile->num_atoms)/profile->num_vars;
    } else {
      atom_density = 0;
    }    

    if (atom_density >= 10.0) {
      // high density: use FW
      create_idl_solver(ctx);
      ctx->arch = CTX_ARCH_IFW;
      enable_diseq_and_or_flattening(ctx);

    } else {
      create_simplex_solver(ctx);
      ctx->arch = CTX_ARCH_SPLX;
    }

  }
  
}


/*
 * Create RDL/SIMPLEX solver based on ctx->dl_profile
 */
static void create_auto_rdl_solver(context_t *ctx) {
  dl_data_t *profile;
  double atom_density;

  assert(ctx->dl_profile != NULL);
  profile = ctx->dl_profile;

  if (profile->num_vars >= 1000) {
    create_simplex_solver(ctx);
    ctx->arch = CTX_ARCH_SPLX;
  } else if (profile->num_vars <= 200 || profile->num_eqs == 0) {
    create_rdl_solver(ctx); 
    ctx->arch = CTX_ARCH_RFW;
  } else {
    // problem density
    if (profile->num_vars > 0) {
      atom_density = ((double) profile->num_atoms)/profile->num_vars;
    } else {
      atom_density = 0;
    }    

    if (atom_density >= 7.0) {
      // high density: use FW
      create_rdl_solver(ctx);
      ctx->arch = CTX_ARCH_RFW;
    } else {
      // low-density: use SIMPLEX
      create_simplex_solver(ctx);
      ctx->arch = CTX_ARCH_SPLX;
    }
  }
}




/*
 * Create the array/function theory solver and attach it to the egraph
 */
static void create_fun_solver(context_t *ctx) {
  fun_solver_t *solver;

  assert(ctx->egraph != NULL && ctx->fun_solver == NULL);

  solver = (fun_solver_t *) safe_malloc(sizeof(fun_solver_t));
  init_fun_solver(solver, ctx->core, &ctx->gate_manager, ctx->egraph, ctx->types);
  egraph_attach_funsolver(ctx->egraph, solver, fun_solver_ctrl_interface(solver),
			  fun_solver_egraph_interface(solver),
			  fun_solver_fun_egraph_interface(solver));

  ctx->fun_solver = solver;
  ctx->fun = fun_solver_funsolver_interface(solver);
}



/*
 * Create the bitvector solver
 * - attach it to the egraph if the egraph exists
 * - attach it to the core otherwise and activate the core
 */
static void create_bv_solver(context_t *ctx) {
  bv_solver_t *solver;
  smt_mode_t cmode;

  assert(ctx->bv_solver == NULL && ctx->core != NULL);

  cmode = core_mode[ctx->mode];
  solver = (bv_solver_t *) safe_malloc(sizeof(bv_solver_t));
  init_bv_solver(solver, ctx->core, ctx->egraph, ctx->bv_manager, ctx->nodes);
  
  if (ctx->egraph != NULL) {
    // make solver a satellite of the egraph
    egraph_attach_bvsolver(ctx->egraph, solver, 
			   bv_solver_ctrl_interface(solver),
			   bv_solver_smt_interface(solver), 
			   bv_solver_egraph_interface(solver),
			   bv_solver_bv_egraph_interface(solver));
  } else {
    // attach solver directly to the core and initialize the core
    init_smt_core(ctx->core, CTX_DEFAULT_CORE_SIZE, solver,
		  bv_solver_ctrl_interface(solver),
		  bv_solver_smt_interface(solver),
		  cmode);
  }

  bv_solver_init_jmpbuf(solver, &ctx->env);
  ctx->bv_solver = solver;
  ctx->bv = bv_solver_bv_interface(solver);
}


/*
 * Allocate and initialize solvers based on architecture and mode
 * - core and gate manager must exist at this point 
 * - if the architecture is either AUTO_IDL or AUTO_RDL, nothing is done yet,
 *   and the core is not initialized.
 * - otherwise, all components are ready and initialized, including the core.
 */
static void init_solvers(context_t *ctx) {
  uint8_t solvers;
  smt_core_t *core;
  smt_mode_t cmode;
  egraph_t *egraph;

  solvers = arch_components[ctx->arch];

  ctx->egraph = NULL;
  ctx->arith_solver = NULL;
  ctx->bv_solver = NULL;
  ctx->fun_solver = NULL;

  ctx->arith = NULL;
  ctx->bv = NULL;
  ctx->fun = NULL;

  // Create egraph first, then satellite solvers
  if (solvers & EGRPH) {
    create_egraph(ctx);
  }

  // Arithmetic solver
  if (solvers & SPLX) {
    create_simplex_solver(ctx);
  } else if (solvers & IFW) {
    create_idl_solver(ctx);
  } else if (solvers & RFW) {
    create_rdl_solver(ctx);
  }

  // Bitvector solver
  if (solvers & BVSLVR) {
    create_bv_solver(ctx);
  }

  // Function theory 
  if (solvers & FSLVR) {
    create_fun_solver(ctx);
  }


  /*
   * At this point all solvers are ready and initialized,
   * except the egraph and core if the egraph is present 
   * or the core if there are no solvers
   */
  cmode = core_mode[ctx->mode];   // initialization mode for the core
  egraph = ctx->egraph;
  core = ctx->core;
  if (egraph != NULL) {
    init_smt_core(core, CTX_DEFAULT_CORE_SIZE, egraph, egraph_ctrl_interface(egraph), 
		  egraph_smt_interface(egraph), cmode);
    egraph_attach_core(egraph, core);

  } else if (ctx->theories == 0) {
    /*
     * Boolean solver only
     */
    assert(ctx->arith_solver == NULL && ctx->bv_solver == NULL && ctx->fun_solver == NULL);
    init_smt_core(core, CTX_DEFAULT_CORE_SIZE, NULL, &null_ctrl, &null_smt, cmode);
  }
}





/*************
 *  CONTEXT  *
 ************/

/*
 * Check mode and architecture
 */
static inline bool valid_mode(context_mode_t mode) {
  return CTX_MODE_ONECHECK <= mode && mode <= CTX_MODE_INTERACTIVE;
}

static inline bool valid_arch(context_arch_t arch) {
  return CTX_ARCH_NOSOLVERS <= arch && arch <= CTX_ARCH_AUTO_RDL;
}



/*
 * Initialize ctx for the given mode and architecture
 * - qflag = true means quantifiers allowed
 * - qflag = false means no quantifiers
 */
void init_context(context_t *ctx, context_mode_t mode, context_arch_t arch, bool qflag) {  
  assert(valid_mode(mode) && valid_arch(arch));

  /*
   * Set architecture and options
   */
  ctx->base_level = 0;
  ctx->mode = mode;
  ctx->arch = arch;
  ctx->theories = arch2theories[arch];
  ctx->options = mode2options[mode];
  if (qflag) {
    // quantifiers require egraph
    assert((ctx->theories & UF_MASK) != 0);
    ctx->theories |= QUANT_MASK;
  }

  /*
   * Get global tables
   */
  ctx->types = __yices_globals.types;
  ctx->terms = __yices_globals.terms;
  ctx->arith_manager = __yices_globals.arith_manager;
  ctx->bv_manager = __yices_globals.bv_manager;
  ctx->bv_store = __yices_globals.bv_store;
  ctx->nodes = __yices_globals.nodes;
  

  /*
   * The core is always needed: allocate it here
   * It's not initialized yet.
   */
  ctx->core = (smt_core_t *) safe_malloc(sizeof(smt_core_t));

  /*
   * Translator + gate manager
   */
  init_translator(&ctx->trans, 0, 0, 0); // all default sizes
  init_gate_manager(&ctx->gate_manager, ctx->core);

  /*
   * Simplification data structures
   */
  init_partition(&ctx->partition, 0, ctx->types);
  init_int_hmap(&ctx->pseudo_subst, 0);
  init_ivector(&ctx->subst_eqs, CTX_DEFAULT_VECTOR_SIZE);

  /*
   * Vectors of top-level formulas, atoms, and equalities (after flattening)
   */
  init_ivector(&ctx->top_eqs, CTX_DEFAULT_VECTOR_SIZE);
  init_ivector(&ctx->top_atoms, CTX_DEFAULT_VECTOR_SIZE);
  init_ivector(&ctx->top_formulas, CTX_DEFAULT_VECTOR_SIZE);

  /*
   * All buffers and auxiliary data structures
   */
  init_tree_stack(&ctx->stack, 0);
  init_istack(&ctx->istack);
  init_ivector(&ctx->aux_vector, CTX_DEFAULT_VECTOR_SIZE);

  /*
   * The caches and internal monarray buffer
   * are allocated on demand.
   */
  ctx->monarray = NULL;
  ctx->monarray_size = 0;
  ctx->cache = NULL;
  ctx->small_cache = NULL;
  ctx->bvbuffer = NULL;
  ctx->bvbuffer2 = NULL;

  /*
   * DL profile: allocated later if needed
   */
  ctx->dl_profile = NULL;

  /*
   * Buffers for model construction
   */
  q_init(&ctx->aux);
  init_bvconstant(&ctx->bv_buffer);

  /*
   * Allocate solvers and initialize them and the core
   * (except if the architecture is AUTO_IDL or AUTO_RDL)
   */
  init_solvers(ctx);
}




/*
 * Delete the arithmetic solver
 */
static void delete_arith_solver(context_t *ctx) {
  uint8_t solvers;

  assert(ctx->arith_solver != NULL);

  solvers = arch_components[ctx->arch];
  if (solvers & IFW) {
    delete_idl_solver(ctx->arith_solver);    
  } else if (solvers & RFW) {
    delete_rdl_solver(ctx->arith_solver);
  } else if (solvers & SPLX) {
    delete_simplex_solver(ctx->arith_solver);
  }
  safe_free(ctx->arith_solver);
  ctx->arith_solver = NULL;
}


/*
 * Delete ctx
 */
void delete_context(context_t *ctx) {
  if (ctx->core != NULL) {
    if (ctx->arch != CTX_ARCH_AUTO_IDL && ctx->arch != CTX_ARCH_AUTO_RDL) {
      delete_smt_core(ctx->core);
    }
    safe_free(ctx->core);
    ctx->core = NULL;
  }

  if (ctx->egraph != NULL) {
    delete_egraph(ctx->egraph);
    safe_free(ctx->egraph);
    ctx->egraph = NULL;
  }

  if (ctx->arith_solver != NULL) {
    delete_arith_solver(ctx);
    ctx->arith_solver = NULL;
  }

  if (ctx->fun_solver != NULL) {
    delete_fun_solver(ctx->fun_solver);
    safe_free(ctx->fun_solver);
    ctx->fun_solver = NULL;
  }

  if (ctx->bv_solver != NULL) {
    delete_bv_solver(ctx->bv_solver);
    safe_free(ctx->bv_solver);
    ctx->bv_solver = NULL;
  }

  delete_translator(&ctx->trans);
  delete_gate_manager(&ctx->gate_manager);

  delete_partition(&ctx->partition);
  delete_int_hmap(&ctx->pseudo_subst);
  delete_ivector(&ctx->subst_eqs);

  delete_ivector(&ctx->top_eqs);
  delete_ivector(&ctx->top_atoms);
  delete_ivector(&ctx->top_formulas);

  delete_tree_stack(&ctx->stack);
  delete_istack(&ctx->istack);
  delete_ivector(&ctx->aux_vector);

  if (ctx->cache != NULL) {
    delete_int_bvset(ctx->cache);
    safe_free(ctx->cache);
    ctx->cache = NULL;
  }

  if (ctx->small_cache != NULL) {
    delete_int_hset(ctx->small_cache);
    safe_free(ctx->small_cache);
    ctx->small_cache = NULL;
  }

  if (ctx->monarray != NULL) {    
    clear_monarray(ctx->monarray, ctx->monarray_size); // maybe redundant, but safe
    safe_free(ctx->monarray);
    ctx->monarray = NULL;
  }

  if (ctx->bvbuffer != NULL) {
    delete_bvarith_buffer(ctx->bvbuffer);
    safe_free(ctx->bvbuffer);
    ctx->bvbuffer = NULL;
  }

  if (ctx->bvbuffer2 != NULL) {
    delete_bvarith_buffer(ctx->bvbuffer2);
    safe_free(ctx->bvbuffer2);
    ctx->bvbuffer2 = NULL;
  }

  if (ctx->dl_profile != NULL) {
    q_clear(&ctx->dl_profile->sum_const);
    safe_free(ctx->dl_profile);
    ctx->dl_profile = NULL;
  }

  q_clear(&ctx->aux);
  delete_bvconstant(&ctx->bv_buffer);
}




/*
 * Reset: remove all assertions and clear all internalization tables
 */
void reset_context(context_t *ctx) {
  ctx->base_level = 0;
  reset_smt_core(ctx->core); // this propagates reset to all solvers
  reset_translator(&ctx->trans);
  reset_gate_manager(&ctx->gate_manager);
  reset_partition(&ctx->partition);
  int_hmap_reset(&ctx->pseudo_subst);
  ivector_reset(&ctx->subst_eqs);
  ivector_reset(&ctx->top_eqs);
  ivector_reset(&ctx->top_atoms);
  ivector_reset(&ctx->top_formulas);
  ivector_reset(&ctx->aux_vector);

  if (ctx->dl_profile != NULL) {
    q_clear(&ctx->dl_profile->sum_const);
    safe_free(ctx->dl_profile);
    ctx->dl_profile = NULL;
  }

  q_clear(&ctx->aux);  
}


/*
 * Push and pop
 */
void context_push(context_t *ctx) {
  assert(context_supports_pushpop(ctx));
  smt_push(ctx->core);  // propagates to all solvers
  translator_push(&ctx->trans);
  gate_manager_push(&ctx->gate_manager);
  ctx->base_level ++;
}

void context_pop(context_t *ctx) {
  assert(ctx->base_level > 0);
  smt_pop(ctx->core);   // propagates to all solvers
  translator_pop(&ctx->trans);
  gate_manager_pop(&ctx->gate_manager);
  ctx->base_level --;
  if (context_has_simplex_solver(ctx)) {
    simplex_reset_tableau(ctx->arith_solver);
  }
}



/*
 * Interrupt the search
 */
void context_stop_search(context_t *ctx) {
  stop_search(ctx->core);
}



/*
 * Cleanup: restore ctx to a good state after check_context 
 * is interrupted.
 */
void context_cleanup(context_t *ctx) {
  // restore the state to IDLE, propagate to all solvers (via pop)
  assert(context_supports_cleaninterrupt(ctx));
  smt_cleanup(ctx->core);
  if (context_has_simplex_solver(ctx)) {
    simplex_reset_tableau(ctx->arith_solver);
  }
}



/*
 * Clear: prepare for more assertions and checks
 * - free the boolean assignment
 * - reset the status to IDLE
 */
void context_clear(context_t *ctx) {
  assert(context_supports_multichecks(ctx));
  smt_clear(ctx->core);
  if (context_has_simplex_solver(ctx)) {
    simplex_reset_tableau(ctx->arith_solver);
  }
}




/***************
 *  UTILITIES  *
 **************/

/*
 * There are two possible caches: 
 * - the 'cache' uses a bitvector representation and 
 *   should be better for operations that visit many terms.
 * - the 'small_cache' uses a hash table and should be better
 *   for operations that visit a small number of terms.
 */

/*
 * Allocate and initialize the internal cache if needed
 */
static int_bvset_t *context_get_cache(context_t *ctx) {
  int_bvset_t *tmp;

  tmp = ctx->cache;
  if (tmp == NULL) {
    tmp = (int_bvset_t *) safe_malloc(sizeof(int_bvset_t));
    init_int_bvset(tmp, 0);
    ctx->cache = tmp;
  }
  return tmp;
}


#if 0
/*
 * Empty the cache: NOT USED
 */
static void context_reset_cache(context_t *ctx) {
  int_bvset_t *tmp;

  tmp = ctx->cache;
  if (tmp != NULL) {
    reset_int_bvset(tmp);
  }
}
#endif

/*
 * Free the cache
 */
static void context_delete_cache(context_t *ctx) {
  int_bvset_t *tmp;

  tmp = ctx->cache;
  if (tmp != NULL) {
    delete_int_bvset(tmp);
    safe_free(tmp);
    ctx->cache = NULL;
  }
}


/*
 * Allocate and initialize the internal small_cache if needed
 */
static int_hset_t *context_get_small_cache(context_t *ctx) {
  int_hset_t *tmp;

  tmp = ctx->small_cache;
  if (tmp == NULL) {
    tmp = (int_hset_t *) safe_malloc(sizeof(int_hset_t));
    init_int_hset(tmp, 32);
    ctx->small_cache = tmp;
  }
  return tmp;
}


/*
 * Empty the small_cache
 */
static void context_reset_small_cache(context_t *ctx) {
  int_hset_t *tmp;

  tmp = ctx->small_cache;
  if (tmp != NULL) {
    int_hset_reset(tmp);
  }
}

#if 0
/*
 * Free the small_cache: NOT USED
 */
static void context_delete_small_cache(context_t *ctx) {
  int_hset_t *tmp;

  tmp = ctx->small_cache;
  if (tmp != NULL) {
    delete_int_hset(tmp);
    safe_free(tmp);
    ctx->small_cache = NULL;
  }
}
#endif


/*
 * Allocate a monomial array large enough for n monomials
 * - n must be smaller than MAX_POLY_SIZE.
 */
static monomial_t *context_get_monarray(context_t *ctx, uint32_t n) {
  monomial_t *tmp;

  assert(n < MAX_POLY_SIZE);

  tmp = ctx->monarray;
  if (tmp == NULL) {
    /*
     * make sure the array is large enough for at least 4 monomials 
     * since that's what most IDL or RDL polynomials require
     */
    if (n < 4) n = 4; 
    tmp = alloc_monarray(n);
    ctx->monarray = tmp;
    ctx->monarray_size = n;
  } else if (n > ctx->monarray_size) {
    tmp = realloc_monarray(tmp, ctx->monarray_size, n);
    ctx->monarray = tmp;
    ctx->monarray_size = n;
  }

  return tmp;
}


/*
 * Allocate and initialize the internal bvarith buffer
 * - n = buffer size in bits
 */
static bvarith_buffer_t *context_get_bvbuffer(context_t *ctx, uint32_t n) {
  bvarith_buffer_t *tmp;

  assert(n > 0);
  tmp = ctx->bvbuffer;
  if (tmp == NULL) {
    tmp = (bvarith_buffer_t *) safe_malloc(sizeof(bvarith_buffer_t));
    init_bvarith_buffer(tmp, ctx->bv_manager, ctx->bv_store);
    ctx->bvbuffer = tmp;
  }

  // initialize/reset to 0b0000...0 with n bits
  bvarith_buffer_prepare(tmp, n);
  return tmp;
}

/*
 * Allocate and initialize the secont bvarith buffer
 * - n = buffer size in bits
 */
static bvarith_buffer_t *context_get_bvbuffer2(context_t *ctx, uint32_t n) {
  bvarith_buffer_t *tmp;

  assert(n > 0);
  tmp = ctx->bvbuffer2;
  if (tmp == NULL) {
    tmp = (bvarith_buffer_t *) safe_malloc(sizeof(bvarith_buffer_t));
    init_bvarith_buffer(tmp, ctx->bv_manager, ctx->bv_store);
    ctx->bvbuffer2 = tmp;
  }

  // initialize/reset to 0b0000...0 with n bits
  bvarith_buffer_prepare(tmp, n);
  return tmp;
}


/*
 * Get the internal code for term t
 */
icode_t get_internal_code(context_t *ctx, term_t t) {
  assert(t >= 0);
  return code_of_term(&ctx->trans, t);
}


/*
 * Number of equalities in the union-find table
 */
uint32_t num_eliminated_eqs(context_t *ctx) {
  return partition_num_eqs(&ctx->partition);
}


/*
 * Number of substitutions in ctx->pseudo_subst
 * - count all substitutions [x := t] with t != NULL_TERM
 */
uint32_t num_substitutions(context_t *ctx) {
  int_hmap_pair_t *p;
  uint32_t n;

  n = 0;
  p = int_hmap_first_record(&ctx->pseudo_subst);
  while (p != NULL) {
    if (p->val != NULL_TERM) {
      n ++;
    }
    p = int_hmap_next_record(&ctx->pseudo_subst, p);
  }
  return n;
}





/*****************************
 *  FORMULA SIMPLIFICATION   *
 ****************************/

/*
 * We attempt to eliminate uninterpreted terms that occur in top-level equalities,
 * by adding them to the partition/substitution table.
 *
 * There are two cases:
 * - equality (X == Y) where both sides are uninterpreted
 * - equality (X == t) where X is uninterpreted and t is not
 *
 * In a first pass, we build equivalence classes: each class contains variables
 * that are all equal, and optionally a unique constant term. If the class contains
 * a constant, then that constant is the root of the class, otherwise the root is 
 * one of the class's elements.
 * 
 * The first pass deals only with the simple cases:
 * - equalities between variables (X == Y): the classes of X and Y are
 *   merged if possible.
 * - equalities between variable and constant (X == t): t is made the root
 *   of the class of X if that's possible.
 *
 * In a second pass, all equalities of the form (X == t) are visited again
 * - let V be the root of X's class in the partition table.
 * - if V is a variable we add a candidate substitution mapping (V |--> t)
 *   to the map ctx->pseudo_subst
 *
 * In the third pass, we remove cycles introduced by the pseudo_subst mapping
 */ 

/*
 * Get root of t's class: create a new class if needed
 */
static term_t get_term_root(context_t *ctx, term_t t) {
  term_t r;

  assert(term_kind(ctx->terms, t) == UNINTERPRETED_TERM);
  r = partition_find(&ctx->partition, t);
  if (r < 0) {
    partition_add_term(&ctx->partition, t, term_type(ctx->terms, t));
    r = t;
  }
  return r;
}

/*
 * Find t's root in the partition:
 * - if t is not present, return t (don't create anything)
 * - otherwise return the root of t's class 
 */
static term_t find_term_root(context_t *ctx, term_t t) {
  term_t r;
  assert(good_term(ctx->terms, t));
  r = partition_find(&ctx->partition, t);
  if (r < 0) r = t;
  return r;
}


/*
 * Find t's type in the partition
 * - if t is in the partition, return the type of its class
 * - otherwise return t's type in the term table
 */
static type_t find_root_type(context_t *ctx, term_t t) {
  term_t r;
  type_t tau;
  assert(good_term(ctx->terms, t));
  r = partition_find(&ctx->partition, t);
  if (r < 0) {
    tau = term_type(ctx->terms, t);
  } else {
    tau = partition_class_type(&ctx->partition, r);
  }
  return tau;
}

/*
 * Check whether t's root type is integer
 */
static bool root_type_is_integer(context_t *ctx, term_t t) {
  return is_integer_type(find_root_type(ctx, t));
}


/*
 * Make t a new root if it's not already one.
 */
static void make_term_root(context_t *ctx, term_t t) {
  if (term_is_root(&ctx->partition, t)) {
    assert(root_is_frozen(&ctx->partition, t));
    return;
  }
  assert(partition_find(&ctx->partition, t) == NULL_TERM);
  partition_add_root(&ctx->partition, t, term_type(ctx->terms, t));
}



/*
 * Check whether t is a variable (uninterpreted term) that has not
 * been internalized yet.
 */
static inline bool is_unassigned_var(context_t *ctx, term_t t) {
  return term_kind(ctx->terms, t) == UNINTERPRETED_TERM &&
    code_of_term(&ctx->trans, t) < 0;
}



/*
 * Check whether t is a constant term
 */
static bool is_constant_term(context_t *ctx, term_t t) {
  term_kind_t kind;

  kind = term_kind(ctx->terms, t);
  return kind == CONSTANT_TERM || kind == BV_CONST_TERM || 
    (kind == ARITH_TERM && polynomial_is_constant(arith_term_desc(ctx->terms, t)));
}


/*
 * Check whether constant term t can be made root of class x
 * (i.e., check whether type of t is a subtype of the class type)
 * - x must be a (non-frozen) root term
 */
static bool compatible_subst_candidate(context_t *ctx, term_t t, term_t x) {
  return is_subtype(ctx->types, term_type(ctx->terms, t), partition_class_type(&ctx->partition, x));
}


/*
 * Get the substitution candidate mapped to t:
 * - t should be an unassigned variable
 * - return NULL_TERM if t is not mapped to anything
 */
static term_t subst_candidate(context_t *ctx, term_t t) {
  int_hmap_pair_t *p;

  p = int_hmap_find(&ctx->pseudo_subst, t);
  if (p == NULL) {
    return NULL_TERM;
  } else {
    assert(p->val >= 0 || p->val == NULL_TERM);
    return p->val;
  }
}


/*
 * Remove the substitution candidate mapped to t
 * - t must have a candidate substitute
 */
static void remove_subst_candidate(context_t *ctx, term_t t) {
  int_hmap_pair_t *p;

  p = int_hmap_find(&ctx->pseudo_subst, t);
  assert(p != NULL);
  p->val = NULL_TERM;
}



/*
 * FIRST PASS
 */

/*
 * Processing of equality e
 * - e is either (eq x y) or (bveq x y) or (aritheq x y)
 * - if e cannot be eliminated and can't be a substitution candidate,
 *   it's added to ctx->top_eqs.
 * - if e can't be eliminated but may be a substitution candidate, it's
 *   added to ctx->subst_eqs.
 */
static void process_toplevel_eq_main(context_t *ctx, term_t x, term_t y, term_t e) { 
  bool ux, uy;
  partition_t *p;

  p = &ctx->partition;
  ux = is_unassigned_var(ctx, x); // ux true means x may be eliminated
  uy = is_unassigned_var(ctx, y); // uy true means y may be eliminated
  
  if (ux && uy) {
    // equality (X == Y)
    x = get_term_root(ctx, x);
    y = get_term_root(ctx, y);
    if (x == y) return; // redundant equality
    if (mergeable_classes(p, x, y)) {
      partition_merge(p, x, y);
    } else {
      ivector_push(&ctx->top_eqs, e);
    }

  } else if (ux) {
    x = get_term_root(ctx, x);
    if (root_is_frozen(p, x)) {
      // X already mapped to a constant
      ivector_push(&ctx->top_eqs, e); 
    } else if (is_constant_term(ctx, y)) {
      if (compatible_subst_candidate(ctx, y, x)) {
	// (X == <constant>) is OK
	make_term_root(ctx, y);
	partition_merge(p, x, y);
      } else {
	// (X == <constant>) is probably false
	// just keep it in top_eqs for future processing
	ivector_push(&ctx->top_eqs, e);
      }
    } else {
      // (X == <term>) 
      ivector_push(&ctx->subst_eqs, e);
    }

  } else if (uy) {    
    y = get_term_root(ctx, y);
    if (root_is_frozen(p, y)) {
      ivector_push(&ctx->top_eqs, e);
    } else if (is_constant_term(ctx, x)) {
      if (compatible_subst_candidate(ctx, x, y)) {
	// (Y == <constant>) is OK
	make_term_root(ctx, x);
	partition_merge(p, x, y);
      } else {
	// keep (Y == constant) in top_eqs for future processing
	ivector_push(&ctx->top_eqs, e);
      }
    } else {
	// (Y == <term>): candidate substitution
      ivector_push(&ctx->subst_eqs, e);
    }
  } else {
    ivector_push(&ctx->top_eqs, e);
  }
}

/*
 * Process e  = (eq x y)
 */
static void process_toplevel_eq(context_t *ctx, term_t e) {
  eq_term_t *d;

  if (ctx->base_level == 0 && context_var_elim_enabled(ctx)) {
    d = eq_term_desc(ctx->terms, e);
    process_toplevel_eq_main(ctx, d->left, d->right, e);
  } else {
    ivector_push(&ctx->top_eqs, e);
  }
 }

/*
 * Process e = (bveq x y)
 */
static void process_toplevel_bveq(context_t *ctx, term_t e) {
  bv_atom_t *d;

  assert(term_kind(ctx->terms, e) == BV_EQ_ATOM);

  if (ctx->base_level == 0 && context_var_elim_enabled(ctx)) {
    d = bvatom_desc(ctx->terms, e);
    process_toplevel_eq_main(ctx, d->left, d->right, e);
  } else {
    ivector_push(&ctx->top_eqs, e);
  }
}


/*
 * Process arithmetic equality (= x y) 
 */
static void process_toplevel_aritheq(context_t *ctx, term_t e) {
  arith_bineq_t *d;

  assert(term_kind(ctx->terms, e) == ARITH_BINEQ_ATOM);
 
  if (ctx->base_level == 0 && context_var_elim_enabled(ctx)) {
    d = arith_bineq_desc(ctx->terms, e);
    process_toplevel_eq_main(ctx, d->left, d->right, e);
  } else {
    ivector_push(&ctx->top_eqs, e);
  }
}





/*
 * SECOND PASS
 */

/*
 * Check whether e is a possible substitution 
 * - e is either (eq x y) or (bveq x y) or (aritheq x y)
 * - one of x or y is a variable, the other is a non-constant term
 * - if e can be turned into a candidate mapping [X --> y] (or [Y --> x]) 
 *   then this mapping is added to the pseudo_subst map
 * Return code:
 * - true means a substitution candidate was created for that equaltiy
 * - false means no candidate created.
 */
static bool process_subst_eq_main(context_t *ctx, term_t x, term_t y) {
  int_hmap_pair_t *mp;
  term_t aux;

  if (is_unassigned_var(ctx, y)) {
    aux = x; x = y; y = aux;
  }

  if (is_unassigned_var(ctx, x)) {
    x = get_term_root(ctx, x);
    if (! root_is_frozen(&ctx->partition, x) && compatible_subst_candidate(ctx, y, x)) {
      mp = int_hmap_get(&ctx->pseudo_subst, x);
      if (mp->val < 0) { // add [X --> y]
	mp->val = y;
	return true;
      }
    }
  }

  return false;
}



/*
 * Second pass: check the substitution candidates stored in ctx->subst_eqs
 * - any equality for which a candidate subst was added is kept in ctx->subs_eqs
 * - all other equalities from subst_eqs are copied into ctx->top_eqs
 */
static void process_subst_eqs(context_t *ctx) {
  term_table_t *terms;
  uint32_t i, j, n;
  term_t e, x, y;
  term_t *a;


#if TRACE
  printf("\n=== PHASE 1 ===\n");
  printf("  %"PRIu32" eliminated eqs\n", partition_num_eqs(&ctx->partition));
  printf("  %"PRIu32" subst candidates\n", ctx->subst_eqs.size);
  printf("  %"PRIu32" eqs\n", ctx->top_eqs.size);
  printf("  %"PRIu32" atoms\n", ctx->top_atoms.size);
  printf("  %"PRIu32" formulas\n", ctx->top_formulas.size);
#endif

  terms = ctx->terms;
  n = ctx->subst_eqs.size;
  a = ctx->subst_eqs.data;
  j = 0;
  for (i=0; i<n; i++) {
    e = a[i];
    switch (term_kind(terms, e)) {
    case EQ_TERM:
      x = eq_term_left(terms, e);
      y = eq_term_right(terms, e);
      break;
    case BV_EQ_ATOM:
      x = bvatom_lhs(terms, e);
      y = bvatom_rhs(terms, e);
      break;
    case ARITH_BINEQ_ATOM:
      x = arith_bineq_left(terms, e);
      y = arith_bineq_right(terms, e);
      break;
    default:
      assert(false);
      continue;  // prevents GCC warning
    }

    if (process_subst_eq_main(ctx, x, y)) {
      // keep e into ctx->subseq_eq
      a[j] = e;
      j ++;
    } else {
      // move it to top_eqs
      ivector_push(&ctx->top_eqs, e);
    }
  }

  ivector_shrink(&ctx->subst_eqs, j);

#if TRACE
  printf("\n=== Phase 2 ===\n");
  printf("  %"PRIu32" eliminated eqs\n", partition_num_eqs(&ctx->partition));
  printf("  %"PRIu32" subst candidates\n", ctx->subst_eqs.size);
  printf("  %"PRIu32" eqs\n", ctx->top_eqs.size);
  printf("  %"PRIu32" atoms\n", ctx->top_atoms.size);
  printf("  %"PRIu32" formulas\n", ctx->top_formulas.size);
#endif
  
}



/*
 * THIRD PASS: REMOVE CYCLES
 */

/*
 * We use a depth-first search in the dependency graph:
 * - vertices are terms,
 * - edges are of two forms: 
 *    t --> u if u is a child subterm of t
 *    x := t  if x is a variable and t is the substitution candidate for x
 *
 * By construction, the graph restricted to edges t --> u (without the 
 * substitution edges) is a DAG. So we can remove cycles by removing some 
 * substitution edges x := t.
 */

/*
 * Visit t: return true if t is on a cycle.
 */
static bool visit(context_t *ctx, term_t t);

static bool visit_array(context_t *ctx, uint32_t n, term_t *a) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (visit(ctx, a[i])) {
      return true;
    }
  }
  return false;
}

static inline bool visit_ite(context_t *ctx, ite_term_t *ite) {
  return visit(ctx, ite->cond) || visit(ctx, ite->then_arg) || visit(ctx, ite->else_arg);
}

static inline bool visit_eq(context_t *ctx, eq_term_t *eq) {
  return visit(ctx, eq->left) || visit(ctx, eq->right);
}

static inline bool visit_app(context_t *ctx, app_term_t *app) {
  return visit(ctx, app->fun) || visit_array(ctx, app->nargs, app->arg);
}

static inline bool visit_or(context_t *ctx, or_term_t *or) {
  return visit_array(ctx, or->nargs, or->arg);
}

static inline bool visit_tuple(context_t *ctx, tuple_term_t *tuple) {
  return visit_array(ctx, tuple->nargs, tuple->arg);
}

static inline bool visit_update(context_t *ctx, update_term_t *update) {
  return visit(ctx, update->fun) || visit(ctx, update->newval) || visit_array(ctx, update->nargs, update->arg);
}

static inline bool visit_distinct(context_t *ctx, distinct_term_t *distinct) {
  return visit_array(ctx, distinct->nargs, distinct->arg);
}

static bool visit_arith(context_t *ctx, polynomial_t *p) {
  uint32_t i, n;
  ivector_t *v;
  term_t *a;
  bool result;

  v = &ctx->aux_vector;
  assert(v->size == 0);
  polynomial_get_terms(p, ctx->arith_manager, v);

  n = v->size;
  a = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    a[i] = v->data[i];
  }
  ivector_reset(v);

  result = visit_array(ctx, n, a);
  free_istack_array(&ctx->istack, a);

  return result;
}

static inline bool visit_arith_bineq(context_t *ctx, arith_bineq_t *eq) {
  return visit(ctx, eq->left) || visit(ctx, eq->right);
}

static bool visit_bvlogic(context_t *ctx, bvlogic_expr_t *e) {
  uint32_t i, n;
  ivector_t *v;
  term_t *a;
  bool result;

  v = &ctx->aux_vector;
  assert(v->size == 0);
  bvlogic_expr_get_terms(e, ctx->bv_manager->bm, ctx->bv_manager, v);

  n = v->size;
  a = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    a[i] = v->data[i];
  }
  ivector_reset(v);

  result = visit_array(ctx, n, a);
  free_istack_array(&ctx->istack, a);

  return result;
}

static bool visit_bvarith(context_t *ctx, bvarith_expr_t *e) {
  uint32_t i, n;
  ivector_t *v;
  term_t *a;
  bool result;

  v = &ctx->aux_vector;
  assert(v->size == 0);
  bvarith_expr_get_terms(e, ctx->bv_manager, v);

  n = v->size;
  a = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    a[i] = v->data[i];
  }
  ivector_reset(v);

  result = visit_array(ctx, n, a);
  free_istack_array(&ctx->istack, a);

  return result;
}

static inline bool visit_bvatom(context_t *ctx, bv_atom_t *a) {
  return visit(ctx, a->left) || visit(ctx, a->right);
}

static inline bool visit_bvapply(context_t *ctx, bvapply_term_t *a) {
  return visit(ctx, a->arg0) || visit(ctx, a->arg1);
}

static bool visit(context_t *ctx, term_t t) {
  icode_t x;
  term_t r;
  term_table_t *terms;
  bool result;


  x = code_of_term(&ctx->trans, t);
  assert(x == white || x == black || x == grey || x == bool2code(true) || x == bool2code(false));

  if (x == white) {
    /*
     * t has not been visited yet
     */
    terms = ctx->terms;
    mark_term_grey(&ctx->trans, t);

    switch (term_kind(terms, t)) {
    case CONSTANT_TERM:
      result = false;
      break;

    case UNINTERPRETED_TERM:
      r = find_term_root(ctx, t);
      if (r != t) {
	result = visit(ctx, r);
      } else {
	// t is a root in the partition table
	r = subst_candidate(ctx, t);
	if (r != NULL_TERM && visit(ctx, r)) {
	  /*
	   * There's a cycle u --> ... --> t := r --> ... --> u
	   * remove the substitution t := r to break the cycle
	   */
	  remove_subst_candidate(ctx, t);
	}      
	result = false;
      }
      break;
      
    case VARIABLE:
      result = false;
      break;

    case NOT_TERM:
      result = visit(ctx, not_term_arg(terms, t));
      break;

    case ITE_TERM:
      result = visit_ite(ctx, ite_term_desc(terms, t));
      break;

    case EQ_TERM:
      result = visit_eq(ctx, eq_term_desc(terms, t));
      break;

    case APP_TERM:
      result = visit_app(ctx, app_term_desc(terms, t));
      break;

    case OR_TERM:
      result = visit_or(ctx, or_term_desc(terms, t));
      break;

    case TUPLE_TERM:
      result = visit_tuple(ctx, tuple_term_desc(terms, t));
      break;

    case SELECT_TERM:
      result = visit(ctx, select_term_arg(terms, t));
      break;

    case UPDATE_TERM:
      result = visit_update(ctx, update_term_desc(terms, t));
      break;

    case DISTINCT_TERM:
      result = visit_distinct(ctx, distinct_term_desc(terms, t));
      break;

    case FORALL_TERM:
      result = visit(ctx, forall_term_body(terms, t));
      break;

    case ARITH_TERM:
    case ARITH_EQ_ATOM:
    case ARITH_GE_ATOM:
      result = visit_arith(ctx, arith_desc(terms, t));
      break;

    case ARITH_BINEQ_ATOM:
      result = visit_arith_bineq(ctx, arith_bineq_desc(terms, t));
      break;

    case BV_LOGIC_TERM:
      result = visit_bvlogic(ctx, bvlogic_term_desc(terms, t));
      break;
      
    case BV_ARITH_TERM:
      result = visit_bvarith(ctx, bvarith_term_desc(terms, t));
      break;

    case BV_CONST_TERM:
      result = false;
      break;

    case BV_EQ_ATOM:
    case BV_GE_ATOM:
    case BV_SGE_ATOM:
      result = visit_bvatom(ctx, bvatom_desc(terms, t));
      break;

    case BV_APPLY_TERM:
      result = visit_bvapply(ctx, bvapply_term_desc(terms, t));
      break;

    default:
      assert(false);
      longjmp(ctx->env, INTERNAL_ERROR);	
    }

    if (result) {
      /*
       * t is on a cycle of grey terms:
       * v --> ... x := u --> .... --> t --> ... --> v
       * all terms on the grey path from from x to v 
       * must be cleared (except v).
       */
      clr_term_color(&ctx->trans, t);
    } else {
      // there's no cycle containing t, we can mark t black
      mark_term_black(&ctx->trans, t);
    }

  } else {
    /*
     * t already visited before or already assigned (either true or false)
     * - if t is black there's no cycle via t
     * - if t is true or false there's no cycle either
     * - if t is grey we've just detected a cycle
     */
    result = (x == grey);
  }
 
  return result;
}



/*
 * Check whether x := y or y := x is still a candidate substitution
 * - if so, explore x in a depth-first manner to detect cycles
 * - otherwise, move equality e to the top_eqs vector 
 *   (e is either the term (eq x y) or (bveq x y) or (aritheq x y))
 */
static void check_subst_cycle(context_t *ctx, term_t x, term_t y, term_t e) {
  int_hmap_pair_t *p;

  if (is_unassigned_var(ctx, y)) {
    x = y;
  }
  assert(is_unassigned_var(ctx, x));

  if (term_is_white(&ctx->trans, x)) {    
    visit(ctx, x);
  }

  /*
   * Check whether the candidate substitution for x's root 
   * has been removed.
   */
  x = find_term_root(ctx, x);
  p = int_hmap_find(&ctx->pseudo_subst, x);  // p->val := candidate subst
  assert(p != NULL);
  if (p->val == NULL_TERM) {
#if TRACE
    if (context_dump_enabled(ctx)) {
      // x := y is no longer a substitution. Move e to top_eqs
      printf("---> Substitution removed: ");
      print_termdef(stdout, e);
      printf("\n");
    }
#endif
    ivector_push(&ctx->top_eqs, e);

#if TRACE
  } else if (context_dump_enabled(ctx)) {
    printf("---> Substitution added: ");
    print_termdef(stdout, e);
    printf("\n");
#endif

  }
}


/*
 * Remove cycles in the candidate substitutions
 */
static void remove_subst_cycles(context_t *ctx) {
  term_table_t *terms;
  uint32_t i, n;
  term_t e, x, y;
  term_t *a;

#if TRACE
  printf("\n=== Phase 3===\n");
#endif
  
  terms = ctx->terms;
  n = ctx->subst_eqs.size;
  a = ctx->subst_eqs.data;
  for (i=0; i<n; i++) {
    e = a[i];
    switch (term_kind(terms, e)) {
    case EQ_TERM:
      x = eq_term_left(terms, e);
      y = eq_term_right(terms, e);
      break;
    case BV_EQ_ATOM:
      x = bvatom_lhs(terms, e);
      y = bvatom_rhs(terms, e);
      break;
    case ARITH_BINEQ_ATOM:
      x = arith_bineq_left(terms, e);
      y = arith_bineq_right(terms, e);
      break;
    default:
      assert(false);
      continue;
    }
    check_subst_cycle(ctx, x, y, e);
  }
}



/*
 * FLATTENING AND SIMPLIFICATION
 */

/*
 * Check whether (eq t1 t2) and (ite c t1 t2) are boolean iff or boolean ite
 * (i.e., whether t1 and t2 are boolens).
 */
static inline bool is_boolean_eq(term_table_t *tbl, eq_term_t *d) {
  return is_boolean_term(tbl, d->left);
}

static inline bool is_boolean_ite(term_table_t *tbl, ite_term_t *d) {
  return is_boolean_term(tbl, d->then_arg);
}


/*
 * Check whether it's consistent to assign boolean value (true or false) 
 * to the class of t.
 * - t must be present in the partition table and root of its class
 * - the assignment is fine if t is already assigned to value,
 *   or t is the boolean constant true or false equal to value,
 *   or t is not mapped to anything (in that case, t must be uninterpreted).
 *
 * NOTE: It's assumed that this function is called within flatten_assertions
 * so t cannot be mapped to a literal. It's either mapped to nothing or 
 * to true/false occ.
 */
static bool boolean_class_map_is_consistent(context_t *ctx, term_t t, bool value) {
  term_table_t *terms;
  int32_t x;

  terms = ctx->terms;
  assert(term_is_root(&ctx->partition, t) && 
	 partition_class_type(&ctx->partition, t) == bool_type(ctx->types));

  x = code_of_term(&ctx->trans, t);
  if (x >= 0) {
    // t is mapped to true or false
    assert(x == bool2code(true) || x == bool2code(false));
    return x == bool2code(value);
  } else if (term_kind(ctx->terms, t) == CONSTANT_TERM) {
    // t is either true_term or false_term   
    return (value && t == true_term(terms)) 
      || (!value && t == false_term(terms));
  } else { 
    assert(is_unassigned_var(ctx, t));
    return true;
  }
}

/*
 * Assign a boolean value (true or false) to the class of t
 * - t must be present in the partition table and root of its class
 * - no effect is t is true_term or false_term. Otherwise, assign
 *   true_occ or false_occ to t and freeze t in the partition.
 */
static void map_class_to_bool(context_t *ctx, term_t t, bool value) {
  partition_t *p;
  int32_t x;

  p = &ctx->partition;
  assert(boolean_class_map_is_consistent(ctx, t, value));
  if (! root_is_frozen(p, t)) {
    freeze_class(p, t);
    x = code_of_term(&ctx->trans, t);
    if (x < 0) {
      map_term_to_bool(&ctx->trans, t, value);
    }
  }
}



/*
 * Flattening: 
 * - given a formula f (or several formulas) on top of the stack
 * - collect all top-level subformulas of f, asserted true or false,
 *   and eliminate the top-level equalities.
 *
 * Simplifications: 
 * - A top level uninterpreted boolean term is eliminated. 
 *   We just record that this term is true or false by mapping it to true_occ or false_occ.
 *     Example: for A in (assert (and (not A) (= x y)), we just record A --> false.
 * - Equalities can be eliminated if they can be turned into substitutions.
 *   These equalities are encoded into the partition table that maps variables to
 *   constants or other variables.
 * - For some equalities of the form (<variable> == <term>), we can't tell yet 
 *   whether they can be eliminated (this depends on whether the left-hand-side
 *   variable occurs in the term). All these equalities are stored in vector 
 *   ctx->subst_eqs and a corresponding mapping <variable> |-> <term> is stored in 
 *   ctx->pseudo_subst.
 * 
 * The flattening result is in ctx->top_eqs, ctx->top_atom, and ctx->top_formulas.
 * - top_eqs contains all top-level equalities that are asserted true and 
 *   can't be eliminated. These equalities are either (eq t v) or (bveq t v) or 
 *   arithmetic equalites (t == v) or (aritheq p 0).
 *   After flattening, the top equalities are asserted first. This gives more opportunity
 *   for simplification to the theory solvers.
 * - top_atoms contains all atoms (asserted true or false) that are not equalities
 *   and all equalities asserted false.
 * - top_formulas contains the rest: either (OR t1 ... t_n) asserted true,
 *   or (ITE c t1 t2) asserted true or false.
 * 
 * For each term t in any of the vectors ctx->top_formulas, ctx->top_eqs, ctx->top_atoms,
 * and ctx->subst_eqs, the code ctx.trans->internal[t] is either true_occ or false_occ
 *
 * Returned code:
 * - a negative code if there's an error,
 * - 1 if the formula is unsat.
 * - 0 otherwise.
 */
static int32_t flatten_assertions(context_t *ctx, tree_stack_t *stack) {
  term_table_t *terms;
  ivector_t *formulas, *eqs, *atoms;
  bool polarity;
  tree_record_t *top;
  int32_t code, i, x;
  term_t t, r;
  or_term_t *d;
  ite_term_t *ite;

  terms = ctx->terms;
  formulas = &ctx->top_formulas;
  eqs = &ctx->top_eqs;
  atoms = &ctx->top_atoms;
  polarity = true;
  code = CTX_NO_ERROR;  

  ivector_reset(formulas);
  ivector_reset(eqs);
  ivector_reset(atoms);
  ivector_reset(&ctx->subst_eqs);

  while (tree_stack_nonempty(stack)) {
    top = tree_stack_top(stack);    
    /*
     * top record contains(term_id, type, kind, counter, descriptor)
     * children of t are in descriptor
     * counter is zero if term has just been pushed onto the stack
     */

    t = top->term;
    if (top->counter == 0) {
      x = code_of_term(&ctx->trans, t);
      if (x != nil) {
	// term t already visited
	if (x == bool2code(polarity)) {
	  tree_stack_pop(stack);
	  continue;
	} else {
	  // contradiction: t == true/ t == false
	  assert(x == bool2code(! polarity));
	  code = TRIVIALLY_UNSAT;
	  goto abort;
	}
      }
    }

    // explore t
    switch (top->kind) {
    case UNUSED_TERM:
      code = INTERNAL_ERROR;
      goto abort;
      
    case CONSTANT_TERM:
      assert(t == true_term(terms) || t == false_term(terms));
      if ((polarity && t == false_term(terms)) ||
	  (! polarity && t == true_term(terms))) {
	// inconsitency detected
	code = TRIVIALLY_UNSAT;
	goto abort;
      }
      map_term_to_bool(&ctx->trans, t, polarity);
      tree_stack_pop(stack);
      break;
      
    case UNINTERPRETED_TERM:
      // if t is in the partition table, map t's class to true or false
      r = partition_find(&ctx->partition, t);
      if (r >= 0) {
	// t is in the partition and r = root of t's class
	if (boolean_class_map_is_consistent(ctx, r, polarity)) {
	  map_class_to_bool(ctx, r, polarity);
	} else {
	  // inconsistency
	  code = TRIVIALLY_UNSAT;
	  goto abort;
	}
      } else {
	// t is not in the partition: make it true or false
	map_term_to_bool(&ctx->trans, t, polarity);
      }
      tree_stack_pop(stack);
      break;
      
    case VARIABLE:
      code = FREE_VARIABLE_IN_FORMULA;
      goto abort;

    case NOT_TERM:
      if (top->counter == 0) {
	// explore the child term after flipping polarity
	top->counter ++; 
	tree_stack_push_term(stack, terms, top->desc.integer);
	polarity = !polarity;
      } else {
	// restore polarity and return
	polarity = !polarity;
	map_term_to_bool(&ctx->trans, t, polarity);
	tree_stack_pop(stack);
      }
      break;

    case OR_TERM:
      if (top->counter == 0 && polarity) {
	// we can't flatten: store t as a top-level formula
	ivector_push(formulas, t);
	map_term_to_bool(&ctx->trans, t, true);
	tree_stack_pop(stack);	
      } else {
	// explore child i where i = top->counter
	assert(! polarity);
	d = (or_term_t *) top->desc.ptr;
	i = top->counter;
	if (i < d->nargs) {
	  top->counter ++;
	  tree_stack_push_term(stack, terms, d->arg[i]);
	} else {
	  // all children have been explored
	  map_term_to_bool(&ctx->trans, t, false);
	  tree_stack_pop(stack);
	}
      }
      break;

    case ITE_TERM:
      assert (is_boolean_ite(terms, top->desc.ptr));
      if (top->counter == 0) {
	// check if the condition is true or false
	ite = (ite_term_t *) top->desc.ptr;
	x = code_of_term(&ctx->trans, ite->cond);
	if (x != nil) {
	  // explore the then or else child
	  top->counter ++;
	  if (x == bool2code(true)) {
	    tree_stack_push_term(stack, terms, ite->then_arg);
	  } else {
	    assert(x == bool2code(false));
	    tree_stack_push_term(stack, terms, ite->else_arg);
	  }	
	} else {
	  // we can't flatten
	  ivector_push(formulas, t);
	  map_term_to_bool(&ctx->trans, t, polarity);
	  tree_stack_pop(stack);
	}
      } else {
	// then or else child has been explored
	map_term_to_bool(&ctx->trans, t, polarity);
	tree_stack_pop(stack);
      }
      break;

    case EQ_TERM:
      if (polarity) {
	process_toplevel_eq(ctx, t);
      } else {
	ivector_push(atoms, t);
      }
      map_term_to_bool(&ctx->trans, t, polarity);
      tree_stack_pop(stack);	
      break;

    case BV_EQ_ATOM:
      if (polarity) {
	process_toplevel_bveq(ctx, t);
      } else {
	ivector_push(atoms, t);
      }
      map_term_to_bool(&ctx->trans, t, polarity);
      tree_stack_pop(stack);	
      break;

    case ARITH_BINEQ_ATOM:
      if (polarity) {
	process_toplevel_aritheq(ctx, t);
      } else {
	ivector_push(atoms, t);
      }
      map_term_to_bool(&ctx->trans, t, polarity);
      tree_stack_pop(stack);
      break;

    case ARITH_EQ_ATOM: // p == 0
      if (polarity) {
	ivector_push(eqs, t);
      } else {
	ivector_push(atoms, t);
      }
      map_term_to_bool(&ctx->trans, t, polarity);
      tree_stack_pop(stack);
      break;

    case APP_TERM:
    case SELECT_TERM:
    case DISTINCT_TERM:
    case FORALL_TERM:
    case ARITH_GE_ATOM:
    case BV_GE_ATOM:
    case BV_SGE_ATOM:      
      // store t as a top-level atom
      ivector_push(atoms, t);
      map_term_to_bool(&ctx->trans, t, polarity);
      tree_stack_pop(stack);
      break;
      
    case TUPLE_TERM:
    case UPDATE_TERM:
    case ARITH_TERM:
    case BV_LOGIC_TERM:
    case BV_ARITH_TERM:
    case BV_CONST_TERM:
    case BV_APPLY_TERM:
      code = TYPE_ERROR;
      goto abort;

    default:
      code = INTERNAL_ERROR;
      goto abort;
    }
  }

  // second and third phases of equality processing
  process_subst_eqs(ctx);
  remove_subst_cycles(ctx);

  return code;

 abort:
  tree_stack_reset(stack);
  return code;
}







/**********************************
 *   ARITHMETIC SIMPLIFICATIONS   *
 *********************************/

/*
 * Check whether term t can be eliminated by an arithmetic substitution
 * - t must be uninterpreted and not internalized yet
 * - t's root in the variable partition must be an uninterpreted term x
 * - x must not be internalized yet
 * - there must not be a substitution x := <term> in the substitution table.
 */
static bool is_elimination_candidate(context_t *ctx, term_t t) {
  term_t r;

  r = find_term_root(ctx, t);
  return is_unassigned_var(ctx, r) && subst_candidate(ctx, r) == NULL_TERM; 
}

/*
 * Auxiliary function: check whether p/a is an integral polynomial
 * assuming all variables are integer.
 * - check whether all coefficients are multiple of a
 * - a must be non-zero
 */
static bool integralpoly_after_div(polynomial_t *p, rational_t *a) {
  uint32_t i, n;

  if (q_is_one(a) || q_is_minus_one(a)) {
    return true;
  }

  n = p->nterms;
  for (i=0; i<n; i++) {
    if (! q_divides(a, &p->mono[i].coeff)) return false;
  }
  return true;
}


/*
 * Build polynomial - p/a + x in the context's monarray where a = coefficient of x in p
 */
static void build_poly_substitution(context_t *ctx, polynomial_t *p, arith_var_t x) {
  monomial_t *q;
  uint32_t i, n;
  arith_var_t y;
  rational_t *a;

  n = p->nterms;

  // first get coefficient of x in p
  a = NULL; // otherwise GCC complains
  for (i=0; i<n; i++) {
    y = p->mono[i].var;
    if (y == x) {
      a = &p->mono[i].coeff;
    }
  }
  assert(a != NULL);

  q = context_get_monarray(ctx, n);

  // compute - p/a (but skip monomial a.x)
  for (i=0; i<n; i++) {
    y = p->mono[i].var;
    if (y != x) {
      q->var = y;
      q_set_neg(&q->coeff, &p->mono[i].coeff);
      q_div(&q->coeff, a);
      q ++;
    }
  }

  // end marker
  q->var = max_idx;
}


/*
 * Check whether a top-level assertion (p == 0) can be
 * rewritten (t == q) where t is not internalized yet.
 * - p = input polynomial
 * - return t or null_term if no adequate t is found
 */
static term_t try_poly_substitution(context_t *ctx, polynomial_t *p) {
  bool all_int;
  uint32_t i, n;
  arith_var_t x;
  term_t t;

  all_int = polynomial_is_int(p, ctx->arith_manager);
  n = p->nterms;
  for (i=0; i<n; i++) {
    x = p->mono[i].var;
    if (arithvar_manager_var_is_primitive(ctx->arith_manager, x)) {
      assert(x != const_idx);
      t = arithvar_manager_term_of_var(ctx->arith_manager, x);
      if (is_elimination_candidate(ctx, t)) {
	if (is_real_term(ctx->terms, t) || 
	    (all_int && integralpoly_after_div(p, &p->mono[i].coeff))) {
	  // t is candidate for elimination
	  return t;
	}
      }
    }
  }
  return NULL_TERM;
}








/********************************************
 *  ANALYSIS FOR DIFFERENCE LOGIC FRAGMENT  *
 *******************************************/


/*
 * Allocate and initialize the profile record
 */
static void init_dlstats(context_t *ctx) {
  dl_data_t *stats;

  stats = (dl_data_t *) safe_malloc(sizeof(dl_data_t));
  q_init(&stats->sum_const);
  stats->num_vars = 0;
  stats->num_atoms = 0;
  stats->num_eqs = 0;

  ctx->dl_profile = stats;
}




/*
 * Check whether x is a primitive variable of that its type matches idl
 * - if idl is true, x must be an integer variable
 * - if idl is false, x must be a real variable
 *
 * NOTE: we could check whether the term t attached to x is uninterpreted,
 * but that will be detected in later phaes of internalization anyway.
 */
static inline bool good_dlvar(context_t *ctx, arith_var_t x, bool idl) {
  return arithvar_manager_var_is_primitive(ctx->arith_manager, x) &&
    (arithvar_manager_var_is_int(ctx->arith_manager, x) == idl);
}

/*
 * If x has not been seen before, increment num_vars
 */
static void count_dlvar(context_t *ctx, arith_var_t x, dl_data_t *stats) {
  term_t t;

  // t := term attached to x
  t = arithvar_manager_term_of_var(ctx->arith_manager, x);
  if (int_bvset_add(ctx->cache, t)) {
    // t not seen before
    stats->num_vars ++;
  }
}


/*
 * Add the absolute value of a to sum_const
 */
static void add_abs_dlconst(rational_t *a, dl_data_t *stats) {
  if (q_is_pos(a)) {
    q_add(&stats->sum_const, a);
  } else {
    q_sub(&stats->sum_const, a);
  }
}

/*
 * Process polynomial p:
 * - check if it's a difference logic polynomial (i.e., A + X - Y)
 * - if idl is true, check that the variables X and Y are integers 
 *   otherwise check that they are reals
 * If all tests pass, update stats and return true, otherwise return false.
 * - the cache must be initialized and contain all the terms already visited
 */
static bool check_diff_logic_poly(context_t *ctx, dl_data_t *stats, polynomial_t *p, bool idl) {
  uint32_t n;
  rational_t *a;
  arith_var_t x, y;
  monomial_t *q;

  n = p->nterms;
  if (n == 0 || n > 3) return false;

  // a points to the constant or a is NULL if the constant is zero
  q = p->mono;
  a = NULL;
  if (q[0].var == const_idx) {
    a = &q[0].coeff;
    q ++;
    n --;
  }

  // check for one or two variables with coefficient +/-1
  if (n == 1 && (q_is_one(&q[0].coeff) || q_is_minus_one(&q[0].coeff))) {
    x = q[0].var;
    y = null_thvar;
  } else if (n == 2 && ((q_is_one(&q[0].coeff) && q_is_minus_one(&q[1].coeff)) ||
			(q_is_minus_one(&q[0].coeff) && q_is_one(&q[1].coeff)))) {
    x = q[0].var;
    y = q[1].var;
  } else {
    return false;
  }

  if (! good_dlvar(ctx, x, idl)) return false;
  if (y != null_thvar && ! good_dlvar(ctx, y, idl)) return false;
  
  // update stats
  count_dlvar(ctx, x, stats);
  if (y != null_thvar) count_dlvar(ctx, y, stats);
  if (a != NULL) add_abs_dlconst(a, stats);
  stats->num_atoms ++;

  return true;
}


/*
 * Process arithmetic equality (t == u)
 * - check that the two terms are uninterpreted 
 * - if idl is true check that they are both integer,
 *   otherwise check that they are both real.
 * - update statistics
 */
static bool check_diff_logic_eq(context_t *ctx, dl_data_t *stats, arith_bineq_t *eq, bool idl) {
  arith_var_t x, y;

  x = term_theory_var(ctx->terms, eq->left);
  y = term_theory_var(ctx->terms, eq->right);
  if (! good_dlvar(ctx, x, idl) || ! good_dlvar(ctx, y, idl)) {
    return false;
  }
  count_dlvar(ctx, x, stats);
  count_dlvar(ctx, y, stats);
  stats->num_atoms ++;

  return true;
}


/*
 * Analyze all arithmetic atoms in term t and fill in stats
 * - if idl is true, this checks for integer difference logic
 *   otherwise, checks for real difference logic
 * - cache must be initialized and contain all terms already visited
 */
static void analyze_dl(context_t *ctx, dl_data_t *stats, term_t t, bool idl) {
  term_table_t *terms;
  eq_term_t *eq;
  ite_term_t *ite;
  or_term_t *or;
  uint32_t i, n;
  term_t r;

  assert(is_boolean_term(ctx->terms, t));

 loop: // Hack to cut tail recursion
  if (int_bvset_add(ctx->cache, t)) {
    /*
     * t not visited yet
     */
    terms = ctx->terms;
    switch (term_kind(terms, t)) {
    case UNINTERPRETED_TERM:
      r = find_term_root(ctx, t);
      if (r != t) {
	t = r;
	goto loop;
      }
      r = subst_candidate(ctx, t);
      if (r != NULL_TERM) {
	t = r;
	goto loop;
      }
      break;

    case NOT_TERM:
      t = not_term_arg(terms, t);
      goto loop;

    case ITE_TERM:
      ite = ite_term_desc(terms, t);
      analyze_dl(ctx, stats, ite->cond, idl);
      analyze_dl(ctx, stats, ite->then_arg, idl);
      analyze_dl(ctx, stats, ite->else_arg, idl);
      break;

    case EQ_TERM:
      eq = eq_term_desc(terms, t);
      if (is_boolean_eq(terms, eq)) {
	analyze_dl(ctx, stats, eq->left, idl);
	analyze_dl(ctx, stats, eq->right, idl);
      } else {
	goto abort;
      }
      break;

    case OR_TERM:
      or = or_term_desc(terms, t);
      n = or->nargs;
      for (i=0; i<n; i++) {
	analyze_dl(ctx, stats, or->arg[i], idl);
      }
      break;

    case ARITH_EQ_ATOM:
      if (! check_diff_logic_poly(ctx, stats, arith_desc(terms, t), idl)) {
	goto abort;
      }
      stats->num_eqs ++;
      break;

    case ARITH_GE_ATOM:
      if (! check_diff_logic_poly(ctx, stats, arith_desc(terms, t), idl)) {
	goto abort;
      }
      break;

    case ARITH_BINEQ_ATOM:
      if (! check_diff_logic_eq(ctx, stats, arith_bineq_desc(terms, t), idl)) {
	goto abort;
      }
      break;

    default:
      goto abort;
    }
  }
  return;

 abort:  
  longjmp(ctx->env, LOGIC_NOT_SUPPORTED);
}
  

/*
 * Check all terms in vector v
 */
static void analyze_diff_logic_vector(context_t *ctx, dl_data_t *stats, ivector_t *v, bool idl) {
  uint32_t i, n;

  n = v->size;
  for (i=0; i<n; i++) {
    analyze_dl(ctx, stats, v->data[i], idl);
  }
}

/*
 * Check difference logic after flattening:
 * - return CTX_NO_ERROR if all assertions are in IDL or RDL
 * - return an error code otherwise
 */
static int32_t analyze_diff_logic(context_t *ctx, bool idl) {
  dl_data_t *stats;
  rational_t correction;
  int code;

  init_dlstats(ctx);
  stats = ctx->dl_profile;
  (void) context_get_cache(ctx); // allocate and initialize the cache
  code = setjmp(ctx->env);
  if (code == 0) {
    analyze_diff_logic_vector(ctx, stats, &ctx->top_eqs, idl);
    analyze_diff_logic_vector(ctx, stats, &ctx->top_atoms, idl);
    analyze_diff_logic_vector(ctx, stats, &ctx->top_formulas, idl);
    analyze_diff_logic_vector(ctx, stats, &ctx->subst_eqs, idl);

    /*
     * for IDL, we need to correct sum_const: since (x - y <= b)
     * is translated to (y - x <= - b -1)
     */
    if (idl) {
      q_init(&correction);
      q_set32(&correction, stats->num_atoms);
      q_add(&stats->sum_const, &correction);
      q_clear(&correction);
    }

#if (TRACE || TEST_DL)
    printf("==== Difference logic ====\n");
    if (idl) {
      printf("---> IDL\n");
    } else {
      printf("---> RDL\n");
    }
    printf("---> %"PRIu32" variables\n", stats->num_vars);
    printf("---> %"PRIu32" atoms\n", stats->num_atoms);
    printf("---> %"PRIu32" equalities\n", stats->num_eqs);
    printf("---> sum const = ");
    q_print(stdout, &stats->sum_const);
    printf("\n");
#endif
    code = CTX_NO_ERROR;
  } else {
#if (TRACE || TEST_DL)
    printf("==== Not difference logic ====\n");
#endif
    if (idl) {
      code = FORMULA_NOT_IDL;
    } else {
      code = FORMULA_NOT_RDL;
    }
  }
  context_delete_cache(ctx);

  return code;
}






/******************************
 *  ANALYSIS FOR UF FRAGMENT  *
 *****************************/

/*
 * Construct the term (eq x y) (normalized) then add it to top_eqs
 * - x and y should not be boolean, bitvector, or arithmetic terms,
 * - we check whether (eq x y) is true or false
 * - if it's false, the return code is TRIVIALLY_UNSAT
 * - if it's true, we do nothing
 * - otherwise, (eq x y) is added to top_eqs, and assigned to true
 */
static int32_t add_aux_eq(context_t *ctx, term_t x, term_t y) {
  term_table_t *terms;
  term_t eq;
  icode_t code;

  terms = ctx->terms;
  assert(!is_arithmetic_term(terms, x) && !is_boolean_term(terms, x) && !is_bitvector_term(terms, x) &&
	 !is_arithmetic_term(terms, y) && !is_boolean_term(terms, y) && !is_bitvector_term(terms, y) &&
	 x != y);

  if (x > y) {
    eq = eq_term(terms, y, x);
  } else {
    eq = eq_term(terms, x, y);
  }

#if TRACE_EQ_ABS
  printf("---> learned equality: ");
  print_termdef(stdout, eq);
  printf("\n");
#endif 

  code = code_of_term(&ctx->trans, eq);
  if (code_is_valid(code)) {
    if (code == bool2code(false)) {
      return TRIVIALLY_UNSAT;
    } else {
      return CTX_NO_ERROR; // do nothing
    }
  }
  
  map_term_to_bool(&ctx->trans, eq, true);
  ivector_push(&ctx->top_eqs, eq);
  return CTX_NO_ERROR;
}


/*
 * Add implied top_level equalities defined by the partition p
 * - return CTX_NO_ERROR if the equalities could be added
 * - return TRIVIALLY_UNSAT if an equality to add is known to be false
 */
static int32_t add_implied_equalities(context_t *ctx, epartition_t *p) {
  uint32_t i, n;
  term_t *q, x, y;
  int32_t k;
  
  n = p->nclasses;
  q = p->data;
  for (i=0; i<n; i++) {
    x = *q++;
    assert(x >= 0);
    y = *q ++;
    while (y >= 0) {
      k = add_aux_eq(ctx, x, y);
      if (k != CTX_NO_ERROR) return k;
      y = *q ++;
    }
  }
  return CTX_NO_ERROR;
}

/*
 * Attempt to learn global equalities implied 
 * by the formulas stored in ctx->top_formulas.
 * Any such equality is added to ctx->top_eqs
 * - return CTX_NO_ERROR if no contradiction is found
 * - return TRIVIALLY_UNSAT if a contradiction is found
 */
static int32_t analyze_uf(context_t *ctx) {
  ivector_t *v;
  uint32_t i, n;
  eq_learner_t eql;
  epartition_t *p;
  int32_t k;

  init_eq_learner(&eql, ctx->terms);
  v = &ctx->top_formulas;
  n = v->size;

  k = CTX_NO_ERROR;
  for (i=0; i<n; i++) {
    p = eq_learner_process(&eql, v->data[i]);
    if (p->nclasses > 0) {
      k = add_implied_equalities(ctx, p);
      if (k != CTX_NO_ERROR) break;
    }
  }

  delete_eq_learner(&eql);
  return k;
}




/********************************
 *  FLATTENING OF DISJUNCTIONS  *
 *******************************/

/*
 * Check whether the arithmetic solver is a difference logic solver
 */
static inline bool context_uses_dlsolver(context_t *ctx) {
  return (arch_components[ctx->arch] & (IFW|RFW)) != 0;
}

/*
 * Build term (not p >= 0) from p
 */
static term_t not_geq_atom_poly(context_t *ctx, polynomial_t *p) {
  uint32_t n;
  monomial_t *q;
  term_table_t *terms;

  // make a copy of p in the internal buffer
  n = p->nterms;
  q = context_get_monarray(ctx, n+1);
  n = copy_monarray(q, p->mono);
  terms = ctx->terms;
  return not_term(terms, arith_geq_atom_from_monarray(terms, q, n));
}

/*
 * Build term (not p <= 0) from p (i.e., (not -p >= 0))
 */
static term_t not_leq_atom_poly(context_t *ctx, polynomial_t *p) {
  uint32_t n;
  monomial_t *q;
  term_table_t *terms;

  // copy -p into the internal buffer
  n = p->nterms;
  q = context_get_monarray(ctx, n+1);
  n = negate_monarray(q, p->mono);
  terms = ctx->terms;
  return not_term(terms, arith_geq_atom_from_monarray(terms, q, n));
}


/*
 * Flatten term t:
 * - if t is already internalized, keep t and add it to v
 * - if t is (OR t1 ... t_n), recursively flatten t_1 ... t_n
 * - if flattening of disequalities is enabled, and t is (NOT (p == 0)) then
 *   we rewrite (NOT p) as (OR (NOT p>=0) (NOT p <= 0))
 * - otherwise store t into v
 * All terms already in v must be in the small cache
 */
static void flatten_or_recur(context_t *ctx, ivector_t *v, term_t t) {
  term_table_t *terms;
  or_term_t *or;
  uint32_t i, n;
  term_t u;

  assert(is_boolean_term(ctx->terms, t));

  if (int_hset_add(ctx->small_cache, t)) {
    // t not already in v and not visited before
    if (code_is_valid(code_of_term(&ctx->trans, t))) {
      // t already internalized, keep it as is
      ivector_push(v, t); 
    } else {
      terms = ctx->terms;
      switch (term_kind(terms, t)) {
      case OR_TERM:
	or = or_term_desc(terms, t);
	n = or->nargs;
	for (i=0; i<n; i++) {
	  flatten_or_recur(ctx, v, or->arg[i]);
	}
	break;
      case NOT_TERM:
	if (context_flatten_diseq_enabled(ctx)) {
	  u = not_term_arg(terms, t);
	  if (term_kind(terms, u) == ARITH_EQ_ATOM) {
	    // t is NOT (p == 0)
	    // rewrite t to (not p >= 0) or (not -p >= 0)
	    ivector_push(v, not_geq_atom_poly(ctx, arith_atom_desc(terms, u)));
	    ivector_push(v, not_leq_atom_poly(ctx, arith_atom_desc(terms, u)));
	    break;
	  }
	}
	/* fall through: can't flatten t */
      default:
	ivector_push(v, t);
	break;
      }
    }
  }
}


/*
 * Flatten a top-level (or t1 .... tp)
 * - initialize the small_cache, then calls the recursive function
 * - the result is stored in v
 */
static void flatten_or(context_t *ctx, ivector_t *v, or_term_t *or) {
  uint32_t i, n;

  assert(v->size == 0);
  (void) context_get_small_cache(ctx); // initialize the cache
  n = or->nargs;
  for (i=0; i<n; i++) {
    flatten_or_recur(ctx, v, or->arg[i]);
  }
  //  context_delete_small_cache(ctx);
  context_reset_small_cache(ctx);
}







/******************************************
 *  BIT-VECTOR ARITHMETIC SIMPLIFICATION  *
 *****************************************/

/*
 * Check whether t is equal a polynomial expression p and return p
 * - return NULL if not
 */
static bvarith_expr_t *context_subst_bvarith_term(context_t *ctx, term_t t) {
  term_table_t *terms;

  terms = ctx->terms;

  // apply substitution to t if any
  if (term_kind(terms, t) == UNINTERPRETED_TERM) {
    t = find_term_root(ctx, t);
    t = subst_candidate(ctx, t);
    if (t == NULL_TERM) {
      return NULL;
    }
  }

  if (term_kind(terms, t) == BV_ARITH_TERM) {
    return bvarith_term_desc(terms, t);
  } else {
    return NULL;
  }
}


/*
 * Same thing for a primitive bitvector variable x
 */
static inline bvarith_expr_t *context_subst_bvvar(context_t *ctx, bv_var_t x) {
  return context_subst_bvarith_term(ctx, bv_var_manager_term_of_var(ctx->bv_manager, x));
}




/*
 * Check whether subsituting variables in product d is possible and 
 * cheap enough
 * - d is x_1^d_1 * ... * x_n ^ d_n
 * - cheap enough for now means that if x_i can be replaced then d_i equals 1
 *   and there are no more than 2 variables to replace
 */
static bool context_acceptable_bvprod_subst(context_t *ctx, varprod_t *d) {
  bvarith_expr_t *q;
  uint32_t i, n, j;
  bv_var_t x;
  bool some_subst;

  n = d->len;
  j = 1;  // number of monomials after substitution
  some_subst = false;
  for (i=0; i<n && j<3; i++) {
    if (d->prod[i].exp == 1) {
      x = d->prod[i].var;
      q = context_subst_bvvar(ctx, x);
      if (q != NULL && q->nterms <= 2) {
	// the subsitution x:= q is considered cheap enough
	j *= q->nterms;
	some_subst = true;
      }
    }
  }

  return some_subst && (j < 3);
}


/*
 * Apply term substitutions to a product (x_1^d_1 ... x_n^d_n)
 * - the result is stored in buffer b (more exactly multiplied with
 *   whatever is in b)
 * - the substitution applied follows the rules above
 */
static void context_apply_bvprod_subst(context_t *ctx, varprod_t *d, bvarith_buffer_t *b) {
  uint32_t i, n;
  bv_var_t x, y;
  bvarith_expr_t *q;

  n = d->len;
  for (i=0; i<n; i++) {
    if (d->prod[i].exp == 1) {
      x = d->prod[i].var;
      q = context_subst_bvvar(ctx, x);
      if (q != NULL && q->nterms <= 2) {
	// replace x by q
	bvarith_buffer_mul_expr(b, q);
      } else {
	// keep x
	bvarith_buffer_mul_var(b, x);
      }
    } else {
      // multiply by y = x^d
      y = bv_var_manager_product_varexps(ctx->bv_manager, 1, &x, &d->prod[i].exp);
      bvarith_buffer_mul_var(b, y);
    }
  }
}




/*
 * Apply term substitution stored in ctx to p
 * - this may construct new terms in ctx->terms
 * - return the resulting term t
 */
static term_t context_simplify_bvarith(context_t *ctx, bvarith_expr_t *p) {
  bvarith_buffer_t *b;
  bvarith_buffer_t *prod;
  bvarith_expr_t *q;
  varprod_t *d;
  uint32_t i, n, size;
  uint32_t aux[2];
  bv_var_t x;
  
  // buffer with bitsize = same as p
  size = p->size;
  b = context_get_bvbuffer(ctx, size);
  n = p->nterms;

  if (size <= 64) {
    /*
     * Narrow buffer
     */
    for (i=0; i<n; i++) {
      x = p->mono[i].var;
      if (bv_var_manager_var_is_primitive(ctx->bv_manager, x)) {
	q = context_subst_bvvar(ctx, x);
	if (q != NULL) {
	  /*
	   * Replace monomial a.x by polynomial a.q in b
	   */
	  narrow_buffer_add_mono_times_expr(b, q, const_idx, p->mono[i].coeff.c);
	  continue;
	}
      } else {
	// x denotes a product (x_1^d_1 ... x_n^d_n)
	d = bv_var_manager_var_product(ctx->bv_manager, x);
	if (context_acceptable_bvprod_subst(ctx, d)) {
	  /*
	   * Replace a.x by a * subst(x_1^d1 ... x_n^d_n)
	   */
	  prod = context_get_bvbuffer2(ctx, size);
	  bvarith_buffer_set_one(prod); // prod := 1
	  context_apply_bvprod_subst(ctx, d, prod); // prod := result of substitution
	  narrow_buffer_add_mono_times_buffer(b, prod, const_idx, p->mono[i].coeff.c);
	  continue;
	}
      }
	       
      /*
       * No substitution performed: keep a.x
       */
      aux[0] = (uint32_t) (p->mono[i].coeff.c & 0xFFFFFFFF); // low-order bits of coeff
      aux[1] = (uint32_t) (p->mono[i].coeff.c >> 32);  // high-order bits
      bvarith_buffer_add_mono(b, x, aux);
    }

  } else {

    /*
     * Wide buffer
     */

    for (i=0; i<n; i++) {
      x = p->mono[i].var;
      if (bv_var_manager_var_is_primitive(ctx->bv_manager, x)) {
	q = context_subst_bvvar(ctx, x);
	if (q != NULL) {
	  /*
	   * Replace monomial a.x by polynomial a.q in b
	   */
	  wide_buffer_add_mono_times_expr(b, q, const_idx, p->mono[i].coeff.ptr);
	  continue;
	}
      } else {
	// x denotes a product (x_1^d_1 ... x_n^d_n)
	d = bv_var_manager_var_product(ctx->bv_manager, x);
	if (context_acceptable_bvprod_subst(ctx, d)) {
	  /*
	   * Replace a.x by a * subst(x_1^d1 ... x_n^d_n)
	   */
	  prod = context_get_bvbuffer2(ctx, size);
	  bvarith_buffer_set_one(prod); // prod := 1
	  context_apply_bvprod_subst(ctx, d, prod); // prod := result of substitution
	  wide_buffer_add_mono_times_buffer(b, prod, const_idx, p->mono[i].coeff.ptr);
	  continue;
	}
      }
	       
      /*
       * No substitution performed: keep a.x
       */
      bvarith_buffer_add_mono(b, x, p->mono[i].coeff.ptr);
    }
  }

  bvarith_buffer_normalize(b);
  return bvarith_term(ctx->terms, b);
}








/*********************
 *  INTERNALIZATION  *
 ********************/

/*
 * Main internalization functions:
 * - convert a term t to an egraph term
 * - convert a boolean term t to a literal
 * - convert an integer or real term t to an arithmetic variable
 * - convert a bitvector term t to a bitvector variable
 */
static occ_t internalize_to_eterm(context_t *ctx, term_t t);
static literal_t internalize_to_literal(context_t *ctx, term_t t);
static thvar_t internalize_to_arith(context_t *ctx, term_t t);
static thvar_t internalize_to_bv(context_t *ctx, term_t t);


/*
 * CREATION OF EGRAPH TERMS
 */


/*
 * Create a new egraph constant of the given type
 */
static eterm_t make_egraph_constant(context_t *ctx, type_t type, int32_t id) {
  assert(type_kind(ctx->types, type) == UNINTERPRETED_TYPE || type_kind(ctx->types, type) == SCALAR_TYPE);
  return egraph_make_constant(ctx->egraph, type, id);
}


/*
 * Create a new egraph variable
 * - type = its type
 */
static eterm_t make_egraph_variable(context_t *ctx, type_t type) {
  eterm_t u;
  bvar_t v;
  
  if (type == bool_type(ctx->types)) {
    v = create_boolean_variable(ctx->core);
    u = egraph_bvar2term(ctx->egraph, v);
  } else {
    u = egraph_make_variable(ctx->egraph, type);    
  }
  return u;
}

/*
 * Add the tuple skolemization axiom for term occurrence 
 * u of type tau, if needed.
 */
static void skolemize_if_tuple(context_t *ctx, occ_t u, type_t tau) {
  type_table_t *types;
  tuple_type_t *d;
  uint32_t i, n;
  occ_t *arg;
  eterm_t tup;

  types = ctx->types;
  if (type_kind(types, tau) == TUPLE_TYPE && !is_maxtype(types, tau)) {
    // instantiate the axiom
    d = tuple_type_desc(types, tau);
    n = d->nelem;
    arg = alloc_istack_array(&ctx->istack, n);
    for (i=0; i<n; i++) {
      arg[i] = pos_occ(make_egraph_variable(ctx, d->elem[i]));
      // recursively skolemize
      skolemize_if_tuple(ctx, arg[i], d->elem[i]);
    }

    tup = egraph_make_tuple(ctx->egraph, n, arg, tau);
    free_istack_array(&ctx->istack, arg);

    egraph_assert_eq_axiom(ctx->egraph, u, pos_occ(tup));
  }
}


/*
 * Build a tuple of same type as t then assert that it's equal to t
 * - u1 must be equal to internal[t]
 * This is the skolemization of (exist (x1...x_n) t == (tuple x1 ... x_n))
 */
static eterm_t skolem_tuple(context_t *ctx, term_t t, occ_t u1) {
  type_t tau;
  eterm_t u;
  tuple_type_t *d;
  uint32_t i, n;
  occ_t *arg;

  assert(occ_of_term(&ctx->trans, t) == u1);

  // tau = term_type(ctx->terms, t);
  tau = find_root_type(ctx, t);
  d = tuple_type_desc(ctx->types, tau);
  n = d->nelem;
  arg = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    arg[i] = pos_occ(make_egraph_variable(ctx, d->elem[i]));
    // recursively skolemize
    skolemize_if_tuple(ctx, arg[i], d->elem[i]);
  }

  u = egraph_make_tuple(ctx->egraph, n, arg, tau);
  free_istack_array(&ctx->istack, arg);

  egraph_assert_eq_axiom(ctx->egraph, u1, pos_occ(u));
  
  return u;
}






/*
 * ARITHMETIC VAR AND POLYNOMIALS
 */

/*
 * Compute internalization for arithmetic variable v
 */
static void internalize_arithvar(context_t *ctx, arith_var_t v) {
  term_t t;
  thvar_t x;

  x = code_of_arithvar(&ctx->trans, v);
  if (x == nil) {
    t = arithvar_manager_term_of_var(ctx->arith_manager, v);
    x = internalize_to_arith(ctx, t);
    map_arithvar(&ctx->trans, v, x);
  }
}


/*
 * Find internalization for all variables of p
 */
static void internalize_polynomial(context_t *ctx, polynomial_t *p) {  
  uint32_t i, n;
  ivector_t *v;
  arith_var_t *a;

  // get all primitive variables of p
  v = &ctx->aux_vector;
  assert(v->size == 0);
  polynomial_get_vars(p, ctx->arith_manager, v);

  // copy v into the integer stack
  n = v->size;
  a = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    a[i] = v->data[i];
  }
  ivector_reset(v);

  // internalize all variables
  for (i=0; i<n; i++) {
    internalize_arithvar(ctx, a[i]);
  }
  free_istack_array(&ctx->istack, a);
}




/*
 * Find internalization for all variables of p except x
 * - this is used when we try to rewrite assertion (p == 0)
 * - into a substitution of the form x := q where x occurs in p 
 */
static void internalize_subst_polynomial(context_t *ctx, polynomial_t *p, arith_var_t x) {  
  uint32_t i, n;
  ivector_t *v;
  arith_var_t *a;

  // get all primitive variables of p
  v = &ctx->aux_vector;
  assert(v->size == 0);
  polynomial_get_vars(p, ctx->arith_manager, v);

  // copy v into the integer stack
  n = v->size;
  a = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    a[i] = v->data[i];
  }
  ivector_reset(v);

  // internalize all variables (but skip x)
  for (i=0; i<n; i++) {
    if (a[i] != x) {
      internalize_arithvar(ctx, a[i]);
    }
  }
  free_istack_array(&ctx->istack, a);
}




/*
 * BITVECTOR VARIABLES AND EXPRESSIONS
 */

/*
 * Compute internalization for bit-vector variable v
 */
static void internalize_bv_var(context_t *ctx, bv_var_t v) {
  term_t t;
  thvar_t x;

  x = code_of_bvvar(&ctx->trans, v);
  if (x == nil) {
    t = bv_var_manager_term_of_var(ctx->bv_manager, v);
    x = internalize_to_bv(ctx, t);
    map_bvvar(&ctx->trans, v, x);
  }
}


/*
 * Find internalization for all variables of p
 */
static void internalize_bvarith(context_t *ctx, bvarith_expr_t *p) {  
  uint32_t i, n;
  ivector_t *v;
  bv_var_t *a;

  // get all primitive variables of p
  v = &ctx->aux_vector;
  assert(v->size == 0);
  bvarith_expr_get_vars(p, ctx->bv_manager, v);

  // copy v into the integer stack
  n = v->size;
  a = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    a[i] = v->data[i];
  }
  ivector_reset(v);

  // internalize all variables
  for (i=0; i<n; i++) {
    internalize_bv_var(ctx, a[i]);
  }
  free_istack_array(&ctx->istack, a);
}


/*
 * Find internalizaetion for all variables of b
 */
static void internalize_bvlogic(context_t *ctx, bvlogic_expr_t *b) {
  uint32_t i, n;
  ivector_t *v;
  bv_var_t *a;

  // get all primitive variables of b
  v = &ctx->aux_vector;
  assert(v->size == 0);
  bvlogic_expr_get_vars(b, ctx->bv_manager->bm, v);

  // copy v into the integer stack
  n = v->size;
  a = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    a[i] = v->data[i];
  }
  ivector_reset(v);

  // internalize all variables
  for (i=0; i<n; i++) {
    internalize_bv_var(ctx, a[i]);
  }
  free_istack_array(&ctx->istack, a);
}





/*
 * CONVERSION OF COMPOSITE TERMS TO EGRAPH TERMS
 */

/*
 * Convert (app f t_1 ... t_n) to an egraph term
 * - type must be the type of that term (should not be Bool)
 * - if a new egraph term u is created, attach a theory variable
 *   of the right type to u.
 */
static occ_t map_apply_to_eterm(context_t *ctx, app_term_t *app, type_t type) {
  eterm_t u;
  occ_t f, *arg;
  uint32_t i, n;

  f = internalize_to_eterm(ctx, app->fun);
  n = app->nargs;
  arg = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    arg[i] = internalize_to_eterm(ctx, app->arg[i]);
  }

  u = egraph_make_apply(ctx->egraph, f, n, arg, type);
  free_istack_array(&ctx->istack, arg);

  skolemize_if_tuple(ctx, pos_occ(u), type);
  return pos_occ(u);
}


/*
 * Convert (select i t) to an egraph term 
 * - type must be the type of that term (should not be bool)
 * - if a new eterm u is created, attach a theory variable to it
 */
static occ_t map_select_to_eterm(context_t *ctx, select_term_t *s, type_t type) {
  occ_t u1;
  eterm_t tuple;
  composite_t *tp;

  u1 = internalize_to_eterm(ctx, s->arg);
  tuple = egraph_get_tuple_in_class(ctx->egraph, term_of(u1));
  if (tuple == null_eterm) {
    tuple = skolem_tuple(ctx, s->arg, u1);
  }

  tp = egraph_term_body(ctx->egraph, tuple);
  assert(composite_body(tp) && tp != NULL && composite_kind(tp) == COMPOSITE_TUPLE);
  return tp->child[s->idx];
}


/*
 * Convert (ite c t1 t2) to an egraph term
 * - type = its type (should not be BOOL, BV, INT or REAL)
 * If the KEEP_ITE flag is set, we just construct an ite term in the egrah.
 * Otherwise, we create an egraph variable v and assert the clauses
 *    c ==> v = t1 and (not c) ==> v = t2
 */
static occ_t map_ite_to_eterm(context_t *ctx, ite_term_t *ite, type_t type) {
  eterm_t u;
  occ_t u1, u2, u3;
  literal_t c, l1, l2;

  c = internalize_to_literal(ctx, ite->cond);
  if (c == true_literal) {
    return internalize_to_eterm(ctx, ite->then_arg);
  }
  if (c == false_literal) {
    return internalize_to_eterm(ctx, ite->else_arg);
  }

  u2 = internalize_to_eterm(ctx, ite->then_arg);
  u3 = internalize_to_eterm(ctx, ite->else_arg);

  if (context_keep_ite_enabled(ctx)) {
    // build if-then-else in the egraph
    u1 = egraph_literal2occ(ctx->egraph, c);
    u = egraph_make_ite(ctx->egraph, u1, u2, u3, type);
  } else {
    // eliminate the if-then-else
    u = make_egraph_variable(ctx, type);
    l1 = egraph_make_eq(ctx->egraph, pos_occ(u), u2);
    l2 = egraph_make_eq(ctx->egraph, pos_occ(u), u3);

    assert_ite(&ctx->gate_manager, c, l1, l2, true);
  }

  return pos_occ(u);
}


/*
 * Convert (update f t_1 ... t_n v) to a term
 * - type = type of the term
 */
static occ_t map_update_to_eterm(context_t *ctx, update_term_t *update, type_t type) {
  eterm_t u;
  occ_t f, v, *arg;
  uint32_t i, n;

  f = internalize_to_eterm(ctx, update->fun);
  v = internalize_to_eterm(ctx, update->newval);
  n = update->nargs;
  arg = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    arg[i] = internalize_to_eterm(ctx, update->arg[i]);
  }

  u = egraph_make_update(ctx->egraph, f, n, arg, v, type);

  free_istack_array(&ctx->istack, arg);

  return pos_occ(u);
}



/*
 * Convert (tuple t_1 ... t_n) to a term
 * - type = type of the tuple
 */
static occ_t map_tuple_to_eterm(context_t *ctx, tuple_term_t *tuple, type_t type) {
  eterm_t u;
  occ_t *arg;
  uint32_t i, n;

  n = tuple->nargs;
  arg = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    arg[i] = internalize_to_eterm(ctx, tuple->arg[i]);
  }

  u = egraph_make_tuple(ctx->egraph, n, arg, type);
  free_istack_array(&ctx->istack, arg);
  return pos_occ(u);
}



/*
 * CONVERSION OF COMPOSITE TERMS TO LITERALS
 */

/*
 * Convert (apply f t_1 ... t_n) to a literal
 * - this creates an egraph atom
 */
static literal_t map_apply_to_literal(context_t *ctx, app_term_t *app) {
  literal_t l;
  occ_t f, *arg;
  uint32_t i, n;

  f = internalize_to_eterm(ctx, app->fun);
  n = app->nargs;
  arg = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    arg[i] = internalize_to_eterm(ctx, app->arg[i]);
  }
  l = egraph_make_pred(ctx->egraph, f, n, arg);
  free_istack_array(&ctx->istack, arg);
  return l;
}


/*
 * Convert (eq t1 t2) to a literal
 */
static literal_t map_eq_to_literal(context_t *ctx, eq_term_t *eq) {
  occ_t u, v;
  literal_t l1, l2, l;

  if (is_boolean_eq(ctx->terms, eq)) {
    l1 = internalize_to_literal(ctx, eq->left);
    l2 = internalize_to_literal(ctx, eq->right);
    l = mk_iff_gate(&ctx->gate_manager, l1, l2);
  } else {
    u = internalize_to_eterm(ctx, eq->left);
    v = internalize_to_eterm(ctx, eq->right);
    l = egraph_make_eq(ctx->egraph, u, v);
  }
  return l;
}


/*
 * Auxiliary function: translate (distinct a[0 ... n-1]) to a literal,
 * when a[0] ... a[n-1] are arithmetic variables. 
 * 
 * We expand this into a quadratic number of disequalities.
 */
static literal_t make_arith_distinct(context_t *ctx, uint32_t n, thvar_t *a) {
  uint32_t i, j;
  ivector_t *v;
  literal_t l;

  assert(n >= 2);

  v = &ctx->aux_vector;
  assert(v->size == 0);
  for (i=0; i<n-1; i++) {
    for (j=i+1; j<n; j++) {
      l = ctx->arith->create_vareq_atom(ctx->arith_solver, a[i], a[j]);
      ivector_push(v, l);
    }
  }
  l = mk_or_gate(&ctx->gate_manager, v->size, v->data);
  ivector_reset(v);
  return not(l);
}

/*
 * Auxiliary function: translate (distinct a[0 ... n-1]) to a literal,
 * when a[0] ... a[n-1] are bitvector variables. 
 * 
 * We expand this into a quadratic number of disequalities.
 */
static literal_t make_bv_distinct(context_t *ctx, uint32_t n, thvar_t *a) {
  uint32_t i, j;
  ivector_t *v;
  literal_t l;

  assert(n >= 2);

  v = &ctx->aux_vector;
  assert(v->size == 0);
  for (i=0; i<n-1; i++) {
    for (j=i+1; j<n; j++) {
      l = ctx->bv->create_eq_atom(ctx->arith_solver, a[i], a[j]);
      ivector_push(v, l);
    }
  }
  l = mk_or_gate(&ctx->gate_manager, v->size, v->data);
  ivector_reset(v);
  return not(l);
}



/*
 * Convert (distinct t_1 ... t_n) to a literal
 */
static literal_t map_distinct_to_literal(context_t *ctx, distinct_term_t *distinct) {
  int32_t *arg;
  literal_t l;
  uint32_t i, n;

  n = distinct->nargs;
  arg = alloc_istack_array(&ctx->istack, n);
  if (context_has_egraph(ctx)) {
    // default: translate to the egraph
    for (i=0; i<n; i++) {
      arg[i] = internalize_to_eterm(ctx, distinct->arg[i]);
    }
    l = egraph_make_distinct(ctx->egraph, n, arg);

  } else if (is_arithmetic_term(ctx->terms, distinct->arg[0])) {
    // translate to arithmetic variables
    for (i=0; i<n; i++) {
      arg[i] = internalize_to_arith(ctx, distinct->arg[i]);
    }
    l = make_arith_distinct(ctx, n, arg);

  } else if (is_bitvector_term(ctx->terms, distinct->arg[0])) {
    // translate to bitvector variables
    for (i=0; i<n; i++) {
      arg[i] = internalize_to_bv(ctx, distinct->arg[i]);
    }
    l = make_bv_distinct(ctx, n, arg);    

  } else {
    longjmp(ctx->env, UF_NOT_SUPPORTED);
  }

  free_istack_array(&ctx->istack, arg);
  return l;
}


/*
 * Convert (or t_1 ... t_n) to a literal
 * Flatten the subterms if flatten or is enabled
 */
static literal_t map_or_to_literal(context_t *ctx, or_term_t *or) {
  literal_t l;
  int32_t *arg;
  uint32_t i, n;
  ivector_t *v;

  if (context_flatten_or_enabled(ctx)) {
    // flatten t_1 ... t_n into vector v
    v = &ctx->aux_vector;
    assert(v->size == 0);
    flatten_or(ctx, v, or);

    // copy content of vector v into the array stack
    n = v->size;
    arg = alloc_istack_array(&ctx->istack, n);
    for (i=0; i<n; i++) {
      arg[i] = v->data[i];
    }
    ivector_reset(v);

    // internalize elements in arg
    for (i=0; i<n; i++) {
      arg[i] = internalize_to_literal(ctx, arg[i]);
    }

  } else {
    // no flattening
    n = or->nargs;
    arg = alloc_istack_array(&ctx->istack, n);
    for (i=0; i<n; i++) {
      arg[i] = internalize_to_literal(ctx, or->arg[i]);
    }
  }

  l = mk_or_gate(&ctx->gate_manager, n, arg);
  free_istack_array(&ctx->istack, arg);
  return l;
}


/*
 * Convert (ite c t1 t2) to a literal
 */
static literal_t map_ite_to_literal(context_t *ctx, ite_term_t *ite) {
  literal_t l1, l2, l3;

  l1 = internalize_to_literal(ctx, ite->cond);
  if (l1 == true_literal) {
    return internalize_to_literal(ctx, ite->then_arg);
  }
  if (l1 == false_literal) {
    return internalize_to_literal(ctx, ite->else_arg);
  }
  l2 = internalize_to_literal(ctx, ite->then_arg);
  l3 = internalize_to_literal(ctx, ite->else_arg);
  return mk_ite_gate(&ctx->gate_manager, l1, l2, l3);
}



/*
 * Bitvector atoms
 */
static literal_t map_bveq_to_literal(context_t *ctx, bv_atom_t *bveq) {
  thvar_t x, y;
  occ_t u, v;
  literal_t l;

  /*
   * It's usually better to add equalities to the egraph
   * so that congruence closure is put to work.
   * The egraph will propagate equalities to the bv_solver itself.
   */
  if (false && context_has_egraph(ctx)) {
    u = internalize_to_eterm(ctx, bveq->left);
    v = internalize_to_eterm(ctx, bveq->right);
    l = egraph_make_eq(ctx->egraph, u, v);
  } else {
    // Create a bitvector atom
    x = internalize_to_bv(ctx, bveq->left);
    y = internalize_to_bv(ctx, bveq->right);
    l= ctx->bv->create_eq_atom(ctx->bv_solver, x, y);
  }
  return l;
}

static literal_t map_bvge_to_literal(context_t *ctx, bv_atom_t *bveq) {
  thvar_t x, y;

  x = internalize_to_bv(ctx, bveq->left);
  y = internalize_to_bv(ctx, bveq->right);
  return ctx->bv->create_ge_atom(ctx->bv_solver, x, y);
}

static literal_t map_bvsge_to_literal(context_t *ctx, bv_atom_t *bveq) {
  thvar_t x, y;

  x = internalize_to_bv(ctx, bveq->left);
  y = internalize_to_bv(ctx, bveq->right);
  return ctx->bv->create_sge_atom(ctx->bv_solver, x, y);
}



/*
 * Arithmetic atoms
 */
static literal_t map_aritheq_to_literal(context_t *ctx, polynomial_t *p) {
  internalize_polynomial(ctx, p);
  return ctx->arith->create_eq_atom(ctx->arith_solver, p, &ctx->trans.arith_map);
}

static literal_t map_arithge_to_literal(context_t *ctx, polynomial_t *p) {
  internalize_polynomial(ctx, p);
  return ctx->arith->create_ge_atom(ctx->arith_solver, p, &ctx->trans.arith_map);
}


// Equality (t1 == t2): add it to the egraph if possible
static literal_t map_arith_bineq_to_literal(context_t *ctx, arith_bineq_t *e) {
  thvar_t x, y;
  occ_t u, v;
  literal_t l;

  if (context_has_egraph(ctx)) {
    u = internalize_to_eterm(ctx, e->left);
    v = internalize_to_eterm(ctx, e->right);
    l = egraph_make_eq(ctx->egraph, u, v);
  } else {
    // No egraph: direct arithmetic atom
    x = internalize_to_arith(ctx, e->left);
    y = internalize_to_arith(ctx, e->right);
    l = ctx->arith->create_vareq_atom(ctx->arith_solver, x, y);
  }

  return l;
}




/*
 * CONVERSION OF COMPOSITE TERMS TO ARITHMETIC VARIABLES
 */

/*
 * Translation of internalization code x to a theory variable
 * - code x = translation code for a term t
 * - if the code is for an egraph term u, then we return the theory variabke
 *   attached to u in the egraph
 * - otherwise x should be the code of a theory variable v, we return v
 */
static thvar_t translate_code_to_arith(context_t *ctx, icode_t x) {
  thvar_t v;

  assert(code_is_valid(x));
  if (code_is_eterm(x)) {
    assert(ctx->egraph != NULL && egraph_term_is_arith(ctx->egraph, code2eterm(x)));
    v = egraph_term_base_thvar(ctx->egraph, code2eterm(x));
  } else {
    v = code2arithvar(x);
  }
  assert(v != null_thvar);

  return v;
}


/*
 * Assert c ==> (v == t) as part of an if-then-else conversion
 * - v is an arithmetic variable
 * - t is a term
 */
static void assert_arith_cond_vareq(context_t *ctx, literal_t c, thvar_t v, term_t t) {
  term_table_t *terms;
  polynomial_t *p;
  icode_t x;
  thvar_t v2;

  terms = ctx->terms;
  // avoid creating an extra variable for t if it's a polynomial
  if (term_kind(terms, t) == ARITH_TERM) {
    x = code_of_term(&ctx->trans, t);
    if (code_is_valid(x)) {
      v2 = translate_code_to_arith(ctx, x);
    } else {
      p = arith_term_desc(terms, t);
      internalize_polynomial(ctx, p);
      ctx->arith->assert_cond_polyeq_axiom(ctx->arith_solver, c, v, p, &ctx->trans.arith_map);
      return;
    }
  } else {
    v2 = internalize_to_arith(ctx, t);    
  }
  ctx->arith->assert_cond_vareq_axiom(ctx->arith_solver, c, v, v2);
}


/*
 * Map (ite c t1 t2) to an arithmetic variable
 * - type = type of (ite c t1 t2). It should be either int or real
 */
static thvar_t map_ite_to_arith(context_t *ctx, ite_term_t *ite, type_t t) {
  literal_t c;
  thvar_t v;

  assert(is_arithmetic_type(t));

  c = internalize_to_literal(ctx, ite->cond);
  if (c == true_literal) {
    return internalize_to_arith(ctx, ite->then_arg);
  }
  if (c == false_literal) {
    return internalize_to_arith(ctx, ite->else_arg);
  }

  v = ctx->arith->create_var(ctx->arith_solver, is_integer_type(t));

  assert_arith_cond_vareq(ctx, c, v, ite->then_arg);      // c implies (v == t1)
  assert_arith_cond_vareq(ctx, not(c), v, ite->else_arg); // (not c) implies (v == t2)

  return v;
}


/*
 * Map polynomial p to an arithmetic variable
 */
static thvar_t map_arith_term_to_arith(context_t *ctx, polynomial_t *p) {
  internalize_polynomial(ctx, p);
  return ctx->arith->create_poly(ctx->arith_solver, p, &ctx->trans.arith_map);
}





/*
 * CONVERSION OF COMPOSITE TERMS TO BITVECTOR VARIABLES
 */


/*
 * Map (ite c t1 t2) to a bitvector variable
 * - type = type of (ite c t1 t2). It should be (bitvector k)
 */
static thvar_t map_ite_to_bv(context_t *ctx, ite_term_t *ite, type_t t) {
  literal_t c;
  thvar_t v1, v2;

  assert(type_kind(ctx->types, t) == BITVECTOR_TYPE);

  c = internalize_to_literal(ctx, ite->cond);
  if (c == true_literal) {
    return internalize_to_bv(ctx, ite->then_arg);
  }
  if (c == false_literal) {
    return internalize_to_bv(ctx, ite->else_arg);
  }

  v1 = internalize_to_bv(ctx, ite->then_arg);
  v2 = internalize_to_bv(ctx, ite->else_arg);

  return ctx->bv->create_bvite(ctx->bv_solver, c, v1, v2);
}




/*
 * Map (bvapply op t1 t2) to a bitvector variable
 */
static thvar_t map_bvapply_to_bv(context_t *ctx, bvapply_term_t *app) {
  thvar_t v1, v2;

  v1 = internalize_to_bv(ctx, app->arg0);
  v2 = internalize_to_bv(ctx, app->arg1);
  return ctx->bv->create_bvop(ctx->bv_solver, app->op, v1, v2);
}


/*
 * Map bitvector polynomial p to a bitvector variable
 */
static thvar_t map_bvarith_to_bv(context_t *ctx, bvarith_expr_t *p) {
  internalize_bvarith(ctx, p);
  return ctx->bv->create_bvpoly(ctx->bv_solver, p, &ctx->trans.bv_map);
}


/*
 * Map bdd array b to a bitvector variable
 */
static thvar_t map_bvlogic_to_bv(context_t *ctx, bvlogic_expr_t *b) {
  internalize_bvlogic(ctx, b);
  return ctx->bv->create_bvlogic(ctx->bv_solver, b, &ctx->trans.bv_map);
}




/*
 * CONVERSION TO ARITHMETIC
 */

/*
 * Internalize an uninterpreted term t to an arithmetic variable
 * - this takes the partition map and the substitution candidates into account
 */
static thvar_t map_uninterpreted_to_arith(context_t *ctx, term_t t) {
  term_t r;
  thvar_t v;

  assert(code_of_term(&ctx->trans, t) < 0 && 
	 term_kind(ctx->terms, t) == UNINTERPRETED_TERM);

  r = find_term_root(ctx, t);
  if (r != t) {
    /*
     * t is not a root: internalize r and map t to the same code as r
     */
    v = internalize_to_arith(ctx, r);
    map_term_to_code(&ctx->trans, t, code_of_term(&ctx->trans, r));

  } else {
    /*
     * t is a root: check the candidate substitution
     */
    r = subst_candidate(ctx, t);
    if (r == NULL_TERM) {
#if TRACE
      printf("\n---> internalize_to_arith: create var for term ");
      print_term_id(stdout, t);
      printf("\n");
      if (term_has_theory_var(terms, t)) {
	printf("---> thvar = ");
	print_arith_var(stdout, term_theory_var(terms, t));
	printf("\n");
      }
#endif
      v = ctx->arith->create_var(ctx->arith_solver, root_type_is_integer(ctx, t));
      map_term_to_arithvar(&ctx->trans, t, v);
    } else {
      v = internalize_to_arith(ctx, r);
      map_term_to_code(&ctx->trans, t, code_of_term(&ctx->trans, r));
    }
  }

  return v;
}


static thvar_t internalize_to_arith(context_t *ctx, term_t t) {
  term_table_t *terms;
  icode_t x;
  int32_t code;
  thvar_t v;
  occ_t u;
  
  assert(is_arithmetic_term(ctx->terms, t));

  if (! context_has_arith_solver(ctx)) {
    code = ARITH_NOT_SUPPORTED;
    goto abort;
  }

  x = code_of_term(&ctx->trans, t);
  if (code_is_valid(x)) {
    return translate_code_to_arith(ctx, x);
  }

  terms = ctx->terms;
  switch (term_kind(terms, t)) {
  case UNINTERPRETED_TERM:
    v = map_uninterpreted_to_arith(ctx, t);
    break;

  case ITE_TERM:
    v = map_ite_to_arith(ctx, ite_term_desc(terms, t), term_type(terms, t));
    map_term_to_arithvar(&ctx->trans, t, v);    
    break;

  case APP_TERM:
    u = map_apply_to_eterm(ctx, app_term_desc(terms, t), term_type(terms, t));
    assert(egraph_term_is_arith(ctx->egraph, term_of(u)));
    map_term_to_occ(&ctx->trans, t, u);
    v = egraph_term_base_thvar(ctx->egraph, term_of(u));
    assert(v != null_thvar);
    // HACK
    if (true && ! base_propagate(ctx->core)) {
      code = TRIVIALLY_UNSAT;
      goto abort;
    }
    break;

  case SELECT_TERM:
    u = map_select_to_eterm(ctx, select_term_desc(terms, t), term_type(terms, t));
    assert(egraph_term_is_arith(ctx->egraph, term_of(u)));
    map_term_to_occ(&ctx->trans, t, u);
    v = egraph_term_base_thvar(ctx->egraph, term_of(u));
    assert(v != null_thvar);
    break;

  case ARITH_TERM:
    v = map_arith_term_to_arith(ctx, arith_term_desc(terms, t));
    map_term_to_arithvar(&ctx->trans, t, v);
    break;
    
    /*
     * All other term kinds can't be arithmetic terms. 
     * Set error code and abort.
     */
  case VARIABLE:
    code = FREE_VARIABLE_IN_FORMULA;
    goto abort;

  default:
    code = INTERNAL_ERROR;
    goto abort;
  }

  return v;

 abort:
  longjmp(ctx->env, code);
}




/*
 * CONVERSION TO BIT VECTORS
 */

/*
 * Translation of internalization code x to a theory variable
 * - code x = translation code for a term t
 * - if the code is for an egraph term u, then we return the theory variabke
 *   attached to u in the egraph
 * - otherwise x should be the code of a theory variable v, we return v
 */
static thvar_t translate_code_to_bv(context_t *ctx, icode_t x) {
  thvar_t v;

  assert(code_is_valid(x));
  if (code_is_eterm(x)) {
    assert(ctx->egraph != NULL && egraph_term_is_bv(ctx->egraph, code2eterm(x)));
    v = egraph_term_base_thvar(ctx->egraph, code2eterm(x));
  } else {
    v = code2bvvar(x);
  }
  assert(v != null_thvar);
  return v;
}


/*
 * Internalize an uninterpreted term t to a bitvector variable
 * - this takes the partition map and the substitution candidates into account
 */
static thvar_t map_uninterpreted_to_bv(context_t *ctx, term_t t) {
  term_table_t *terms;
  term_t r;
  thvar_t v;

  assert(code_of_term(&ctx->trans, t) < 0 && 
	 term_kind(ctx->terms, t) == UNINTERPRETED_TERM);

  terms = ctx->terms;
  r = find_term_root(ctx, t);
  if (r != t) {
    /*
     * t is not a root: internalize r and map t to the same code as r
     */
    v = internalize_to_bv(ctx, r);
    map_term_to_code(&ctx->trans, t, code_of_term(&ctx->trans, r));

  } else {
    /* 
     * t is a root: check candidate substitution
     */
    r = subst_candidate(ctx, t);
    if (r == NULL_TERM) {
      v = ctx->bv->create_var(ctx->bv_solver, term_bitsize(terms, t));
      map_term_to_bvvar(&ctx->trans, t, v);
    } else {
      v = internalize_to_bv(ctx, r);
      map_term_to_code(&ctx->trans, t, code_of_term(&ctx->trans, r));
    }
  }
  return v;
}


/*
 * Main internalization function for bitvector terms
 */
static thvar_t internalize_to_bv(context_t *ctx, term_t t) {
  term_table_t *terms;
  icode_t x;
  int32_t code;
  thvar_t v;
  occ_t u;
  term_t q;

  if (! context_has_bv_solver(ctx)) {
    code = BV_NOT_SUPPORTED;
    goto abort;
  }

  assert(is_bitvector_term(ctx->terms, t));

  x = code_of_term(&ctx->trans, t);
  if (code_is_valid(x)) {
    return translate_code_to_bv(ctx, x);
  }

  terms = ctx->terms;

  switch (term_kind(terms, t)) {
  case UNINTERPRETED_TERM:
    v = map_uninterpreted_to_bv(ctx, t);    
    break;

  case ITE_TERM:
    v = map_ite_to_bv(ctx, ite_term_desc(terms, t), term_type(terms, t));
    map_term_to_bvvar(&ctx->trans, t, v);    
    break;

  case APP_TERM:
    u = map_apply_to_eterm(ctx, app_term_desc(terms, t), term_type(terms, t));
    assert(egraph_term_is_bv(ctx->egraph, term_of(u)));
    map_term_to_occ(&ctx->trans, t, u);
    v = egraph_term_base_thvar(ctx->egraph, term_of(u));
    assert(v != null_thvar);
    // HACK
    if (true && ! base_propagate(ctx->core)) {
      code = TRIVIALLY_UNSAT;
      goto abort;
    }
    break;

  case SELECT_TERM:
    u = map_select_to_eterm(ctx, select_term_desc(terms, t), term_type(terms, t));
    assert(egraph_term_is_bv(ctx->egraph, term_of(u)));
    map_term_to_occ(&ctx->trans, t, u);
    v = egraph_term_base_thvar(ctx->egraph, term_of(u));
    assert(v != null_thvar);
    break;

  case BV_LOGIC_TERM:
    v = map_bvlogic_to_bv(ctx, bvlogic_term_desc(terms, t));
    map_term_to_bvvar(&ctx->trans, t, v);
    break;

  case BV_ARITH_TERM:
    q = t;
    if (context_bvarith_elim_enabled(ctx)) {
      // try simplification
      q = context_simplify_bvarith(ctx, bvarith_term_desc(terms, t));
      assert(term_kind(terms, q) == BV_ARITH_TERM);
    }
    if (q != t) {
      /*
       * The simplification worked:
       * - internalize q first then map t to the same variable
       * TODO: check whether recursively calling internalize_to_bv(ctx, q)
       * works better.
       */
      x = code_of_term(&ctx->trans, q);
      if (code_is_valid(x)) {
	v = translate_code_to_bv(ctx, x);
      } else {
	v = map_bvarith_to_bv(ctx, bvarith_term_desc(terms, q));
	map_term_to_bvvar(&ctx->trans, q, v);
      }
    } else {
      // no simplification found
      v = map_bvarith_to_bv(ctx, bvarith_term_desc(terms, t));
    }
    map_term_to_bvvar(&ctx->trans, t, v);
    break;

  case BV_CONST_TERM:
    v = ctx->bv->create_const(ctx->bv_solver, bvconst_term_desc(terms, t));
    map_term_to_bvvar(&ctx->trans, t, v);
    break;
    
  case BV_APPLY_TERM:
    v = map_bvapply_to_bv(ctx, bvapply_term_desc(terms, t));
    map_term_to_bvvar(&ctx->trans, t, v);
    break;

    /*
     * All other term kinds can't be bit-vector terms. 
     * Set error code and abort.
     */
  case VARIABLE:
    code = FREE_VARIABLE_IN_FORMULA;
    goto abort;

  default:
    code = INTERNAL_ERROR;
    goto abort;
  }

  return v;

 abort:
  longjmp(ctx->env, code);
}






/*
 * CONVERSION TO LITERALS
 */

/*
 * Translation of internalization code x to a literal
 * - code x = translation code for a term t
 * - if the code is for an egraph term u, then we return the theory variable
 *   attached to u in the egraph
 * - otherwise x should be the code of a theory variable v, we return v
 */
static literal_t translate_code_to_literal(context_t *ctx, icode_t x) {
  occ_t t;

  assert(code_is_valid(x));
  if (code_is_eterm(x)) {
    t = code2occ(x);
    if (term_of(t) == true_eterm) {
      return mk_lit(bool_const, polarity_of(t));
    } else {
      assert(ctx->egraph != NULL);
      return egraph_occ2literal(ctx->egraph, t);
    }
  } else {
    return code2literal(x);
  }
}

/*
 * Internalize an uninterpreted term t to a literal
 * - take partition map and substitution candidate into account
 */
static literal_t map_uninterpreted_to_literal(context_t *ctx, term_t t) {
  term_t r;
  literal_t l;

  assert(code_of_term(&ctx->trans, t) < 0 &&
	 term_kind(ctx->terms, t) == UNINTERPRETED_TERM);

  r = find_term_root(ctx, t);
  if (r != t) {
    /*
     * t is not a root: internalize r and map t to the same code as r
     */
    l = internalize_to_literal(ctx, r);
    map_term_to_code(&ctx->trans, t, code_of_term(&ctx->trans, r));

  } else {
    /* 
     * t is a root: check candidate substitution
     */
    r = subst_candidate(ctx, t);
    if (r == NULL_TERM) {
      l = pos_lit(create_boolean_variable(ctx->core));
      map_term_to_literal(&ctx->trans, t, l);
    } else {
      l = internalize_to_literal(ctx, r);
      map_term_to_code(&ctx->trans, t, code_of_term(&ctx->trans, r));
    }
  }
   
  return l;
}


/*
 * Main internalization function for boolean terms
 */
static literal_t internalize_to_literal(context_t *ctx, term_t t) {
  term_table_t *terms;
  icode_t x;
  int32_t code;
  literal_t l;
  occ_t u;

  assert(is_boolean_term(ctx->terms, t));
  x = code_of_term(&ctx->trans, t);
  if (code_is_valid(x)) {
    return translate_code_to_literal(ctx, x);
  }

  terms = ctx->terms;

  switch (term_kind(terms, t)) {
  case CONSTANT_TERM:
    if (t == false_term(terms)) {
      l = false_literal;
    } else if (t == true_term(terms)) {
      l = true_literal;
    } else {
      code = INTERNAL_ERROR;
      goto abort;
    }
    break;

  case UNINTERPRETED_TERM:
    return map_uninterpreted_to_literal(ctx, t);
    
  case NOT_TERM:
    l = not(internalize_to_literal(ctx, not_term_arg(terms, t)));
    break;

  case ITE_TERM:
    l = map_ite_to_literal(ctx, ite_term_desc(terms, t));
    break;

  case EQ_TERM:
    l = map_eq_to_literal(ctx, eq_term_desc(terms, t));
    break;

  case APP_TERM:
    l = map_apply_to_literal(ctx, app_term_desc(terms, t));
    // HACK
    if (true && ! base_propagate(ctx->core)) {
      code = TRIVIALLY_UNSAT;
      goto abort;
    }
    break;

  case OR_TERM:
    l = map_or_to_literal(ctx, or_term_desc(terms, t));
    break;

  case SELECT_TERM:
    u = map_select_to_eterm(ctx, select_term_desc(terms, t), bool_type(ctx->types));
    assert(egraph_term_is_bool(ctx->egraph, term_of(u)));
    map_term_to_occ(&ctx->trans, t, u);
    return egraph_occ2literal(ctx->egraph, u);

  case DISTINCT_TERM:
    l = map_distinct_to_literal(ctx, distinct_term_desc(terms, t));
    break;

  case FORALL_TERM:
    code = QUANTIFIERS_NOT_SUPPORTED;
    goto abort;

  case ARITH_EQ_ATOM:
    l = map_aritheq_to_literal(ctx, arith_atom_desc(terms, t));
    break;
			       
  case ARITH_GE_ATOM:
    l = map_arithge_to_literal(ctx, arith_atom_desc(terms, t));
    break;

  case ARITH_BINEQ_ATOM:
    l = map_arith_bineq_to_literal(ctx, arith_bineq_desc(terms, t));
    break;

  case BV_EQ_ATOM:
    l = map_bveq_to_literal(ctx, bvatom_desc(terms, t));
    break;

  case BV_GE_ATOM:
    l = map_bvge_to_literal(ctx, bvatom_desc(terms, t));
    break;

  case BV_SGE_ATOM:
    l = map_bvsge_to_literal(ctx, bvatom_desc(terms, t));
    break;

  case VARIABLE:
    code = FREE_VARIABLE_IN_FORMULA;
    goto abort;

  default:
    code = INTERNAL_ERROR;
    goto abort;
  }
  
  map_term_to_literal(&ctx->trans, t, l);
  return l;

 abort:
  longjmp(ctx->env, code);
}




/*
 * CONVERSION TO EGRAPH TERMS
 */

/*
 * Convert arithmetic variable v to an egraph term:
 * - if v has an eterm u attached, return pos_occ(u)
 * - otherwise, create a fresh egraph variable u and attach v to u
 * - tau = type of v (either int_type or real_type)
 */
static occ_t translate_arithvar_to_eterm(context_t *ctx, thvar_t v, type_t tau) {
  eterm_t u;

  assert(is_arithmetic_type(tau));

  // get term for v in arithmetic solver
  u = ctx->arith->eterm_of_var(ctx->arith_solver, v);
  if (u == null_eterm) {
    // create a fresh egraph variable for v
    u = egraph_thvar2term(ctx->egraph, v, tau);
  }

  return pos_occ(u);
}


/*
 * Convert bitvector variable v to an egraph term
 * - if v has an eterm attached, return it
 * - otherwise, create a fresh egraph variable u and attach it to v
 * - tau = type of v (must be (bitvector k)
 */
static occ_t translate_bvvar_to_eterm(context_t *ctx, thvar_t v, type_t tau) {
  eterm_t u;

  assert(type_kind(ctx->types, tau) == BITVECTOR_TYPE);

  // get term for v in bvsolver
  u = ctx->bv->eterm_of_var(ctx->bv_solver, v);
  if (u == null_eterm) {
    u = egraph_thvar2term(ctx->egraph, v, tau);
  }  
  return pos_occ(u);
}



/*
 * Convert variable v into an eterm internalization for t
 * - if v is mapped to an existing egraph term u, return pos_occ(u)
 * - otherwise create an egraph variable u and attach v to u
 *   and record the converse mapping (v --> u) in the relevant theory solver
 */
static occ_t translate_thvar_to_eterm(context_t *ctx, term_t t, thvar_t v) {
  type_t tau;

  assert(code_of_term(&ctx->trans, t) < 0);

  tau = term_type(ctx->terms, t);
  switch (type_kind(ctx->types, tau)) {
  case INT_TYPE:
  case REAL_TYPE:
    return translate_arithvar_to_eterm(ctx, v, tau);
  case BITVECTOR_TYPE:
    return translate_bvvar_to_eterm(ctx, v, tau);
  default:
    assert(false);
    longjmp(ctx->env, INTERNAL_ERROR);	
  }
}


/*
 * Convert code x mapped to t to an egraph term
 * - t = term
 * - x = code of t in the translator 
 * - x must refer either to a literal or to a theory variable in the
 *   bitvector or arithmetic solver.
 */
static occ_t translate_code_to_eterm(context_t *ctx, term_t t, icode_t x) {
  occ_t u;
  type_t tau;

  assert(code_of_term(&ctx->trans, t) == x && code_is_valid(x));
  if (code_is_eterm(x)) {
    u = code2occ(x);
  } else {
    // convert the theory variable to an egraph term
    tau = term_type(ctx->terms, t);
    switch (type_kind(ctx->types, tau)) {
    case BOOL_TYPE:
      u = egraph_literal2occ(ctx->egraph, code2literal(x));
      break;

    case INT_TYPE:
    case REAL_TYPE:
      u = translate_arithvar_to_eterm(ctx, code2var(x), tau);
      break;

    case BITVECTOR_TYPE:
      u = translate_bvvar_to_eterm(ctx, code2var(x), tau);
      break;

    default:
      assert(false);
      longjmp(ctx->env, INTERNAL_ERROR);	
    }

    // save the mapping t --> term occurrence in the translation table
    remap_term_to_occ(&ctx->trans, t, u);
  }

  return u;   
}


/*
 * Internalize an uninterpreted term t to an egraph term
 * - takes the partition map and the substitution candidates into account
 */
static occ_t map_uninterpreted_to_eterm(context_t *ctx, term_t t) {
  term_t r;
  occ_t u;
  type_t tau;

  assert(code_of_term(&ctx->trans, t) < 0 &&
	 term_kind(ctx->terms, t) == UNINTERPRETED_TERM);

  r = find_term_root(ctx, t);
  if (r != t) {
    /*
     * t is not a root: internalize r and map t to the same code as r
     */
    u = internalize_to_eterm(ctx, r);
    map_term_to_code(&ctx->trans, t, code_of_term(&ctx->trans, r));    

  } else {
    /* 
     * t is a root: check candidate substitution
     */
    r = subst_candidate(ctx, t);
    if (r == NULL_TERM) {
      tau = find_root_type(ctx, t);
      u = pos_occ(make_egraph_variable(ctx, tau));
      map_term_to_occ(&ctx->trans, t, u);
      /*
       * If u has (non-maximal) tuple type
       * we instantiate the skolemization axiom for u
       * to give each component of u its correct type
       * in the egraph.
       */
      skolemize_if_tuple(ctx, u, tau);

    } else {
      u = internalize_to_eterm(ctx, r);
      assert(code_of_term(&ctx->trans, r) == occ2code(u));
      map_term_to_occ(&ctx->trans, t, u);
    }
  }

  return u;
}


/*
 * Main internalization function
 */
static occ_t internalize_to_eterm(context_t *ctx, term_t t) {
  term_table_t *terms;
  icode_t x;
  int32_t code;
  occ_t u;
  literal_t l;
  thvar_t v;

  if (! context_has_egraph(ctx)) {
    code = UF_NOT_SUPPORTED;
    goto abort;
  }

  x = code_of_term(&ctx->trans, t);
  if (code_is_valid(x)) {
    return translate_code_to_eterm(ctx, t, x);
  }

  terms = ctx->terms;

  /*
   * term t not internalized yet
   * if it's a boolean term, convert to a literal l then 
   * remap l to an egraph term.
   */
  if (is_boolean_term(terms, t)) {
    l = internalize_to_literal(ctx, t);
    u = egraph_literal2occ(ctx->egraph, l);
    remap_term_to_occ(&ctx->trans, t, u);
    return u;
  }

  
  /*
   * t not boolean
   */
  switch (term_kind(terms, t)) {
  case CONSTANT_TERM:
    u = pos_occ(make_egraph_constant(ctx, term_type(terms, t), constant_term_index(terms, t)));
    break;
    
  case UNINTERPRETED_TERM:
    return map_uninterpreted_to_eterm(ctx, t);

  case VARIABLE:
    code = FREE_VARIABLE_IN_FORMULA;
    goto abort;

  case ITE_TERM:
    u = map_ite_to_eterm(ctx, ite_term_desc(terms, t), term_type(terms, t));
    break;

  case APP_TERM:
    u = map_apply_to_eterm(ctx, app_term_desc(terms, t), term_type(terms, t));
    // HACK
    if (true && ! base_propagate(ctx->core)) {
      code = TRIVIALLY_UNSAT;
      goto abort;
    }
    break;

  case TUPLE_TERM:
    u = map_tuple_to_eterm(ctx, tuple_term_desc(terms, t), term_type(terms, t));
    break;

  case SELECT_TERM:
    u = map_select_to_eterm(ctx, select_term_desc(terms, t), term_type(terms, t));
    break;

  case UPDATE_TERM:
    u = map_update_to_eterm(ctx, update_term_desc(terms, t), term_type(terms, t));
    // HACK
    if (true && ! base_propagate(ctx->core)) {
      code = TRIVIALLY_UNSAT;
      goto abort;
    }
    break;

  case ARITH_TERM:
    v = map_arith_term_to_arith(ctx, arith_term_desc(terms, t));
    u = translate_thvar_to_eterm(ctx, t, v);
    break;

  case BV_LOGIC_TERM:
    v = map_bvlogic_to_bv(ctx, bvlogic_term_desc(terms, t));
    u = translate_thvar_to_eterm(ctx, t, v);
    break;

  case BV_ARITH_TERM:
    v = map_bvarith_to_bv(ctx, bvarith_term_desc(terms, t));
    u = translate_thvar_to_eterm(ctx, t, v);
    break;

  case BV_CONST_TERM:
    if (! context_has_bv_solver(ctx)) {
      code = BV_NOT_SUPPORTED;
      goto abort;
    }
    v = ctx->bv->create_const(ctx->bv_solver, bvconst_term_desc(terms, t));
    u = translate_thvar_to_eterm(ctx, t, v);
    break;

  case BV_APPLY_TERM:
    v = map_bvapply_to_bv(ctx, bvapply_term_desc(terms, t));
    u = translate_thvar_to_eterm(ctx, t, v);
    break;

  default:
    code = INTERNAL_ERROR;
    goto abort;
  }

  map_term_to_occ(&ctx->trans, t, u);
  return u;

 abort:
  longjmp(ctx->env, code);
}





/*
 * TOP-LEVEL ASSERTIONS
 */

/*
 * Assert a top-level equality t
 * - must be called after t has been mapped to true or false
 */
static void assert_toplevel_eq(context_t *ctx, term_t t) {
  term_table_t *terms;
  eq_term_t *eq;
  occ_t u1, u2;
  literal_t l1, l2;
  bool tt;
  
  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  eq = eq_term_desc(terms, t);
  if (is_boolean_eq(terms, eq)) {
    l1 = internalize_to_literal(ctx, eq->left);
    l2 = internalize_to_literal(ctx, eq->right);
    assert_iff(&ctx->gate_manager, l1, l2, tt);
  } else {
    u1 = internalize_to_eterm(ctx, eq->left);
    u2 = internalize_to_eterm(ctx, eq->right);
    if (tt) {
      egraph_assert_eq_axiom(ctx->egraph, u1, u2);
    } else {
      egraph_assert_diseq_axiom(ctx->egraph, u1, u2);
    }
  }
}


/*
 * Assertions (distinct a[0...n-1]) == tt
 * when a[0] ... a[n-1] are arithmetic or bitvector variables.
 */
static void assert_arith_distinct(context_t *ctx, uint32_t n, thvar_t *a, bool tt) {
  literal_t l;

  l = make_arith_distinct(ctx, n, a);
  if (! tt) {
    l = not(l);
  }
  add_unit_clause(ctx->core, l);
}

static void assert_bv_distinct(context_t *ctx, uint32_t n, thvar_t *a, bool tt) {
  literal_t l;

  l = make_bv_distinct(ctx, n, a);
  if (! tt) {
    l = not(l);
  }
  add_unit_clause(ctx->core, l);
}




/*
 * Top-level distinct: t must be (distinct t_0 .... t_{n-1})
 * and it must be mapped to true or false.
 */
static void assert_toplevel_distinct(context_t *ctx, term_t t) {
  term_table_t *terms;
  distinct_term_t *distinct;
  int32_t i, n;
  int32_t *a;
  bool tt;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  distinct = distinct_term_desc(terms, t);

  n = distinct->nargs;
  a = alloc_istack_array(&ctx->istack, n);

  if (context_has_egraph(ctx)) {
    // assert directly into the egraph
    for (i=0; i<n; i++) {
      a[i] = internalize_to_eterm(ctx, distinct->arg[i]);
    }

    if (tt) {
      egraph_assert_distinct_axiom(ctx->egraph, n, a);
    } else {
      egraph_assert_notdistinct_axiom(ctx->egraph, n, a);
    }

  } else if (is_arithmetic_term(ctx->terms, distinct->arg[0])) {
    // translate to arithmetic then assert
    for (i=0; i<n; i++) {
      a[i] = internalize_to_arith(ctx, distinct->arg[i]);
    }
    assert_arith_distinct(ctx, n, a, tt);

  } else if (is_bitvector_term(ctx->terms, distinct->arg[0])) {
    // translate to bitvectors then assert
    for (i=0; i<n; i++) {
      a[i] = internalize_to_bv(ctx, distinct->arg[i]);
    }
    assert_bv_distinct(ctx, n, a, tt);    

  } else {
    longjmp(ctx->env, UF_NOT_SUPPORTED);
  }

  free_istack_array(&ctx->istack, a);
}



/*
 * Top-level (apply ... ) predicate
 */
static void assert_toplevel_apply(context_t *ctx, term_t t) {
  term_table_t *terms;
  app_term_t *app;
  uint32_t i, n;
  occ_t f, *a;
  bool tt;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));

  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  app = app_term_desc(terms, t);
  f = internalize_to_eterm(ctx, app->fun);
  n = app->nargs;
  a = alloc_istack_array(&ctx->istack, n);
  for (i=0; i<n; i++) {
    a[i] = internalize_to_eterm(ctx, app->arg[i]);
  }
  if (tt) {
    egraph_assert_pred_axiom(ctx->egraph, f, n, a);
  } else {
    egraph_assert_notpred_axiom(ctx->egraph, f, n, a);
  }
  free_istack_array(&ctx->istack, a);
}

/*
 * Top-level (select ...) assertion
 */
static void assert_toplevel_select(context_t *ctx, term_t t) {
  term_table_t *terms;
  select_term_t *select;
  occ_t u;
  bool ff;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  ff = term_mapped_to_false(&ctx->trans, t);
  terms = ctx->terms;
  select = select_term_desc(terms, t);
  u = map_select_to_eterm(ctx, select, bool_type(ctx->types));
  if (ff) {
    u = opposite_occ(u);
  }
  egraph_assert_axiom(ctx->egraph, u);
}


/*
 * Arithmetic atoms
 */
static void assert_toplevel_aritheq(context_t *ctx, term_t t) {
  term_table_t *terms;
  polynomial_t *p, *q;
  bool tt;
  term_t u;
  int32_t x;
  thvar_t v;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  p = arith_atom_desc(terms, t);

  if (tt && context_arith_elim_enabled(ctx)) {
    // try to eliminate a variable of p
    u = try_poly_substitution(ctx, p);
    if (u != NULL_TERM) {
      assert(is_unassigned_var(ctx, u));
      x = term_theory_var(terms, u);

      /*
       * Replace u by its root variable (in the partition table)
       */
      u = find_term_root(ctx, u);
      assert(is_unassigned_var(ctx, u) && subst_candidate(ctx, u) == NULL_TERM);

      /*
       * To prevent cycles (occurs check):
       * - first internalize all variables of p except x
       * - if u is still unassigned after that, there are no cycles
       *   so we can perform the substitution.
       */
      internalize_subst_polynomial(ctx, p, x);
      if (is_unassigned_var(ctx, u)) {
	build_poly_substitution(ctx, p, x);
	q = monarray_getpoly(ctx->monarray, p->nterms - 1);
	v = map_arith_term_to_arith(ctx, q);
#if TRACE
	printf("---> toplevel equality: ");
	print_termdef(stdout, t);
	printf(" simplified to ");
	print_arith_var(stdout, term_theory_var(terms, u));
	printf(" := ");
	print_polynomial(stdout, q);
	printf("\n");
#endif
	map_term_to_arithvar(&ctx->trans, u, v);
	free_polynomial(q);

      } else {
	/*
	 * Some variables of p other than x depend on t so the substitution
	 * is not valid.
	 */
	internalize_arithvar(ctx, x); // i.e., finish internalization of p
	ctx->arith->assert_eq_axiom(ctx->arith_solver, p, &ctx->trans.arith_map, true);
      }

      return;
    }
  }

  // default
  internalize_polynomial(ctx, p);
  ctx->arith->assert_eq_axiom(ctx->arith_solver, p, &ctx->trans.arith_map, tt);
}


static void assert_toplevel_arithge(context_t *ctx, term_t t) {
  term_table_t *terms;
  polynomial_t *p;
  bool tt;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  p = arith_atom_desc(terms, t);
  internalize_polynomial(ctx, p);
  ctx->arith->assert_ge_axiom(ctx->arith_solver, p, &ctx->trans.arith_map, tt);
}


// binary equality (t1 == t2)
static void assert_toplevel_arith_bineq(context_t *ctx, term_t t) {
  term_table_t *terms;
  arith_bineq_t *e;
  occ_t u, v;
  thvar_t x, y;
  bool tt;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  e = arith_bineq_desc(terms, t);

  if (context_has_egraph(ctx)) {
    // Assert (u != v) or (u == v) in the egraph
    u = internalize_to_eterm(ctx, e->left);
    v = internalize_to_eterm(ctx, e->right);
    if (tt) {
      egraph_assert_eq_axiom(ctx->egraph, u, v);
    } else {
      egraph_assert_diseq_axiom(ctx->egraph, u, v);
    }

  } else {

    // Direct assertion in the arithmetic solver
    x = internalize_to_arith(ctx, e->left);
    y = internalize_to_arith(ctx, e->right);

    ctx->arith->assert_vareq_axiom(ctx->arith_solver, x, y, tt);
  }
}


/*
 * Bit-vector axioms
 */
static void assert_toplevel_bveq(context_t *ctx, term_t t) {
  term_table_t *terms;
  bv_atom_t *a;
  bool tt;
  occ_t u, v;
  thvar_t x, y;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  a = bvatom_desc(terms, t);

  if (false && context_has_egraph(ctx)) {
    // Assert (u != v) or (u == v) in the egraph
    u = internalize_to_eterm(ctx, a->left);
    v = internalize_to_eterm(ctx, a->right);
    if (tt) {
      egraph_assert_eq_axiom(ctx->egraph, u, v);
    } else {
      egraph_assert_diseq_axiom(ctx->egraph, u, v);
    }
    
  } else {
    x = internalize_to_bv(ctx, a->left);
    y = internalize_to_bv(ctx, a->right);
    ctx->bv->assert_eq_axiom(ctx->bv_solver, x, y, tt);
  }
}


static void assert_toplevel_bvge(context_t *ctx, term_t t) {
  term_table_t *terms;
  bv_atom_t *a;
  bool tt;
  thvar_t x, y;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  a = bvatom_desc(terms, t);
  x = internalize_to_bv(ctx, a->left);
  y = internalize_to_bv(ctx, a->right);
  ctx->bv->assert_ge_axiom(ctx->bv_solver, x, y, tt);
}


static void assert_toplevel_bvsge(context_t *ctx, term_t t) {
  term_table_t *terms;
  bv_atom_t *a;
  bool tt;
  thvar_t x, y;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  a = bvatom_desc(terms, t);
  x = internalize_to_bv(ctx, a->left);
  y = internalize_to_bv(ctx, a->right);
  ctx->bv->assert_sge_axiom(ctx->bv_solver, x, y, tt);
}


/*
 * Top-level atom
 */
static void assert_toplevel_atom(context_t *ctx, term_t t) {
  term_table_t *terms;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  terms = ctx->terms;
  switch (term_kind(terms, t)) {
  case EQ_TERM:
    assert_toplevel_eq(ctx, t);
    break;
  case APP_TERM:
    assert_toplevel_apply(ctx, t);
    break;
  case SELECT_TERM:
    assert_toplevel_select(ctx, t);
    break;
  case DISTINCT_TERM:
    assert_toplevel_distinct(ctx, t);
    break;
  case ARITH_EQ_ATOM:
    assert_toplevel_aritheq(ctx, t);
    break;
  case ARITH_GE_ATOM:
    assert_toplevel_arithge(ctx, t);
    break;
  case ARITH_BINEQ_ATOM:
    assert_toplevel_arith_bineq(ctx, t);
    break;
  case BV_EQ_ATOM:
    assert_toplevel_bveq(ctx, t);
    break;
  case BV_GE_ATOM:
    assert_toplevel_bvge(ctx, t);
    break;
  case BV_SGE_ATOM:
    assert_toplevel_bvsge(ctx, t);
    break;

  default:
    assert(false);
    longjmp(ctx->env, INTERNAL_ERROR);
  }
}


/*
 * TOP-LEVEL FORMULAS
 */

static void assert_toplevel_formula(context_t *ctx, term_t t);

/*
 * Add a top-level assertion of the form (t == tt):
 * - i.e., assert t if tt is true, assert (not t) if tt is false
 * - this function maps t to tt (if t is not already mapped to something), 
 *   then it recursively calls assert_toplevel_formula
 *
 * This is used when toplevel formulas are reduced to some of their
 * subterms, in ways that flattening did not detect. 
 * Examples
 *   (ite c t1 t2) ---> t1 if c reduces to true in a theory solver
 */
static void assert_toplevel_subterm(context_t *ctx, term_t t, bool tt) {
  int32_t x;
  literal_t l;

  x = code_of_term(&ctx->trans, t);
  if (x == nil) {
    map_term_to_bool(&ctx->trans, t, tt);
    // BUG: here. The preconditions of assert_toplevel are not always
    // satisfied here
    assert_toplevel_formula(ctx, t);
  } else {
    l = internalize_to_literal(ctx, t);
    if (tt) {
      add_unit_clause(ctx->core, l);
    } else {
      add_unit_clause(ctx->core, not(l));
    }
  }
}

/*
 * If or-flattening is enabled flatten then add a clause,
 * otherwise just add a clause.
 */
static void assert_toplevel_or(context_t *ctx, term_t t) {
  term_table_t *terms;
  bool tt;
  ivector_t *v;
  or_term_t *or;
  uint32_t i, n;
  literal_t *a;


  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  or = or_term_desc(terms, t);

  if (tt) {
    /*
     * Flatten then add a clause (or skip flattening)
     */
    if (context_flatten_or_enabled(ctx)) {
      /*
       * Flatten into aux_vector v
       */
      v = &ctx->aux_vector;
      assert(v->size == 0);
      flatten_or(ctx, v, or);

      // copy content of vector v into the array stack
      n = v->size;
      a = alloc_istack_array(&ctx->istack, n);
      for (i=0; i<n; i++) {
	a[i] = v->data[i];
      }
      ivector_reset(v);

      // internalize elements in a
      for (i=0; i<n; i++) {
	a[i] = internalize_to_literal(ctx, a[i]);
      }

    } else {
      /*
       * No flattening
       */
      n = or->nargs;
      a = alloc_istack_array(&ctx->istack, n);
      for (i=0; i<n; i++) {
	a[i] = internalize_to_literal(ctx, or->arg[i]);
      }
    }

    // In both cases, a[0... n-1] contains the clause
    add_clause(ctx->core, n, a);
    free_istack_array(&ctx->istack, a); 

  } else {
    /*
     * Assert not (or t1 .... t_n): assert (not t1) ... (not t_n)
     */
    n = or->nargs;
    for (i=0; i<n; i++) {
      assert_toplevel_subterm(ctx, or->arg[i], false);
    }    
  }
}


#if 0
// NOT USED ANYMORE: ALWAYS USE assert_toplevel_eq 
static void assert_toplevel_iff(context_t *ctx, term_t t) {
  term_table_t *terms;
  eq_term_t *eq;
  literal_t l1, l2;
  bool tt;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  eq = eq_term_desc(terms, t);
  l1 = internalize_to_literal(ctx, eq->left);
  l2 = internalize_to_literal(ctx, eq->right);
  assert_iff(&ctx->gate_manager, l1, l2, tt);
}
#endif

static void assert_toplevel_ite(context_t *ctx, term_t t) {
  term_table_t *terms;
  ite_term_t *ite;
  literal_t l1, l2, l3;
  bool tt;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));

  tt = term_mapped_to_true(&ctx->trans, t);
  terms = ctx->terms;
  ite = ite_term_desc(terms, t);
  l1 = internalize_to_literal(ctx, ite->cond);
  if (l1 == true_literal) {
    assert_toplevel_subterm(ctx, ite->then_arg, tt);
  } else if (l1 == false_literal) {
    assert_toplevel_subterm(ctx, ite->else_arg, tt);
  } else {
    l2 = internalize_to_literal(ctx, ite->then_arg);
    l3 = internalize_to_literal(ctx, ite->else_arg);
    assert_ite(&ctx->gate_manager, l1, l2, l3, tt);
  }
}

static void assert_toplevel_not(context_t *ctx, term_t t) {
  term_t u;
  bool tt;

  assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));

  tt = term_mapped_to_true(&ctx->trans, t);
  u = not_term_arg(ctx->terms, t);
  assert_toplevel_subterm(ctx, u, !tt);
}

static void assert_toplevel_formula(context_t *ctx, term_t t) {
  term_table_t *terms;

  terms = ctx->terms;
  switch (term_kind(terms, t)) {
  case UNINTERPRETED_TERM:
    // nothing to do: t is mapped to true or false 
    assert(term_mapped_to_true(&ctx->trans, t) || term_mapped_to_false(&ctx->trans, t));
    break;
  case EQ_TERM:
    // Can't assume that this is an equality between boolean terms
    // since assert_toplevel_formula may be called from assert_toplevel_subterm.
    //    assert(is_boolean_eq(terms, eq_term_desc(terms, t)));
    //    assert_toplevel_iff(ctx, t);
    assert_toplevel_eq(ctx, t);
    break;
  case ITE_TERM:
    assert(is_boolean_ite(terms, ite_term_desc(terms, t)));
    assert_toplevel_ite(ctx, t);
    break;
  case OR_TERM:
    assert_toplevel_or(ctx, t);
    break;
  case NOT_TERM:
    assert_toplevel_not(ctx, t);
    break;
  case APP_TERM:
    assert_toplevel_apply(ctx, t);
    break;
  case SELECT_TERM:
    assert_toplevel_select(ctx, t);
    break;
  case DISTINCT_TERM:
    assert_toplevel_distinct(ctx, t);
    break;
  case ARITH_EQ_ATOM:
    assert_toplevel_aritheq(ctx, t);
    break;
  case ARITH_GE_ATOM:
    assert_toplevel_arithge(ctx, t);
    break;
  case ARITH_BINEQ_ATOM:
    assert_toplevel_arith_bineq(ctx, t);
    break;
  case BV_EQ_ATOM:
    assert_toplevel_bveq(ctx, t);
    break;
  case BV_GE_ATOM:
    assert_toplevel_bvge(ctx, t);
    break;
  case BV_SGE_ATOM:
    assert_toplevel_bvsge(ctx, t);
    break;
  default:
    assert(false);
    longjmp(ctx->env, INTERNAL_ERROR);
  }
}



/***************************
 *  FULL INTERNALIZATION   *
 **************************/

/*
 * Full internalization: to be called after flattening.
 * - ctx.top_eq must contain all top-level, non-boolean, true equalities
 * - ctx.top_atoms must contain all other atoms 
 * - ctx.top_formuals must contain all non-atomic formulas that 
 *   can't be flattened 
 * - for each t in any of these vectors, ctx.trans->internal[t] must be
 *   either true_occ or false_occ.
 *
 * Return code:
 * - TRIVIALLY_UNSAT if an inconsistency is detected
 * - CTX_NO_ERROR if it all worked and the problem is not trivially unsat
 * - a negative code if an error is encountered
 */
static int32_t internalize(context_t *ctx) {
  int code;
  uint32_t i, n;
  ivector_t *v;

  code = setjmp(ctx->env);
  if (code == 0) {
    /*
     * Notify the solver(s)
     */
    internalization_start(ctx->core);

    /*
     * Assert all top-level equalities first
     */
    v = &ctx->top_eqs;
    n = v->size;
    for (i=0; i<n; i++) {
#if TRACE
      if (context_dump_enabled(ctx)) {
	printf("\n=== eq %"PRIu32" ===\n", i);
	print_termdef(stdout, v->data[i]);
	printf("\n");
      }
#endif
      assert_toplevel_atom(ctx, v->data[i]);
      if (true && ! base_propagate(ctx->core)) {
	return TRIVIALLY_UNSAT;
      }
    }

    if (false && ! base_propagate(ctx->core)) {
      return TRIVIALLY_UNSAT;
    }

    /*
     * Assert all other top-level atoms
     */
    v = &ctx->top_atoms;
    n = v->size;
    for (i=0; i<n; i++) {
#if TRACE
      if (context_dump_enabled(ctx)) {
	if (term_mapped_to_true(&ctx->trans, v->data[i])) {
	  printf("\n=== atom %"PRIu32" (true) ===\n", i);
	} else {
	  printf("\n=== atom %"PRIu32" (false) ===\n", i);
	}
	print_termdef(stdout, v->data[i]);
	printf("\n");
      }
#endif
      assert_toplevel_atom(ctx, v->data[i]);
      if (true && ! base_propagate(ctx->core)) {
	return TRIVIALLY_UNSAT;
      }
    }

    if (false && ! base_propagate(ctx->core)) {
      return TRIVIALLY_UNSAT;
    }

    /* 
     * Assert the rest
     */
    v = &ctx->top_formulas;
    n = v->size;
    for (i=0; i<n; i++) {
#if TRACE
      if (context_dump_enabled(ctx)) {
	printf("\n=== formula %"PRIu32" out of %"PRIu32" ===\n", i, n);
	print_termdef(stdout, v->data[i]);
	printf("\n");
      }
#endif
      assert_toplevel_formula(ctx, v->data[i]);
    }
    if (! base_propagate(ctx->core)) {
      return TRIVIALLY_UNSAT;
    }

    return CTX_NO_ERROR;
    
  } else {
    /*
     * Return from longjmp(ctx->env, code)
     */
    reset_istack(&ctx->istack);
    return code;
  }
}



/****************
 *  ASSERTIONS  *
 ***************/

/*
 * Assert the formulas f[0] ... f[n-1]
 * - ctx must be in the IDLE state
 * - return code: 
 *   CTX_NO_ERROR if all formulas can be internalized
 *   TRIVIALLY_UNSAT if the internalization detects a contradiction
 *   a negative error code otherwise
 */
static int32_t context_process_formulas(context_t *ctx, uint32_t n, term_t *f) {
  int32_t code;
  uint32_t i;

  // Flatten the formulas
  assert(tree_stack_empty(&ctx->stack));
  for (i=0; i<n; i++) {
    tree_stack_push_term(&ctx->stack, ctx->terms, f[i]);
  }
  code = flatten_assertions(ctx, &ctx->stack);

#if TRACE
  printf("\n=== SUMMARY ===\n");
  printf("  %"PRIu32" eliminated eqs\n", partition_num_eqs(&ctx->partition));
  printf("  %"PRIu32" subst candidates\n", ctx->subst_eqs.size);
  printf("  %"PRIu32" equalities\n", ctx->top_eqs.size);
  printf("  %"PRIu32" atoms\n", ctx->top_atoms.size);
  printf("  %"PRIu32" formulas\n", ctx->top_formulas.size);
#endif

#if TRACE
  if (context_dump_enabled(ctx)) {
    printf("\n---> code = %"PRId32"\n", code);
    printf("\n---> substitutions:\n");
    print_partition(stdout, ctx);
    print_substitutions(stdout, ctx);
  }
#endif

  if (code != CTX_NO_ERROR) return code;

  // Optional/heuristic processing
  switch(ctx->arch) {
  case CTX_ARCH_EG:
    if (context_eq_abstraction_enabled(ctx)) {
      code = analyze_uf(ctx);
      if (code != CTX_NO_ERROR) return code;
    }
    break;

  case CTX_ARCH_AUTO_IDL:
    code = analyze_diff_logic(ctx, true);
    if (code != CTX_NO_ERROR) return code;
    create_auto_idl_solver(ctx);
    break;

  case CTX_ARCH_IFW:
    code = analyze_diff_logic(ctx, true);
    if (code != CTX_NO_ERROR) return code;
    break;

  case CTX_ARCH_AUTO_RDL:
    code = analyze_diff_logic(ctx, false);
    if (code != CTX_NO_ERROR) return code;
    create_auto_rdl_solver(ctx);
    break;

  case CTX_ARCH_RFW:
    code = analyze_diff_logic(ctx, false);
    if (code != CTX_NO_ERROR) return code;
    break;

  default:
    break;
  }
  
  return internalize(ctx); 
}



/*
 * Assert all formulas f[0] ... f[n-1]
 * - return a negative code on error
 * - return TRIVIALLY_UNSAT if an inconsistency is detected
 * - return CTX_NO_ERROR (0) otherwise
 */
int32_t assert_formulas(context_t *ctx, uint32_t n, term_t *f) {
  int32_t code;

  assert(ctx->arch == CTX_ARCH_AUTO_IDL || 
	 ctx->arch == CTX_ARCH_AUTO_RDL ||
	 smt_status(ctx->core) == STATUS_IDLE);

  code = context_process_formulas(ctx, n, f);
  if (code == TRIVIALLY_UNSAT && 
      ctx->arch != CTX_ARCH_AUTO_IDL &&
      ctx->arch != CTX_ARCH_AUTO_RDL &&
      smt_status(ctx->core) != STATUS_UNSAT) {
    // force UNSAT in the core too
    // BUG FIX: we can't do that in AUTO_IDL/AUTO_RDL modes since
    // the core is not initialized yet.
    add_empty_clause(ctx->core);
    ctx->core->status = STATUS_UNSAT;
  }

  return code;
}


/*
 * Assert formula f
 */
int32_t assert_formula(context_t *ctx, term_t f) {
  return assert_formulas(ctx, 1, &f);
}








/************************************
 *  UTILITY FOR MODEL CONSTRUCTION  *
 ***********************************/

/*
 * Check whether t is a replaced by a term v via the substitution table
 * or partition. Return v if there's such a term. Return NULL_TERM otherwise.
 */
term_t context_find_term_subst(context_t *ctx, term_t t) {
  term_t r, v;

  v = NULL_TERM;
  if (term_kind(ctx->terms, t) == UNINTERPRETED_TERM) {
    r = find_term_root(ctx, t);
    v = subst_candidate(ctx, r);
    if (r != t && v == NULL_TERM) {
      v = r;
    }
  }

  return v;
}


