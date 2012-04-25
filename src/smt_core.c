/*
 * DPLL(T) CORE
 */

#include <assert.h>
#include <stddef.h>
#include <float.h>


#include "memalloc.h"
#include "int_array_sort.h"
#include "prng.h"
#include "gcd.h"
#include "smt_core.h"

#define TRACE 0
#define DEBUG 0

#if DEBUG || TRACE
#include <stdio.h>
#include <inttypes.h>

extern void print_literal(FILE *f, literal_t l);
extern void print_bval(FILE *f, bval_t b);

#endif

#if DEBUG

// All debugging functions are defined at the end of this file
static void check_heap_content(smt_core_t *s);
static void check_heap(smt_core_t *s);
static void check_propagation(smt_core_t *s);
static void check_marks(smt_core_t *s);
static void check_theory_conflict(smt_core_t *s, literal_t *a);
static void check_theory_explanation(smt_core_t *s, literal_t l);
static void check_watched_literals(smt_core_t *s, uint32_t n, literal_t *a);

#endif



/***********************************
 * Externalize USE_END_WATCH flag  *
 **********************************/

#if USE_END_WATCH
const char * const smt_compile_option = "end_watch";
#else
const char * const smt_compile_option = "no end_watch";
#endif



/********************************
 * CLAUSES AND LEARNED CLAUSES  *
 *******************************/

/*
 * Get first watched literal of cl
 */
static inline literal_t get_first_watch(clause_t *cl) {
  return cl->cl[0];
}

/*
 * Get second watched literal of cl
 */
static inline literal_t get_second_watch(clause_t *cl) {
  return cl->cl[1];
}

/*
 * Get watched literal of index (1 - i) in cl.
 * \param i = 0 or 1
 */
static inline literal_t get_other_watch(clause_t *cl, uint32_t i) {
  // flip low-order bit of i
  return cl->cl[i ^ 1];
}

/*
 * Get pointer to learned_clause in which clause cl is embedded. 
 */
static inline learned_clause_t *learned(clause_t *cl) {
  return (learned_clause_t *)(((char *)cl) - offsetof(learned_clause_t, clause));
}

/*
 * Activity of a learned clause
 */
static inline float get_activity(clause_t *cl) {
  return learned(cl)->activity;
}

/*
 * Set the activity to act
 */
static inline void set_activity(clause_t *cl, float act) {
  learned(cl)->activity = act;
}

/*
 * Increase the activity of a learned clause by delta
 */
static inline void increase_activity(clause_t *cl, float delta) {
  learned(cl)->activity += delta;
}

/*
 * Multiply activity by scale
 */
static inline void multiply_activity(clause_t *cl, float scale) {
  learned(cl)->activity *= scale;
}

/*
 * Mark a clause cl for removal
 */
static inline void mark_for_removal(clause_t *cl) {
  cl->cl[0] = - cl->cl[0];
  cl->cl[1] = - cl->cl[1];
}

static inline bool is_clause_to_be_removed(clause_t *cl) {
  return cl->cl[0] < 0 || cl->cl[1] < 0;
}

/*
 * Restore a removed clause: flip the signs back
 */
static inline void restore_removed_clause(clause_t *cl) {
  cl->cl[0] = - cl->cl[0];
  cl->cl[1] = - cl->cl[1];  
}


/*
 * Clause length
 */
static uint32_t clause_length(clause_t *cl) {
  literal_t *a;

  a = cl->cl + 2;
  while (*a >= 0) {
    a ++;
  }

  return a - cl->cl;
}

/*
 * Allocate and initialize a new clause (not a learned clause)
 * \param len = number of literals
 * \param lit = array of len literals
 * The watched pointers are not initialized
 */
static clause_t *new_clause(uint32_t len, literal_t *lit) {
  clause_t *result;
  uint32_t i;

  result = (clause_t *) safe_malloc(sizeof(clause_t) + sizeof(literal_t) + 
				    len * sizeof(literal_t));

  for (i=0; i<len; i++) {
    result->cl[i] = lit[i];
  }
  result->cl[i] = end_clause; // end marker: not a learned clause

  return result;
}

/*
 * Delete clause cl
 * cl must be a non-learned clause, allocated via the previous function.
 */
static inline void delete_clause(clause_t *cl) {
  safe_free(cl);
}

/*
 * Allocate and initialize a new learned clause
 * \param len = number of literals
 * \param lit = array of len literals
 * The watched pointers are not initialized. 
 * The activity is initialized to 0.0
 */
static clause_t *new_learned_clause(uint32_t len, literal_t *lit) {
  learned_clause_t *tmp;
  clause_t *result;
  uint32_t i;

  tmp = (learned_clause_t *) safe_malloc(sizeof(learned_clause_t) + sizeof(literal_t) + 
					 len * sizeof(literal_t));
  tmp->activity = 0.0;
  result = &(tmp->clause);

  for (i=0; i<len; i++) {
    result->cl[i] = lit[i];
  }
  result->cl[i] = end_learned; // end marker: learned clause

  return result;
}

/*
 * Delete learned clause cl
 * cl must have been allocated via the new_learned_clause function
 */
static inline void delete_learned_clause(clause_t *cl) {
  safe_free(learned(cl));
}



/********************
 *  CLAUSE VECTORS  *
 *******************/

/*
 * Create a clause vector of capacity n. 
 */
static clause_t **new_clause_vector(uint32_t n) {
  clause_vector_t *tmp;

  tmp = (clause_vector_t *) safe_malloc(sizeof(clause_vector_t) + n * sizeof(clause_t *));
  tmp->capacity = n;
  tmp->size = 0;

  return tmp->data;
}

/*
 * Clean up: free memory used by v
 */
static void delete_clause_vector(clause_t **v) {
  safe_free(cv_header(v));
}

/*
 * Add clause cl at the end of vector *v. Assumes *v has been initialized.
 */
static void add_clause_to_vector(clause_t ***v, clause_t *cl) {
  clause_vector_t *vector;
  clause_t **d;  
  uint32_t i, n;

  d = *v;
  vector = cv_header(d);
  i = vector->size;
  if (i == vector->capacity) {
    n = i + 1;
    n += (n >> 1); // n = new capacity
    if (n > MAX_CLAUSE_VECTOR_SIZE) {
      out_of_memory();
    }
    vector = (clause_vector_t *) 
      safe_realloc(vector, sizeof(clause_vector_t) + n * sizeof(clause_t *));
    vector->capacity = n;
    d = vector->data;
    *v = d;
  }
  d[i] = cl;
  vector->size = i+1;
}


/*
 * Reset clause vector v: set its size to 0
 */
static inline void reset_clause_vector(clause_t **v) {
  set_cv_size(v, 0);
}




/**********************
 *  LITERAL VECTORS   *
 *********************/

/*
 * When used to store binary clauses literal vectors are initially
 * NULL.  Memory is allocated on the first addition and literal
 * vectors are terminated by -1.
 *
 * For a vector v of size i, the literals are stored 
 * in v[0],...,v[i-1], and v[i] = -1
 */

/*
 * Add literal l at the end of vector *v
 * - allocate a fresh vector if *v == NULL
 * - resize *v if *v is full.
 * - add -1 terminator after l.
 */
static void add_literal_to_vector(literal_t **v, literal_t l) {
  literal_vector_t *vector;
  literal_t *d;
  uint32_t i, n;

  d = *v;
  if (d == NULL) {
    i = 0;
    n = DEF_LITERAL_VECTOR_SIZE;
    vector = (literal_vector_t *)
      safe_malloc(sizeof(literal_vector_t) + n * sizeof(literal_t));
    vector->capacity = n;
    d = vector->data;
    *v = d;
  } else {
    vector = lv_header(d);
    i = vector->size;
    n = vector->capacity;
    if (i >= n - 1) {
      n ++;
      n += n>>1; // new cap = 50% more than old capacity
      if (n > MAX_LITERAL_VECTOR_SIZE) {
	out_of_memory();
      }
      vector = (literal_vector_t *) 
	safe_realloc(vector, sizeof(literal_vector_t) + n * sizeof(literal_t));
      vector->capacity = n;
      d = vector->data;
      *v = d;
    }
  }

  assert(i + 1 < vector->capacity);
  
  d[i] = l;
  d[i+1] = null_literal;
  vector->size = i+1;
}


/*
 * Delete literal vector v
 */
static inline void delete_literal_vector(literal_t *v) {
  if (v != NULL) {
    safe_free(lv_header(v));
  }
}


/*
 * Remove the last literal from vector v
 */
static inline void literal_vector_pop(literal_t *v) {
  uint32_t i;

  i = get_lv_size(v);
  assert(i > 0);
  i --;
  v[i] = null_literal;
  set_lv_size(v,  i);
}


/*
 * Last element of vector v (used in assert)
 */
static inline literal_t last_lv_elem(literal_t *v) {
  assert(v != NULL && get_lv_size(v) > 0);
  return v[get_lv_size(v) - 1];
}



/***********
 *  STACK  *
 **********/

/*
 * Initialize stack s for nvar
 */
static void init_stack(prop_stack_t *s, uint32_t nvar) {
  s->lit = (literal_t *) safe_malloc(nvar * sizeof(literal_t));
  s->level_index = (uint32_t *) safe_malloc(DEFAULT_NLEVELS * sizeof(uint32_t));
  s->level_index[0] = 0;
  s->top = 0;
  s->prop_ptr = 0;
  s->theory_ptr = 0;
  s->nlevels = DEFAULT_NLEVELS;
}

/*
 * Extend the size: nvar = new size
 */
static void extend_stack(prop_stack_t *s, uint32_t nvar) {
  s->lit = (literal_t *) safe_realloc(s->lit, nvar * sizeof(literal_t));
}

/*
 * Extend the level_index array by 50%
 */
static void increase_stack_levels(prop_stack_t *s) {
  uint32_t n;

  n = s->nlevels;
  n += n>>1;
  s->level_index = (uint32_t *) safe_realloc(s->level_index, n * sizeof(uint32_t));
  s->nlevels = n;
}

/*
 * Reset the stack (empty it)
 */
static void reset_stack(prop_stack_t *s) {
  s->top = 0;
  s->prop_ptr = 0;
  s->theory_ptr = 0;
  s->level_index[0] = 0;
}


/*
 * Free memory used by stack s 
 */
static void delete_stack(prop_stack_t *s) {
  free(s->lit);
  free(s->level_index);
}

/*
 * Push literal l on top of stack s
 */
static inline void push_literal(prop_stack_t *s, literal_t l) {
  uint32_t i;
  i = s->top;
  s->lit[i] = l;
  s->top = i + 1;
}




/**********
 *  HEAP  *
 *********/

/*
 * Initialize heap for n variables
 * - heap is initially empty: heap_last = 0
 * - heap[0] = -1 is a marker, with activity[-1] higher 
 *   than any variable activity.
 * - activity increment and threshold are set to their
 *   default initial value.
 */
static void init_heap(var_heap_t *heap, uint32_t n) {
  uint32_t i;
  double *tmp;

  heap->size = n;
  tmp = (double *) safe_malloc((n+1) * sizeof(double));
  heap->activity = tmp + 1;
  heap->heap_index = (int32_t *) safe_malloc(n * sizeof(int32_t));
  heap->heap = (bvar_t *) safe_malloc((n+1) * sizeof(bvar_t));

  for (i=0; i<n; i++) {
    heap->heap_index[i] = -1;
    heap->activity[i] = 0.0;
  }

  heap->activity[-1] = DBL_MAX;
  heap->heap[0] = -1;
  heap->heap_last = 0;

  heap->act_increment = INIT_VAR_ACTIVITY_INCREMENT;
  heap->inv_act_decay = 1/VAR_DECAY_FACTOR;
}

/*
 * Extend the heap for n variables
 */
static void extend_heap(var_heap_t *heap, uint32_t n) {
  uint32_t old_size, i;
  double *tmp;

  old_size = heap->size;
  assert(old_size < n);
  heap->size = n;
  tmp = heap->activity - 1;
  tmp = (double *) safe_realloc(tmp, (n+1) * sizeof(double));
  heap->activity = tmp + 1;
  heap->heap_index = (int32_t *) safe_realloc(heap->heap_index, n * sizeof(int32_t));
  heap->heap = (int32_t *) safe_realloc(heap->heap, (n+1) * sizeof(int32_t));

  for (i=old_size; i<n; i++) {
    heap->heap_index[i] = -1;
    heap->activity[i] = 0.0;
  }
}

/*
 * Free the heap
 */
static void delete_heap(var_heap_t *heap) {
  safe_free(heap->activity - 1);
  safe_free(heap->heap_index);
  safe_free(heap->heap);
}


/*
 * Reset: remove all variables from the heap and set their activities to 0
 */
static void reset_heap(var_heap_t *heap) {
  uint32_t i, n;

  n = heap->size;
  for (i=0; i<n; i++) {
    heap->heap_index[i] = -1;
    heap->activity[i] = 0.0;
  }
  heap->heap_last = 0; 

  // reset actitivity parameters: this makes a difference (2010/08/10)
  heap->act_increment = INIT_VAR_ACTIVITY_INCREMENT;
  heap->inv_act_decay = 1/VAR_DECAY_FACTOR;
}


/*
 * EXPERIMENT: TEST TWO HEAP ORDERINGS
 * - if BREAK_TIES is set, then ties are broken by ranking 
 *   the variable with smallest index higher than the other.
 *   (seems to work better on bitvector benchmarks)
 * - otherwise, we don't attempt to break ties.
 *
 * NOTE: if BREAK_TIES is set, then rescale_var_activities 
 * may not preserve the intended heap ordering. We ignore this
 * issue for now, it should not matter much anyway??
 */

#define BREAK_TIES 1

/*
 * Comparison: return true if x precedes y in the heap ordering (strict ordering) 
 * - ax = activity of x
 * - ay = activity of y
 */
static inline bool heap_cmp(bvar_t x, bvar_t y, double ax, double ay) {
#if BREAK_TIES
  return (ax > ay) || (ax == ay && x < y);
#else
  return ax > axy;
#endif
}

// variant: act = activity array
static inline bool heap_precedes(double *act, bvar_t x, bvar_t y) {
  return heap_cmp(x, y, act[x], act[y]);
}



/*
 * Move x up in the heap.
 * i = current position of x in the heap (or heap_last if x is being inserted)
 */
static inline void update_up(var_heap_t *heap, bvar_t x, uint32_t i) {
  double ax, *act;
  int32_t *index;
  bvar_t *h, y;
  uint32_t j;

  h = heap->heap;
  index = heap->heap_index;
  act = heap->activity;

  ax = act[x];

  j = i >> 1;    // parent of i
  y = h[j];      // variable at position j in the heap

  // The loop terminates since act[h[0]] = DBL_MAX and h[0] = -1
  while (heap_cmp(x, y, ax, act[y])) {
    // move y down, into position i
    h[i] = y;
    index[y] = i;

    // move i and j up
    i = j;
    j >>= 1;
    y = h[j];
  }

  // i is the new position for variable x
  h[i] = x;
  index[x] = i;  
}


/*
 * Remove element at index i in the heap.
 * Replace it by the current last element.
 */
static inline void update_down(var_heap_t *heap, uint32_t i) {
  double az, *act;
  int32_t* index; 
  bvar_t *h, x, y, z;
  uint32_t j, last;

  h = heap->heap;
  index = heap->heap_index;
  act = heap->activity;
  last = heap->heap_last;
  heap->heap_last = last - 1;

  assert(i <= last && act[h[i]] >= act[h[last]]);

  if (last == i) return;  // last element was removed

  z = h[last]; // last element
  az = act[z]; // activity of last heap element.

  j = 2 * i;      // left child of i
  
  while (j + 1 < last) {
    // find child of i with highest activity.
    x = h[j];
    y = h[j+1];
    if (heap_precedes(act, y, x)) {
      j++; 
      x = y;
    }

    // x = child of node i of highest activity
    // j = position of x in the heap (j = 2i or j = 2i+1)
    if (heap_cmp(z, x, az, act[x])) {
      h[i] = z;
      index[z] = i;
      return;
    }

    // Otherwise, move x up, into heap[i]
    h[i] = x;
    index[x] = i;

    // go down one step.
    i = j;
    j <<= 1;
  }

  // Final steps: j + 1 >= last:
  // x's position is either i or j.
  if (j < last) {
    x = h[j];
    if (heap_cmp(z, x, az, act[x])) {
      h[i] = z;
      index[z] = i;
    } else {
      h[i] = x;
      index[x] = i;
      h[j] = z;
      index[z] = j;
    }
  } else {
    h[i] = z;
    index[z] = i;
  }
}


/*
 * Insert x into the heap, using its current activity.
 * No effect if x is already in the heap.
 * - x must be between 0 and nvars - 1 
 */
static inline void heap_insert(var_heap_t *heap, bvar_t x) {
  if (heap->heap_index[x] < 0) {
    // x not in the heap
    heap->heap_last ++;
    update_up(heap, x, heap->heap_last);
  }
}

/*
 * Remove x from the heap
 */
static void heap_remove(var_heap_t *heap, bvar_t x) {
  int32_t i, j;
  bvar_t y;

  i = heap->heap_index[x];
  if (i < 0) return; // x is not in the heap

  heap->heap_index[x] = -1;

  j = heap->heap_last;
  y = heap->heap[j]; // last variable  

  if (i == j) {
    // x was the last element
    assert(x == y);
    heap->heap_last --;
  } else if (heap_precedes(heap->activity, x, y)) {
    // in update down, h[i] is replaced by last element (i.e. y)
    update_down(heap, i);
  } else {
    // replace x by y and move y up the heap
    heap->heap[i] = y;
    heap->heap_last --;
    update_up(heap, y, i);
  }
}


/*
 * Get and remove the top element
 * - returns null_bvar (i.e., -1) if the heap is empty.
 */
static inline bvar_t heap_get_top(var_heap_t *heap) {  
  bvar_t top;

  if (heap->heap_last == 0) {
    return null_bvar;
  }

  // remove top element
  top = heap->heap[1];
  heap->heap_index[top] = -1;

  // repair the heap
  update_down(heap, 1);

  return top;
}

/*
 * Rescale variable activities: divide by VAR_ACTIVITY_THRESHOLD
 * \param heap = pointer to a heap structure
 * \param n = number of variables
 */
static void rescale_var_activities(var_heap_t *heap, uint32_t n) {
  uint32_t i;
  double *act;

  heap->act_increment *= INV_VAR_ACTIVITY_THRESHOLD;
  act = heap->activity;
  for (i=0; i<n; i++) {
    act[i] *= INV_VAR_ACTIVITY_THRESHOLD;
  }
}




/*****************
 *  TRAIL STACK  *
 ****************/

/*
 * Initialize a trail stack. Size = 0
 */
static void init_trail_stack(trail_stack_t *stack) {
  stack->size = 0;
  stack->top = 0;
  stack->data = NULL;
}

/*
 * Save level:
 * - v = number of variables
 * - u = number of unit clauses
 * - b = number of binary clauses
 * - p = number of (non-unit and non-binary) problem clauses
 * - b_ptr = boolean propagation pointer
 * - t_ptr = theory propagation pointer
 */
static void trail_stack_save(trail_stack_t *stack, uint32_t v, uint32_t u, uint32_t b, uint32_t p, 
			     uint32_t b_ptr, uint32_t t_ptr) {
  uint32_t i, n;

  i = stack->top;
  n = stack->size;
  if (i == n) {
    if (n == 0) {
      n = DEFAULT_DPLL_TRAIL_SIZE;
    } else {
      n += n;
      if (n >= MAX_DPLL_TRAIL_SIZE) {
	out_of_memory();
      }
    }
    stack->data = (trail_t *) safe_realloc(stack->data, n * sizeof(trail_t));
    stack->size = n;
  }
  stack->data[i].nvars = v;
  stack->data[i].nunits = u;
  stack->data[i].nbins = b;
  stack->data[i].nclauses = p;
  stack->data[i].prop_ptr = b_ptr;
  stack->data[i].theory_ptr = t_ptr;

  stack->top = i + 1;
}


/*
 * Get top record
 */
static inline trail_t *trail_stack_top(trail_stack_t *stack) {
  assert(stack->top > 0);
  return stack->data + (stack->top - 1);
}

/*
 * Remove top record
 */
static inline void trail_stack_pop(trail_stack_t *stack) {
  assert(stack->top > 0);
  stack->top --;
}


/*
 * Empty the stack
 */
static inline void reset_trail_stack(trail_stack_t *stack) {
  stack->top = 0;
}


/*
 * Delete
 */
static inline void delete_trail_stack(trail_stack_t *stack) {
  safe_free(stack->data);
  stack->data = NULL;
}



/******************
 *   ATOM TABLE   *
 *****************/

/*
 * Initialization: the table is initially empty.
 */
static void init_atom_table(atom_table_t *tbl) {
  tbl->has_atom = NULL;
  tbl->atom = NULL;
  tbl->size = 0;
  tbl->natoms = 0;
}


/*
 * Make room for more atoms: n = new size
 */
static void resize_atom_table(atom_table_t *tbl, uint32_t n) {
  uint32_t k;

  // round up to a multiple of 8
  n = (n + 7) & ~7;
  k = tbl->size;

  if (n > k) {
    assert(n <= MAX_ATOM_TABLE_SIZE);

    tbl->has_atom = extend_bitvector(tbl->has_atom, n);
    tbl->atom = (void **) safe_realloc(tbl->atom, n * sizeof(void *));
    tbl->size = n;

    // clear new bitvector elements
    clear_bitvector(tbl->has_atom + (k>>3), n - k);
  }
}


/*
 * Deletion
 */
static void delete_atom_table(atom_table_t *tbl) {
  delete_bitvector(tbl->has_atom);
  safe_free(tbl->atom);
  tbl->has_atom = NULL;
  tbl->atom = NULL;
}


/*
 * Reset the table: empty it
 */
static void reset_atom_table(atom_table_t *tbl) {
  tbl->natoms = 0;
  clear_bitvector(tbl->has_atom, tbl->size);
}




/*
 * Attach atom atm to variable v: 
 * - v must not have an atom attached already and there must be enough
 * room in tbl for variable v (i.e., tbl must be resized before this
 * function is called).
 */
static void add_atom(atom_table_t *tbl, bvar_t v, void *atm) {
  assert(v < tbl->size && !tst_bit(tbl->has_atom, v));
  set_bit(tbl->has_atom, v);
  tbl->atom[v] = atm;
  tbl->natoms ++;
}


/*
 * Remove the atom attached to v
 */
static void remove_atom(atom_table_t *tbl, bvar_t v) {
  assert(v < tbl->size && tst_bit(tbl->has_atom, v));
  clr_bit(tbl->has_atom, v);
  tbl->atom[v] = NULL;
  tbl->natoms --;
}




/*****************
 *  LEMMA QUEUE  *
 ****************/

/*
 * Initialize queue: nothing is allocated yet
 */
static void init_lemma_queue(lemma_queue_t *queue) {
  queue->capacity = 0;
  queue->nblocks = 0;
  queue->free_block = 0;
  queue->block = NULL;
}

/*
 * Delete all allocated blocks and the array queue->block
 */
static void delete_lemma_queue(lemma_queue_t *queue) {
  uint32_t i;

  for (i=0; i<queue->nblocks; i++) {
    safe_free(queue->block[i]);
  }
  safe_free(queue->block);
  queue->block = NULL;
}


/*
 * Increase capacity: increase the size of the block array
 */
static void increase_lemma_queue_capacity(lemma_queue_t *queue) {
  uint32_t  n;

  n = 2 * queue->capacity; // new capacity 
  if (n == 0) {
    n = DEF_LEMMA_BLOCKS;
  }

  if (n >= MAX_LEMMA_BLOCKS) {
    out_of_memory();
  }

  queue->block = (lemma_block_t **) safe_realloc(queue->block, n * sizeof(lemma_block_t *));
  queue->capacity = n;
}


/*
 * Allocate a block of the given size
 */
static lemma_block_t *new_lemma_block(uint32_t size) {
  lemma_block_t *tmp;

  if (size >= MAX_LEMMA_BLOCK_SIZE) {
    out_of_memory();
  }

  tmp = (lemma_block_t *) safe_malloc(sizeof(lemma_block_t) + size * sizeof(literal_t));
  tmp->size = size;
  tmp->ptr = 0;

  return tmp;
}


/*
 * Find a block b that has space for n literals (i.e., b->size - b->ptr >= n)
 * - use the top_block if that works, otherwise use the next block
 * - allocate blocks if necessary
 */
static lemma_block_t *find_block_for_lemma(lemma_queue_t *queue, uint32_t n) {
  uint32_t i, j;
  lemma_block_t *tmp;

  /*
   * invariants:
   * 0 <= free_block <= nblocks <= capacity
   * block has size = capacity
   * if 0 <= i < free_block-1 then block[i] is full
   * if free_block > 0, then block[free_block-1] is not empty and not full
   * if free_block <= i < nblocks then block[i] is allocated and empty
   * if nblocks <= i < capacity then block[i] is not allocated
   */
  i = queue->free_block;
  if (i > 0) {
    // try the current block
    tmp = queue->block[i-1];
    assert(tmp != NULL && tmp->ptr > 0);
    if (tmp->size - tmp->ptr >= n) return tmp;    
  }

  // current block does not exist or it's full.
  // search for a large enough block among block[free_blocks ... nblocks-1]
  for (j=i; j<queue->nblocks; j++) {
    tmp = queue->block[j];
    assert(tmp != NULL && tmp->ptr == 0);
    if (tmp->size >= n) {
      // swap block[i] and block[j]
      queue->block[j] = queue->block[i];
      queue->block[i] = tmp;
      queue->free_block ++;
      return tmp;
    }
  }

  // we need to allocate a new block, large enough for n literals
  if (n < DEF_LEMMA_BLOCK_SIZE) {
    n = DEF_LEMMA_BLOCK_SIZE;
  }
  tmp = new_lemma_block(n);

  // make room in queue->block if necessary
  j = queue->nblocks;
  if (j >= queue->capacity) {
    increase_lemma_queue_capacity(queue);
    assert(queue->nblocks < queue->capacity);
  }

  queue->block[j] = queue->block[i];
  queue->block[i] = tmp;
  queue->free_block ++;
  queue->nblocks ++;

  return tmp;  
}


/*
 * Push literal array a[0] ... a[n-1] as a lemma
 */
static void push_lemma(lemma_queue_t *queue, uint32_t n, literal_t *a) {
  lemma_block_t *blk;
  uint32_t i;
  literal_t *b;

  blk = find_block_for_lemma(queue, n+1);
  assert(queue->free_block > 0 && blk == queue->block[queue->free_block-1] 
	 && blk->ptr + n < blk->size);

  b = blk->data + blk->ptr;
  for (i=0; i<n; i++) {
    b[i] = a[i];
  }
  b[i] = null_literal; // end-marker;
  i++;
  blk->ptr += i;
}





/*
 * Empty the queue
 */
static void reset_lemma_queue(lemma_queue_t *queue) {
  uint32_t i;

  if (queue->nblocks > LEMMA_BLOCKS_TO_KEEP) {
    // keep 4 blocks, delete the others to save memory
    for (i=0; i<LEMMA_BLOCKS_TO_KEEP; i++) {
      queue->block[i]->ptr = 0;
    }
    for (i=4; i<queue->nblocks; i++) {
      safe_free(queue->block[i]);
      queue->block[i] = NULL;
    }
    queue->nblocks = LEMMA_BLOCKS_TO_KEEP;

  } else {
    // keep all the allocated blocks
    for (i=0; i<queue->nblocks; i++) {
      queue->block[i]->ptr = 0;
    }
  }
  queue->free_block = 0;
}


/*
 * Check whether the queue is empty
 */
static inline bool empty_lemma_queue(lemma_queue_t *queue) {
  return queue->free_block == 0;
}





/************************
 *   CHECKPOINT STACK   *
 ***********************/

/*
 * Initialization: nothing is allocated
 */
static void init_checkpoint_stack(checkpoint_stack_t *stack) {
  stack->size = 0;
  stack->top = 0;
  stack->data = NULL;
}

/*
 * Delete 
 */
static void delete_checkpoint_stack(checkpoint_stack_t *stack) {
  safe_free(stack->data);
  stack->data = NULL;
}


/*
 * Increase the size
 */
static void extend_checkpoint_stack(checkpoint_stack_t *stack) {
  uint32_t n;

  n = stack->size;
  n += n>>1;     // make it 50% larger
  if (n == 0) {
    // first allocation
    n = DEF_CHECKPOINT_STACK_SIZE;
  }

  if (n >= MAX_CHECKPOINT_STACK_SIZE) {
    out_of_memory();
  }

  stack->data = (checkpoint_t *) safe_realloc(stack->data, n * sizeof(checkpoint_t));
  stack->size = n;
}


/*
 * Check whether the stack is empty
 */
static inline bool empty_checkpoint_stack(checkpoint_stack_t *stack) {
  return stack->top == 0;
}

static inline bool non_empty_checkpoint_stack(checkpoint_stack_t *stack) {
  return stack->top > 0;
}


/*
 * Get the top checkpoint
 */
static inline checkpoint_t *top_checkpoint(checkpoint_stack_t *stack) {
  assert(non_empty_checkpoint_stack(stack));
  return stack->data + (stack->top - 1);
}

/*
 * Remove the top checkpoint
 */
static inline void pop_checkpoint(checkpoint_stack_t *stack) {
  assert(non_empty_checkpoint_stack(stack));
  stack->top --;
}

/*
 * Push a checkpoint 
 * - d = decision level,
 * - n = number of terms
 */
static void push_checkpoint(checkpoint_stack_t *stack, uint32_t d, uint32_t n) {
  uint32_t i;

  i = stack->top;
  if (i >= stack->size) {
    extend_checkpoint_stack(stack);    
    assert(i < stack->size);
  }
  stack->data[i].dlevel = d;
  stack->data[i].nvars = n;
  stack->top = i+1;
}


/*
 * Reset: empty the stack
 */
static inline void reset_checkpoint_stack(checkpoint_stack_t *stack) {
  stack->top = 0;
}






/************************
 *  STATISTICS RECORD   *
 ***********************/


/*
 * Initialize a statistics record
 */
static void init_statistics(dpll_stats_t *stat) {
  stat->restarts = 0;
  stat->simplify_calls = 0;
  stat->reduce_calls = 0;
  stat->remove_calls = 0;
  stat->decisions = 0;
  stat->random_decisions = 0;
  stat->propagations = 0;
  stat->conflicts = 0;
  stat->th_props = 0;
  stat->th_prop_lemmas = 0;
  stat->th_conflicts = 0;
  stat->th_conflict_lemmas = 0;
  stat->prob_literals = 0;
  stat->learned_literals = 0;
  stat->prob_clauses_deleted = 0;
  stat->learned_clauses_deleted = 0;
  stat->bin_clauses_deleted = 0;
  stat->literals_before_simpl = 0;
  stat->subsumed_literals = 0;
}


/*
 * Reset = same thing as init
 */
static inline void reset_statistics(dpll_stats_t *stats) {
  init_statistics(stats);
}




/************************
 *  GENERAL OPERATIONS  *
 ***********************/

/*
 * Initialize an smt core
 * - n = initial vsize = size of the variable-indexed arrays
 * - th = theory solver
 * - ctrl = descriptor of control functions for th
 * - smt = desriptor of the SMT functions for th
 * - mode = to select optional features
 * This creates the predefined "constant" variable and the true/false literals
 *
 * The clause and variable activity increments, and the randonmess
 * parameters are set to their default values
 */
void init_smt_core(smt_core_t *s, uint32_t n, void *th, 
		   th_ctrl_interface_t *ctrl, th_smt_interface_t *smt,
		   smt_mode_t mode) {
  uint32_t lsize;
  
  s->th_solver = th;
  s->th_ctrl = *ctrl; // make a full copy
  s->th_smt = *smt;   // ditto

  s->status = STATUS_IDLE;

  switch (mode) {    
  case SMT_MODE_BASIC:
    s->option_flag = 0;
    break;

  case SMT_MODE_PUSHPOP:
    s->option_flag = PUSH_POP_MASK;
    break;

  default:
    s->option_flag = PUSH_POP_MASK|CLEAN_INTERRUPT_MASK;
    break;
  }

  // ensure there's room for the constants
  if (n == 0) n = 1;
  lsize = 2 * n;

  if (n >= MAX_VARIABLES) {
    out_of_memory();
  }


  // counters
  s->nvars = 1; 
  s->nlits = 2;
  s->vsize = n;
  s->lsize = lsize;

  s->nb_clauses = 0;
  s->nb_prob_clauses = 0;
  s->nb_bin_clauses = 0;
  s->nb_unit_clauses = 0;
  
  s->simplify_bottom = 0;
  s->simplify_props = 0;
  s->simplify_threshold = 0;

  s->aux_literals = 0;
  s->aux_clauses = 0;

  s->decision_level = 0;
  s->base_level = 0;

  s->cla_inc = INIT_CLAUSE_ACTIVITY_INCREMENT;
  s->inv_cla_decay = 1/CLAUSE_DECAY_FACTOR;
  s->scaled_random = (uint32_t) (VAR_RANDOM_FACTOR * VAR_RANDOM_SCALE);

  // theory caching: disabled initially
  s->th_cache_enabled = false;
  s->th_cache_cl_size = 0;

  // conflict data: no need to initialize conflict_buffer
  s->inconsistent = false;
  s->theory_conflict = false;
  s->conflict = NULL;
  s->false_clause = NULL;

  // auxiliary buffers
  init_ivector(&s->buffer, DEF_LBUFFER_SIZE);
  init_ivector(&s->buffer2, DEF_LBUFFER_SIZE);
  init_ivector(&s->explanation, DEF_LBUFFER_SIZE);

  // clause database: all empty
  s->problem_clauses = new_clause_vector(DEF_CLAUSE_VECTOR_SIZE);
  s->learned_clauses = new_clause_vector(DEF_CLAUSE_VECTOR_SIZE);
  init_ivector(&s->binary_clauses, 0);

  
  /*
   * Variable-indexed arrays
   *
   * level is indexed from -1 to n-1
   * level[-1] = UINT32_MAX (never assigned, marker variable)
   */
  s->antecedent = (antecedent_t *) safe_malloc(n * sizeof(antecedent_t)); 
  s->level = (uint32_t *) safe_malloc((n + 1) * sizeof(uint32_t)) + 1;
  s->mark = allocate_bitvector(n);
  s->polarity = allocate_bitvector(n);
  s->level[-1] = UINT32_MAX;

  /*
   * Literal-indexed arrays
   *
   * value is indexed from -2 to 2n-1:
   * value[-2] = value[-1] = VAL_UNDEF (end markers for clauses)
   */
  s->value = (uint8_t *) safe_malloc((lsize + 2) * sizeof(uint8_t)) + 2;
  s->value[-2] = VAL_UNDEF; // end_learned marker
  s->value[-1] = VAL_UNDEF; // end_clause marker
  s->bin = (literal_t **) safe_malloc(lsize * sizeof(literal_t *));
  s->watch = (link_t *) safe_malloc(lsize * sizeof(link_t));
#if USE_END_WATCH
  s->end_watch = (link_t **) safe_malloc(lsize * sizeof(link_t *));
#endif

  /*
   * Initialize data structures for true_literal and false_literal
   */
  assert(const_bvar == 0 && true_literal == 0 && false_literal == 1 && s->nvars > 0);
  s->level[const_bvar] = 0;
  s->value[true_literal] = VAL_TRUE;
  s->value[false_literal] = VAL_FALSE;
  set_bit(s->mark, const_bvar);
  s->bin[true_literal] = NULL;
  s->bin[false_literal] = NULL;
  s->watch[true_literal] = NULL_LINK;
  s->watch[false_literal] = NULL_LINK;
#if USE_END_WATCH
  s->end_watch[true_literal] = &s->watch[true_literal];
  s->end_watch[false_literal] = &s->watch[false_literal];
#endif

  init_stack(&s->stack, n);
  init_heap(&s->heap, n);  
  init_lemma_queue(&s->lemmas);
  init_statistics(&s->stats);
  init_atom_table(&s->atoms);
  init_trail_stack(&s->trail_stack);
  init_checkpoint_stack(&s->checkpoints);
  s->cp_flag = false;
}


/*
 * Delete: free all allocated memory
 */
void delete_smt_core(smt_core_t *s) {
  uint32_t i, n;
  clause_t **cl;

  delete_ivector(&s->buffer);
  delete_ivector(&s->buffer2);
  delete_ivector(&s->explanation);

  // Delete all the clauses
  cl = s->problem_clauses;
  n = get_cv_size(cl);
  for (i=0; i<n; i++) {
    delete_clause(cl[i]);
  }
  delete_clause_vector(cl);

  cl = s->learned_clauses;
  n = get_cv_size(cl);
  for (i=0; i<n; i++) {
    delete_learned_clause(cl[i]);
  }
  delete_clause_vector(cl);

  delete_ivector(&s->binary_clauses);

  // var-indexed arrays  
  safe_free(s->antecedent);
  safe_free(s->level - 1);
  delete_bitvector(s->mark);
  delete_bitvector(s->polarity);

  // literal-indexed arrays
  safe_free(s->value - 2);
  n = s->nlits;
  for (i=0; i<n; i++) {
    delete_literal_vector(s->bin[i]);
  }
  safe_free(s->bin);
  safe_free(s->watch);
#if USE_END_WATCH
  safe_free(s->end_watch);
#endif

  delete_stack(&s->stack);
  delete_heap(&s->heap);
  delete_lemma_queue(&s->lemmas);
  delete_atom_table(&s->atoms);
  delete_trail_stack(&s->trail_stack);
  delete_checkpoint_stack(&s->checkpoints);
}


/*
 * Reset: remove all variables, atoms, and clauses
 * - also calls reset on the attached theory solver
 * - we don't call atom_deleted for the solver.
 */
void reset_smt_core(smt_core_t *s) { 
  uint32_t i, n;
  clause_t **cl;

  s->status = STATUS_IDLE;

  // delete the clauses
  cl = s->problem_clauses;
  n = get_cv_size(cl);
  for (i=0; i<n; i++) {
    delete_clause(cl[i]);
  }
  reset_clause_vector(cl);

  cl = s->learned_clauses;
  n = get_cv_size(cl);
  for (i=0; i<n; i++) {
    delete_learned_clause(cl[i]);
  }
  reset_clause_vector(cl);

  ivector_reset(&s->binary_clauses);

  // delete binary-watched literal vectors
  n = s->nlits;
  for (i=0; i<n; i++) {
    delete_literal_vector(s->bin[i]);
  }

  reset_stack(&s->stack);
  reset_heap(&s->heap);
  reset_lemma_queue(&s->lemmas);
  reset_statistics(&s->stats);
  reset_atom_table(&s->atoms);
  reset_trail_stack(&s->trail_stack);
  reset_checkpoint_stack(&s->checkpoints);  
  s->cp_flag = false;

  // reset all counters
  s->nvars = 1;
  s->nlits = 2;
  s->nb_clauses = 0;
  s->nb_prob_clauses = 0;  // fixed 2010/08/10 (was missing)
  s->nb_bin_clauses = 0;
  s->nb_unit_clauses = 0;
  s->simplify_bottom = 0;
  s->simplify_props = 0;
  s->simplify_threshold = 0;
  s->decision_level = 0;
  s->base_level = 0;

  // heuristic parameters: it makes a difference to reset cla_inc
  s->cla_inc = INIT_CLAUSE_ACTIVITY_INCREMENT;
  s->inv_cla_decay = 1/CLAUSE_DECAY_FACTOR;
  s->scaled_random = (uint32_t) (VAR_RANDOM_FACTOR * VAR_RANDOM_SCALE);

  // reset conflict data
  s->inconsistent = false;
  s->theory_conflict = false;
  s->conflict = NULL;
  s->false_clause = NULL;

  // reset the theory solver
  s->th_ctrl.reset(s->th_solver);
}


#if USE_END_WATCH

/*
 * Reset the end_watch pointers of the empty lists after reallocation
 */
static void reset_end_watch(smt_core_t *s) {
  uint32_t i, n;

  n = s->nlits;
  for (i=0; i<n; i++) {
    if (s->watch[i] == NULL_LINK) {
      s->end_watch[i] = &s->watch[i];
    }
  }
}

#endif

/*
 * Extend solver: make room for more variables
 * - n = new vsize
 */
static void extend_smt_core(smt_core_t *s, uint32_t n) {
  uint32_t lsize;

  assert(n >= s->vsize);

  if (n >= MAX_VARIABLES) {
    out_of_memory();
  }

  lsize = 2 * n;
  s->vsize = n;
  s->lsize = lsize;

  s->antecedent = (antecedent_t *) safe_realloc(s->antecedent, n * sizeof(antecedent_t));
  s->level = (uint32_t *) safe_realloc(s->level - 1, (n + 1) * sizeof(uint32_t)) + 1;
  s->mark = extend_bitvector(s->mark, n);
  s->polarity = extend_bitvector(s->polarity, n);

  s->value = (uint8_t *) safe_realloc(s->value - 2, (lsize + 2) * sizeof(uint8_t)) + 2;
  s->bin = (literal_t **) safe_realloc(s->bin, lsize * sizeof(literal_t *));
  s->watch = (link_t *) safe_realloc(s->watch, lsize * sizeof(link_t));
#if USE_END_WATCH
  s->end_watch = (link_t **) safe_realloc(s->end_watch, lsize * sizeof(link_t *));
  reset_end_watch(s);
#endif

  extend_heap(&s->heap, n);
  extend_stack(&s->stack, n);
}



/*
 * Change the heuristic parameters:
 * - must not be called when search is under way
 * - factor must be between 0 and 1.0
 */
void set_var_decay_factor(smt_core_t *s, double factor) {
  assert(s->status != STATUS_SEARCHING && 0.0 < factor && factor < 1.0);
  s->heap.inv_act_decay = 1/factor;
}

void set_clause_decay_factor(smt_core_t *s, float factor) {
  assert(s->status != STATUS_SEARCHING && 0.0F < factor && factor < 1.0F);
  s->inv_cla_decay = 1/factor;
}

void set_randomness(smt_core_t *s, float random_factor) {
  assert(s->status != STATUS_SEARCHING && 0.0F <= random_factor && random_factor < 1.0F);
  s->scaled_random = (uint32_t)(random_factor * VAR_RANDOM_SCALE);
}


/*
 * Set the internal seed 
 */
void smt_set_seed(uint32_t x) {
  random_seed(x);
}




/**************************
 *  VARIABLE ALLOCATION   *
 *************************/

/*
 * Initialize all arrays for a new variable x
 * - antecedent[x] = NULL
 * - level[x] = UINT32_MAX
 * - mark[x] = 0
 * - polarity[x] = 0 (negative polarity preferred)
 * - activity[x] = 0 (in heap)
 *
 * For l=pos_lit(x) or neg_lit(x):
 * - value[l] = VAL_UNDEF
 * - bin[l] = NULL
 * - watch[l] = NULL
 */
static void init_variable(smt_core_t *s, bvar_t x) {
  literal_t l0, l1;

  clr_bit(s->mark, x);
  clr_bit(s->polarity, x);
  s->level[x] = UINT32_MAX;
  s->antecedent[x] = mk_literal_antecedent(null_literal);

  // HACK for testing initial order
  //  assert(s->heap.heap_index[x] < 0);
  //  s->heap.activity[x] = (10000.0/(x+1));
  // end of HACK
  heap_insert(&s->heap, x);

  l0 = pos_lit(x);
  l1 = neg_lit(x);
  s->value[l0] = VAL_UNDEF;
  s->value[l1] = VAL_UNDEF;
  s->bin[l0] = NULL;
  s->bin[l1] = NULL;
  s->watch[l0] = NULL_LINK;
  s->watch[l1] = NULL_LINK;
#if USE_END_WATCH
  s->end_watch[l0] = &s->watch[l0];
  s->end_watch[l1] = &s->watch[l1];
#endif
  
}

/*
 * Create a fresh variable and return its index
 * - the index is x = s->nvars
 */
bvar_t create_boolean_variable(smt_core_t *s) {
  uint32_t new_size, i;

  i = s->nvars;
  if (i >= s->vsize) {
    new_size = s->vsize + 1;
    new_size += new_size >> 1;
    extend_smt_core(s, new_size);
    assert(i < s->vsize);
  }

  init_variable(s, i);
  s->nvars ++;
  s->nlits += 2;

  return i;
}

/*
 * Add n fresh boolean variables: indices are allocated starting
 * from s->nvars (i.e., if s->nvars == v before the call, the 
 * new variables have indices v, v+1, ... v+n-1).
 */
void add_boolean_variables(smt_core_t *s, uint32_t n) {
  uint32_t nv, new_size, i;

  nv = s->nvars;
  if (nv + n > s->vsize) {
    new_size = s->vsize + 1;
    new_size += new_size >> 1;
    if (new_size < nv + n) {
      new_size = nv + n;
    }
    extend_smt_core(s, new_size);
    assert(nv + n <= s->vsize);
  }

  for (i=nv; i<nv+n; i++) {
    init_variable(s, i);
  }

  s->nvars += n;
  s->nlits += 2 * n;
}



/*
 * Attach atom a to boolean variable x
 * - x must not have an atom attached already
 */
void attach_atom_to_bvar(smt_core_t *s, bvar_t x, void *atom) {
  atom_table_t *tbl;

  tbl = &s->atoms;
  if (tbl->size <= x) {
    // make atom table as large as s->vsize
    resize_atom_table(tbl, s->vsize);
  }
  add_atom(tbl, x, atom);
}


/*
 * Check whether x has an atom attached
 */
bool bvar_has_atom(smt_core_t *s, bvar_t x) {
  atom_table_t *tbl;

  assert(0 <= x && x < s->nvars);
  tbl = &s->atoms;
  return x < tbl->size && tst_bit(tbl->has_atom, x);
}


/*
 * Return the atom attached to x (NULL if there's none)
 */
void *bvar_atom(smt_core_t *s, bvar_t x) {
  atom_table_t *tbl;

  assert(0 <= x && x < s->nvars);
  tbl = &s->atoms;
  if (x < tbl->size && tst_bit(tbl->has_atom, x)) {
    return tbl->atom[x];
  } else {
    return NULL;
  }
}


/*
 * Remove atom attached to x
 */
void remove_bvar_atom(smt_core_t *s, bvar_t x) {
  atom_table_t *tbl;

  assert(0 <= x && x < s->nvars);
  tbl = &s->atoms;
  if (x < tbl->size && tst_bit(tbl->has_atom, x)) {
    remove_atom(tbl, x);
  }
}


/*
 * Set the initial activity of variable x
 */
void set_bvar_activity(smt_core_t *s, bvar_t x, double a) {
  assert(0 <= x && x < s->nvars && a < DBL_MAX);
  heap_remove(&s->heap, x);
  s->heap.activity[x] = a;
  heap_insert(&s->heap, x);
}





/**************************
 *  VARIABLE ASSIGNMENTS  *
 *************************/

/*
 * Assign literal l at the base level
 */
static void assign_literal(smt_core_t *s, literal_t l) {
  bvar_t v;

#if TRACE
  printf("---> DPLL:   Assigning literal ");
  print_literal(stdout, l);
  printf(", decision level = %"PRIu32"\n", s->decision_level);
#endif
  assert(0 <= l && l < s->nlits);
  assert(s->value[l] == VAL_UNDEF);
  assert(s->decision_level == s->base_level);

  s->value[l] = VAL_TRUE;
  s->value[not(l)] = VAL_FALSE;
  push_literal(&s->stack, l);

  v = var_of(l);
  s->level[v] = s->base_level;
  s->antecedent[v] = mk_literal_antecedent(null_literal);
  set_bit(s->mark, v); // assigned at (or below) base_level
}


/*
 * Decide literal: increase decision level then
 * assign literal l to true and push it on the stack
 */
void decide_literal(smt_core_t *s, literal_t l) {
  uint32_t k;
  bvar_t v;

  assert(s->status == STATUS_SEARCHING && s->value[l] == VAL_UNDEF);

  s->stats.decisions ++;

  // Increase decision level
  k = s->decision_level + 1;
  s->decision_level = k;
  if (s->stack.nlevels <= k) {
    increase_stack_levels(&s->stack);
  }
  s->stack.level_index[k] = s->stack.top;

  s->value[l] = VAL_TRUE;
  s->value[not(l)] = VAL_FALSE;
  push_literal(&s->stack, l);

  v = var_of(l);
  s->level[v] = k;
  s->antecedent[v] = mk_literal_antecedent(null_literal);

  // Notify the theory solver
  s->th_ctrl.increase_decision_level(s->th_solver);
 
#if TRACE
  printf("\n---> DPLL:   Decision: literal ");
  print_literal(stdout, l);
  printf(", decision level = %"PRIu32"\n", s->decision_level);
#endif
}



/*
 * Assign literal l to true with the given antecedent
 * - s->mark[v] is set if decision level = base level
 */
static void implied_literal(smt_core_t *s, literal_t l, antecedent_t a) {
  bvar_t v;

  assert(s->value[l] == VAL_UNDEF);

#if TRACE
  printf("---> DPLL:   Implied literal ");
  print_literal(stdout, l);
  printf(", decision level = %"PRIu32"\n", s->decision_level);
#endif

  s->stats.propagations ++;

  s->value[l] = VAL_TRUE;
  s->value[not(l)] = VAL_FALSE;
  push_literal(&s->stack, l);

  v = var_of(l);
  s->level[v] = s->decision_level;
  s->antecedent[v] = a;
  if (s->decision_level == s->base_level) {
    set_bit(s->mark, v);
    s->nb_unit_clauses ++;
  }
}


void propagate_literal(smt_core_t *s, literal_t l, void *expl) {
  bvar_t v;

  assert(s->value[l] == VAL_UNDEF);
  assert(bvar_has_atom(s, var_of(l))); 

#if TRACE
  printf("---> DPLL:   Theory prop ");
  print_literal(stdout, l);
  printf(", decision level = %"PRIu32"\n", s->decision_level);
#endif

  s->stats.propagations ++;
  s->stats.th_props ++;

  s->value[l] = VAL_TRUE;
  s->value[not(l)] = VAL_FALSE;
  push_literal(&s->stack, l);

  v = var_of(l);
  s->level[v] = s->decision_level;
  s->antecedent[v] = mk_generic_antecedent(expl);
  if (s->decision_level == s->base_level) {
    set_bit(s->mark, v);
    s->nb_unit_clauses ++;
  }
}


/***************************
 *  HEURISTICS/ACTIVITIES  *
 **************************/

/*
 * Counter to test PRNG
 */
static uint32_t nrnd = 0;

double random_tries_fraction(smt_core_t *s) {
  return ((double) nrnd)/s->stats.decisions;
}

/*
 * Select an unassigned literal: returns null_literal if all literals 
 * are assigned. Use activity-based heuristic + randomization.
 */
literal_t select_unassigned_literal(smt_core_t *s) {
  uint32_t rnd;
  bvar_t x;
  uint8_t *v;

#if DEBUG
  check_heap(s);
#endif

  v = s->value;

  rnd = random_uint32() & VAR_RANDOM_MASK;
  if (rnd < s->scaled_random) {
    nrnd ++;
    x = random_uint(s->nvars);
    assert(0 <= x && x < s->nvars);
    if (v[pos_lit(x)] == VAL_UNDEF) {
#if TRACE
      printf("---> DPLL:   Random selection: variable ");
      print_bvar(stdout, x);
      printf("\n");
      fflush(stdout);
#endif
      s->stats.random_decisions ++;
      goto var_found;
    }
  }

  /* 
   * select unassigned variable x with highest activity
   * HACK: this works (and terminates) even if the heap is empty,
   * because pos_lit(-1) = -2 and v[-2] = VAL_UNDEF.
   */
  do {
    x = heap_get_top(&s->heap);
  } while (v[pos_lit(x)] != VAL_UNDEF);

  if (x < 0) return null_literal;

  // if polarity[x] == 1 use pos_lit(x) otherwise use neg_lit(x)
 var_found:
  if (tst_bit(s->polarity, x)) {
    return pos_lit(x);
  } else {
    return neg_lit(x);
  }
}


#if 0

// OLD VERSION

/*
 * Select an unassigned literal: returns null_literal if all literals 
 * are assigned. Use activity-based heuristic + randomization.
 */
literal_t select_unassigned_literal(smt_core_t *s) {
  literal_t l;
  uint32_t rnd;
  bvar_t x;
  uint8_t *v;


#if DEBUG
  check_heap(s);
#endif

  v = s->value;

  rnd = random_uint32() & VAR_RANDOM_MASK;
  if (rnd < s->scaled_random) {
    nrnd ++;
    l = random_uint(s->nlits);
    if (v[l] == VAL_UNDEF) {
#if TRACE
      printf("---> DPLL:   Random selection: literal ");
      print_literal(stdout, l);
      printf("\n");
      fflush(stdout);
#endif
      s->stats.random_decisions ++;
      return l;
    }
  }

  /* 
   * select unassigned variable x with highest activity
   * HACK: this works (and terminates) even if the heap is empty,
   * because pos_lit(-1) = -2 and v[-2] = VAL_UNDEF.
   */
  do {
    x = heap_get_top(&s->heap);
  } while (v[pos_lit(x)] != VAL_UNDEF);

  if (x < 0) return null_literal;

  // if polarity[x] == 1 use pos_lit(x) otherwise use neg_lit(x)
  if (tst_bit(s->polarity, x)) {
    return pos_lit(x);
  } else {
    return neg_lit(x);
  }
}

#endif


/*
 * Get the unassigned variable of highest activity
 * return null_bvar if all variables are assigned
 */
bvar_t select_most_active_bvar(smt_core_t *s) {
  bvar_t x;
  uint8_t *v;

  v = s->value;
  do {
    x = heap_get_top(&s->heap);
  } while (v[pos_lit(x)] != VAL_UNDEF);

  return x;
}



/*
 * Choose an unassigned variable randomly (uniformly)
 * return null_bvar if all variables are assigned
 */
bvar_t select_random_bvar(smt_core_t *s) {
  bvar_t x, y;
  uint8_t *v;
  uint32_t n, d;

  v = s->value;
  n = s->nvars;
  x = random_uint(n); // 0 ... n-1
  if (v[pos_lit(x)] == VAL_UNDEF) return x;
  
  if (all_variables_assigned(s)) return null_bvar;

  // d = random increment, must be prime with n.
  d = 1 + random_uint(n - 1); // 1 ... n-1
  while (gcd32(d, n) != 1) d--;

  // search in sequence x, x+d, x+2d, ... (modulo n)
  y = x;
  do {
    y += d;
    if (y > n) y -= n;
    assert(x != y); // don't loop
  } while (v[pos_lit(y)] != VAL_UNDEF);

  return y;
}




#if 0

// Not needed?
/*
 * Reset heap and reinsert all variables that are not assigned
 */
static void init_variable_order(smt_core_t *s) {
  uint32_t x;

  reset_heap(&s->heap);
  for (x=0; x<s->nvars; x++) {
    if (s->value[pos_lit(x)] == VAL_UNDEF) {
      heap_insert(&s->heap, x);
    }
  }
}

#endif



/*
 * Increase activity of variable x
 */
static void increase_bvar_activity(smt_core_t *s, bvar_t x) {
  int32_t i;
  var_heap_t *heap;
#if DEBUG
  bool rescaled = false;
#endif

  heap = &s->heap;
  if ((heap->activity[x] += heap->act_increment) > VAR_ACTIVITY_THRESHOLD) {
    rescale_var_activities(heap, s->nvars);
#if DEBUG
    rescaled = true;
#endif
  }

  // move x up if it's in the heap
  i = heap->heap_index[x];
  if (i >= 0) {
    update_up(heap, x, i);
  }

#if DEBUG
  if (rescaled) {
    check_heap(s);
  }
#endif

}



/***********************
 *  CLAUSE ACTIVITIES  *
 **********************/

/*
 * Rescale activity of all the learned clauses
 * (divide all by CLAUSE_ACTIVITY_THRESHOLD)
 */
static void rescale_clause_activities(smt_core_t *s) {
  uint32_t i, n;
  clause_t **v;

  s->cla_inc *= INV_CLAUSE_ACTIVITY_THRESHOLD;
  v = s->learned_clauses;
  n = get_cv_size(v);
  for (i=0; i<n; i++) {
    multiply_activity(v[i], INV_CLAUSE_ACTIVITY_THRESHOLD);
  }
}


/*
 * Increase activity of learned clause cl
 */
static inline void increase_clause_activity(smt_core_t *s, clause_t *cl) {
  increase_activity(cl, s->cla_inc);
  if (get_activity(cl) > CLAUSE_ACTIVITY_THRESHOLD) {
    rescale_clause_activities(s);
  }
}




/*******************
 *  BACKTRACKING   *
 ******************/

/*
 * Backtrack core to decision level back_level
 * - undo all literal assignments of level >= back_level + 1
 * - requires decision_level > back_level >= base_level
 * Also clear conflict data and sets cp_flag if deletion of atoms is enabled
 *
 * NOTE: this function does not force the theory solver to backtrack.
 */
static void backtrack(smt_core_t *s, uint32_t back_level) {
  uint32_t i, k;
  literal_t *u, l;
  bvar_t x;

#if TRACE
  printf("---> DPLL:   Backtracking to level %"PRIu32"\n\n", back_level);
#endif

  assert(s->base_level <= back_level && back_level < s->decision_level);

  u = s->stack.lit;
  k = s->stack.level_index[back_level + 1];
  i = s->stack.top;
  while (i > k) {
    i --;
    l = u[i];

    assert(s->value[l] == VAL_TRUE);
    assert(s->level[var_of(l)] > back_level);

    s->value[l] = VAL_UNDEF;
    s->value[not(l)] = VAL_UNDEF;

    x = var_of(l);
    heap_insert(&s->heap, x);

    // save current polarity: 0 if l =neg_lit(x), 1 if l = pos_lit(x);
    assign_bit(s->polarity, x, is_pos(l));
  }

  s->stack.top = i;
  s->stack.prop_ptr = i;
  s->stack.theory_ptr = i;
  s->decision_level = back_level;

  // Update the cp_flag: the deletion of atoms is enabled if there's a checkpoint
  // and if the top checkpoint has level >= the new decision level
  s->cp_flag = non_empty_checkpoint_stack(&s->checkpoints) && 
    top_checkpoint(&s->checkpoints)->dlevel >= back_level;
}



/*
 * Cause both s and the theory solver to backtrack
 */
static inline void backtrack_to_level(smt_core_t *s, uint32_t back_level) {
  if (back_level < s->decision_level) {
    backtrack(s, back_level);
    s->th_ctrl.backtrack(s->th_solver, back_level);
  }
}

static inline void backtrack_to_base_level(smt_core_t *s) {
  backtrack_to_level(s, s->base_level);
}



/***************
 *  CONFLICTS  *
 **************/

/*
 * Record a two-literal conflict: clause {l0, l1} is false
 */
static inline void record_binary_conflict(smt_core_t *s, literal_t l0, literal_t l1) {
#if TRACE
  printf("\n---> DPLL:   Binary conflict: {");
  print_literal(stdout, l0);
  printf(", ");
  print_literal(stdout, l1);
  printf("}\n");
#endif

  assert(! s->theory_conflict);

  s->inconsistent = true;
  s->conflict_buffer[0] = l0;
  s->conflict_buffer[1] = l1; 
  s->conflict_buffer[2] = end_clause;
  s->conflict = s->conflict_buffer;  
}


/*
 * Record cl as a conflict clause
 */
static inline void record_clause_conflict(smt_core_t *s, clause_t *cl) {
#if TRACE
  uint32_t i;
  literal_t ll;

  printf("\n---> DPLL:   Conflict: {");
  print_literal(stdout, get_first_watch(cl));
  printf(", ");
  print_literal(stdout, get_second_watch(cl));
  i = 2;
  ll = cl->cl[i];
  while (ll >= 0) {
    printf(", ");
    print_literal(stdout, ll);
    i++;
    ll = cl->cl[i];
  }
  printf("}\n");
#endif

  assert(! s->theory_conflict);

  s->inconsistent = true;
  s->false_clause = cl;
  s->conflict = cl->cl;
}


/*
 * Externally generated conflict (i.e., by a theory solver)
 * - a must be an array of literals terminated by null_literal
 * - all literals in a must be false in the current assignement
 */
void record_theory_conflict(smt_core_t *s, literal_t *a) {
#if TRACE
  uint32_t i;
  literal_t l;

  printf("---> DPLL:   Theory conflict: {");
  i = 0;
  l = a[i];
  if (l >= 0) {
    print_literal(stdout, l);
    i ++;
    l = a[i];
    while (l >= 0) {
      printf(", ");
      print_literal(stdout, l);
      i++;
      l = a[i];
    }
  }
  printf("}\n");
#endif

#if DEBUG
  check_theory_conflict(s, a);
#endif

  assert(! s->inconsistent && ! s->theory_conflict);
  s->stats.th_conflicts ++;
  s->inconsistent = true;
  s->theory_conflict = true;
  s->false_clause = NULL;
  s->conflict = a;
}


/*
 * Short cuts
 */
void record_empty_theory_conflict(smt_core_t *s) {
  s->conflict_buffer[0] = null_literal;  
  record_theory_conflict(s, s->conflict_buffer);
}

void record_unit_theory_conflict(smt_core_t *s, literal_t l) {
  s->conflict_buffer[0] = l;
  s->conflict_buffer[1] = null_literal;
  record_theory_conflict(s, s->conflict_buffer);
}

void record_binary_theory_conflict(smt_core_t *s, literal_t l1, literal_t l2) {
  s->conflict_buffer[0] = l1;
  s->conflict_buffer[1] = l2;
  s->conflict_buffer[2] = null_literal;
  record_theory_conflict(s, s->conflict_buffer);
}

void record_ternary_theory_conflict(smt_core_t *s, literal_t l1, literal_t l2, literal_t l3) {
  s->conflict_buffer[0] = l1;
  s->conflict_buffer[1] = l2;
  s->conflict_buffer[2] = l3;
  s->conflict_buffer[3] = null_literal;
  record_theory_conflict(s, s->conflict_buffer);
}



/*************************
 *  BOOLEAN PROPAGATION  *
 ************************/

/*
 * Propagation via binary clauses:
 * - val = literal value array (must be s->value)
 * - l0 = literal (must be false in the current assignment)
 * - v = array of literals terminated by -1 (must be s->bin[l])
 * v must be != NULL 
 *
 * Return true if there's no conflict, false otherwise.
 */
static inline bool propagation_via_bin_vector(smt_core_t *s, uint8_t *val, literal_t l0, literal_t *v) {
  literal_t l1;
  bval_t v1;

  assert(v != NULL);
  assert(s->value == val && s->bin[l0] == v && s->value[l0] == VAL_FALSE);

  for (;;) {
    // Search for non-true literals in v
    // This terminates since val[end_marker] = VAL_UNDEF
    do {
      l1 = *v ++;
      v1 = val[l1];
    } while (v1 == VAL_TRUE);

    if (l1 < 0) break; // end_marker

    if (v1 == VAL_UNDEF) {
      implied_literal(s, l1, mk_literal_antecedent(l0));
    } else {
      record_binary_conflict(s, l0, l1);
      return false;
    }
  }

  return true;
}


#if 1

/*
 * Propagation via the watched lists of a literal l0.
 * - val = literal value array (must be s->value)
 *
 * Return true if there's no conflict, false otherwise
 */
static inline bool propagation_via_watched_list(smt_core_t *s, uint8_t *val, literal_t l0) {
  clause_t *cl;
  link_t *list;
  link_t link;
  bval_t v1;
  uint32_t k, i;
  literal_t l1, l, *b;

  assert(s->value == val);

  list = &s->watch[l0];
  link = *list;
  while (link != NULL_LINK) {
    cl = clause_of(link);
    i = idx_of(link);
    l1 = get_other_watch(cl, i);
    v1 = val[l1];

    assert(next_of(link) == cl->link[i]);
    assert(cdr_ptr(link) == cl->link + i);

    if (v1 == VAL_TRUE) {
      /*
       * Skip clause cl: it's already true
       */
      *list = link;
      list = cl->link + i;
      link = cl->link[i];

    } else {
      /*
       * Search for a new watched literal in cl.
       * The loop terminates since cl->cl terminates with an end marked 
       * and val[end_marker] == VAL_UNDEF.
       */
      k = 1;
      b = cl->cl;
      do {
	k ++;
	l = b[k];
      } while (val[l] == VAL_FALSE);
      
      if (l >= 0) {
	/*
	 * l occurs in b[k] = cl->cl[k] and is either TRUE or UNDEF
	 * make l a new watched literal
	 * - swap b[i] and b[k]
         * - insert cl into l's watched list (link[i])
	 */
	b[k] = b[i];
	b[i] = l;

	// insert cl in watch[l] list and move to the next clause
	link = cl->link[i];
#if USE_END_WATCH
	cl->link[i] = NULL_LINK;
	*s->end_watch[l] = mk_link(cl, i);
	s->end_watch[l] = &cl->link[i];
#else
	s->watch[l] = cons(i, cl, s->watch[l]);
#endif

      } else {
	/*
	 * All literals of cl, except possibly l1, are false
	 */
	if (v1 == VAL_UNDEF) {
	  // l1 is implied
	  implied_literal(s, l1, mk_clause_antecedent(cl, i^1));

	  // move to the next clause
	  *list = link;
	  list = cl->link + i;
	  link = cl->link[i];

	} else {
	  // v1 == VAL_FALSE: conflict found
	  record_clause_conflict(s, cl);
	  *list = link;
	  return false;
	}
      }
    }
  }

  *list = NULL_LINK;
#if USE_END_WATCH
  s->end_watch[l0] = list;
#endif

  return true;
}

#else

/*
 * VARIANT IMPLEMENTATION: DON'T LOOK AT OTHER WATCH LITERAL IMMEDIATELY
 *
 * Propagation via the watched lists of a literal l0.
 * - val = literal value array (must be s->value)
 *
 * Return true if there's no conflict, false otherwise
 */
static inline bool propagation_via_watched_list(smt_core_t *s, uint8_t *val, literal_t l0) {
  clause_t *cl;
  link_t *list;
  link_t link;
  bval_t v1;
  uint32_t k, i;
  literal_t l1, l, *b;

  assert(s->value == val);

  list = &s->watch[l0];
  link = *list;
  while (link != NULL_LINK) {
    cl = clause_of(link);
    i = idx_of(link);

    assert(next_of(link) == cl->link[i]);
    assert(cdr_ptr(link) == cl->link + i);

    /*
     * Search for a new watched literal in cl.
     * The loop terminates since cl->cl terminates with an end marked 
     * and val[end_marker] == VAL_UNDEF.
     */
    k = 1;
    b = cl->cl;
    do {
      k ++;
      l = b[k];
    } while (val[l] == VAL_FALSE);
      
    if (l >= 0) {
      /*
       * l occurs in b[k] = cl->cl[k] and is either TRUE or UNDEF
       * make l a new watched literal
       * - swap b[i] and b[k]
       * - insert cl into l's watched list (link[i])
       */
      b[k] = b[i];
      b[i] = l;

      // insert cl in watch[l] list and move to the next clause
      link = cl->link[i];
#if USE_END_WATCH
      cl->link[i] = NULL_LINK;
      *s->end_watch[l] = mk_link(cl, i);
      s->end_watch[l] = &cl->link[i];
#else
      s->watch[l] = cons(i, cl, s->watch[l]);
#endif

    } else {

      l1 = get_other_watch(cl, i);
      v1 = val[l1];

      /*
       * All literals of cl, except possibly l1, are false
       */
      switch (v1) {
      case VAL_UNDEF:
	// l1 is implied
	implied_literal(s, l1, mk_clause_antecedent(cl, i^1));
	// fall-through intended

      case VAL_TRUE:
	// move to the next clause
	*list = link;
	list = cl->link + i;
	link = cl->link[i];
	break;

      case VAL_FALSE:
	// v1 == VAL_FALSE: conflict found
	record_clause_conflict(s, cl);
	*list = link;
	return false;
      }
    }
  }

  *list = NULL_LINK;
#if USE_END_WATCH
  s->end_watch[l0] = list;
#endif

  return true;
}

#endif



#if 1

/*
 * Full boolean propagation: until either the propagation queue is empty,
 * or a conflict is found
 * - result = true if no conflict, false otherwise
 */
static bool boolean_propagation(smt_core_t *s) {
  uint8_t *val;
  literal_t *queue;
  literal_t l, *bin;
  uint32_t i;

  val = s->value;
  queue = s->stack.lit;

  for (i = s->stack.prop_ptr; i < s->stack.top; i++) {
    l = not(queue[i]);
    
    bin = s->bin[l];
    if (bin != NULL && ! propagation_via_bin_vector(s, val, l, bin)) {
      // conflict found
      return false;
    }
    
    if (! propagation_via_watched_list(s, val, l)) {
      return false;
    }
  }

  s->stack.prop_ptr = i;

#if DEBUG
  check_propagation(s);
#endif

  return true;
}

#else

/*
 * Variant: do propagation via only the binary vectors before a 
 * full propagation via the watched lists.
 */
static bool boolean_propagation(smt_core_t *s) {
  uint8_t *val;
  literal_t *queue;
  literal_t l, *bin;
  uint32_t i, j;

  val = s->value;
  queue = s->stack.lit;

  i = s->stack.prop_ptr;
  j = i;
  for (;;) {
    if (i < s->stack.top) {
      l = not(queue[i]);
      i ++;
      bin = s->bin[l];
      if (bin != NULL && ! propagation_via_bin_vector(s, val, l, bin)) {
	return false;
      }
    } else if (j < s->stack.top) {
      l = not(queue[j]);
      j ++;
      if (! propagation_via_watched_list(s, val, l)) {
	return false;
      }
    } else {
      break;
    }
  }
  s->stack.prop_ptr = i;

#if DEBUG
  check_propagation(s);
#endif

  return true;
}

#endif


/**************************************
 *  PROPAGATION TO THE THEORY SOLVER  *
 *************************************/

/*
 * Propagate all atom assignments to the theory solver
 * - return true if no conflict is found
 * - return false otherwise
 */
static bool theory_propagation(smt_core_t *s) {
  uint32_t i, n;
  byte_t *has_atom;
  void **atom;
  literal_t *queue;
  literal_t l;
  bvar_t x;

  /*
   * IMPORTANT: make sure the theory_solver does not 
   * create new atoms within the assert_atom function
   */
  n = s->atoms.size;
  has_atom = s->atoms.has_atom;
  atom = s->atoms.atom;
  queue = s->stack.lit;

  for (i = s->stack.theory_ptr; i < s->stack.top; i++) {
    l = queue[i];
    x = var_of(l);
    if (x < n && tst_bit(has_atom, x)) {
      if (! s->th_smt.assert_atom(s->th_solver, atom[x], l)) {
	// theory conflict reported
	//	assert(s->inconsistent && s->theory_conflict);
	/*
	 * HACK: Changed this assert because the bvsolver adds the empty clause
	 * rather than create a theory conflict.
	 */
	assert(s->inconsistent);
	return false;
      }
    }
  }

  s->stack.theory_ptr = i;

  return s->th_ctrl.propagate(s->th_solver);
}



/************************
 *   FULL PROPAGATION   *
 ***********************/

/*
 * Propagate all boolean assignments and all atoms,
 * repeat if the theory solver has implied some literals
 * - return false if a conflict is detected
 * - return true otherwise
 */
static bool smt_propagation(smt_core_t *s) {
  bool code;
  uint32_t n;

  if (s->atoms.natoms == 0) {
    // purely boolean problem
    return boolean_propagation(s);
  }

  do {
    code = boolean_propagation(s);
    if (! code) break;
    n = s->stack.top;
    code = theory_propagation(s);
  } while (code && n < s->stack.top);

  return code;
}




/***********************************
 *  MARKS FOR CONFLICT RESOLUTION  *
 **********************************/

static inline bool is_var_unmarked(smt_core_t *s, bvar_t x) {
  return ! tst_bit(s->mark, x);
}

static inline bool is_var_marked(smt_core_t *s, bvar_t x) {
  return tst_bit(s->mark, x);
}

static inline void set_var_mark(smt_core_t *s, bvar_t x) {
  set_bit(s->mark, x);
}

static inline void clr_var_mark(smt_core_t *s, bvar_t x) {
  clr_bit(s->mark, x);
}


static inline bool is_lit_unmarked(smt_core_t *s, literal_t l) {
  return ! tst_bit(s->mark, var_of(l));
}

static inline bool is_lit_marked(smt_core_t *s, literal_t l) {
  return tst_bit(s->mark, var_of(l));
}

static inline void set_lit_mark(smt_core_t *s, literal_t l) {
  set_bit(s->mark, var_of(l));
}

static inline void clear_lit_mark(smt_core_t *s, literal_t l) {
  clr_bit(s->mark, var_of(l));
}


/*
 * Decision level for assigned literal l
 */
static inline uint32_t d_level(smt_core_t *s, literal_t l) {
  return s->level[var_of(l)];
}



/*********************
 *  LEARNED CLAUSES  *
 ********************/

/*
 * Auxiliary function: add { l1, l2} as a binary clause
 * - l1 and l2 must be distinct (and not complementary)
 * - we put the function here because it's used by add_learned_clause
 */
static void direct_binary_clause(smt_core_t *s, literal_t l1, literal_t l2) {
#if TRACE
  printf("---> DPLL:   Add binary clause: { ");
  print_literal(stdout, l1);
  printf(" ");
  print_literal(stdout, l2);
  printf(" }\n");
#endif

  add_literal_to_vector(s->bin + l1, l2);
  add_literal_to_vector(s->bin + l2, l1);
  s->nb_bin_clauses ++;

  if (s->base_level > 0) {
    // make a copy for push/pop
    ivector_push(&s->binary_clauses, l1);
    ivector_push(&s->binary_clauses, l2);
  }
}


/*
 * Add an array of literals a as a new learned clause, after conflict resolution.
 * - n must be at least 1
 * - all literals must be assigned to false
 * - a[0] must be the implied literal: all other literals must have
 *   a lower assignment level than a[0].
 * - backtrack to the decision_level where a[0] is implied, then
 *   add a[0] to the propagation queue
 */
static void add_learned_clause(smt_core_t *s, uint32_t n, literal_t *a) {
  clause_t *cl;
  uint32_t i, j, k, q;
  literal_t l0, l1;
  
#if TRACE
  printf("---> DPLL:   Learned clause: {");
  for (i=0; i<n; i++) {
    printf(" ");
    print_literal(stdout, a[i]);
  }
  printf(" }\n");
#endif

  l0 = a[0];

  if (n == 1) { 

    backtrack_to_base_level(s);
    if (s->value[l0] == VAL_FALSE) {
      // conflict (the whole thing is unsat)
      s->inconsistent = true;
      s->conflict = s->conflict_buffer;
      s->conflict_buffer[0] = l0;
      s->conflict_buffer[1] = end_clause;
    } else {
#if TRACE
      printf("---> DPLL:   Add learned unit clause: { ");
      print_literal(stdout, l0);
      printf(" }\n");
#endif
      assign_literal(s, l0);
      s->nb_unit_clauses ++;
    }
 
  } else if (n == 2) {

    l1 = a[1];
    k = s->level[var_of(l1)];
    assert(k < s->level[var_of(l0)]);

    direct_binary_clause(s, l0, l1);
    backtrack_to_level(s, k);
    implied_literal(s, l0, mk_literal_antecedent(l1));

  } else {

    // find literal of second highest level in a[0 ... n-1]
    j = 1;
    k = s->level[var_of(a[1])];
    for (i=2; i<n; i++) {
      q = s->level[var_of(a[i])];
      if (q > k) {
	k = q;
	j = i;
      }
    }

    // swap a[1] and a[j]
    l1 = a[j]; a[j] = a[1]; a[1] = l1;

    // create the new clause with l0 and l1 as watched literals
    cl = new_learned_clause(n, a);
    add_clause_to_vector(&s->learned_clauses, cl);
    increase_clause_activity(s, cl);

#if USE_END_WATCH
    // add cl at the end of the watch lists of l0 and l1
    cl->link[0] = NULL_LINK;
    cl->link[1] = NULL_LINK;

    *s->end_watch[l0] = mk_link(cl, 0);
    s->end_watch[l0] = &cl->link[0];

    *s->end_watch[l1] = mk_link(cl, 1);
    s->end_watch[l1] = &cl->link[1];
#else
    // add cl at the start of watch[l0] and watch[l1]
    s->watch[l0] = cons(0, cl, s->watch[l0]);
    s->watch[l1] = cons(1, cl, s->watch[l1]);
#endif

    s->nb_clauses ++;
    s->stats.learned_literals += n;

    // backtrack and assert l0
    assert(k < s->level[var_of(l0)]);
    backtrack_to_level(s, k);

    implied_literal(s, l0, mk_clause0_antecedent(cl));
  }
}


/*
 * Auxiliary function: attempt to add a[0] ... a[n-1] as a learned clause
 * - search for two literals of decision_level == s->decision level then
 *   use them as watched literals.
 * - if such literals can't be found don't do anything
 * Return true if the clause was added/false otherwise
 */
static bool try_cache_theory_clause(smt_core_t *s, uint32_t n, literal_t *a) {
  clause_t *cl;
  uint32_t i, j, d;
  literal_t l, l0, l1;

  d = s->decision_level;

  if (n == 2) {
    // add as binary clause
    if (d_level(s, a[0]) == d && d_level(s, a[1]) == d) {
      direct_binary_clause(s, a[0], a[1]);

#if TRACE
      printf("---> DPLL: cached theory clause: { ");
      print_literal(stdout, a[0]);
      printf(" ");
      print_literal(stdout, a[1]);
      printf(" }\n");
#endif
      return true;
    }

    return false;

  } else if (n > 2) {

    i = 0;
    while (i<n) { 
      l = a[i];
      if (d_level(s, l) == d) goto found0;
      i ++;
    }
    return false;

  found0:
    j = i;
    l0 = l;  // l0 == a[j] == first watched literal
    i ++;
    while (i<n) {
      l = a[i];
      if (d_level(s, l) == d) goto found1;
      i ++;
    }
    return false;

  found1:
    l1 = l; // l1 == a[i] == second watched literal

    assert(l0 != l1 && j < i && d_level(s, l0) == d && d_level(s, l1) == d);

    // swap a[0] and a[j] == l0, swap a[1] and a[i] == l1
    a[j] = a[0]; a[0] = l0;
    a[i] = a[1]; a[1] = l1;

#if TRACE
    printf("---> DPLL: cached theory clause: {");
    for (i=0; i<n; i++) {
      printf(" ");
      print_literal(stdout, a[i]);
    }
    printf(" }\n");
#endif

    // create the new clause with l0 and l1 as watched literals
    cl = new_learned_clause(n, a);
    add_clause_to_vector(&s->learned_clauses, cl);
    increase_clause_activity(s, cl);

#if USE_END_WATCH
    // add cl at the end of the watch lists of l0 and l1
    cl->link[0] = NULL_LINK;
    cl->link[1] = NULL_LINK;

    *s->end_watch[l0] = mk_link(cl, 0);
    s->end_watch[l0] = &cl->link[0];

    *s->end_watch[l1] = mk_link(cl, 1);
    s->end_watch[l1] = &cl->link[1];
#else
    // insert cl at the head of watch[l0] and watch[l1]
    s->watch[l0] = cons(0, cl, s->watch[l0]);
    s->watch[l1] = cons(1, cl, s->watch[l1]);
#endif

    s->nb_clauses ++;
    s->stats.learned_literals += n;
    return true;
  }

  return false;
}


/*
 * Attempt to add a theory conflict as a learned clause
 * - a = array of literals
 * - n = size fo the array
 * - all literals a[0] ... a[n-1] must be false
 * The clause is added if we can find two literals of level == s->decision_level.
 * Uses s->buffer2.
 */
static void try_cache_theory_conflict(smt_core_t *s, uint32_t n, literal_t *a) {
  ivector_t *v;
  uint32_t i;
  literal_t l;

  if (n < 2 || n > s->th_cache_cl_size) return;

  v = &s->buffer2;
  assert(v->size == 0);

  // remove literals false at the base level
  for (i=0; i<n; i++) {
    l = a[i];
    assert(s->value[l] == VAL_FALSE && d_level(s, l) <= s->decision_level);
    if (d_level(s, l) > s->base_level) {
      ivector_push(v, l);
    }
  }

  // remove duplicate literals then try to add
  ivector_remove_duplicates(v);
  if (try_cache_theory_clause(s, v->size, v->data)) {
    s->stats.th_conflict_lemmas ++;
  }

  ivector_reset(v);
}



/*
 * Attempt to add a theory implication "(a[0] and ... and a[n-1]) implies l0"
 * as a learned clause.
 * - a[0] ... a[n-1] and l0 must all be true
 * - l0 must be assigned with level == current decision level
 * The clause is added if we can find another literal of that level among a[0 .. n-1].
 * Uses s->buffer2.
 */
static void try_cache_theory_implication(smt_core_t *s, uint32_t n, literal_t *a, literal_t l0) {
  ivector_t *v;
  uint32_t i;
  literal_t l;

  if (n == 0 || n >= s->th_cache_cl_size) return;

  v = &s->buffer2;
  assert(v->size == 0);

  assert(d_level(s, l0) == s->decision_level && s->value[l0] == VAL_TRUE);
  ivector_push(v, l0);
  
  // turn the implication into a clause
  // ignore literals assigned at the base level
  for (i=0; i<n; i++) {
    l = a[i];
    assert(s->value[l] == VAL_TRUE && d_level(s, l) <= s->decision_level);
    if (d_level(s, l) > s->base_level) {
      ivector_push(v, not(l));
    }
  }

  // now v->data contains l0 or (not a[0]) or ... or (not a[n-1])
  ivector_remove_duplicates(v);
  if (try_cache_theory_clause(s, v->size, v->data)) {
    s->stats.th_prop_lemmas ++;
  }

  ivector_reset(v);
}



/**************************************
 *  CONFLICT ANALYSIS AND RESOLUTION  *
 *************************************/

/*
 * Turn a generic antecedent into a conjunction of literals:
 * - store the literals in s->explanation
 *
 * IMPORTANT: the theory solver must ensure causality. All literals in s->explanation
 * must be before l in the assignment/propagation stack.
 */
static void explain_antecedent(smt_core_t *s, literal_t l, antecedent_t a) {
  assert(s->value[l] == VAL_TRUE && a == s->antecedent[var_of(l)] && 
	 antecedent_tag(a) == generic_tag);

  ivector_reset(&s->explanation);
  s->th_smt.expand_explanation(s->th_solver, l, generic_antecedent(a), &s->explanation);

#if DEBUG
  check_theory_explanation(s, l);
#endif
}


/*
 * Auxiliary function to accelerate clause simplification (cf. Minisat). 
 * This builds a hash of the decision levels in a literal array.
 * b = array of literals
 * n = number of literals
 */
static inline uint32_t signature(smt_core_t *s, literal_t *b, uint32_t n) {
  uint32_t i, u;

  u = 0;
  for (i=0; i<n; i++) {
    u |= 1 << (d_level(s, b[i]) & 31);
  }
  return u;
}

/*
 * Check whether decision level for literal l matches the hash sgn
 */
static inline bool check_level(smt_core_t *s, literal_t l, uint32_t sgn) {
  return (sgn & (1 << (d_level(s, l) & 31))) != 0;
}


/*
 * Analyze literal antecedents of not(l) to check whether l is subsumed.
 * - sgn = signature of the learned clause
 * level of l must match sgn (i.e., check_level(sol, l, sgn) is not 0).
 * 
 * - returns false if l is not subsumed: either because not(l) has no antecedents
 *   or if an antecedent of not(l) has a decision level that does not match sgn.
 * - returns true otherwise.
 *
 * Unmarked antecedents are marked and pushed into sol->buffer2.
 */
static bool analyze_antecedents(smt_core_t *s, literal_t l, uint32_t sgn) {
  bvar_t x;
  antecedent_t a;
  literal_t l1;
  uint32_t i;
  ivector_t *b;
  literal_t *c;

  x = var_of(l);
  a = s->antecedent[x];
  if (a == mk_literal_antecedent(null_literal)) {
    return false;
  }

  b = &s->buffer2;
  switch (antecedent_tag(a)) {
  case clause0_tag:
  case clause1_tag:
    c = clause_antecedent(a)->cl;
    i = clause_index(a);
    assert(c[i] == not(l));
    // other watched literal
    l1 = c[i^1];
    if (is_lit_unmarked(s, l1)) {
      // l1 has the same decision level as l so there's no need to call check_level
      set_lit_mark(s, l1);
      ivector_push(b, l1);
    }
    // rest of the clause
    i = 2;
    l1 = c[i];
    while (l1 >= 0) {
      if (is_lit_unmarked(s, l1)) {
	if (check_level(s, l1, sgn)) {
	  set_lit_mark(s, l1);
	  ivector_push(b, l1);
	} else {
	  return false;
	}
      }
      i ++;
      l1 = c[i];
    }
    break;

  case literal_tag:
    l1 = literal_antecedent(a);
    if (is_lit_unmarked(s, l1)) {
      set_lit_mark(s, l1);
      ivector_push(b, l1);
    }
    break;
    
  case generic_tag:
    explain_antecedent(s, not(l), a);
    c = s->explanation.data;
    // (and c[0] ... c[n-1]) implies (not l)
    for (i=0; i<s->explanation.size; i++) {
      l1 = not(c[i]);
      if (is_lit_unmarked(s, l1)) {
	if (check_level(s, l1, sgn)) {
	  set_lit_mark(s, l1);
	  ivector_push(b, l1);
	} else {
	  return false;
	}	
      }
    }
    break;
  }

  return true;
}


/*
 * Check whether literal l is subsumed by other marked literals
 * - sgn = signature of the learned clause (in which l occurs)
 * s->buffer2 is used as a queue
 */
static bool subsumed(smt_core_t *s, literal_t l, uint32_t sgn) {
  uint32_t i, n;
  ivector_t *b;

  b = &s->buffer2;
  n = b->size;
  i = n;
  while (analyze_antecedents(s, l, sgn)) {
    if (i < b->size) {
      l = b->data[i];
      i ++;
    } else {
      return true;
    }
  }

  // cleanup
  for (i=n; i<b->size; i++) {
    clear_lit_mark(s, b->data[i]);
  }
  b->size = n;

  return false;
}


/*
 * Simplification of a learned clause
 * - the clause is stored in s->buffer as an array of literals
 * - s->buffer[0] is the implied literal
 */
static void simplify_learned_clause(smt_core_t *s) {
  uint32_t hash;
  literal_t *b;
  literal_t l;
  uint32_t i, j, n;

  b = s->buffer.data;
  n = s->buffer.size;
  hash = signature(s, b+1, n-1); // skip b[0]. It cannot subsume anything.

  assert(s->buffer2.size == 0);

  // remove the subsumed literals
  j = 1;
  for (i=1; i<n; i++) {
    l = b[i];
    if (subsumed(s, l, hash)) { 
      // Hack: move l to buffer2 to clear its mark later
      ivector_push(&s->buffer2, l); 
    } else {
      // keep l in buffer
      b[j] = l;
      j ++;
    }
  }

  s->stats.literals_before_simpl += n;
  s->stats.subsumed_literals += n - j;
  s->buffer.size = j;

  // remove the marks of literals in learned clause
  for (i=0; i<j; i++) {
    clear_lit_mark(s, b[i]);
  }

  // remove the marks of subsumed literals
  b = s->buffer2.data;
  n = s->buffer2.size;
  for (i=0; i<n; i++) {
    clear_lit_mark(s, b[i]);
  }

  ivector_reset(&s->buffer2);
}




/*
 * Compute the conflict level of a conflict a:
 * - a must be an array of literals terminated by null_literal/end_clause
 *   or by end_learned (i.e., by a negative number)
 * - if a is empty, the conflict level is set to s->base_level
 *   otherwise conflict level = max { d_level(l) | l in the clause }
 * 
 * Also set s->th_conflict_size to the number of literals in the conflict clause.
 * This is used by the caching heuristics.
 *
 * Note: computing conflict level is necessary for theory conflicts.
 * For conflicts detected by boolean propagation, the conflict_level
 * is the same as the current decision_level
 */
static uint32_t get_conflict_level(smt_core_t *s, literal_t *a) {
  uint32_t k, q, i; 
  literal_t l;

  k = s->base_level;
  i = 0;
  for (;;) {
    l = a[i];
    if (l < 0) break;
    assert(s->value[l] == VAL_FALSE);
    q = d_level(s, l);
    if (q > k) { 
      k = q;
    }
    i ++;
  }

  s->th_conflict_size = i;

  return k;
}




/*
 * Search for first UIP and build the learned clause
 * d = solver state
 * - s->conflict stores a conflict clause (i.e., an array of literals 
 *   terminated by -1 or -2 with all literals false).
 *
 * result:
 * - the learned clause is stored in s->buffer as an array of literals
 * - s->buffer.data[0] is the implied literal
 */

#define process_literal(l)                    \
do {                                          \
  x = var_of(l);                              \
  if (is_var_unmarked(s, x)) {                \
    set_var_mark(s, x);                       \
    increase_bvar_activity(s, x);             \
    if (s->level[x] < conflict_level) {       \
      ivector_push(buffer, l);                \
    } else {                                  \
      unresolved ++;                          \
    }                                         \
  }                                           \
} while(0)


static void resolve_conflict(smt_core_t *s) {
  uint32_t i, j, conflict_level, unresolved;
  literal_t l, b;
  bvar_t x;
  literal_t *c,  *stack;
  antecedent_t a;
  clause_t *cl;
  ivector_t *buffer;

  assert(s->inconsistent);
  assert(s->theory_conflict || get_conflict_level(s, s->conflict) == s->decision_level);

  s->stats.conflicts ++;

  c = s->conflict;
  conflict_level = s->decision_level;

  /*
   * adjust conflict_level and backtrack to that level if the conflict
   * was reported by the theory solver.
   */
  if (s->theory_conflict) {
    conflict_level = get_conflict_level(s, c);
    assert(s->base_level <= conflict_level && conflict_level <= s->decision_level);
    backtrack_to_level(s, conflict_level);
    assert(s->decision_level == conflict_level);

    // Cache as a clause
    if (s->th_cache_enabled) {
      try_cache_theory_conflict(s, s->th_conflict_size, c);
    }
  }

  if (conflict_level == s->base_level) {
    // can't be resolved: unsat problem
    return;
  }

#if DEBUG
  check_marks(s);
#endif


  /*
   * buffer stores the new clause (built by resolution)
   */
  buffer = &s->buffer;
  ivector_reset(buffer);
  unresolved = 0;

  // reserve space for the implied literal
  ivector_push(buffer, null_literal); 

  /*
   * scan the conflict clause
   * - all literals of dl < conflict_level are added to buffer
   * - all literals are marked
   * - unresolved = number of literals in the conflict
   *   clause whose decision level is equal to conflict_level
   */
  l = *c;
  while (l >= 0) {
    process_literal(l);
    c ++;
    l = *c;
  }
  
  /*
   * If the conflict is a learned clause, increase its activity
   */
  if (l == end_learned) {
    increase_clause_activity(s, s->false_clause);
  }

  assert(unresolved > 0);

  /*
   * Scan the assignement stack from top to bottom and process the
   * antecedent of all marked literals:
   * - all the literals processed have decision_level == conflict_level
   * - the code works if unresolved == 1 (which may happen for theory conflicts)
   */
  stack = s->stack.lit;
  j = s->stack.top;
  for (;;) {
    j --;
    b = stack[j];
    assert(d_level(s, b) == conflict_level);
    if (is_lit_marked(s, b)) {
      if (unresolved == 1) {
	// not b is the implied literal; we're done.
	buffer->data[0] = not(b);
	break;

      } else {
	unresolved --;
	clear_lit_mark(s, b);
	a = s->antecedent[var_of(b)];
	/*
	 * Process b's antecedent:
	 */
	switch (antecedent_tag(a)) {
	case clause0_tag:
	case clause1_tag:
	  cl = clause_antecedent(a);
	  i = clause_index(a);
	  c = cl->cl;
	  assert(c[i] == b);
	  // process other watched literal
	  l = c[i^1];
	  process_literal(l);
	  // rest of the clause
	  c += 2;
	  l = *c;
	  while (l >= 0) {
	    process_literal(l);
	    c ++;
	    l = *c;
	  }
	  if (l == end_learned) {
	    increase_clause_activity(s, cl);
	  }
	  break;

	case literal_tag:
	  l = literal_antecedent(a);
	  process_literal(l);
	  break;

	case generic_tag:
	  explain_antecedent(s, b, a);
	  c = s->explanation.data;
	  // explanation is c[0] ... c[n-1] where ((and c[0] ... c[n-1]) implies b)
	  for (i=0; i<s->explanation.size; i++) {
	    l = not(c[i]);
	    assert(d_level(s, l) <= conflict_level);
	    process_literal(l);
	  }
	  // cache the implication as a learned clause
	  if (s->th_cache_enabled) {
	    assert(i == s->explanation.size);
	    try_cache_theory_implication(s, i, c, b);
	  }
	  break;
	}
      }
    }
  }

  /*
   * Simplify the learned clause and clear the marks
   */
  simplify_learned_clause(s);

#if DEBUG
  check_marks(s);
#endif

  /*
   * Clear the conflict flags
   */
  s->inconsistent = false;
  s->theory_conflict = false;

  /*
   * Add the learned clause: this causes backtracking 
   * and assert the implied literal
   */
  add_learned_clause(s, s->buffer.size, s->buffer.data);
}





/*************************************
 *  ADDITION OF LEMMAS AND CLAUSES   *
 ************************************/

/*
 * Before addition, clauses are simplified with respect to the
 * base-level assignment:
 * - if a clause contains a literal true at the base level, 
 *   or two complementary literals, then it's trivially true
 *   so it is ignored.
 * - otherwise all literals false at the base level are removed
 *   and duplicate literals are removed
 * - at this point, the clause contains no literals assigned 
 *   at the base level.
 *
 * For a clause added during the search, we examine its truth value
 * at the current decision level.
 * - the clause if true if one of its literals is true
 * - the clause if false if all its literals are false
 * - the clause is undef if it has some undef literals and all other
 *   literals are false.
 * To find watched literals:
 * - for a false clause, take two literals of highest d_level
 * - for an undef clause, take two unassigned literals if that's possible,
 *   otherwise take the unassigned literal + a false literal of highest d_level
 * - for a true clause, let 
 *       d = min { d_level(l) | l in clause and l is true }
 *   then the watched literals can be any literals of d_level >= d.
 * 
 * We backtrack if the clause is false or if it contains a single 
 * unassigned literal and all other literals are false.
 * - the backtrack level k is computed as follows:
 *   - for unit clauses, k = the base level
 *   - for a clause with a single undef literal.
 *       k = max { d_level(l) | l in clause and l is false }
 *   - for a false clause, sort literals in decreasing d_level order
 *       k = d_level of the second literal in this order
 * - after backtracking to level k: two cases are possible
 *   1) all literals are still false. We record the clause as a conflict.
 *      (this may overwrite an existing conflict but that's OK. We need
 *      to keep the conflict of lowest d_level anyway).
 *   2) one literal is unassigned, all other are false. We do a 
 *      unit propagation step (to make the unassigned literal true).
 *
 * If several clauses are added in succession, then one may cause a conflict
 * at level k0 that gets cleared later by another clause that causes backtracking
 * to a lower level.
 */   

/*
 * Create a (simplified) problem clause
 * - a must not contain duplicate literals
 * - a must not be trivially true at the base level
 * - a[0] and a[1] must be valid watched literals
 * - return the new clause
 */
static clause_t *new_problem_clause(smt_core_t *s, uint32_t n, literal_t *a) {
  clause_t *cl;
  literal_t l;

#if TRACE
  uint32_t i;
  printf("---> DPLL:   Add problem clause: {");
  for (i=0; i<n; i++) {
    printf(" ");
    print_literal(stdout, a[i]);
  }
  printf(" }\n");
#endif

  cl = new_clause(n, a);
  add_clause_to_vector(&s->problem_clauses, cl);

#if USE_END_WATCH
  // add cl at the end of watch lists
  cl->link[0] = NULL_LINK;
  cl->link[1] = NULL_LINK;

  l = a[0];
  *s->end_watch[l] = mk_link(cl, 0);
  s->end_watch[l] = &cl->link[0];

  l = a[1];
  *s->end_watch[l] = mk_link(cl, 1);
  s->end_watch[l] = &cl->link[1];

#else
  // add cl at the start of watch lists
  l = a[0];
  s->watch[l] = cons(0, cl, s->watch[l]);

  l = a[1];
  s->watch[l] = cons(1, cl, s->watch[l]);
#endif

  s->nb_prob_clauses ++;
  s->nb_clauses ++;
  s->stats.prob_literals += n;

  return cl;
}


/*
 * Add unit clause { l } after simplification
 * - l must not be assigned at the base level
 */
static void add_simplified_unit_clause(smt_core_t *s, literal_t l) {
#if TRACE
  printf("---> DPLL:   Add unit clause: { ");
  print_literal(stdout, l);
  printf(" }\n");
#endif

  if (s->inconsistent && s->decision_level > s->base_level) {
    s->inconsistent = false; // clear conflict
  }
  backtrack_to_base_level(s);
  assign_literal(s, l);
  s->nb_unit_clauses ++;
}


/*
 * Add binary clause { l0 l1 } after simplification
 * - l0 and l1 must not be assigned at the base level
 */
static void add_simplified_binary_clause(smt_core_t *s, literal_t l0, literal_t l1) {
  uint32_t k0, k1;
  bval_t v0, v1;

  direct_binary_clause(s, l0, l1); // add the clause

  if (s->base_level == s->decision_level) {
    assert(s->value[l0] == VAL_UNDEF && s->value[l1] == VAL_UNDEF);
    return;
  }

  k0 = UINT32_MAX; 
  k1 = UINT32_MAX;
  v0 = s->value[l0];
  if (v0 != VAL_UNDEF) k0 = s->level[var_of(l0)];
  v1 = s->value[l1];
  if (v1 != VAL_UNDEF) k1 = s->level[var_of(l1)];

  if (v0 == VAL_FALSE && k0 < k1) {
    // l1 implied at level k0
    if (k0 < s->decision_level) {
      backtrack_to_level(s, k0);
      s->inconsistent = false; // clear conflict if any
    }
    implied_literal(s, l1, mk_literal_antecedent(l0));    

  } else if (v1 == VAL_FALSE && k1 < k0) {
    // l0 implied at level k1
    if (k1 < s->decision_level) {
      backtrack_to_level(s, k1);
      s->inconsistent = false; // clear conflict if any
    }
    implied_literal(s, l0, mk_literal_antecedent(l1));

  } else if (v0 == VAL_FALSE && v1 == VAL_FALSE) {
    assert(k0 == k1);
    // conflict at level k0
    backtrack_to_level(s, k0);
    record_binary_conflict(s, l0, l1);
  }   
}


/*
 * For selecting watched literals, we pick two literals
 * that are minimal for a preference relation <
 *
 * To compare l and l', we look at 
 *  (v  k) where v = value of l and k = level of l
 *  (v' k') where v' = value of l' and k' = level of l'
 *
 * Rules:
 *  (true, _) < (undef, _) < (false, _)
 *  k < k' implies (true, k) < (true, k')
 *  k' < k implies (false, k) < (false, k')
 *
 * Other choices are possible but we must ensure
 *   (undef, _) < (false, _)
 *   k' < k implies (false, k) < (false, k')
 *
 * Prefer returns true if (v1, k1) < (v2, k2)
 */
static inline bool prefer(bval_t v1, uint32_t k1, bval_t v2, uint32_t k2) {
  if (v1 == v2) {
    return (v1 == VAL_TRUE && k1 < k2) || (v1 == VAL_FALSE && k1 > k2);
  } else {
    assert(VAL_TRUE > VAL_UNDEF && VAL_UNDEF > VAL_FALSE);
    return v1 > v2; 
  }
}

/*
 * Add simplified clause { a[0] ... a[n-1] }
 * - a[0] ... a[n-1] are not assigned at the base level
 * - n >= 3
 */
static void add_simplified_clause(smt_core_t *s, uint32_t n, literal_t *a) {
  uint32_t i, k0, k1, k;
  bval_t v0, v1, v;
  literal_t l;
  clause_t *cl;

  assert(n >= 3);

  if (s->base_level == s->decision_level) {
    new_problem_clause(s, n, a);
    return;
  }

  // find watched literals
  l = a[0];
  v0 = s->value[l];
  k0 = s->level[var_of(l)];

  l = a[1];
  v1 = s->value[l];
  k1 = s->level[var_of(l)];
  if (prefer(v1, k1, v0, k0)) {
    // swap a[0] and a[1]
    a[1] = a[0]; a[0] = l;
    v = v0; v0 = v1; v1 = v;
    k = k0; k0 = k1; k1 = k;
  }

  for (i=2; i<n; i++) {
    l = a[i];
    v = s->value[l];
    k = s->level[var_of(l)];
    if (prefer(v, k, v0, k0)) {
      // circular rotation: a[i] --> a[0] --> a[1] --> a[i]
      a[i] = a[1]; a[1] = a[0]; a[0] = l;
      v1 = v0; k1 = k0;
      v0 = v; k0 = k; 
    } else if (prefer(v, k, v1, k1)) {
      // swap a[i] and a[1]
      a[i] = a[1]; a[1] = l;
      v1 = v; k1 = k;
    }
  }

#if DEBUG
  check_watched_literals(s, n, a);
#endif

  cl = new_problem_clause(s, n, a);

  if (v0 == VAL_UNDEF) k0 = UINT32_MAX;
  if (v1 == VAL_UNDEF) k1 = UINT32_MAX;

  if (v0 == VAL_FALSE && k0 < k1) {
    // a[1] implied at level k0
    if (k0 < s->decision_level) {
      backtrack_to_level(s, k0);
      s->inconsistent = false; // clear conflict if any
    }
    implied_literal(s, a[1], mk_clause1_antecedent(cl));

  } else if (v1 == VAL_FALSE && k1 < k0) {
    // a[0] implied at level k1
    if (k1 < s->decision_level) {
      backtrack_to_level(s, k1);
      s->inconsistent = false; // clear conflict if any
    }
    implied_literal(s, a[0], mk_clause0_antecedent(cl));

  } else if (v0 == VAL_FALSE && v1 == VAL_FALSE) {
    assert(k0 == k1);
    backtrack_to_level(s, k0);
    record_clause_conflict(s, cl);
  }
 
}




/*
 * Simplify clause a[0... n-1]
 * - remove all literals false at base-level
 * - remove duplicate literals
 * - check whether the clause is true at base-level
 * Return code: 
 * - true means the clause is not trivial
 *   n is updated and the simplified clause is stored in a[0 .. n-1].
 * - false means the clause contains complementary literals or 
 *   a literal true at the base level
 */
static bool preprocess_clause(smt_core_t *s, uint32_t *n, literal_t *a) {
  uint32_t i, j, m;
  literal_t l, l_aux;

  m = *n;
  if (m == 0) return true;

  // remove duplicates/check for complementary literals
  int_array_sort(a, m);
  l = a[0];
  j = 1;
  for (i=1; i<m; i++) {
    l_aux = a[i];
    if (l_aux != l) {
      if (l_aux == not(l)) return false; // true clause
      a[j++] = l_aux;
      l = l_aux;
    }
  }
  m = j;

  // remove false literals/check for true literals
  j = 0;
  for (i=0; i<m; i++) {
    l = a[i];
    switch (literal_base_value(s, l)) {
    case VAL_FALSE: break;
    case VAL_UNDEF: a[j++] = l; break;
    case VAL_TRUE: return false; // true clause
    }
  }

  *n = j;  
  return true;
}





/*
 * External API to add clauses:
 * - can be called if s->status is IDLE or SEARCHING
 * - if a clause is added on the fly, when decision_level > base_level,
 *   we copy it into the lemma queue for future processing.
 * The theory solver may call the clause-addition function within 
 * its propagate or backtrack functions.
 */

/*
 * Check for on-the-fly addition
 * (if compiled in DEBUG mode also abort
 *  if s->status is not IDLE or SEARCHING or INTERRUPTED).
 */
static inline bool on_the_fly(smt_core_t *s) {
  assert((s->status == STATUS_IDLE && s->decision_level == s->base_level) || 
	 (s->status == STATUS_SEARCHING && s->decision_level >= s->base_level) || 
	 (s->status == STATUS_INTERRUPTED && s->decision_level >= s->base_level));
  //  return s->decision_level > s->base_level;
  //  return s->status == STATUS_SEARCHING;
  return s->status != STATUS_IDLE;
}

/*
 * Record the empty clause as a conflict
 * - if resolve conflict is called after this, it will do the 
 * right thing (namely, see that the conflict can't be resolved).
 */
static inline void record_empty_conflict(smt_core_t *s) {
  assert(s->decision_level == s->base_level);

#if TRACE
  printf("---> DPLL:   Add empty clause: {}\n");
#endif
  s->inconsistent = true;
  s->conflict_buffer[0] = end_clause;
  s->conflict = s->conflict_buffer;  
}


/*
 * Add the empty clause (we provide this for completeness)
 */
void add_empty_clause(smt_core_t *s) {
  if (on_the_fly(s)) {
    push_lemma(&s->lemmas, 0, NULL);
    return;
  }
  record_empty_conflict(s);
}


/*
 * Add a unit clause
 */
void add_unit_clause(smt_core_t *s, literal_t l) {
  if (on_the_fly(s) && s->decision_level > s->base_level) {
    push_lemma(&s->lemmas, 1, &l);
    return;
  }

#if TRACE
  printf("---> DPLL:   Add unit clause: { ");
  print_literal(stdout, l);
  printf(" }\n");
#endif

  assert(0 <= l && l < s->nlits);

  if (s->value[l] == VAL_TRUE && s->level[var_of(l)] <= s->base_level) {
    return; // l is already true at the base level
  }

  if (s->value[l] == VAL_FALSE) {
    // conflict (the whole thing is unsat)
    s->inconsistent = true;
    s->conflict = s->conflict_buffer;
    s->conflict_buffer[0] = l;
    s->conflict_buffer[1] = end_clause;

  } else {
    assign_literal(s, l);
    s->nb_unit_clauses ++;
  }
}


/*
 * Simplify then add clause a[0 ... n-1]
 * - this modifies array a 
 */
void add_clause_unsafe(smt_core_t *s, uint32_t n, literal_t *a) {
  if (on_the_fly(s)) {
    push_lemma(&s->lemmas, n, a);
    return;
  }

  if (preprocess_clause(s, &n, a)) {
    if (n > 2) {
      //      add_simplified_clause(s, n, a);
      new_problem_clause(s, n, a);
    } else if (n == 2) {
      //      add_simplified_binary_clause(s, a[0], a[1]);
      direct_binary_clause(s, a[0], a[1]);
    } else if (n == 1) {
      add_simplified_unit_clause(s, a[0]);
    } else {
      record_empty_conflict(s);
    }
  }
#if TRACE
  else {
    printf("---> DPLL:   Skipped true clause\n");
  }
#endif
}

/*
 * Simplify then add clause a[0 ... n-1]
 * - makes a copy of a so it's safe to use
 */
void add_clause(smt_core_t *s, uint32_t n, literal_t *a) {
  ivector_t *v;

  if (on_the_fly(s)) {
    push_lemma(&s->lemmas, n, a);
    return;
  }

  // use s->buffer2 as an auxiliary buffer to make a copy of a[0 .. n-1]
  v = &s->buffer2;
  assert(v->size == 0);
  ivector_copy(v, a, n);
  assert(v->size == n);

  // use the copy here
  a = v->data;
  if (preprocess_clause(s, &n, a)) {
    if (n > 2) {
      //      add_simplified_clause(s, n, a);
      new_problem_clause(s, n, a);
    } else if (n == 2) {
      //      add_simplified_binary_clause(s, a[0], a[1]);
      direct_binary_clause(s, a[0], a[1]);
    } else if (n == 1) {
      add_simplified_unit_clause(s, a[0]);
    } else {
      record_empty_conflict(s);
    }
  }
#if TRACE
  else {
    printf("---> DPLL:   Skipped true clause\n");
  }
#endif

  ivector_reset(v);
}


/*
 * Short cuts
 */
void add_binary_clause(smt_core_t *s, literal_t l1, literal_t l2) {
  literal_t a[2];

  a[0] = l1;
  a[1] = l2;
  add_clause_unsafe(s, 2, a);
}

void add_ternary_clause(smt_core_t *s, literal_t l1, literal_t l2, literal_t l3) {
  literal_t a[3];

  a[0] = l1;
  a[1] = l2;
  a[2] = l3;
  add_clause_unsafe(s, 3, a);
}





/********************************
 *  DEAL WITH THE LEMMA QUEUE   *
 *******************************/

/* 
 * Find the length of a lemma a: 
 * - a must be terminated by null_literal (or any negative end marker)
 */
static uint32_t lemma_length(literal_t *a) {
  uint32_t n;

  n = 0;
  while (*a >= 0) {
    a ++;
    n ++;
  }
  return n;
}


/*
 * Add lemma: similar to add_clause but does more work
 * - n = length of the lemma
 * - a = array of literals (lemma is a[0] ... a[n-1])
 */
static void add_lemma(smt_core_t *s, uint32_t n, literal_t *a) {
  if (preprocess_clause(s, &n, a)) {
    if (n > 2) {
      add_simplified_clause(s, n, a);
    } else if (n == 2) {
      add_simplified_binary_clause(s, a[0], a[1]);
    } else if (n == 1) {
      add_simplified_unit_clause(s, a[0]);
    } else {
      backtrack_to_base_level(s);
      record_empty_conflict(s);
    }
  }
#if TRACE
  else {
    printf("---> DPLL:   Skipped true lemma\n");
  }
#endif

}


/*
 * Add all queued lemmas to the database.
 * - this may cause backtracking
 * - a conflict clause may be recorded
 * If so, conflict resolution must called outside this function
 */
static void add_all_lemmas(smt_core_t *s) {
  lemma_block_t *tmp;
  literal_t *lemma;
  uint32_t i, j, n;

  for (i=0; i<s->lemmas.free_block; i++) {
    tmp = s->lemmas.block[i];
    lemma = tmp->data;
    j = 0;
    while (j < tmp->ptr) {
      /* 
       * it's possible for new lemmas to be added within this loop
       * - because clause addition may cause backtracking and
       * the theory solver is allowed to create lemmas within backtrack.
       */
      n = lemma_length(lemma);
      add_lemma(s, n, lemma);
      n ++; // skip the end marker
      j += n;
      lemma += n;
    }
  }

  // Empty the queue now:
  reset_lemma_queue(&s->lemmas);
}





/*********************************
 *  DELETION OF LEARNED CLAUSES  *
 ********************************/

/*
 * Reorder an array  a[low ... high-1] of learned clauses so that
 * the clauses are divided in two half arrays:
 * - the clauses of highest activities are all stored in a[low...half - 1]  
 * - the clauses of lowest activities are in a[half ... high-1], 
 * where half = (low + high) / 2.
 */
static void quick_split(clause_t **a, uint32_t low, uint32_t high) {
  uint32_t i, j, half;
  float pivot;
  clause_t *aux;

  if (high <= low + 1) return;

  half = (low + high)/2;

  do {
    i = low;
    j = high;
    pivot = get_activity(a[i]);

    do { j --; } while (get_activity(a[j]) < pivot);
    do { i ++; } while (i <= j && get_activity(a[i]) > pivot);

    while (i < j) {
      // a[i].act <= pivot and a[j].act >= pivot: swap a[i] and a[j]
      aux = a[i];
      a[i] = a[j];
      a[j] = aux;

      do { j--; } while (get_activity(a[j]) < pivot);
      do { i++; } while (get_activity(a[i]) > pivot);
    }

    // a[j].act >= pivot, a[low].act = pivot: swap a[low] and a[i]
    aux = a[low];
    a[low] = a[j];
    a[j] = aux;

    /*
     * at this point:
     * - all clauses in a[low,.., j - 1] have activity >= pivot
     * - activity of a[j] == pivot
     * - all clauses in a[j+1,..., high-1] have activity <= pivot
     * reapply the procedure to whichever of the two subarrays 
     * contains the half point
     */
    if (j < half) {
      low = j + 1;
    } else {
      high = j;
    }    
  } while (j != half);
}


/*
 * Apply this to a vector v of learned_clauses
 */
static inline void reorder_clause_vector(clause_t **v) {
  quick_split(v, 0, get_cv_size(v));
}


/*
 * Auxiliary function: follow clause list of l0
 * Remove all clauses marked for removal
 */
static inline void cleanup_watch_list(smt_core_t *s, literal_t l0) {
  link_t lnk;
  clause_t *cl;
  link_t *list;

  list = s->watch + l0;
  for (lnk = *list; lnk != NULL_LINK; lnk = next_of(lnk)) {
    cl = clause_of(lnk);
    if (! is_clause_to_be_removed(cl)) {
      *list = lnk;
      list = cdr_ptr(lnk);
    }
  }

  *list = NULL_LINK; // end of list
#if USE_END_WATCH
  s->end_watch[l0] = list;
#endif
}


/*
 * Update all watch lists: remove all clauses marked for deletion.
 */
static void cleanup_watch_lists(smt_core_t *s) {
  uint32_t i, n;

  n = s->nlits;
  for (i=0; i<n; i ++) {
    cleanup_watch_list(s, i);
  }
}


/*
 * Check whether cl is an antecedent clause
 */
static inline bool clause_is_locked(smt_core_t *s, clause_t *cl) {
  literal_t l0, l1;

  l0 = get_first_watch(cl);
  l1 = get_second_watch(cl);

  return (s->value[l0] != VAL_UNDEF && s->antecedent[var_of(l0)] == mk_clause0_antecedent(cl))
    || (s->value[l1] != VAL_UNDEF && s->antecedent[var_of(l1)] == mk_clause1_antecedent(cl));
}


/*
 * Delete all clauses that are marked for deletion
 */
static void delete_learned_clauses(smt_core_t *s) {
  uint32_t i, j, n;
  clause_t **v;

  v = s->learned_clauses;
  n = get_cv_size(v);

  // clean up all the watch-literal lists
  cleanup_watch_lists(s);

  // do the real deletion
  s->stats.learned_literals = 0;

  j = 0;
  for (i = 0; i<n; i++) {
    if (is_clause_to_be_removed(v[i])) {
      delete_learned_clause(v[i]);
    } else {
      s->stats.learned_literals += clause_length(v[i]);
      v[j] = v[i];
      j ++;
    }
  }

  // set new size of the learned clause vector
  set_cv_size(s->learned_clauses, j);
  s->nb_clauses -= (n - j);

  s->stats.learned_clauses_deleted += (n - j);  
}


/*
 * Delete half the learned clauses, minus the locked ones (Minisat style).
 * This is expensive: the function scans and reconstructs the
 * watched lists.
 */
void reduce_clause_database(smt_core_t *s) {
  uint32_t i, n;
  clause_t **v;
  float act_threshold;

  v = s->learned_clauses;
  n = get_cv_size(v);
  if (n == 0) return;

  // put the clauses with lowest activity in the upper
  // half of the learned clause vector.
  reorder_clause_vector(v);

  act_threshold = s->cla_inc/n;

  // prepare for deletion: all non-locked clauses, with activity less
  // than activitiy_threshold are marked for deletion.
  for (i=0; i<n/2; i++) {
    if (get_activity(v[i]) <= act_threshold && ! clause_is_locked(s, v[i])) {
      mark_for_removal(v[i]);
    }
  }
  for (i = n/2; i<n; i++) {
    if (! clause_is_locked(s, v[i])) {
      mark_for_removal(v[i]);
    }
  }

  delete_learned_clauses(s);
  s->stats.reduce_calls ++;
}





/*******************************************************
 *  ZCHAFF-STYLE CLAUSE DELETION (AS IN YICES 1.0.XX)  *
 ******************************************************/

/*
 * Number of unassigned literals in clause cl
 */
static uint32_t unassigned_literals(smt_core_t *s, clause_t *cl) {
  uint32_t n;
  literal_t l, *a;

  n = 0;
  a = cl->cl;
  l = *a;
  while (l >= 0) {
    if (s->value[l] == VAL_UNDEF) n ++;
    a ++;
    l = *a;
  }

  return n;
}


/*
 * Delete irrelevant clauses (Zchaff-style)
 * - split the set of learned clauses into two parts: old-clauses and young-clauses
 * - if there are n learned clauses in total, then the n/young_ratio most recent are young,
 *   the rest are old. (young_ratio is 16)
 */
void remove_irrelevant_learned_clauses(smt_core_t *s) {
  clause_t **v;
  clause_t *cl;
  uint32_t i, n, p, relevance;
  float coeff;

  v = s->learned_clauses;
  n = get_cv_size(v);
  if (n == 0) return;

  p = n - n/TAIL_RATIO;
  coeff = (HEAD_ACTIVITY - TAIL_ACTIVITY)/n;
  
  for (i=0; i<n; i++) {
    cl = v[i];
    if (! clause_is_locked(s, cl)) {
      relevance = i < p ? HEAD_RELEVANCE : TAIL_RELEVANCE;
      if (get_activity(cl) < HEAD_ACTIVITY - coeff * i && 
	  unassigned_literals(s, cl) > relevance) {
	mark_for_removal(cl);
      }
    }
  }

  delete_learned_clauses(s);
  s->stats.remove_calls ++;
}






/*********************************************************
 *  SIMPLICATION: REMOVE CLAUSES TRUE AT THE BASE LEVEL  *
 ********************************************************/

/*
 * Simplify clause cl, given the current literal assignment
 * - mark cl for deletion if it's true 
 * - otherwise remove the false literals
 * The watched literals are unchanged. 
 *
 * Update the counters aux_clauses and aux_literals if the clause
 * is not marked for removal.
 *
 * This is sound only at level 0.
 */
static void simplify_clause(smt_core_t *s, clause_t *cl) {
  uint32_t i, j;
  literal_t l;

  assert(s->base_level == 0 && s->decision_level ==0);

  i = 0;
  j = 0;
  do {
    l = cl->cl[i];
    i ++;
    switch (s->value[l]) {
      //    case VAL_FALSE:
      //      break;

    case VAL_UNDEF:
      cl->cl[j] = l;
      j ++;
      break;

    case VAL_TRUE:
      mark_for_removal(cl);
      return;
    }
  } while (l >= 0);

  s->aux_literals += j - 1;
  s->aux_clauses ++;
  // could migrate cl to two-literal if j is 3??
}


/*
 * Check whether cl is true at the base level. If so mark it
 * for removal.
 */
static void mark_true_clause(smt_core_t *s, clause_t *cl) {
  uint32_t i;
  literal_t l;

  assert(s->base_level == s->decision_level);

  i = 0;
  do {
    l = cl->cl[i];
    i ++;
    if (s->value[l] == VAL_TRUE) {
      mark_for_removal(cl);
      return;
    }
  } while (l >= 0);

  s->aux_literals += (i - 1);
  s->aux_clauses ++;
}


/*
 * Simplify the set of clauses given the current assignment:
 * - remove all clauses that are true (from the watched literals)
 * - remove false literals from learned clauses
 */
static void simplify_clause_set(smt_core_t *s) {
  uint32_t i, j, n;
  clause_t **v;

  assert(s->decision_level == s->base_level);

  if (s->base_level == 0) {
    /*
     * Apply thorough simplifications at level 0
     */
    // simplify problem clauses
    s->aux_literals = 0;
    s->aux_clauses = 0;
    v = s->problem_clauses;
    n = get_cv_size(v);
    for (i=0; i<n; i++) {
      if (! is_clause_to_be_removed(v[i]) && 
	  ! clause_is_locked(s, v[i])) {
	simplify_clause(s, v[i]);
      }
    }
    s->stats.prob_literals = s->aux_literals;
    s->nb_prob_clauses = s->aux_clauses;

    // simplify learned clauses
    s->aux_literals = 0;
    s->aux_clauses = 0;
    v = s->learned_clauses;
    n = get_cv_size(v);
    for (i=0; i<n; i++) {
      assert(! is_clause_to_be_removed(v[i]));
      if (! clause_is_locked(s, v[i])) {
	simplify_clause(s, v[i]);
      }
    }
    s->stats.learned_literals = s->aux_literals;

  } else {
    /*
     * Mark the true clauses for removal
     */
    // mark the true problem clauses
    s->aux_literals = 0;
    s->aux_clauses = 0;
    v = s->problem_clauses;
    n = get_cv_size(v);
    for (i=0; i<n; i++) {
      if (! is_clause_to_be_removed(v[i]) && 
	  ! clause_is_locked(s, v[i])) {
	mark_true_clause(s, v[i]);
      }
    }
    s->stats.prob_literals = s->aux_literals;
    s->nb_prob_clauses = s->aux_clauses;

    // mark the true learned clauses
    s->aux_literals = 0;
    v = s->learned_clauses;
    n = get_cv_size(v);
    for (i=0; i<n; i++) {
      assert(! is_clause_to_be_removed(v[i]));
      if (! clause_is_locked(s, v[i])) {
	mark_true_clause(s, v[i]);
      }
    }
    s->stats.learned_literals = s->aux_literals;

  }

  /*
   * cleanup the watched literal lists: all marked (i.e., true)
   * clauses are removed from the lists.
   */
  cleanup_watch_lists(s);

  /*
   * Remove the true simplified problem clauses for good
   * if we're at base_level 0
   */
  if (s->base_level == 0) {
    v = s->problem_clauses;
    n = get_cv_size(v);
    j = 0;
    for (i=0; i<n; i++) {
      if (is_clause_to_be_removed(v[i])) {
	delete_clause(v[i]);
      } else {
	v[j] = v[i];
	j ++;
      }
    }
    set_cv_size(v, j);
    s->nb_clauses -= n - j;
    s->stats.prob_clauses_deleted += n - j;
  }


  /*
   * Remove the marked (i.e. true) learned clauses for good
   */
  v = s->learned_clauses;
  n = get_cv_size(v);
  j = 0;
  for (i=0; i<n; i++) {
    if (is_clause_to_be_removed(v[i])) {
      delete_learned_clause(v[i]);
    } else {
      v[j] = v[i];
      j ++;
    }
  }
  set_cv_size(v, j);
  s->nb_clauses -= n - j;
  s->stats.learned_clauses_deleted += n - j;
}


/*
 * Clean up a binary-clause vector v
 * - assumes that v is non-null
 * - remove all literals of v that are assigned at the base level
 * - for each deleted literal, increment sol->stats.aux_literals.
 * This is sound only at level 0.
 */
static void cleanup_binary_clause_vector(smt_core_t *s, literal_t *v) {
  uint32_t i, j;
  literal_t l;

  i = 0;
  j = 0;
  do {
    l = v[i++];
    if (s->value[l] == VAL_UNDEF) { //keep l
      v[j ++] = l;
    }    
  } while (l >= 0); // end-marker is copied too

  s->aux_literals += i - j;
  set_lv_size(v, j - 1);
}


/*
 * Simplify all binary vectors affected by the current assignment
 * - scan the literals in the stack from sol->simplify_bottom to sol->stack.top
 * - remove all the binary clauses that contain one such literal
 * - free the binary watch vectors
 *
 * Should not be used at base_level > 0, otherwise pop won't restore the 
 * binary clauses properly.
 */
static void simplify_binary_vectors(smt_core_t *s) {
  uint32_t i, j, n;
  literal_t l0, *v0, l1;

  assert(s->decision_level == 0 && s->base_level == 0);

  s->aux_literals = 0;   // counts the number of literals removed
  for (i = s->simplify_bottom; i < s->stack.top; i++) {
    l0 = s->stack.lit[i];

    // remove all binary clauses that contain l0
    v0 = s->bin[l0];
    if (v0 != NULL) {
      n = get_lv_size(v0);
      for (j=0; j<n; j++) {
	l1 = v0[j];
	if (s->value[l1] == VAL_UNDEF) {
	  // sol->bin[l1] is non null.
	  assert(s->bin[l1] != NULL);
	  cleanup_binary_clause_vector(s, s->bin[l1]);
	}
      }

      delete_literal_vector(v0);
      s->bin[l0] = NULL;
      s->aux_literals += n;
    }

    // remove all binary clauses that contain not(l0)
    l0 = not(l0);
    v0 = s->bin[l0];
    if (v0 != NULL) {
      s->aux_literals += get_lv_size(v0);
      delete_literal_vector(v0);
      s->bin[l0] = NULL;
    }
  }

  s->aux_literals /= 2;
  s->stats.bin_clauses_deleted += s->aux_literals;
  s->nb_bin_clauses -= s->aux_literals;

  s->aux_literals = 0;
}


/*
 * Simplify the database: problem clauses and learned clauses
 * - remove clauses that are true at the base level from the watched lists
 * - if base_level is 0, also remove binary clauses that are true at the
 *   base level.
 */
static void simplify_clause_database(smt_core_t *s) {
  assert(s->stack.top == s->stack.prop_ptr && s->decision_level == s->base_level);

  simplify_clause_set(s);
  if (s->base_level == 0) {
    simplify_binary_vectors(s);
  }

  s->stats.simplify_calls ++;

  /*
   * The next call to simplify_clause_database is enabled when
   *   s->decision_level == base_level && 
   *   s->stack.top > s->simplify_bottom &&
   *   s->stats.propagations > s->simplify_props + s->simplify_threshold
   *
   * s->simplify_threshold is set to the total number of literals.
   *
   * This is an arbitrary choice that avoids calling simplify too often. 
   * This is copied from Minisat.
   */ 
  s->simplify_bottom = s->stack.top;
  s->simplify_props = s->stats.propagations;
  s->simplify_threshold = s->stats.learned_literals + s->stats.prob_literals + 
    2 * s->nb_bin_clauses;
}




/**************
 *  PUSH/POP  *
 *************/

/*
 * Push: 
 * - clear current assignment and reset status to IDLE if necessary
 * - save current base-level state
 * - notify the theory solver
 * - increase base level
 */
void smt_push(smt_core_t *s) {
  uint32_t k;

  /*
   * Abort if push_pop is not enabled
   */
  assert((s->option_flag & PUSH_POP_MASK) != 0);

  /*
   * Reset assignment and status 
   */
  if (s->status == STATUS_UNKNOWN || s->status == STATUS_SAT) {
    smt_clear(s);
  }

  assert(s->status == STATUS_IDLE && s->decision_level == s->base_level);

  /*
   * Save current state:
   * - number of variables
   * - number of unit clauses
   * - number of binary clauses
   * - number of problem clauses
   * - propagation pointers
   */
  trail_stack_save(&s->trail_stack, 
		   s->nvars, s->nb_unit_clauses, s->binary_clauses.size, 
		   get_cv_size(s->problem_clauses), 
		   s->stack.prop_ptr, s->stack.theory_ptr);

  /*
   * Notify the theory solver
   */
  s->th_ctrl.push(s->th_solver);

  /*
   * Increase the base_level (and decision_level)
   */
  k = s->base_level + 1;
  s->base_level = k;
  s->decision_level = k;
  if (s->stack.nlevels <= k) {
    increase_stack_levels(&s->stack);
  }
  s->stack.level_index[k] = s->stack.top;
  
}


/*
 * Mark all learned clauses for removal
 */
static void remove_all_learned_clauses(smt_core_t *s) {
  uint32_t i, n;
  clause_t **v;

  v = s->learned_clauses;
  n = get_cv_size(v);

  for (i=0; i<n; i++) {
    mark_for_removal(v[i]);
  }
}


/*
 * Mark problem clauses (at indices n, n+1, ...)
 */
static void remove_problem_clauses(smt_core_t *s, uint32_t n) {
  uint32_t m;
  clause_t **v;
  clause_t *cl;

  v = s->problem_clauses;
  m = get_cv_size(v);
  while (n < m) {
    cl = v[n];
    if (! is_clause_to_be_removed(cl)) {
      mark_for_removal(cl);
    }
    n ++;
  }
}


/*
 * Reset the watch lists (to empty lists)
 */
static void reset_watch_lists(smt_core_t *s) {
  uint32_t i, n;

  n = s->nlits;
  for (i=0; i<n; i++) {
    s->watch[i] = NULL_LINK;
#if USE_END_WATCH
    s->end_watch[i] = &s->watch[i];
#endif
  }
}


/*
 * Restore all non-binary/non-unit clauses (to previous base-level)
 * Also restore stats.prob_literals
 * - n = number of problem clauses at the start of the current base level
 */
static void restore_clauses(smt_core_t *s, uint32_t n) {
  uint32_t i, m, nlits;
  clause_t **v;
  clause_t *cl;
  literal_t l;

  // mark clauses for removal
  remove_all_learned_clauses(s);
  remove_problem_clauses(s, n);

  // empty the watch lists
  reset_watch_lists(s);
  
  // do the real deletion
  v = s->learned_clauses;
  m = get_cv_size(v);
  for (i=0; i<m; i++) {
    delete_learned_clause(v[i]);
  }
  reset_clause_vector(v);

  v = s->problem_clauses;
  m = get_cv_size(v);
  for (i=n; i<m; i++) {
    delete_clause(v[i]);
  }
  set_cv_size(v, n);

  /*
   * put all problem clauses back into the watch lists
   * and restore the marked problem clauses in v[0 ... n-1] 
   */
  nlits = 0;   // to count the total number of literals
  for (i=0; i<n; i++) {
    cl = v[i];
    if (is_clause_to_be_removed(cl)) {
      restore_removed_clause(cl);
      assert(cl->cl[0] >= 0 && cl->cl[1] >= 0);
    }
    nlits += clause_length(cl);

#if USE_END_WATCH
    // add cl at the end of the watch lists
    cl->link[0] = NULL_LINK;
    cl->link[1] = NULL_LINK;

    l = cl->cl[0];
    *s->end_watch[l] = mk_link(cl, 0);
    s->end_watch[l] = &cl->link[0];

    l = cl->cl[1];
    *s->end_watch[l] = mk_link(cl, 1);
    s->end_watch[l] = &cl->link[1];
#else
    // add cl at the start of its watch lists
    l = cl->cl[0];
    s->watch[l] = cons(0, cl, s->watch[l]);

    l = cl->cl[1];
    s->watch[l] = cons(1, cl, s->watch[l]);
#endif
  }


  s->nb_clauses = n;
  s->nb_prob_clauses = n;
  s->stats.prob_literals = nlits;
  s->stats.learned_literals = 0;
}


/*
 * Keep binary clauses in binary_clauses[0... n-1]
 * Remove the ones in binary_clauses[n ... ]
 */
static void restore_binary_clauses(smt_core_t *s, uint32_t n) {
  uint32_t i;
  literal_t l0, l1;
  literal_t *bin_clauses;
  
  bin_clauses = s->binary_clauses.data;
  i = s->binary_clauses.size;
  assert((i & 1) == 0  && (n & 1) == 0 && i >= n);

  // number of clauses removed = (i - n)/2
  s->nb_bin_clauses -= (i - n)/2;

  while (i > n) {
    i --;
    l0 = bin_clauses[i];
    i --;
    l1 = bin_clauses[i];
    // last clause added = { l1, l0 }
    assert(last_lv_elem(s->bin[l0]) == l1 && last_lv_elem(s->bin[l1]) == l0);
    literal_vector_pop(s->bin[l0]);
    literal_vector_pop(s->bin[l1]);
  }

  ivector_shrink(&s->binary_clauses, n);
}


/*
 * Keep variables 0 ... n-1. Remove the rest
 * Must be called after restore_clauses.
 *
 * Atoms are removed if needed, but we don't call 
 * s->th_smt.delete_atom, since s->th_ctrl.pop has been 
 * called before this.
 */
static void restore_variables(smt_core_t *s, uint32_t n) {
  uint32_t i, nv;
  literal_t l0, l1;

  nv = s->nvars;
  for (i=n; i<nv; i++) {
    heap_remove(&s->heap, i);
    if (bvar_has_atom(s, i)) {
      remove_atom(&s->atoms, i);
    }

    l0 = pos_lit(i);
    l1 = neg_lit(i);
    delete_literal_vector(s->bin[l0]);
    delete_literal_vector(s->bin[l1]);
    s->bin[l0] = NULL;
    s->bin[l1] = NULL;
    s->watch[l0] = NULL_LINK;
    s->watch[l1] = NULL_LINK;
  }

  s->nvars = n;
  s->nlits = 2 * n;
}


/*
 * Remove the mark of all variables assigned at the current base_level
 */
static void clear_base_level_marks(smt_core_t *s) {
  uint32_t i, k, n;
  literal_t *u, l;
  bvar_t x;

  u = s->stack.lit;
  k = s->base_level;
  n = s->stack.top;
  for (i=s->stack.level_index[k]; i<n; i++) {
    l = u[i];
    x = var_of(l);
    assert(s->value[l] == VAL_TRUE);
    assert(s->level[x] == k);
    assert(is_var_marked(s, x));
    clr_var_mark(s, x);
  }
}


/*
 * Pop: backtrack to previous base_level
 * - can be called after the search terminates or from an idle state
 * - should not be called if status is INTERRUPTED or SEARCHING
 */
void smt_pop(smt_core_t *s) {
  trail_t *top;

  /*
   * Abort if push_pop is not enabled or if there's no pushed state
   */
  assert((s->option_flag & PUSH_POP_MASK) != 0 && s->base_level > 0 &&
	 s->status != STATUS_INTERRUPTED && s->status != STATUS_SEARCHING);

  // We need to backtrack before calling the pop function of th_solver
  backtrack_to_base_level(s);
  s->th_ctrl.pop(s->th_solver);

  clear_base_level_marks(s);
  top = trail_stack_top(&s->trail_stack);
  restore_clauses(s, top->nclauses);
  restore_binary_clauses(s, top->nbins);

  s->base_level --;
  backtrack(s, s->base_level);
  s->nb_unit_clauses = top->nunits;

  restore_variables(s, top->nvars);

  // restore the propagation pointers
  s->stack.prop_ptr = top->prop_ptr;
  s->stack.theory_ptr = top->theory_ptr;

  trail_stack_pop(&s->trail_stack);

  // reset status
  s->status = STATUS_IDLE;
}


/*
 * Cleanup after search was interrupted or returned unsat
 * - the clean state was pushed on the trail stack on start_search
 * - we just call pop
 */
void smt_cleanup(smt_core_t *s) {
  assert((s->status == STATUS_INTERRUPTED || s->status == STATUS_UNSAT) 
	 && (s->option_flag & CLEAN_INTERRUPT_MASK) != 0); 
  s->status = STATUS_IDLE; // make sure pop does not abort
  smt_pop(s);
}


/*
 * Clear the current boolean assignment and reset status to IDLE
 */
void smt_clear(smt_core_t *s) {
  assert(s->status == STATUS_SAT || s->status == STATUS_UNKNOWN);
  /*
   * In clean-interrupt mode, we restore the state to what it was 
   * before the search started. This also backtracks to the base_level
   * and clears the current assignment.
   */
  if ((s->option_flag & CLEAN_INTERRUPT_MASK) != 0) {
    smt_pop(s);
  } else {
    // no state to restore. Just backtrack and clear the assignment
    backtrack_to_base_level(s);
    s->status = STATUS_IDLE;
  }
}


/*
 * Cleanup after unsat.
 */
void smt_clear_unsat(smt_core_t *s) {
  assert(s->status == STATUS_UNSAT);
  /*
   * In clean-interrupt mode, we restore the state to what it was 
   * before the search started (using pop), but we leave 
   * status UNSAT.
   */
  if ((s->option_flag & CLEAN_INTERRUPT_MASK) != 0) {
    smt_pop(s);
    s->status = STATUS_UNSAT;
  }
}



/*****************
 *  CHECKPOINTS  *
 ****************/

/*
 * Record current decision level and number of variables
 * - any variables created after the checkpoints will
 * be deleted when the solver backtracks to a lower decision level
 */
void smt_checkpoint(smt_core_t *s) {
  assert(s->status == STATUS_SEARCHING);
  push_checkpoint(&s->checkpoints, s->decision_level, s->nvars);
  s->cp_flag = false;
}


/*
 * Heuristic for deletion of variables and atoms created on the fly:
 *
 * The checkpoints divide the boolean variables in disjoint segments 
 * - the top segment is [n, s->nvars - 1] where n <= nvars
 * - each segment is assigned a decision level (the decision level at the 
 *   time the checkpoint was created)
 * - the top segment can be deleted if the following conditions are satisfied:
 *   1) s->cp_flag is true (i.e., after backtracking)
 *   2) the segment level is <= the current decision level in s
 *   3) all variables in the segment are unassigned.
 * - if these condistions hold, we remove all variables in [n, s->nvars - 1] 
 *   and all the atoms attached to these variable, then we consider the next
 *   segment in the checkpoint stack.
 * - after this, we remove all the clauses that refer to a deleted variables
 */


/*
 * Attempt to remove the top segment [n, s->nvars - 1]
 * - if all variables in this segment are unassigned, then their 
 *   atoms are removed and the function returns true
 * - otherwise, the function returns false and does nothing
 * Note: it does nothing if n == s->nvars, but returns true in that case.
 * The boolean variables are not fully deleted yet.
 */
static bool delete_variables(smt_core_t *s, uint32_t n) {
  bvar_t x;
  atom_table_t *tbl;
  uint32_t m;

  assert(n <= s->nvars);

  m = s->nvars;
  for (x=n; x<m; x++) {
    if (bvar_value(s, x) != VAL_UNDEF) return false;
  }

  // delete the atoms
  tbl = &s->atoms;
  if (tbl->size < m) {
    m = tbl->size;
  }
  for (x=n; x<m; x++) {
    heap_remove(&s->heap, x);
    if (tst_bit(tbl->has_atom, x)) {
      s->th_smt.delete_atom(s->th_solver, tbl->atom[x]);
      remove_atom(tbl, x);
    }
  }

  // update s->nvars and s->nlits
  s->nvars = n;  
  s->nlits = 2 * n;

  return true;
}

/*
 * Remove all literals >= max in literal vector v
 * - assumes that v is non-null
 */
static void cleanup_garbage_in_binary_clause_vector(smt_core_t *s, literal_t *v) {
  uint32_t i, j;
  literal_t l, max;  

  max = pos_lit(s->nvars);
  i = 0;
  j = 0;
  do {
    l = v[i++];
    if (l < max) { // keep l
      v[j ++] = l;
    }
  } while (l >= 0); // the end-marker is negative. It's copied too

  s->aux_literals += i - j; // number of deleted literals
  set_lv_size(v, j - 1);
}


/*
 * Remove all binary clauses that contain a removed variable.
 * - old_nvar = number of variables before removal 
 */
static void remove_garbage_bin_clauses(smt_core_t *s, uint32_t old_nvars) {
  literal_t max, l, l0, *v0;
  uint32_t i, j, n;
  ivector_t *v;

  max = pos_lit(s->nvars); // all literals of index >= max are dead

  // cleanup the binary_clause vector
  v = &s->binary_clauses;
  n = v->size;
  j = 0;
  for (i=0; i<n; i+=2) {
    if (v->data[i] < max && v->data[i+1] < max) {
      v->data[j] = v->data[i];
      v->data[j+1] = v->data[i+1];
      j += 2;
    }
  }

  // s->aux_literal counts the number of literals removed
  s->aux_literals = 0;

  // cleanup the vectors bin[l]
  for (l0=max; l0<pos_lit(old_nvars); l0++) {
    // l0 is a removed literal
    v0 = s->bin[l0];
    if (v0 != NULL) {
      n = get_lv_size(v0);
      for (j=0; j<n; j++) {
	l = v0[j];
	if (l < max && s->bin[l] != NULL) {
	  cleanup_garbage_in_binary_clause_vector(s, s->bin[l]);
	}
      }
      delete_literal_vector(v0);
      s->bin[l0] = NULL;
      s->aux_literals += n;
      s->watch[l0] = NULL_LINK; // not strictly necessary
    }
  }

  // update the statistics
  s->aux_literals /= 2;
  s->stats.bin_clauses_deleted += s->aux_literals;
  s->nb_bin_clauses -= s->aux_literals;
}



/*
 * Check whether clause cl contains literals >= max
 * If it does, mark it for deletion.
 * Use s->aux_literals to count the number of literals kept
 */
static void mark_clause_to_remove(smt_core_t *s, clause_t *cl, literal_t max) {
  uint32_t i;
  literal_t l;

  i = 0;
  l = cl->cl[i];
  while (l >= 0) {
    if (l >= max) {
      assert(! clause_is_locked(s, cl));
      mark_for_removal(cl);
      return;
    }
    i ++;
    l = cl->cl[i];
  }
  s->aux_literals += (i - 1);
}



/*
 * Cleanup the problem and learned clauses after removal of variables
 */
static void remove_garbage_clauses(smt_core_t *s) {
  literal_t max; 
  uint32_t i, j, n;
  clause_t **v;

  max = pos_lit(s->nvars); // all literals of index >= max are dead

  // mark clauses to be deleted
  s->aux_literals = 0;   // count the number of literals in non-deleted clauses
  v = s->problem_clauses;
  n = get_cv_size(v);
  for (i=0; i<n; i++) {
    mark_clause_to_remove(s, v[i], max);
  }
  s->stats.prob_literals = s->aux_literals;

  s->aux_literals = 0;
  v = s->learned_clauses;
  n = get_cv_size(v);
  for (i=0; i<n; i++) {
    mark_clause_to_remove(s, v[i], max);
  }
  s->stats.learned_literals = s->aux_literals;

  // clean up watched list for 0 ... nvars-1
  cleanup_watch_lists(s);

  // delete the problem clauses
  v = s->problem_clauses;
  n = get_cv_size(v);
  j = 0;
  for (i=0; i<n; i++) {
    if (is_clause_to_be_removed(v[i])) {
      delete_clause(v[i]);
    } else {
      v[j] = v[i];
      j++;
    }
  }
  set_cv_size(v, j);
  s->nb_clauses -= n - j;
  s->stats.prob_clauses_deleted += n - j;

  // delete the learned clauses
  v = s->learned_clauses;
  n = get_cv_size(v);
  j = 0;
  for (i=0; i<n; i++) {
    if (is_clause_to_be_removed(v[i])) {
      delete_learned_clause(v[i]);
    } else {
      v[j] = v[i];
      j ++;
    }
  }
  set_cv_size(v, j);
  s->nb_clauses -= n - j;
  s->stats.learned_clauses_deleted += n - j;
}



/*
 * Deletion of irrelevant atoms and variables
 */
static void delete_irrelevant_variables(smt_core_t *s) {
  uint32_t old_nvars;
  checkpoint_stack_t *cp;
  checkpoint_t *p;
  bool dflag;

  old_nvars = s->nvars;

  dflag = false; // true if some variables are removed
  cp = &s->checkpoints;
  for (;;) {
    if (empty_checkpoint_stack(cp)) break;
    p = top_checkpoint(cp);
    if (p->dlevel < s->decision_level) break; // can't delete that segment
    if (delete_variables(s, p->nvars)) {
      // variables in p->nvars to s->nvars have been removed
      dflag = true;
      pop_checkpoint(cp);
      assert(s->nvars == p->nvars);
    } else {
      break;
    }
  }

  if (dflag) {
    s->th_smt.end_atom_deletion(s->th_solver);
    remove_garbage_clauses(s);
    remove_garbage_bin_clauses(s, old_nvars);
  }
}




/*
 * Purge all literals that refer to a dynamic variable
 * from the assignment stack.
 */
static void purge_all_dynamic_atoms(smt_core_t *s) {  
  checkpoint_stack_t *cp;
  literal_t *u, l;
  uint32_t base_nvars;
  uint32_t i, j, k;
  bvar_t x;

  assert(s->base_level == s->decision_level && 
	 s->stack.top == s->stack.prop_ptr && 
	 s->stack.top == s->stack.theory_ptr &&
	 s->nb_unit_clauses == s->stack.top);

  cp = &s->checkpoints;
  if (non_empty_checkpoint_stack(cp)) {
    // number of variables to keep = nvars at the bottom checkpoint
    base_nvars = cp->data[0].nvars;

    // remove all literals whose var is >= base_nvars from 
    // the assignment stack
    u = s->stack.lit;
    k = s->stack.top;
    j = 0;
    for (i=0; i<k; i++) {
      l = u[i];
      x = var_of(l);
      if (x >= base_nvars) {
	// variable to delete
	s->value[l] = VAL_UNDEF;
	s->value[not(l)] = VAL_UNDEF;
      } else {
	// keep l
	u[j] = l;
	j ++;
      }
    }

    // restore the stack pointers
    s->stack.top = j;
    s->stack.prop_ptr = j;
    s->stack.theory_ptr = j;

    s->nb_unit_clauses = j;    
  }

}




/**********************
 *  SEARCH FUNCTIONS  *
 *********************/

/*
 * New round of assertions
 */
void internalization_start(smt_core_t *s) {
  assert(s->status == STATUS_IDLE && s->decision_level == s->base_level);

#if TRACE
  printf("\n---> DPLL START\n");
  fflush(stdout);
#endif

  s->inconsistent = false;
  s->theory_conflict = false;
  s->conflict = NULL;
  s->false_clause = NULL;
  s->th_ctrl.start_internalization(s->th_solver);
}


/*
 * Propagate at the base  
 * - this is used to detect early inconsistencies during internalization
 */
bool base_propagate(smt_core_t *s) {
  assert(s->status == STATUS_IDLE && s->decision_level == s->base_level);

#if TRACE
  printf("\n---> DPLL BASE PROPAGATE\n");
  fflush(stdout);
#endif
  
  /*
   * If s is inconsistent (i.e., the empty clause was added) then there's
   * nothing more to do.
   *
   * Otherwise, force a call to theory_propagation first
   * - this ensures that the theory solver has a chance to detect inconsistencies,
   *   even if it has not created atoms yet.
   * - this is necessary because asserted axioms may be handled directly by
   *   the solver, without causing literals/atoms to be created in the core.
   */
  if (!s->inconsistent && theory_propagation(s) && smt_propagation(s)) {
    return true;
  }

  assert(s->inconsistent);
  s->status = STATUS_UNSAT;
  return false;
}

/*
 * Prepare for the search:
 * - initialize variable heap 
 * - set status to searching
 * - if clean_interrupt is enabled, save the current state to 
 *   enable cleanup after interrupt (this uses push)
 */
void start_search(smt_core_t *s) {
  assert(s->status == STATUS_IDLE && s->decision_level == s->base_level);

#if TRACE
  printf("\n---> DPLL START\n");
  fflush(stdout);
#endif
  
  if ((s->option_flag & CLEAN_INTERRUPT_MASK) != 0) {
    /*
     * in clean-interrupt mode, save the current state so
     * that it can be restored after a call to stop_search.
     */
    smt_push(s);
  }

  s->status = STATUS_SEARCHING;
  s->inconsistent = false;
  s->theory_conflict = false;
  s->conflict = NULL;
  s->false_clause = NULL;

  s->stats.restarts = 0;
  s->stats.simplify_calls = 0;
  s->stats.reduce_calls = 0;
  s->stats.decisions = 0;
  s->stats.random_decisions = 0;
  s->stats.conflicts = 0;
  s->simplify_bottom = 0;
  s->simplify_props = 0;
  s->simplify_threshold = 0;

  /*
   * Allow theory solver to do whatever initializations it needs
   */
  s->th_ctrl.start_search(s->th_solver);

#if DEBUG
  check_heap_content(s);
  check_heap(s);
#endif  
}


/*
 * Stop the search: set status to interrupted
 * - this can be called from a signal handler to interrupt the solver
 * - if clean_interrupt is enabled,  the state at start_search can be restored by
 *   calling smt_cleanup(s)
 */
void stop_search(smt_core_t *s) {
  if (s->status == STATUS_SEARCHING) {
    s->status = STATUS_INTERRUPTED;
  }
}


/*
 * Main solving function.
 *
 * It executes the following loop:
 * 1) if lemmas are present, integrate them to the clause database
 * 2) perform boolean and theory propagation
 * 3) if a conflict is found, resolve that conflict otherwise
 *    exit the loop
 */
void smt_process(smt_core_t *s) {  
  while (s->status == STATUS_SEARCHING) {
    if (s->inconsistent) {
      resolve_conflict(s);
      if (s->inconsistent) {
	// conflict could not be resolved: unsat problem
	s->status = STATUS_UNSAT;
      }
      // decay activities after every conflict
      s->cla_inc *= s->inv_cla_decay;
      s->heap.act_increment *= s->heap.inv_act_decay;

    } else if (s->cp_flag) {
      delete_irrelevant_variables(s);
      s->cp_flag = false;

    } else if (! empty_lemma_queue(&s->lemmas)) {
      add_all_lemmas(s);

    } else {
      /*
       * propagation can create a conflict or add lemmas.
       * if it doesn't we're done.
       */
      if (smt_propagation(s) && empty_lemma_queue(&s->lemmas)) break;
    }
  }

  // try to simplify at the base level
  if (s->status == STATUS_SEARCHING &&
      s->decision_level == s->base_level &&
      s->stack.top > s->simplify_bottom && 
      s->stats.propagations >= s->simplify_props + s->simplify_threshold) {
    simplify_clause_database(s);
  }	

}




/*
 * End-of-search check: delayed theory solving:
 * - call the final_check function of the theory solver
 * - if that creates new variables or lemmas or report a conflict
 *   then smt_process is called
 * - return false if more processing is required, 
 *   return true if the theory solver does not trigger anything more.
 * - if the result is true then the whole thing is SAT/UNKNOWN
 */
void smt_final_check(smt_core_t *s) {
  assert(s->status == STATUS_SEARCHING);
  switch (s->th_ctrl.final_check(s->th_solver)) {
  case FCHECK_CONTINUE: 
    /*
     * deal with conflicts or lemmas if any.
     * leave status as it is so that the search can proceeed
     */
    smt_process(s);
    break;
    /*
     * Otherwise: update status to stop the search
     */
  case FCHECK_SAT:
    s->status = STATUS_SAT;
    break;
  case FCHECK_UNKNOWN:
    s->status = STATUS_UNKNOWN;
    break;
  }
}




/*
 * Restart: cause s and the theory solver to backtrack to base_level 
 * (do nothing if decision_level == base_level)
 */
void smt_restart(smt_core_t *s) {
  assert(s->status == STATUS_SEARCHING);

#if TRACE
  printf("\n---> DPLL RESTART\n");
#endif
  s->stats.restarts ++;
  if (s->base_level < s->decision_level) {
    backtrack(s, s->base_level);
    s->th_ctrl.backtrack(s->th_solver, s->base_level);
    // clear the checkpoints
    if (s->cp_flag) {
      purge_all_dynamic_atoms(s);
    }
  }
}





/*******************
 *  CHECK CLAUSES  *
 ******************/

/*
 * Check whether all binary clauses are true in the current assignment
 */
static bool all_binary_clauses_are_true(smt_core_t *s) {
  literal_t l0, l, *v;

  for (l0=0; l0<s->nlits; l0++) {
    if (s->value[l0] != VAL_TRUE) {
      // check whether l is true for all binary clauses {l0, l}
      v = s->bin[l0];
      if (v != NULL) {
	// this loop terminates with l<0 (end-marker) if all clauses {l0, l} are true
	do { l = *v ++; } while (s->value[l] == VAL_TRUE);
	if (l >= 0) return false;
      }
    }
  }

  return true;
}


/*
 * Check whether clause cl is true
 */
static bool clause_is_true(smt_core_t *s, clause_t *cl) {
  uint32_t i;
  literal_t l;

  i = 0;
  do {
    l = cl->cl[i];
    i ++;
    if (s->value[l] == VAL_TRUE) return true;
  } while (l >= 0);

  return false;
}


/*
 * Check whether all problem clauses are true
 */
static bool all_problem_clauses_are_true(smt_core_t *s) {
  uint32_t i, n;
  clause_t **v;

  v = s->problem_clauses;
  n = get_cv_size(v);
  for (i=0; i<n; i++) {
    if (! clause_is_true(s, v[i])) return false;
  }

  return true;
}


/*
 * Check whether all problem clauses are true in the current assignment
 */
bool all_clauses_true(smt_core_t *s) {
  return all_binary_clauses_are_true(s) && all_problem_clauses_are_true(s);
}




/*******************************************
 *   MODEL GENERATION/LITERAL ASSIGNMENTS  *
 ******************************************/

/*
 * Collect all true literals in vector v
 */
void collect_true_literals(smt_core_t *s, ivector_t *v) {
  uint32_t i, n;
  literal_t *lit;

  ivector_reset(v);

  lit = s->stack.lit;
  n = s->stack.top;
  for (i=0; i<n; i++) {
    ivector_push(v, lit[i]);    
  }
}


/*
 * Collect all the decision literals: store them in v
 */
void collect_decision_literals(smt_core_t *s, ivector_t *v) {
  uint32_t i, k, n;
  literal_t *lit;

  ivector_reset(v);

  lit = s->stack.lit;
  n = s->decision_level;
  for (k=s->base_level+1; k<=n; k++) {
    i = s->stack.level_index[k];
    ivector_push(v, lit[i]);
  }
}




/*************************
 *  DEBUGGING FUNCTIONS  *
 ************************/

#if DEBUG

#if 0
// NOT USED
/*
 * Check whether all variables in the heap have activity <= x
 */
static void check_top_var(smt_core_t *s, bvar_t x) {
  uint32_t i, n;
  bvar_t y;
  var_heap_t *heap;
  
  heap = &s->heap;
  n = heap->heap_last;
  for (i=1; i<n; i++) {
    y = heap->heap[i];
    if (s->value[y] == VAL_UNDEF && heap->activity[y] > heap->activity[x]) {
      printf("ERROR: incorrect heap\n");
      fflush(stdout);
    }
  }
}
#endif

/*
 * Check that all unassigned variables are in the heap
 */
static void check_heap_content(smt_core_t *s) {
  uint32_t x;

  if (s->heap.size < s->nvars) {
    printf("ERROR: incorrect heap: heap_size is smaller than the number of variables\n");
    fflush(stdout);
    return;
  }

  for (x=0; x<s->nvars; x++) {
    if (s->value[pos_lit(x)] == VAL_UNDEF && s->heap.heap_index[x] < 0) {
      printf("ERROR: incorrect heap: unassigned variable %"PRIu32" is not in the heap\n", x);
      fflush(stdout);
    }
  }
}


/*
 * Check that the heap is correct
 */
static void check_heap(smt_core_t *s) {
  double *act;
  bvar_t *h, x;
  int32_t *index;
  uint32_t j, k, last;

  h = s->heap.heap;
  index = s->heap.heap_index;
  act = s->heap.activity;
  last = s->heap.heap_last;

  for (j=1; j<=last; j++) {
    x = h[j];
    if (index[x] != j) {
      printf("ERROR: incorrect heap: inconsistent index for variable %"PRId32"\n", x);
      printf("       heap_index is %"PRId32", should be %"PRIu32"\n", index[x], j);
      fflush(stdout);
    }

    k = j>>1;
    if (k < j && act[h[k]] < act[x]) {
    //    if (k < j && heap_precedes(act, x, h[k])) {
      printf("ERROR: incorrect heap order: child %"PRIu32" has higher activity than its parent %"PRIu32"\n", j, k);
      fflush(stdout);
    }
  }
}


#if 0
// NOT USED
/*
 * Check literal vector
 */
static void check_literal_vector(literal_t *v) {
  uint32_t i, n;

  if (v != NULL) {
    n = get_lv_size(v);
    i = get_lv_capacity(v);
    if (n > i - 1) {
      printf("ERROR: overflow in literal vector %p: size = %"PRIu32", capacity = %"PRIu32"\n",
	     v, n, i);
    } else {
      for (i=0; i<n; i++) {
	if (v[i] < 0) {
	  printf("ERROR: negative literal %"PRId32" in vector %p at index %"PRIu32" (size = %"PRIu32")\n", 
		 v[i], v, i, n);
	}	
      }
      if (v[i] != null_literal) {
	printf("ERROR: missing terminator in vector %p (size = %"PRIu32")\n", v, n);
      }
    }
  }
}
#endif


/*
 * Check propagation results
 */
static void check_propagation_bin(smt_core_t *s, literal_t l0) {
  literal_t l1, *v;
  uint8_t *val;

  v = s->bin[l0];
  val = s->value;
  if (v == NULL || val[l0] != VAL_FALSE) return;

  l1 = *v ++;
  while (l1 >= 0) {
    if (val[l1] == VAL_UNDEF) {
      printf("ERROR: missed propagation. Binary clause {%"PRId32", %"PRId32"}\n", l0, l1);
    } else if (val[l1] == VAL_FALSE) {
      printf("ERROR: missed conflict. Binary clause {%"PRId32", %"PRId32"}\n", l0, l1);
    }
    l1 = *v ++;
  }
}

static inline int32_t indicator(bval_t v, bval_t c) {
  return (v == c) ? 1 : 0;
}

static void check_watch_list(smt_core_t *s, literal_t l, clause_t *cl) {
  link_t lnk;

  for (lnk = s->watch[l]; lnk != NULL_LINK; lnk = next_of(lnk)) {
    if (clause_of(lnk) == cl) {
      return;
    }
  }

  printf("ERROR: missing watch, literal = %"PRId32", clause = %p\n", l, clause_of(lnk));
}


static void check_propagation_clause(smt_core_t *s, clause_t *cl) {
  literal_t l0, l1, l;
  literal_t *d;
  uint8_t *val;
  int32_t nf, nt, nu;
  uint32_t i;

  nf = 0;
  nt = 0;
  nu = 0;
  val = s->value;

  l0 = get_first_watch(cl);
  nf += indicator(val[l0], VAL_FALSE);
  nt += indicator(val[l0], VAL_TRUE);
  nu += indicator(val[l0], VAL_UNDEF);

  l1 = get_second_watch(cl);
  nf += indicator(val[l1], VAL_FALSE);
  nt += indicator(val[l1], VAL_TRUE);
  nu += indicator(val[l1], VAL_UNDEF);

  d = cl->cl;
  i = 2;
  l = d[i];
  while (l >= 0) {
    nf += indicator(val[l], VAL_FALSE);
    nt += indicator(val[l], VAL_TRUE);
    nu += indicator(val[l], VAL_UNDEF);

    i ++;
    l = d[i];
  }

  if (nt == 0 && nu == 0) {
    printf("ERROR: missed conflict. Clause {%"PRId32", %"PRId32"", l0, l1);
    i = 2;
    l = d[i];
    while (l >= 0) {
      printf(", %"PRId32"", l);
      i ++;
      l = d[i];
    }
    printf("} (addr = %p)\n", cl);
  }

  if (nt == 0 && nu == 1) {
    printf("ERROR: missed propagation. Clause {%"PRId32", %"PRId32"", l0, l1);
    i = 2;
    l = d[i];
    while (l >= 0) {
      printf(", %"PRId32"", l);
      i ++;
      l = d[i];
    }
    printf("} (addr = %p)\n", cl);
  }

  check_watch_list(s, l0, cl);
  check_watch_list(s, l1, cl);
}

static void check_propagation(smt_core_t *s) {
  literal_t l0;
  uint32_t i, n;
  clause_t **v;

  for (l0=0; l0<s->nlits; l0++) {
    check_propagation_bin(s, l0);
  }

  v = s->problem_clauses;
  n = get_cv_size(v);
  for (i=0; i<n; i++) {
    if (! is_clause_to_be_removed(v[i])) {
      check_propagation_clause(s, v[i]);
    }
  }

  v = s->learned_clauses;
  n = get_cv_size(v);
  for (i=0; i<n; i++) check_propagation_clause(s, v[i]);
}



/*
 * Check that marks/levels and assignments are consistent
 */
static void check_marks(smt_core_t *s) {
  uint32_t i, n;
  bvar_t x;
  literal_t l;

  for (x=0; x<s->nvars; x++) {
    if (is_var_marked(s, x) && s->level[x] > s->base_level) {
      printf("Warning: var %"PRId32" marked but level[%"PRId32"] = %"PRIu32"\n", x, x, s->level[x]);
      fflush(stdout);
    }
  }

  n = s->nb_unit_clauses;
  for (i=0; i<n; i++) {
    l = s->stack.lit[i];
    if (is_lit_unmarked(s, l)) {
      printf("Warning: literal %"PRId32" assigned at level %"PRIu32" but not marked\n", 
	     l, s->level[var_of(l)]);
      fflush(stdout);
    }
  }
}


/*
 * Auxiliary function: print array of literal as a clause (array a)
 * - a must be terminated by null_literal
 */
static void print_literal_array(literal_t *a) {
  uint32_t i;
  literal_t l;

  printf("{");
  i = 0;
  l = a[i];
  while (l >= 0) {
    printf(" ");
    print_literal(stdout, l);
    i ++;
    l = a[i];
  }
  printf(" }");
}

/*
 * Check that all literals in a are false (theory conflict)
 * - a must be terminated by null_literal
 */
static void check_theory_conflict(smt_core_t *s, literal_t *a) {
  uint32_t i;
  literal_t l;

  i = 0;
  l = a[i];
  while (l >= 0) {
    if (s->value[l] != VAL_FALSE) {
      printf("Warning: invalid theory conflict. Literal %"PRId32" is not false\n", l);
      printf("Conflict: ");
      print_literal_array(a);
      printf("\n");
      fflush(stdout);
      return;
    }
    i ++;
    l = a[i];
  }  
}

/*
 * Auxiliary function: if flag is true, print warning message when v
 * is an invalid explanation.
 */
static void print_theory_explanation_warning(ivector_t *v, literal_t l0, bool *flag) {
  uint32_t i;
  literal_t l;

  if (*flag) {
    printf("\nWarning: invalid theory explanation:");
    for (i=0; i<v->size; i++) {
      l = v->data[i];
      printf(" ");
      print_literal(stdout, l);
    }
    printf(" for  ");
    print_literal(stdout, l0);
    printf("\n");
    *flag = false;
  }
}

/*
 * Return true if l0 is before l in the assignment queue
 * both must have the same decision level k
 */
static bool check_precedence(smt_core_t *s, literal_t l0, literal_t l) {
  uint32_t k, i;
  literal_t l1;

  if (l0 == l) return false;

  k = d_level(s, l0);
  assert(k == d_level(s, l));
  i = s->stack.level_index[k];
  for (;;) {
    assert(i < s->stack.top);
    l1 = s->stack.lit[i];
    assert(d_level(s, l1) == k);
    if (l1 == l0) return true;
    if (l1 == l) return false;
    i ++;
  }
}

/*
 * Check causality on theory explanations:
 * - l: literal assigned by theory propagation
 * - s->explanation: literals that imply l
 * (s->explanation is interpreted as a conjunction of literals)
 * all literals in explanation must be before l in the assignment stack
 */
static void check_theory_explanation(smt_core_t *s, literal_t l) {
  uint32_t i, n, k;
  literal_t l0;
  bool print;

  k = d_level(s, l);
  n = s->explanation.size;
  print = true;
  for (i=0; i<n; i++) {
    l0 = s->explanation.data[i];    

    if (s->value[l0] != VAL_TRUE) {
      print_theory_explanation_warning(&s->explanation, l, &print);
      printf("Literal %"PRId32" should be true\n", l0);

    } else if (d_level(s, l0) > k) {
      print_theory_explanation_warning(&s->explanation, l, &print);
      printf("Literal %"PRId32" has higher decision level than %"PRId32"\n", l0, l);

    } else if (d_level(s, l0) == k && ! check_precedence(s, l0, l)) {
      print_theory_explanation_warning(&s->explanation, l, &print);
      printf("Literal %"PRId32" is after %"PRId32" in the assignment queue\n", l0, l);
      
    }
  }
  if (print) {
    fflush(stdout);
  }
}


/*
 * Check whether a[0] and a[1] are valid watched literals for 
 * the clause a[0] ... a[n-1]. (n >= 2)
 */
static void print_lit_val_level(literal_t l, bval_t v, uint32_t k) {
  printf("---> "); 
  print_literal(stdout, l);
  printf(": value = ");
  print_bval(stdout, v);
  if (v != VAL_UNDEF) {
    printf(" at level %"PRIu32, k);
  }
  printf("\n");
}

static void check_watched_literals(smt_core_t *s, uint32_t n, literal_t *a) {
  literal_t l;
  bval_t v0, v1, v;
  uint32_t k0, k1, k, i;

  l = a[0];
  v0 = s->value[l];
  k0 = s->level[var_of(l)];

  l = a[1];
  v1 = s->value[l];
  k1 = s->level[var_of(l)];

  for (i=2; i<n; i++) {
    l = a[i];
    v = s->value[l];
    k = s->level[var_of(l)];
    if (prefer(v, k, v0, k0) || prefer(v, k, v1, k1)) {
      goto error;
    }
  }
  return;

 error:
  printf("Error: incorrect watched literals in new clause\n");
  printf("Clause: {");
  for (i=0; i<n; i++) {
    printf(" ");
    print_literal(stdout, a[i]);
  }
  printf(" }\n");
  print_lit_val_level(a[0], v0, k0);
  print_lit_val_level(a[1], v1, k1);
  print_lit_val_level(l, v, k);
}

#endif
