/*
 * CONTEXT
 *
 * Initial version (for UF only) done in October 2007.
 *
 * New design: started March 18, 2008.
 * - updated to use the new smt_core + egraph + solver architecture
 * - includes support for push and pop
 * - intended to be more flexible and customizable than Yices 1:
 *   - context can support a subset of theories
 *   - solvers can have different implementations
 *
 * Dec. 30, 2008: Removed CURRYING option
 */

/*
 * Main components of the context
 * ------------------------------
 *
 * Supported features:
 * - One can create a context that does not support all features, this
 *   should make it more efficient.
 * - Optional features are:
 *   - support for several calls to assert/check
 *   - support for push/pop
 *   - support for clean interrupts (see smt_core)
 * - This gives four possible modes of use:
 *   1) ONECHECK (basic solver):
 *      Assert formulas, check whether they are satisfiable, exit
 *   2) MULTICHECK:
 *      Can repeat "assert formulas, check satisfiability" as long
 *      as the status is not UNSAT.
 *   3) PUSHPOP
 *      Similar to Yices 1 model: push create a backtrack point,
 *      pop returns to the previous backtrack point/
 *   4) CLEANINTERRUPT
 *      Like pushpop + support for restoring the context to a clean
 *      state after the search is interrupted (i.e., better for 
 *      interactive use).
 
 * Solvers and architecture description:
 * - Each context includes a core Boolean SAT solver, combined with one or more 
 *   theory solvers. We want to support different combinations of solvers,
 *   and possibly different implemantations for some solvers (e.g., different
 *   arihtmetic solvers).
 * - There are at most four solvers present: 
 *   - uf_solver: egraph
 *   - arith_solver: a solver for whatever variant of arithmetic is in use
 *   - bv_solver: solver for bitvector theory
 *   - fun_solver: solver for array/function theory
 * - The solver combination to use is defined by an architecture description
 *   parameter given when the context is initialized. The architecture specifies 
 *   which solvers are present (and for some solver, which implementation is used).
 * - Additional flag: defines whether quantifiers are supported or not
 * - The architecture description also determines which theories are supported.
 *
 * Delayed architecture selection:
 * - In ONECHECK mode, it can be useful to select the solvers after the
 *   formulas are known. We use a special architecture code "auto-select" to 
 *   support this.
 *

 * Input tables:
 * - global term and type tables for Yices terms 
 * - the variable managers for arithmetic and bitvector variables
 * These tables are where the terms are created by term_api.c
 *
 * Internalization:
 * - Tables and buffers for converting the input terms to the internal form
 *   required by the theory solvers.
 */

#ifndef __CONTEXT_H
#define __CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>

#include "tree_stack.h"
#include "int_stack.h"
#include "int_hash_map.h"
#include "int_bv_sets.h"
#include "translation.h"

#include "terms.h"
#include "gates_manager.h"
#include "smt_core.h"
#include "egraph.h"
#include "models.h"



/*************************
 *  COMPILE-TIME OPTION  *
 ************************/

/*
 * String set depending on the USE_REDUCE flag
 */
extern char *reduce_compile_option;



/************************
 *  OPTIONAL FUNCTIONS  *
 ***********************/

/*
 * Bit mask for specifying which features are supported
 */
#define MULTICHECKS_OPTION_MASK 0x1
#define PUSHPOP_OPTION_MASK     0x2
#define CLEANINT_OPTION_MASK    0x4


/*
 * Possible modes
 */
typedef enum {
  CTX_MODE_ONECHECK,
  CTX_MODE_MULTICHECKS,
  CTX_MODE_PUSHPOP,
  CTX_MODE_INTERACTIVE,
} context_mode_t;

#define NUM_MODES (CTX_MODE_INTERACTIVE+1)


/*
 * More bit masks for enabling/disabling simplification
 * - VARELIM eliminate variables (via substitution)
 * - FLATTENOR rewrites (or ... (or ...) ...) into a single (or ....)
 * - FLATTENDISEQ rewrite arithmetic disequality 
 *      (not (p == 0)) into (or (not (p >= 0)) (not (-p >= 0)))
 *   FLATTENDISEQ requires FLATTENOR to be enabled
 * - EQABSTRACT enables the abstraction-based equality learning heuristic
 * - ARITHELIM enables variable elimination in arithmetic
 * - KEEP_ITE: keep if-then-else terms in the egraph
 *
 * Options passed to the simplex solver when it's created
 * - EAGER_LEMMAS
 * - ENABLE_ICHECK
 */
#define VARELIM_OPTION_MASK      0x10
#define FLATTENOR_OPTION_MASK    0x20
#define FLATTENDISEQ_OPTION_MASK 0x40
#define EQABSTRACT_OPTION_MASK   0x80
#define ARITHELIM_OPTION_MASK    0x100
#define KEEP_ITE_OPTION_MASK     0x200
#define BVARITHELIM_OPTION_MASK  0x400

#define PREPROCESSING_OPTIONS_MASK \
 (VARELIM_OPTION_MASK|FLATTENOR_OPTION_MASK|FLATTENDISEQ_OPTION_MASK|\
  EQABSTRACT_OPTION_MASK|ARITHELIM_OPTION_MASK|KEEP_ITE_OPTION_MASK|BVARITHELIM_OPTION_MASK)

#define SPLX_EGRLMAS_OPTION_MASK  0x10000
#define SPLX_ICHECK_OPTION_MASK   0x20000

// FOR TESTING
#define DUMP_OPTION_MASK        0x80000000



/***************************************
 *  ARCHITECTURES/SOLVER COMBINATIONS  *
 **************************************/

/*
 * An architecture is specified by one of the following codes
 * - each architecture defines a set of solvers (and the supported theories)
 * - the special "auto" codes can be used if mode is CTX_MODE_ONECHECK
 *
 * Note: these are anticipated/planned architectures. Most don't exist yet.
 * Some may be removed and other added.
 */
typedef enum {
  CTX_ARCH_NOSOLVERS,    // core only
  CTX_ARCH_EG,           // egraph
  CTX_ARCH_SPLX,         // simplex
  CTX_ARCH_IFW,          // integer floyd-warshall
  CTX_ARCH_RFW,          // real floyd-warshall
  CTX_ARCH_BV,           // bitvector solver
  CTX_ARCH_EGFUN,        // egraph+array/function theory
  CTX_ARCH_EGSPLX,       // egraph+simplex
  CTX_ARCH_EGBV,         // egraph+bitvector solver
  CTX_ARCH_EGFUNSPLX,    // egraph+fun+simplex
  CTX_ARCH_EGFUNBV,      // egraph+fun+bitvector
  CTX_ARCH_EGFUNSPLXBV,  // all solvers (should be the default)

  CTX_ARCH_AUTO_IDL,     // either simplex or integer floyd-warshall
  CTX_ARCH_AUTO_RDL,     // either simplex or real floyd-warshall
} context_arch_t;


#define NUM_ARCH (CTX_ARCH_AUTO_RDL+1)


/*
 * Supported theories (including arithmetic and function theory variants)
 * - a 32bit theories word indicate what's supported
 * - each bit selects a theory
 * The theory words is derived from the architecture descriptor
 */
#define UF_MASK        0x1
#define BV_MASK        0x2
#define IDL_MASK       0x4
#define RDL_MASK       0x8
#define LIA_MASK       0x10
#define LRA_MASK       0x20
#define LIRA_MASK      0x40
#define NLIRA_MASK     0x80     // non-linear arithmeatic
#define FUN_UPDT_MASK  0x100
#define FUN_EXT_MASK   0x200
#define QUANT_MASK     0x400

// arith means all variants of linear arithmetic are supported
#define ARITH_MASK (LIRA_MASK|LRA_MASK|LIA_MASK|RDL_MASK|IDL_MASK)

// nlarith_mask means non-linear arithmetic is supported too
#define NLARITH_MASK (NLIRA_MASK|ARITH_MASK)

// fun means both function theories are supported
#define FUN_MASK   (FUN_UPDT_MASK|FUN_EXT_MASK)

// all theories, except non-linear arithmetic
#define ALLTH_MASK (UF_MASK|BV_MASK|ARITH_MASK|FUN_MASK)




/***************
 *  PARTITION  *
 **************/

/*
 * Union-find data structure defines a substitution mapping uninterpreted terms to 
 * uninterpreted or constant terms
 * - the data structure build a set of disjoint classes.
 * - each class contains terms known to be equal
 * - all terms in a class, except possibly the root is uninterpreted
 * - each class has a type, which is the inf of the types of the class elements
 *   (e.g. if x has tuple-type (int, real) and y has tuple-typle (real, int) then
 *    the class {x, y} has type (int, int)).
 * The substitution for a term 'x' is the root of its class.
 *
 * For each term t in the union-find strucure:
 * - parent[t] = term id
 * - type[t] = type label
 * - rank[t] = an 8bit value
 * - t is the root of its class if we have parent[t] == t, and then type[t]
 *   is the type of the class.
 * - t is not in a class if  parent[t] is NULL_TERM
 *
 * Other components: 
 * - size = size of all arrays
 * - nelems = index of the largest term added + 1
 * - types = pointer to the type table where all types are defined
 */
typedef struct partition_s {
  uint32_t nelems;
  uint32_t size;
  term_t *parent;
  type_t *type;
  uint8_t *rank;
  type_table_t *types;
} partition_t;

#define DEF_PARTITION_SIZE 100
#define MAX_PARTITION_SIZE (UINT32_MAX/sizeof(int32_t))






/**************************
 *  ARITHMETIC INTERFACE  *
 *************************/

/*
 * An arithmetic solver must implement the following internalization functions:
 *
 * 1) thvar_t create_var(void *solver, bool is_int)
 *    - this must return the index of a new arithmetic variable (no eterm attached)
 *    - if is_int is true, that variable must have integer type, 
 *      otherwise, it must be a real.
 *
 * 2) thvar_t create_poly(void *solver, polynomial_t *p, itable_t *arith_map)
 *    - must return a theory variable equal to p with variables renamed as 
 *      defined by arith_map
 *    - for every variable x in p, itable_get(arith_map, x) is a thvar of solver,
 *      returned by a previous call to create_var or create_poly
 *
 * 3) void attach_eterm(void *solver, thvar_t v, eterm_t t)
 *    - attach egraph term t to theory variable v
 *    - this function may be omitted for standalone solvers (no egraph is used in that case)
 *
 * NOTE: this function is also used by the egraph.
 *
 * 4) eterm_t eterm_of_var(void *solver, thvar_t v)
 *    - must return the eterm t attached to v (if any) or null_eterm if v has no term attached
 *    - this function may be omitted for standalone solvers (no egraph)
 *
 * NOTE: this function is also used by the egraph.
 *
 * 5) literal_t create_eq_atom(void *solver, polynomial_t *p, itable_t *arith_map)
 *    - must create the atom (p == 0) and return the corresponding literal
 *    - variables of p are renamed as defined by arith_map.
 *
 * 6) literal_t create_ge_atom(void *solver, polynomial_t *p, itable_t *arith_map)
 *    - must create the atom (p >= 0) 
 *
 * 7) literal_t create_vareq_atom(void *solver, thvar_t x, thvar_t y)
 *    - create the atom x == y
 *
 * 8) literal_t create_polyeq_atom(void *solver, thvar_t x, polynomial_t *p, itable_t *arith_map)
 *    - create the atom x == p and return the corresponding literal
 *    - variables of p must be renamed as defined by arith_map.
 *
 * Assertion of top-level axioms:
 *
 * 9) void assert_eq_axiom(void *solver, polynomial_t *p, itable_t *arith_map, bool tt)
 *    - if tt assert (p == 0) otherwise assert (p != 0)
 * 
 * 10) void assert_ge_axiom(void *solver, polynomial_t *p, itable_t *arith_map, bool tt)
 *    - if tt assert (p >= 0) otherwise assert (p < 0)
 *
 * 11) void assert_vareq_axiom(void *solver, thvar_t x, thvar_t y, bool tt)
 *     - if tt assert x == y, otherwise assert x != y
 *
 * 12) void assert_cond_vareq_axiom(void *solver, literal_t c, thvar_t x, thvar_t y)
 *     - assert (c implies x == y) as an axiom
 *     - this is used as part of the if-then-else conversion
 *
 * 13) void assert_cond_polyeq_axiom(void *solver, literal_t c, thvar_t x, polynomial_t *p, 
 *                                   itable_t *arith_map)
 *     - assert (c implies x = p) as an axiom
 *     - the variables of p must be renamed as defined by arith_map
 *     - this is used as part of the if-then-else conversion
 * 
 * Exception mechanism:
 * - when the solver is created/initialized it's given a pointer b to a jmp_buf internal to
 *   the context. If the solver fails in some way during internalization, it can call 
 *   longjmp(*b, error_code) to interrupt the internalization and return control to the 
 *   context. The valid error_codes are defined below.
 *
 * Model construction functions: used when the context_solver reaches SAT (or UNKNOWN)
 * 
 * First build_model is called. The solver must construct an assignment M from variables to 
 * rationals at that point. Then the context can query for the value of a variable x in M.
 * If the solver cannot assign a rational value to x, it can signal this when value_in_model
 * is called. M must not be changed until the context calls free_model.
 *
 * 14) void build_model(void *solver)
 *    - build a model M: maps variable to rationals.
 *  (or do nothing if the solver does not support model construction).
 *
 * 15) bool value_in_model(void *solver, thvar_t x, rational_t *v)
 *    - must return true copy the value of x in M into v if that value is available.
 *    - return false otherwise (e.g., if model construction is not supported by
 *    solver or x has an irrational value).
 *
 * 16) void free_model(void *solver)
 *    - notify solver that M is no longer needed.
 */

typedef thvar_t (*create_var_fun_t)(void *solver, bool is_int);
typedef thvar_t (*create_poly_fun_t)(void *solver, polynomial_t *p, itable_t *arith_map);
typedef void    (*attach_eterm_fun_t)(void *solver, thvar_t v, eterm_t t);
typedef eterm_t (*eterm_of_var_fun_t)(void *solver, thvar_t v);
typedef literal_t (*create_arith_atom_fun_t)(void *solver, polynomial_t *p, itable_t *arith_map);
typedef literal_t (*create_arith_vareq_atom_fun_t)(void *solver, thvar_t x, thvar_t y);
typedef literal_t (*create_arith_polyeq_atom_fun_t)(void *solver, thvar_t x, polynomial_t *p, itable_t *arith_map);

typedef void (*assert_arith_axiom_fun_t)(void *solver, polynomial_t *p, itable_t *arith_map, bool tt);
typedef void (*assert_arith_vareq_axiom_fun_t)(void *solver, thvar_t x, thvar_t y, bool tt);
typedef void (*assert_arith_cond_vareq_axiom_fun_t)(void* solver, literal_t c, thvar_t x, thvar_t y);
typedef void (*assert_arith_cond_polyeq_axiom_fun_t)(void* solver, literal_t c, thvar_t x,
						     polynomial_t *p, itable_t *arith_map);

typedef void (*build_model_fun_t)(void *solver);
typedef void (*free_model_fun_t)(void *solver);
typedef bool (*arith_val_in_model_fun_t)(void *solver, thvar_t x, rational_t *v);

typedef struct arith_interface_s {
  create_var_fun_t create_var;
  create_poly_fun_t create_poly;
  attach_eterm_fun_t attach_eterm;
  eterm_of_var_fun_t eterm_of_var;

  create_arith_atom_fun_t create_eq_atom;
  create_arith_atom_fun_t create_ge_atom;
  create_arith_vareq_atom_fun_t create_vareq_atom;
  create_arith_polyeq_atom_fun_t create_polyeq_atom;

  assert_arith_axiom_fun_t assert_eq_axiom;
  assert_arith_axiom_fun_t assert_ge_axiom;
  assert_arith_vareq_axiom_fun_t assert_vareq_axiom;
  assert_arith_cond_vareq_axiom_fun_t assert_cond_vareq_axiom;
  assert_arith_cond_polyeq_axiom_fun_t assert_cond_polyeq_axiom;

  build_model_fun_t build_model;
  free_model_fun_t free_model;
  arith_val_in_model_fun_t value_in_model;
} arith_interface_t;




/********************************
 *  BITVECTOR SOLVER INTERFACE  *
 *******************************/

/*
 * Term creation
 *
 * 1) thvar_t create_var(void *solver, uint32_t n)
 *    - must return the index of a new bitvector variable (no eterm attached)
 *    - n = number of bits of that variable
 *
 * 2) thvar_t create_const(void *solver, bvconst_term_t *const)
 *    - must return the index of a variable x equal to the constant const
 *    - const->nbits = number of bits
 *    - const->bits = array of uint32_t words (constant value)
 *
 * 3) thvar_t create_bvpoly(void *solver, bvarith_expr_t *poly, itable_t *bv_map)
 *    - must return a theory variable that represents poly with variables renamed as 
 *      defined by bv_map
 *    - for every primitive variable x of poly, itable_get(bv_map, x) is a
 *      theory variable that corresponds to x in the solver.
 *
 * 4) thvar_t create_bvlogic(void *solver, bvlogic_expr_t *expr, itable_t *bv_map)
 *    - must return a theory variable that represent expr, with variables renamed as
 *      defined by bv_map.
 *    - for every variable x of expr (occurring in the leaf nodes), itable_get(bv_map, x)
 *      is a theory variable that corresponds to x in solver.
 *
 * 5) thvar_t create_bvop(void *solver, bvop_t op, thvar_t x, thvar_t y)
 *    - create (op x y): x and y are two theory variables in solver
 *    - op is one of the bvop codes defined in terms.h
 *
 * 6) thvar_t create_bvite(void *solver, literal_t c, thvar_t x, thvar_t y) 
 *    - create (ite c x y): x and y are two theory variables in solver,
 *    and c is a literal in the core.
 *
 * 7) void attach_eterm(void *solver, thvar_t v, eterm_t t)
 *    - attach egraph term t to theory variable v of solver
 *
 * 8) eterm_t eterm_of_var(void *solver, thvar_t v)
 *    - return the egraph term attached to v in solver (or null_eterm
 *    if v has no egraph term attached).
 *
 * Atom creation:
 * 8) literal_t create_eq_atom(void *solver, thvar_t x, thvar_t y)
 * 9) literal_t create_ge_atom(void *solver, thvar_t x, thvar_t y)
 * 10) literal_t create_sge_atom(void *solver, thvar_t x, thvar_t y)
 *
 * Axiom assertion:
 * assert axiom if tt is true, the negation of axiom otherwise
 * 11) void assert_eq_axiom(void *solver, thvar_t x, thvar_t y, bool tt)
 * 12) void assert_ge_axiom(void *solver, thvar_t x, thvar_t y, bool tt)
 * 13) void assert_sge_axiom(void *solver, thvar_t x, thvar_t y, bool tt)
 *
 * Model construction: same functions as in arithmetic solvers
 * 14) void build_model(void *solver)
 * 15) void free_model(void *solver)
 * 16) bool value_in_model(void *solver, thvar_t x, bvconstant_t *v):
 *     must copy the value of x into v and return true. If model construction is 
 *     not supported or the value is not available, must return false.
 */

typedef thvar_t (*create_bvvar_fun_t)(void *solver, uint32_t n);
typedef thvar_t (*create_bvconst_fun_t)(void *solver, bvconst_term_t *bv);
typedef thvar_t (*create_bvpoly_fun_t)(void *solver, bvarith_expr_t *poly, itable_t *bv_map);
typedef thvar_t (*create_bvlogic_fun_t)(void *solver, bvlogic_expr_t *expr, itable_t *bv_map);
typedef thvar_t (*create_bvop_fun_t)(void *solver, bvop_t op, thvar_t x, thvar_t y);
typedef thvar_t (*create_bvite_fun_t)(void *solver, literal_t l, thvar_t x, thvar_t y);
typedef literal_t (*create_bvatom_fun_t)(void *solver, thvar_t x, thvar_t y);
typedef void (*assert_bvaxiom_fun_t)(void *solver, thvar_t x, thvar_t y, bool tt);

typedef bool (*bv_val_in_model_fun_t)(void *solver, thvar_t x, bvconstant_t *v);

typedef struct bvsolver_interface_s {
  create_bvvar_fun_t create_var;
  create_bvconst_fun_t create_const;
  create_bvpoly_fun_t create_bvpoly;
  create_bvlogic_fun_t create_bvlogic;
  create_bvop_fun_t create_bvop;
  create_bvite_fun_t create_bvite;
  attach_eterm_fun_t attach_eterm;
  eterm_of_var_fun_t eterm_of_var;

  create_bvatom_fun_t create_eq_atom;
  create_bvatom_fun_t create_ge_atom;
  create_bvatom_fun_t create_sge_atom;

  assert_bvaxiom_fun_t assert_eq_axiom;
  assert_bvaxiom_fun_t assert_ge_axiom;
  assert_bvaxiom_fun_t assert_sge_axiom;

  build_model_fun_t build_model;
  free_model_fun_t free_model;
  bv_val_in_model_fun_t value_in_model;
} bvsolver_interface_t;




/***************************************
 *  SOLVER FOR FUNCTION/ARRAY THEORY   *
 **************************************/

/*
 * 1) thvar_t create_var(void *solver, type_t tau)
 * 2) void attach_eterm(void *solver, thvar_t v, eterm_t t);
 */

typedef thvar_t (*create_fvar_fun_t)(void *solver, type_t tau);

typedef struct funsolver_interface_s {
  create_fvar_fun_t create_var;
  attach_eterm_fun_t attach_eterm;
} funsolver_interface_t;




/************************
 *  DIFF LOGIC PROFILE  *
 ***********************/

/*
 * For difference logic, we can use either the simplex solver
 * or a specialized Floyd-Warshall solver. The decision is 
 * based on the following parameters:
 * - density = number of atoms / number of variables
 * - sum_const = sum of the absolute values of all constants in the 
 *   difference logic atoms
 * - num_eqs = number of equalities (among all atoms)
 * dl_data stores the relevant data
 */
typedef struct dl_data_s {
  rational_t sum_const;
  uint32_t num_vars;
  uint32_t num_atoms;
  uint32_t num_eqs;
} dl_data_t;





/*************
 *  CONTEXT  *
 ************/

typedef struct context_s {
  // mode + architecture
  context_mode_t mode;
  context_arch_t arch;

  // theories flag
  uint32_t theories;

  // options flag
  uint32_t options;

  // base_level == number of calls to push
  uint32_t base_level;

  // core and theory solvers
  smt_core_t *core;
  egraph_t *egraph;
  void *arith_solver;
  void *bv_solver;
  void *fun_solver;

  // solver interfaces
  arith_interface_t *arith;
  bvsolver_interface_t *bv;
  funsolver_interface_t *fun;
  
  // input are all from the following tables (from yices_globals.h)
  type_table_t *types; 
  term_table_t *terms;
  arithvar_manager_t *arith_manager;
  bv_var_manager_t *bv_manager;
  object_store_t *bv_store;
  node_table_t *nodes;

  // internalization table + hash table for boolean gates 
  translator_t trans;
  gate_manager_t gate_manager;

  // data structures for formula simplification
  partition_t partition;
  int_hmap_t pseudo_subst;
  ivector_t subst_eqs;

  // formulas after flattening
  ivector_t top_eqs, top_atoms, top_formulas;

  // auxiliary buffers and structures for internalization
  tree_stack_t stack;
  int_stack_t istack;
  ivector_t aux_vector;

  monomial_t *monarray;
  uint32_t monarray_size;
  int_bvset_t *cache;
  int_hset_t *small_cache;

  // for simplification of bit-vector arithmetic expressions
  bvarith_buffer_t *bvbuffer;
  bvarith_buffer_t *bvbuffer2;

  // diff-logic profile
  dl_data_t *dl_profile;

  // auxiliary buffers for model construction
  rational_t aux;
  bvconstant_t bv_buffer;

  // for exception handling
  jmp_buf env;
} context_t;


/*
 * Default initial size of auxiliary buffers and vectors
 */
#define CTX_DEFAULT_AUX_SIZE 20
#define CTX_MAX_AUX_SIZE (UINT32_MAX/4)
#define CTX_DEFAULT_VECTOR_SIZE 10


/*
 * Default initial size for the solvers
 */
#define CTX_DEFAULT_CORE_SIZE 100


/*
 * Error and return codes used by internalization procedures:
 * - negative codes indicate an error
 * - these codes can also be used by the theory solvers to report exceptions.
 */
enum {
  TRIVIALLY_UNSAT = 1,   // simplifies to false
  CTX_NO_ERROR = 0,      // internalization succeeds/not solved
  INTERNAL_ERROR = -1,
  TYPE_ERROR = -2,
  FREE_VARIABLE_IN_FORMULA = -3,
  LOGIC_NOT_SUPPORTED = -4,
  UF_NOT_SUPPORTED = -5,
  ARITH_NOT_SUPPORTED = -6,
  BV_NOT_SUPPORTED = -7,
  FUN_NOT_SUPPORTED = -8,
  QUANTIFIERS_NOT_SUPPORTED = -9,
  FORMULA_NOT_IDL = -10,
  FORMULA_NOT_RDL = -11,
  NONLINEAR_NOT_SUPPORTED = -12,
  ARITHSOLVER_EXCEPTION = -13,
  BVSOLVER_EXCEPTION = -14,
};

#define NUM_INTERNALIZATION_ERRORS 15




/*********************************
 *  SEARCH PARAMETERS AND FLAGS  *
 ********************************/

/*
 * Possible branching heuristics:
 * - determine whether to assign the decision literal to true or false
 */
typedef enum {
  BRANCHING_DEFAULT,  // use internal smt_core cache
  BRANCHING_NEGATIVE, // branch l := false
  BRANCHING_POSITIVE, // branch l := true
  BRANCHING_THEORY,   // defer to the theory solver
  BRANCHING_TH_NEG,   // defer to theory solver for atoms, branch l := false otherwise
  BRANCHING_TH_POS,   // defer to theory solver for atoms, branch l := true otherwise
  BRANCHING_BV,       // experimental: default for atoms, random otherwise
} branch_t;



typedef struct param_s {
  /*
   * Restart heuristic: similar to PICOSAT or MINISAT
   *
   * If fast_restart is true: PICOSAT-style heuristic
   * - inner restarts based on c_threshold
   * - outer restarts based on d_threshold
   *
   * If fast_restart is false: MINISAT-style restarts
   * - c_threshold and c_factor are used
   * - d_threshold and d_threshold are ignored
   * - to get periodic restart set c_factor = 1.0
   */
  bool     fast_restart;
  uint32_t c_threshold;     // initial value of c_threshold
  uint32_t d_threshold;     // initial value of d_threshold
  double   c_factor;        // increase factor for next c_threshold
  double   d_factor;        // increase factor for next d_threshold

  /*
   * Clause-deletion heuristic
   * - initial reduce_threshold is max(r_threshold, num_prob_clauses * r_fraction)
   * - increase by r_factor on every outer restart provided reduce was called in that loop
   */
  uint32_t r_threshold;
  double   r_fraction;
  double   r_factor;

  /*
   * SMT Core parameters:
   * - randomness and var_decay are used by the branching heuristic
   *   the default branching mode uses the cached polarity in smt_core.
   * - clause_decay influence clause deletion
   * 
   * SMT Core caching of theory lemmas:
   * - if cache_tclauses is true, then the core internally turns 
   *   some theory lemmas into learned clauses
   * - for the core, a theory lemma is either a conflict reported by
   *   the theory solver or a theory implication
   * - a theory implication is considered for caching if it's involved
   *   in a conflict resolution
   * - parmeter tclause_size controls the lemma size: only theory lemmas 
   *   of size <= tclause_size are turned into learned clauses
   */
  double   var_decay;       // decay factor for variable activity
  float    randomness;      // probability of a random pick in select_unassigned_literal
  branch_t branching;       // branching heuristic
  float    clause_decay;    // decay factor for learned-clause activity
  bool     cache_tclauses;
  uint32_t tclause_size;

  /*
   * EGRAPH PARAMETERS
   *
   * Control of the Ackermann lemmas
   * - use_dyn_ack: if true, the dynamic ackermann heuristic is enabled for 
   *   non-boolean terms
   * - use_bool_dyn_ack: if true, the dynamic ackermann heuristic is enabled
   *   for boolean terms
   *
   * Limits to stop the Ackermann trick if too many lemmas are generated
   * - max_ackermann: limit for the non-boolean version
   * - max_boolackermann: limit for the boolem version
   *
   * The Ackermann clauses may require the construction of new equality
   * terms that were not present in the context. This is controlled by
   * the egraph's quota on auxiliary equalities. The quota is initialized
   * to max(aux_eq_ratio * n, aux_eq_quota) where n is the total
   * number of terms in the initial egraph.
   *
   * Control of interface equality generation: set a limit on
   * the number of interface equalities created per round.
   */
  bool     use_dyn_ack;
  bool     use_bool_dyn_ack;
  uint32_t max_ackermann;
  uint32_t max_boolackermann;
  uint32_t aux_eq_quota;
  double   aux_eq_ratio;
  uint32_t max_interface_eqs;


  /*
   * SIMPLEX PARAMETERS
   * - simplex_prop: if true enable propagation via propagation table
   * - max_prop_row_size: limit on the size of the propagation rows
   * - bland_threshold: threshold that triggers switching to Bland's rule
   * - integer_check_period: how often the integer solver is called
   */
  bool     use_simplex_prop;
  uint32_t max_prop_row_size;
  uint32_t bland_threshold;
  int32_t  integer_check_period;


  /*
   * ARRAY SOLVER PARAMETERS
   * - max_update_conflicts: limit on the number of update axioms generated
   *   per call to final_check
   * - max_extensionality: limit on the number of extensionality axioms 
   *   generated per call to reconcile_model
   */
  uint32_t max_update_conflicts;
  uint32_t max_extensionality;

} param_t;







/********************************
 *  INITIALIZATION AND CONTROL  *
 *******************************/

/*
 * Initialize ctx for the given mode and architecture
 * - qflag = false means quantifier-free variant
 * - qflag = true means quantifiers allowed
 * If arch is one of the ARCH_AUTO_... variants, then mode must be ONECHECK
 */
extern void init_context(context_t *ctx, context_mode_t mode, context_arch_t arch, bool qflag);


/*
 * Deletion
 */
extern void delete_context(context_t *ctx);


/*
 * Reset: remove all assertions
 */
extern void reset_context(context_t *ctx);


/*
 * Push and pop
 * - should not be used if the push_pop option is disabled
 */
extern void context_push(context_t *ctx);
extern void context_pop(context_t *ctx);




/****************************
 *   ASSERTIONS AND CHECK   *
 ***************************/

/*
 * Assert a boolean formula f.
 *
 * The context status must be IDLE.
 *
 * Return code:
 * - TRIVIALLY_UNSAT means that an inconsistency is detected
 *   (in that case the context status is set to UNSAT)
 * - CTX_NO_ERROR means no internalization error and status not 
 *   determined
 * - otherwise, the code is negative. The assertion could 
 *   not be processed.
 */
extern int32_t assert_formula(context_t *ctx, term_t f);


/*
 * Assert all formulas f[0] ... f[n-1]
 * same return code as above.
 */
extern int32_t assert_formulas(context_t *ctx, uint32_t n, term_t *f);




/*
 * Initialize params with default values
 */
extern void init_params_to_defaults(param_t *parameters);


/*
 * Check whether the context is consistent
 * - parameters = search and heuristic parameters to use
 * - if parameters is NULL, the default values are used
 * - if verbose is true, some statistics are displayed on stdout
 *   at every restart.
 *
 * return status: either STATUS_UNSAT, STATUS_SAT, STATUS_UNKNOWN, 
 * STATUS_INTERRUPTED (these codes are defined in smt_core.h)
 */
extern smt_status_t check_context(context_t *ctx, param_t *parameters, bool verbose);



/*
 * Build a model: the context's status must be STATUS_SAT or STATUS_UNKNOWN
 * - this function allocates a new model and return a pointer to it
 * - the model maps a value to every uninterpreted terms present in ctx's 
 *   internalization tables
 * - if keep_subst is true, the model also stores the current substitution
 *   as defined by ctx->pseudo_subst
 * - the user must delete this model using free_model (declared in models.h)
 */
extern model_t *context_build_model(context_t *ctx, bool keep_subst);



/*
 * Interrupt the search
 * - this can be called after check_context from a signal handler
 * - this interrupts the current search
 * - if clean_interrupt is enabled, calling context_cleanup will
 *   restore the solver to a good state, equivalent to the state 
 *   before the call to check_context
 * - otherwise, the solver is in a bad state from which new assertions
 *   can't be processed. Cleanup is possible via pop (if push/pop is supported)
 *   or reset.
 */
extern void context_stop_search(context_t *ctx);


/*
 * Cleanup after check is interrupted
 * - must not be called if the clean_interupt option is disabled
 * - restore the context to a good state (status = IDLE)
 */
extern void context_cleanup(context_t *ctx);


/*
 * Clear boolean assignment and return to the IDLE state.
 * - this can be called after check returns UNKNOWN or SEARCHING
 *   provided the context's mode isn't ONECHECK
 * - after this call, additional formulas can be asserted and 
 *   another call to check_context is allowed. Model construction 
 *   is no longer possible.
 */
extern void context_clear(context_t *ctx);




/*
 * For debugging, check atoms and internal consistency
 */
extern void context_check_atoms(context_t *ctx);




/*****************************
 *  INTERNALIZATION OPTIONS  *
 ****************************/

/*
 * Set or clear preprocessing options
 */
static inline void enable_variable_elimination(context_t *ctx) {
  ctx->options |= VARELIM_OPTION_MASK;
}

static inline void disable_variable_elimination(context_t *ctx) {
  ctx->options &= ~VARELIM_OPTION_MASK;
}

static inline void enable_or_flattening(context_t *ctx) {
  ctx->options |= FLATTENOR_OPTION_MASK;
}

static inline void disable_or_flattening(context_t *ctx) {
  ctx->options &= ~FLATTENOR_OPTION_MASK;
}

static inline void enable_diseq_and_or_flattening(context_t *ctx) {
  ctx->options |= FLATTENOR_OPTION_MASK|FLATTENDISEQ_OPTION_MASK;
}

static inline void disable_diseq_and_or_flattening(context_t *ctx) {
  ctx->options &= ~(FLATTENOR_OPTION_MASK|FLATTENDISEQ_OPTION_MASK);
}

static inline void enable_eq_abstraction(context_t *ctx) {
  ctx->options |= EQABSTRACT_OPTION_MASK;
}

static inline void disable_eq_abstraction(context_t *ctx) {
  ctx->options &= ~EQABSTRACT_OPTION_MASK;
}

static inline void enable_arith_elimination(context_t *ctx) {
  ctx->options |= ARITHELIM_OPTION_MASK;
}

static inline void disable_arith_elimination(context_t *ctx) {
  ctx->options &= ~ARITHELIM_OPTION_MASK;
}

static inline void enable_keep_ite(context_t *ctx) {
  ctx->options |= KEEP_ITE_OPTION_MASK;
}

static inline void disable_keep_ite(context_t *ctx) {
  ctx->options &= ~KEEP_ITE_OPTION_MASK;
}

static inline void enable_bvarith_elimination(context_t *ctx) {
  ctx->options |= BVARITHELIM_OPTION_MASK;
}

static inline void disable_bvarith_elimination(context_t *ctx) {
  ctx->options &= ~BVARITHELIM_OPTION_MASK;
}


/*
 * Simplex-related options
 */
extern void enable_splx_eager_lemmas(context_t *ctx);
extern void disable_splx_eager_lemmas(context_t *ctx);
extern void enable_splx_periodic_icheck(context_t *ctx);
extern void disable_splx_periodic_icheck(context_t *ctx);


/*
 * Chek which options are enabled
 */
static inline bool context_var_elim_enabled(context_t *ctx) {
  return (ctx->options & VARELIM_OPTION_MASK) != 0;
}

static inline bool context_flatten_or_enabled(context_t *ctx) {
  return (ctx->options & FLATTENOR_OPTION_MASK) != 0;
}

static inline bool context_flatten_diseq_enabled(context_t *ctx) {
  return (ctx->options & FLATTENDISEQ_OPTION_MASK) != 0;
}

static inline bool context_eq_abstraction_enabled(context_t *ctx) {
  return (ctx->options & EQABSTRACT_OPTION_MASK) != 0;
}

static inline bool context_arith_elim_enabled(context_t *ctx) {
  return (ctx->options & ARITHELIM_OPTION_MASK) != 0;
}

static inline bool context_keep_ite_enabled(context_t *ctx) {
  return (ctx->options & KEEP_ITE_OPTION_MASK) != 0;
}

static inline bool context_bvarith_elim_enabled(context_t *ctx) {
  return (ctx->options & BVARITHELIM_OPTION_MASK) != 0;
}

static inline bool context_has_preprocess_options(context_t *ctx) {
  return (ctx->options & PREPROCESSING_OPTIONS_MASK) != 0;
}

static inline bool context_dump_enabled(context_t *ctx) {
  return (ctx->options & DUMP_OPTION_MASK) != 0;
}

static inline bool splx_eager_lemmas_enabled(context_t *ctx) {
  return (ctx->options & SPLX_EGRLMAS_OPTION_MASK) != 0;
}

static inline bool splx_periodic_icheck_enabled(context_t *ctx) {
  return (ctx->options & SPLX_ICHECK_OPTION_MASK) != 0;
}


/*
 * Provisional: set/clear/test dump mode
 */
static inline void enable_dump(context_t *ctx) {
  ctx->options |= DUMP_OPTION_MASK;
}

static inline void disable_dump(context_t *ctx) {
  ctx->options &= ~DUMP_OPTION_MASK;
}





/********************************
 *  CHECK THEORIES AND SOLVERS  *
 *******************************/

/*
 * Supported theories
 */
static inline bool context_allows_uf(context_t *ctx) {
  return (ctx->theories & UF_MASK) != 0;
}

static inline bool context_allows_bv(context_t *ctx) {
  return (ctx->theories & BV_MASK) != 0;
}

static inline bool context_allows_idl(context_t *ctx) {
  return (ctx->theories & IDL_MASK) != 0;
}

static inline bool context_allows_rdl(context_t *ctx) {
  return (ctx->theories & RDL_MASK) != 0;
}

static inline bool context_allows_lia(context_t *ctx) {
  return (ctx->theories & LIA_MASK) != 0;
}

static inline bool context_allows_lra(context_t *ctx) {
  return (ctx->theories & LRA_MASK) != 0;
}

static inline bool context_allows_lira(context_t *ctx) {
  return (ctx->theories & LIRA_MASK) != 0;
}

static inline bool context_allows_nlarith(context_t *ctx) {
  return (ctx->theories & NLIRA_MASK) != 0;
}

static inline bool context_allows_fun_updates(context_t *ctx) {
  return (ctx->theories & FUN_UPDT_MASK) != 0;
}

static inline bool context_allows_extensionality(context_t *ctx) {
  return (ctx->theories & FUN_EXT_MASK) != 0;
}

static inline bool context_allows_quantifiers(context_t *ctx) {
  return (ctx->theories & QUANT_MASK) != 0;
}


/*
 * Check which solvers are present
 */
static inline bool context_has_egraph(context_t *ctx) {
  return ctx->egraph != NULL;
}

static inline bool context_has_arith_solver(context_t *ctx) {
  return ctx->arith_solver != NULL;
}

static inline bool context_has_bv_solver(context_t *ctx) {
  return ctx->bv_solver != NULL;
}

static inline bool context_has_fun_solver(context_t *ctx) {
  return ctx->fun_solver != NULL;
}


/*
 * Check which variant of the arithmetic solver is present
 */
extern bool context_has_idl_solver(context_t *ctx);
extern bool context_has_rdl_solver(context_t *ctx);
extern bool context_has_simplex_solver(context_t *ctx);




/*
 * Optional features
 */
static inline bool context_supports_multichecks(context_t *ctx) {
  return (ctx->options & MULTICHECKS_OPTION_MASK) != 0;
}

static inline bool context_supports_pushpop(context_t *ctx) {
  return (ctx->options & PUSHPOP_OPTION_MASK) != 0;
}

static inline bool context_supports_cleaninterrupt(context_t *ctx) {
  return (ctx->options & CLEANINT_OPTION_MASK) != 0;
}



/***************
 *  UTILITIES  *
 **************/

/*
 * Get the translation code of term t
 */
extern icode_t get_internal_code(context_t *ctx, term_t t);


/*
 * Get the number of equalities eliminated using the 
 * union-find table
 */
extern uint32_t num_eliminated_eqs(context_t *ctx);


/*
 * Get the number of substitutions (i.e., 
 * mapping X := u kept after flattening)
 */
extern uint32_t num_substitutions(context_t *ctx);


/*
 * Number of top-level atoms/equalities/formulas/substitution candidates
 */
static inline uint32_t num_top_eqs(context_t *ctx) {
  return ctx->top_eqs.size;
}

static inline uint32_t num_top_atoms(context_t *ctx) {
  return ctx->top_atoms.size;
}

static inline uint32_t num_top_formulas(context_t *ctx) {
  return ctx->top_formulas.size;
}

static inline uint32_t num_subst_candidates(context_t *ctx) {
  return ctx->subst_eqs.size;
}



/*
 * Get the diff-logic profile record
 * - it's NULL if the benchmark is not QF_IDL or QF_RDL
 */
static inline dl_data_t *get_diff_logic_profile(context_t *ctx) {
  return ctx->dl_profile;
}



/*
 * Check whether t is mapped to another term v (via variable elimination)
 * - if so return v
 * - otherwise return null_term
 */
extern term_t context_find_term_subst(context_t *ctx, term_t t);



/*
 * Read the status: returns one of 
 *  STATUS_IDLE        (before check_context)
 *  STATUS_SEARCHING   (during check_context)
 *  STATUS_UNKNOWN
 *  STATUS_SAT
 *  STATUS_UNSAT
 *  STATUS_INTERRUPTED
 */
static inline smt_status_t context_status(context_t *ctx) {
  return smt_status(ctx->core);
}




/*
 * Read the base_level (= number of calls to push)
 */
static inline uint32_t context_base_level(context_t *ctx) {
  return ctx->base_level;
}



#endif /* __CONTEXT_H */
