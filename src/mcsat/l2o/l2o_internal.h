/*
 * This file is part of the Yices SMT Solver.
 * Copyright (C) 2017 SRI International.
 *
 * Yices is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Yices is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Yices.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MCSAT_L2O_INTERNAL_H_
#define MCSAT_L2O_INTERNAL_H_

#include "mcsat/l2o/l2o.h"

#include <stdint.h>

typedef struct {
  uint32_t n_var;
  uint32_t n_var_fixed;
  term_t *var;
  double *val;
} l2o_search_state_t;

typedef struct {
  double_hmap_t eval_map;
  double_hmap_t eval_cache;
  ivector_t modified_vars;

  /** the l2o the evaluator is associated to */
  l2o_t *l2o;
} l2o_evaluator_t;

void l2o_search_state_construct_empty(l2o_search_state_t *state);

void l2o_search_state_destruct(l2o_search_state_t *state);

static inline
bool l2o_search_state_is_empty(const l2o_search_state_t *state) {
  return state->n_var == 0;
}

/** checks if l2o term t has any of free variables of set_of_vars */
bool l2o_term_has_variables(l2o_t *l2o, term_t t, const ivector_t *set_of_vars);

/**
 * Approximately evaluates term_eval t substituting variables v with double values x. The assignment has to be total.
 */
double l2o_evaluate_term_approx(l2o_t *l2o, l2o_evaluator_t *evaluator, term_t term);

void l2o_evaluator_construct(l2o_t *l2o, l2o_evaluator_t *evaluator);

void l2o_evaluator_destruct(l2o_evaluator_t *evaluator);

void l2o_evaluator_reset(l2o_evaluator_t *evaluator);

/** Moves the eval_map to cache.
 * The evaluator must not be used anymore until a new state is set or a reset is performed */
void l2o_evaluator_update_cache(l2o_evaluator_t *evaluator);

void l2o_evaluator_set_state(l2o_evaluator_t *evaluator, const l2o_search_state_t *state);

/**
 * Hill climbing algorithm with cost function t (to be minimized), variables v (some of which have fixed values), and starting point x
 */
void hill_climbing(l2o_t *l2o, term_t t, l2o_search_state_t *state);

#endif /* MCSAT_L2O_INTERNAL_H_ */
