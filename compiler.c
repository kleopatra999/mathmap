/*
 * compiler.c
 *
 * MathMap
 *
 * Copyright (C) 2002-2004 Mark Probst
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <glib.h>
#ifndef OPENSTEP
#include <gmodule.h>
#else
#include <mach-o/dyld.h>
#endif

#include "gtypes.h"
#include "mathmap.h"
#include "vars.h"
#include "internals.h"
#include "tags.h"
#include "exprtree.h"
#include "overload.h"
#include "postfix.h"
#include "internals.h"
#include "jump.h"
#include "scanner.h"
#include "bitvector.h"

//#define NO_CONSTANTS_ANALYSIS

struct _value_t;

typedef struct
{
    int number;
    int last_index;
} temporary_t;

#define TYPE_INT             1
#define TYPE_FLOAT           2
#define TYPE_COMPLEX         3
#define TYPE_COLOR           4
#define TYPE_MATRIX          5
#define TYPE_VECTOR          6

#define MAX_PROMOTABLE_TYPE  TYPE_COMPLEX

#define CONST_MAX            (CONST_ROW | CONST_COL)

typedef int type_t;

typedef struct _compvar_t
{
    variable_t *var;		/* 0 if compvar is a temporary */
    temporary_t *temp;		/* 0 if compvar is a variable */
    int n;			/* n/a if compvar is a temporary */
    type_t type;
    struct _value_t *current;
    struct _value_t *values;
} compvar_t;

struct _statement_list_t;
struct _statement_t;

typedef struct _value_t
{
    compvar_t *compvar;
    int global_index;
    int index;			/* SSA index */
    struct _statement_t *def;
    struct _statement_list_t *uses;
    unsigned int const_type : 2; /* defined in internals.h */
    unsigned int least_const_type_directly_used_in : 2;
    unsigned int least_const_type_multiply_used_in : 2;
    unsigned int have_defined : 1; /* used in c code output */
    struct _value_t *next;	/* next value for same compvar */
} value_t;

typedef struct _value_list_t
{
    value_t *value;
    struct _value_list_t *next;
} value_list_t;

#define PRIMARY_VALUE        1
#define PRIMARY_INT_CONST    2
#define PRIMARY_FLOAT_CONST  3

typedef struct
{
    int type;
    union
    {
	value_t *value;
	int int_const;
	float float_const;
    } v;
} primary_t;

#define OP_NOP 0
#define OP_ADD 1
#define OP_SUB 2
#define OP_NEG 3
#define OP_MUL 4
#define OP_DIV 5
#define OP_MOD 6
#define OP_ABS 7
#define OP_MIN 8
#define OP_MAX 9
#define OP_SQRT 10
#define OP_HYPOT 11
#define OP_SIN 12
#define OP_COS 13
#define OP_TAN 14
#define OP_ASIN 15
#define OP_ACOS 16
#define OP_ATAN 17
#define OP_ATAN2 18
#define OP_POW 19
#define OP_EXP 20
#define OP_LOG 21
#define OP_SINH 22
#define OP_COSH 23
#define OP_TANH 24
#define OP_ASINH 25
#define OP_ACOSH 26
#define OP_ATANH 27
#define OP_GAMMA 28
#define OP_FLOOR 29
#define OP_EQ 30
#define OP_LESS 31
#define OP_LEQ 32
#define OP_NOT 33
#define OP_PRINT 34
#define OP_NEWLINE 35
#define OP_ORIG_VAL 36
#define OP_RED 37
#define OP_GREEN 38
#define OP_BLUE 39
#define OP_ALPHA 40
#define OP_COMPLEX 41
#define OP_C_REAL 42
#define OP_C_IMAG 43
#define OP_C_SQRT 44
#define OP_C_SIN 45
#define OP_C_COS 46
#define OP_C_TAN 47
#define OP_C_ASIN 48
#define OP_C_ACOS 49
#define OP_C_ATAN 50
#define OP_C_POW 51
#define OP_C_EXP 52
#define OP_C_LOG 53
#define OP_C_ARG 54
#define OP_C_SINH 55
#define OP_C_COSH 56
#define OP_C_TANH 57
#define OP_C_ASINH 58
#define OP_C_ACOSH 59
#define OP_C_ATANH 60
#define OP_C_GAMMA 61
#define OP_MAKE_M2X2 62
#define OP_MAKE_M3X3 63
#define OP_FREE_MATRIX 64
#define OP_MAKE_V2 65
#define OP_MAKE_V3 66
#define OP_FREE_VECTOR 67
#define OP_VECTOR_NTH 68
#define OP_SOLVE_LINEAR_2 69
#define OP_SOLVE_LINEAR_3 70
#define OP_NOISE 71
#define OP_RAND 72
#define OP_USERVAL_INT 73
#define OP_USERVAL_FLOAT 74
#define OP_USERVAL_BOOL 75
#define OP_USERVAL_CURVE 76
#define OP_USERVAL_COLOR 77
#define OP_USERVAL_GRADIENT 78
#define OP_MAKE_COLOR 79
#define OP_OUTPUT_COLOR 80

#define NUM_OPS             81

#define TYPE_PROP_CONST      1
#define TYPE_PROP_MAX        2

typedef int type_prop_t;

typedef struct _operation_t
{
    char *name;
    int num_args;
    type_prop_t type_prop;
    type_t const_type;		/* used only if type_prop == TYPE_PROP_CONST */
    int is_pure;
} operation_t;

#define RHS_PRIMARY          1
#define RHS_INTERNAL         2
#define RHS_OP               3

#define MAX_OP_ARGS          9

typedef struct
{
    int type;
    union
    {
	primary_t primary;
	internal_t *internal;
	struct
	{
	    operation_t *op;
	    primary_t args[MAX_OP_ARGS];
	} op;
    } v;
} rhs_t;

#define STMT_NIL             0
#define STMT_ASSIGN          1
#define STMT_PHI_ASSIGN      2
#define STMT_IF_COND         3
#define STMT_WHILE_LOOP      4

#define SLICE_XY_CONST       1
#define SLICE_X_CONST        2
#define SLICE_Y_CONST        4
#define SLICE_NO_CONST       8

typedef struct _statement_t
{
    int type;
    union
    {
	struct
	{
	    value_t *lhs;
	    rhs_t *rhs;
	    rhs_t *rhs2;	/* only valid for STMT_PHI_ASSIGN */
	    value_t *old_value;	/* only valid for STMT_PHI_ASSIGN */
	} assign;
	struct
	{
	    rhs_t *condition;
	    struct _statement_t *consequent;
	    struct _statement_t *alternative;
	    struct _statement_t *exit;
	} if_cond;
	struct
	{
	    struct _statement_t *entry;
	    rhs_t *invariant;
	    struct _statement_t *body;
	} while_loop;
    } v;
    struct _statement_t *parent;
    unsigned int slice_flags;
    struct _statement_t *next;
} statement_t;

typedef struct _statement_list_t
{
    statement_t *stmt;
    struct _statement_list_t *next;
} statement_list_t;

static operation_t ops[NUM_OPS];

static int next_temp_number = 1;

static statement_t *first_stmt = 0;
static statement_t **emit_loc = &first_stmt;
static statement_t dummy_stmt = { STMT_NIL };

#define STMT_STACK_SIZE            64

static statement_t *stmt_stack[STMT_STACK_SIZE];
static int stmt_stackp = 0;

#define CURRENT_STACK_TOP       ((stmt_stackp > 0) ? stmt_stack[stmt_stackp - 1] : 0)
#define UNSAFE_EMIT_STMT(s,l) \
    ({ (s)->parent = CURRENT_STACK_TOP; \
       (s)->next = (l); (l) = (s); })

/*** hash tables ***/

static GHashTable*
direct_hash_table_copy (GHashTable *table)
{
    GHashTable *copy = g_hash_table_new(&g_direct_hash, &g_direct_equal);

    void copy_entry (gpointer key, gpointer value, gpointer user_data)
	{ g_hash_table_insert(copy, key, value); }

    g_hash_table_foreach(table, &copy_entry, 0);

    return copy;
}

/*** value sets ***/

typedef bit_vector_t value_set_t;

/* This is updated by new_value.  We assume that no new values are generated
 * at the time value sets are used.  */
static int next_value_global_index = 0;

static value_set_t*
new_value_set (void)
{
    return new_bit_vector(next_value_global_index, 0);
}

static void
value_set_add (value_set_t *set, value_t *val)
{
    bit_vector_set(set, val->global_index);
}

static int
value_set_contains (value_set_t *set, value_t *val)
{
    return bit_vector_bit(set, val->global_index);
}

static value_set_t*
value_set_copy (value_set_t *set)
{
    return copy_bit_vector(set);
}

static void
free_value_set (value_set_t *set)
{
    free_bit_vector(set);
}

/*** pools ***/

#define GRANULARITY                sizeof(long)
#define FIRST_POOL_SIZE            2048
#define NUM_POOLS                  16

static int active_pool = -1;
static int fill_ptr = 0;

static long *pools[NUM_POOLS];

void
free_pools (void)
{
    int i;

    /* printf("alloced %d pools\n", active_pool + 1); */
    for (i = 0; i <= active_pool; ++i)
	free(pools[i]);

    active_pool = -1;
}

void*
pool_alloc (int size)
{
    int pool_size;
    void *p;

    if (active_pool < 0)
    {
	active_pool = 0;
	pools[0] = (long*)malloc(GRANULARITY * FIRST_POOL_SIZE);
	fill_ptr = 0;

	memset(pools[0], 0, GRANULARITY * FIRST_POOL_SIZE);
    }

    pool_size = FIRST_POOL_SIZE << active_pool;
    size = (size + GRANULARITY - 1) / GRANULARITY;

    if (fill_ptr + size >= pool_size)
    {
	++active_pool;
	assert(active_pool < NUM_POOLS);
	pools[active_pool] = (long*)malloc(GRANULARITY * (FIRST_POOL_SIZE << active_pool));
	fill_ptr = 0;

	memset(pools[active_pool], 0, GRANULARITY * (FIRST_POOL_SIZE << active_pool));
    }

    assert(fill_ptr + size < pool_size);

    p = pools[active_pool] + fill_ptr;
    fill_ptr += size;

    return p;
}

int
op_index (operation_t *op)
{
    return op - ops;
}

#define alloc_stmt()               ((statement_t*)pool_alloc(sizeof(statement_t)))
#define alloc_value()              ((value_t*)pool_alloc(sizeof(value_t)))
#define alloc_rhs()                ((rhs_t*)pool_alloc(sizeof(rhs_t)))
#define alloc_compvar()            (compvar_t*)pool_alloc(sizeof(compvar_t))

static value_t*
new_value (compvar_t *compvar)
{
    value_t *val = alloc_value();

    val->compvar = compvar;	/* dummy value */
    val->global_index = next_value_global_index++;
    val->index = -1;
    val->def = &dummy_stmt;
    val->uses = 0;
    val->const_type = CONST_NONE;
    val->least_const_type_directly_used_in = CONST_MAX;
    val->least_const_type_multiply_used_in = CONST_MAX;
    val->have_defined = 0;
    val->next = 0;

    return val;
}

compvar_t*
make_temporary (void)
{
    temporary_t *temp = (temporary_t*)pool_alloc(sizeof(temporary_t));
    compvar_t *compvar = alloc_compvar();
    value_t *val = new_value(compvar);

    temp->number = next_temp_number++;
    temp->last_index = 0;

    compvar->var = 0;
    compvar->temp = temp;
    compvar->type = TYPE_INT;
    compvar->current = val;
    compvar->values = val;

    return compvar;
}

compvar_t*
make_variable (variable_t *var, int n)
{
    compvar_t *compvar = alloc_compvar();
    value_t *val = new_value(compvar);

    compvar->var = var;
    compvar->temp = 0;
    compvar->n = n;
    compvar->type = TYPE_INT;
    compvar->current = val;
    compvar->values = val;

    return compvar;
}

value_t*
make_lhs (compvar_t *compvar)
{
    value_t *val = new_value(compvar);

    val->next = compvar->values;
    compvar->values = val;

    return val;
}

statement_list_t*
prepend_statement (statement_t *stmt, statement_list_t *rest)
{
    statement_list_t *lst = (statement_list_t*)pool_alloc(sizeof(statement_list_t));

    lst->stmt = stmt;
    lst->next = rest;

    return lst;
}

void
add_use (value_t *val, statement_t *stmt)
{
    val->uses = prepend_statement(stmt, val->uses);
}

void
remove_use (value_t *val, statement_t *stmt)
{
    statement_list_t **lst = &val->uses;

    while (*lst != 0)
    {
	statement_list_t *elem = *lst;

	if (elem->stmt == stmt)
	{
	    *lst = elem->next;

	    return;
	}

	lst = &(*lst)->next;
    }

    assert(0);
}

value_t*
make_value_copy (value_t *val)
{
    return make_lhs(val->compvar);
}

void
set_value_current (value_t *val, value_t *new_current)
{
    val->compvar->current = new_current;
}

value_t*
current_value (compvar_t *compvar)
{
    return compvar->current;
}

void
assign_value_index_and_make_current (value_t *val)
{
    if (val->compvar->var != 0)
	val->index = ++val->compvar->var->last_index[val->compvar->n];
    else
	val->index = ++val->compvar->temp->last_index;

    set_value_current(val, val);
}

primary_t
make_int_const_primary (int int_const)
{
    primary_t primary;

    primary.type = PRIMARY_INT_CONST;
    primary.v.int_const = int_const;

    return primary;
}

primary_t
make_compvar_primary (compvar_t *compvar)
{
    primary_t primary;

    primary.type = PRIMARY_VALUE;
    primary.v.value = current_value(compvar);

    return primary;
}

rhs_t*
make_int_const_rhs (int int_const)
{
    rhs_t *rhs = alloc_rhs();

    rhs->type = RHS_PRIMARY;
    rhs->v.primary.type = PRIMARY_INT_CONST;
    rhs->v.primary.v.int_const = int_const;

    return rhs;
}

rhs_t*
make_float_const_rhs (float float_const)
{
    rhs_t *rhs = alloc_rhs();

    rhs->type = RHS_PRIMARY;
    rhs->v.primary.type = PRIMARY_FLOAT_CONST;
    rhs->v.primary.v.float_const = float_const;

    return rhs;
}

rhs_t*
make_value_rhs (value_t *val)
{
    rhs_t *rhs = alloc_rhs();

    assert(val != 0);

    rhs->type = RHS_PRIMARY;
    rhs->v.primary.type = PRIMARY_VALUE;
    rhs->v.primary.v.value = val;

    return rhs;
}

rhs_t*
make_compvar_rhs (compvar_t *compvar)
{
    return make_value_rhs(current_value(compvar));
}

rhs_t*
make_internal_rhs (internal_t *internal)
{
    rhs_t *rhs = alloc_rhs();

    rhs->type = RHS_INTERNAL;
    rhs->v.internal = internal;

    return rhs;
}

rhs_t*
make_op_rhs (int op_index, ...)
{
    rhs_t *rhs = alloc_rhs();
    va_list ap;
    int i;

    rhs->type = RHS_OP;
    rhs->v.op.op = &ops[op_index];

    va_start(ap, op_index);
    for (i = 0; i < ops[op_index].num_args; ++i)
	rhs->v.op.args[i] = va_arg(ap, primary_t);
    va_end(ap);

    return rhs;
}

statement_t*
find_phi_assign (statement_t *stmts, compvar_t *compvar)
{
    for (; stmts != 0; stmts = stmts->next)
    {
	/* we assert this because this function is called before any
	   optimization takes place, hence no statements are changed to
	   nils */

	assert(stmts->type == STMT_PHI_ASSIGN);

	if (stmts->v.assign.lhs->compvar == compvar)
	    return stmts;
    }

    return 0;
}

primary_t*
find_value_in_rhs (value_t *val, rhs_t *rhs)
{
    switch (rhs->type)
    {
	case RHS_PRIMARY :
	    if (rhs->v.primary.type == PRIMARY_VALUE
		&& rhs->v.primary.v.value == val)
		return &rhs->v.primary;
	    else
		return 0;
	    break;

	case RHS_INTERNAL :
	    return 0;

	case RHS_OP :
	    {
		int i;

		for (i = 0; i < rhs->v.op.op->num_args; ++i)
		    if (rhs->v.op.args[i].type == PRIMARY_VALUE
			&& rhs->v.op.args[i].v.value == val)
			return &rhs->v.op.args[i];
		return 0;
	    }
	    break;
    }

    return 0;
}

static void
for_each_value_in_rhs (rhs_t *rhs, void (*func) (value_t *value))
{
    if (rhs->type == RHS_PRIMARY && rhs->v.primary.type == PRIMARY_VALUE)
	func(rhs->v.primary.v.value);
    else if (rhs->type == RHS_OP)
    {
	int i;

	for (i = 0; i < rhs->v.op.op->num_args; ++i)
	{
	    primary_t *arg = &rhs->v.op.args[i];

	    if (arg->type == PRIMARY_VALUE)
		func(arg->v.value);
	}
    }
}

static void
for_each_assign_statement (statement_t *stmts, void (*func) (statement_t *stmt))
{
    while (stmts != 0)
    {
	switch (stmts->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_ASSIGN :
	    case STMT_PHI_ASSIGN :
		func(stmts);
		break;

	    case STMT_IF_COND :
		for_each_assign_statement(stmts->v.if_cond.consequent, func);
		for_each_assign_statement(stmts->v.if_cond.alternative, func);
		for_each_assign_statement(stmts->v.if_cond.exit, func);
		break;

	    case STMT_WHILE_LOOP :
		for_each_assign_statement(stmts->v.while_loop.entry, func);
		for_each_assign_statement(stmts->v.while_loop.body, func);
		break;

	    default :
		assert(0);
	}

	stmts = stmts->next;
    }
}

static void
for_each_value_in_statements (statement_t *stmt, void (*func) (value_t *value))
{
    while (stmt != 0)
    {
	switch (stmt->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_PHI_ASSIGN :
		for_each_value_in_rhs(stmt->v.assign.rhs2, func);
	    case STMT_ASSIGN :
		for_each_value_in_rhs(stmt->v.assign.rhs, func);
		func(stmt->v.assign.lhs);
		break;

	    case STMT_IF_COND :
		for_each_value_in_rhs(stmt->v.if_cond.condition, func);
		for_each_value_in_statements(stmt->v.if_cond.consequent, func);
		for_each_value_in_statements(stmt->v.if_cond.alternative, func);
		for_each_value_in_statements(stmt->v.if_cond.exit, func);
		break;

	    case STMT_WHILE_LOOP :
		for_each_value_in_rhs(stmt->v.while_loop.invariant, func);
		for_each_value_in_statements(stmt->v.while_loop.entry, func);
		for_each_value_in_statements(stmt->v.while_loop.body, func);
		break;

	    default :
		assert(0);
	}

	stmt = stmt->next;
    }
}

/* checks whether stmt is a direct or indirect child of limit.  if stmt ==
 * limit, it does not count as a child */
static int
stmt_is_within_limit (statement_t *stmt, statement_t *limit)
{
    if (limit == 0)
	return 1;

    do
    {
	stmt = stmt->parent;
    } while (stmt != 0 && stmt != limit);

    if (stmt == 0)
	return 0;
    return 1;
}

static void
rewrite_uses (value_t *old, primary_t new, statement_t *limit)
{
    statement_list_t **lst;

    if (new.type == PRIMARY_VALUE)
	assert(old != new.v.value);

    lst = &old->uses;
    while (*lst != 0)
    {
	statement_list_t *elem = *lst;
	statement_t *stmt = elem->stmt;
	primary_t *primary;

	/* we do not rewrite phis in the loop we're currently working on */
	if (stmt_is_within_limit(stmt, limit)
	    && !(stmt->type == STMT_PHI_ASSIGN && stmt->parent == limit))
	{
	    switch (stmt->type)
	    {
		case STMT_ASSIGN :
		    primary = find_value_in_rhs(old, stmt->v.assign.rhs);
		    break;

		case STMT_PHI_ASSIGN :
		    primary = find_value_in_rhs(old, stmt->v.assign.rhs);
		    if (primary == 0)
			primary = find_value_in_rhs(old, stmt->v.assign.rhs2);
		    break;

		case STMT_IF_COND :
		    primary = find_value_in_rhs(old, stmt->v.if_cond.condition);
		    break;

		case STMT_WHILE_LOOP :
		    primary = find_value_in_rhs(old, stmt->v.while_loop.invariant);
		    break;

		default :
		    assert(0);
	    }

	    assert(primary != 0 && primary->v.value == old);

	    *primary = new;
	    if (new.type == PRIMARY_VALUE)
		add_use(new.v.value, stmt);

	    *lst = elem->next;
	}
	else
	    lst = &elem->next;
    }
}

static void
rewrite_uses_to_value (value_t *old, value_t *new, statement_t *limit)
{
    primary_t primary;

    if (old == new)
	return;

    primary.type = PRIMARY_VALUE;
    primary.v.value = new;

    rewrite_uses(old, primary, limit);
}

void
commit_assign (statement_t *stmt)
{
    statement_t *tos;

    if (stmt_stackp > 0)
    {
	tos = stmt_stack[stmt_stackp - 1];

	switch (tos->type)
	{
	    case STMT_IF_COND :
		{
		    statement_t *phi_assign = find_phi_assign(tos->v.if_cond.exit,
							      stmt->v.assign.lhs->compvar);

		    if (phi_assign == 0)
		    {
			phi_assign = alloc_stmt();

			phi_assign->type = STMT_PHI_ASSIGN;

			phi_assign->v.assign.lhs = make_value_copy(stmt->v.assign.lhs);

			phi_assign->v.assign.rhs = make_value_rhs(current_value(stmt->v.assign.lhs->compvar));
			add_use(current_value(stmt->v.assign.lhs->compvar), phi_assign);

			phi_assign->v.assign.rhs2 = make_value_rhs(current_value(stmt->v.assign.lhs->compvar));
			add_use(current_value(stmt->v.assign.lhs->compvar), phi_assign);

			phi_assign->v.assign.old_value = current_value(stmt->v.assign.lhs->compvar);

			phi_assign->v.assign.lhs->def = phi_assign;

			UNSAFE_EMIT_STMT(phi_assign, tos->v.if_cond.exit);
		    }

		    if (tos->v.if_cond.alternative == 0)
		    {
			assert(phi_assign->v.assign.rhs->type = RHS_PRIMARY
			       && phi_assign->v.assign.rhs->v.primary.type == PRIMARY_VALUE);
			remove_use(phi_assign->v.assign.rhs->v.primary.v.value, phi_assign);

			phi_assign->v.assign.rhs = make_value_rhs(stmt->v.assign.lhs);
			add_use(stmt->v.assign.lhs, phi_assign);
		    }
		    else
		    {
			assert(phi_assign->v.assign.rhs2->type = RHS_PRIMARY
			       && phi_assign->v.assign.rhs2->v.primary.type == PRIMARY_VALUE);
			remove_use(phi_assign->v.assign.rhs2->v.primary.v.value, phi_assign);

			phi_assign->v.assign.rhs2 = make_value_rhs(stmt->v.assign.lhs);
			add_use(stmt->v.assign.lhs, phi_assign);
		    }
		}
		break;

	    case STMT_WHILE_LOOP :
		{
		    statement_t *phi_assign = find_phi_assign(tos->v.while_loop.entry, stmt->v.assign.lhs->compvar);

		    if (phi_assign == 0)
		    {
			phi_assign = alloc_stmt();

			phi_assign->type = STMT_PHI_ASSIGN;

			phi_assign->v.assign.lhs = make_value_copy(stmt->v.assign.lhs);

			phi_assign->v.assign.rhs = make_value_rhs(current_value(stmt->v.assign.lhs->compvar));

			add_use(current_value(stmt->v.assign.lhs->compvar), phi_assign);

			phi_assign->v.assign.lhs->def = phi_assign;

			UNSAFE_EMIT_STMT(phi_assign, tos->v.while_loop.entry);

			phi_assign->v.assign.rhs2 = make_value_rhs(stmt->v.assign.lhs);
			add_use(stmt->v.assign.lhs, phi_assign);

			phi_assign->v.assign.old_value = current_value(stmt->v.assign.lhs->compvar);

			rewrite_uses_to_value(current_value(stmt->v.assign.lhs->compvar), phi_assign->v.assign.lhs, tos);
		    }
		    else
		    {
			assert(phi_assign->v.assign.rhs2->type = RHS_PRIMARY
			       && phi_assign->v.assign.rhs2->v.primary.type == PRIMARY_VALUE);
			remove_use(phi_assign->v.assign.rhs2->v.primary.v.value, phi_assign);

			phi_assign->v.assign.rhs2 = make_value_rhs(stmt->v.assign.lhs);
			add_use(stmt->v.assign.lhs, phi_assign);
		    }
		}
		break;

	    default :
		assert(0);
	}
    }

    assign_value_index_and_make_current(stmt->v.assign.lhs);
}

void
emit_stmt (statement_t *stmt)
{
    void add_use_in_stmt (value_t *value)
	{ add_use(value, stmt); }

    assert(stmt->next == 0);

    stmt->parent = CURRENT_STACK_TOP;

    *emit_loc = stmt;
    emit_loc = &stmt->next;

    switch (stmt->type)
    {
	case STMT_NIL :
	    break;

	case STMT_ASSIGN :
	    for_each_value_in_rhs(stmt->v.assign.rhs, add_use_in_stmt);
	    stmt->v.assign.lhs->def = stmt;
	    break;

	case STMT_PHI_ASSIGN :
	    for_each_value_in_rhs(stmt->v.assign.rhs, add_use_in_stmt);
	    for_each_value_in_rhs(stmt->v.assign.rhs2, add_use_in_stmt);
	    stmt->v.assign.lhs->def = stmt;
	    break;

	case STMT_IF_COND :
	    for_each_value_in_rhs(stmt->v.if_cond.condition, add_use_in_stmt);
	    break;

	case STMT_WHILE_LOOP :
	    for_each_value_in_rhs(stmt->v.while_loop.invariant, add_use_in_stmt);
	    break;

	default :
	    assert(0);
    }
}

void
emit_nil (void)
{
    statement_t *stmt = alloc_stmt();

    stmt->type = STMT_NIL;
    stmt->next = 0;

    emit_stmt(stmt);
}

void
emit_assign (value_t *lhs, rhs_t *rhs)
{
    statement_t *stmt = alloc_stmt();

    stmt->type = STMT_ASSIGN;
    stmt->next = 0;

    stmt->v.assign.lhs = lhs;
    stmt->v.assign.rhs = rhs;

    emit_stmt(stmt);

    commit_assign(stmt);
}

void
start_if_cond (rhs_t *condition)
{
    statement_t *stmt = alloc_stmt();

    stmt->type = STMT_IF_COND;
    stmt->next = 0;

    stmt->v.if_cond.condition = condition;
    stmt->v.if_cond.consequent = 0;
    stmt->v.if_cond.alternative = 0;
    stmt->v.if_cond.exit = 0;

    emit_stmt(stmt);
    stmt_stack[stmt_stackp++] = stmt;

    emit_loc = &stmt->v.if_cond.consequent;
}

static void
reset_values_for_phis (statement_t *phi, int delete)
{
    for (; phi != 0; phi = phi->next)
    {
	assert(phi->type == STMT_PHI_ASSIGN);

	set_value_current(phi->v.assign.lhs, phi->v.assign.old_value);

	if (delete)
	    phi->v.assign.old_value = 0;
    }
}

void
switch_if_branch (void)
{
    statement_t *stmt;

    assert(stmt_stackp > 0);

    stmt = stmt_stack[stmt_stackp - 1];

    assert(stmt->type == STMT_IF_COND && stmt->v.if_cond.alternative == 0);

    if (stmt->v.if_cond.consequent == 0)
	emit_nil();

    reset_values_for_phis(stmt->v.if_cond.exit, 0);

    emit_loc = &stmt->v.if_cond.alternative;
}

void
end_if_cond (void)
{
    statement_t *stmt, *phi;

    assert(stmt_stackp > 0);

    stmt = stmt_stack[--stmt_stackp];

    assert(stmt->type == STMT_IF_COND && stmt->v.if_cond.consequent != 0);

    if (stmt->v.if_cond.alternative == 0)
	emit_nil();

    if (stmt->v.if_cond.exit == 0)
    {
	statement_t *nil = alloc_stmt();

	nil->type = STMT_NIL;

	UNSAFE_EMIT_STMT(nil, stmt->v.if_cond.exit);
    }

    reset_values_for_phis(stmt->v.if_cond.exit, 1);

    for (phi = stmt->v.if_cond.exit; phi != 0; phi = phi->next)
    {
	assert(phi->type == STMT_PHI_ASSIGN);

	commit_assign(phi);
    }

    emit_loc = &stmt->next;
}

void
start_while_loop (rhs_t *invariant)
{
    statement_t *stmt = alloc_stmt();

    stmt->type = STMT_WHILE_LOOP;
    stmt->next = 0;

    stmt->v.while_loop.entry = 0;
    stmt->v.while_loop.body = 0;

    {
	value_t *value;
	statement_t *phi_assign;

	assert(invariant->type == RHS_PRIMARY && invariant->v.primary.type == PRIMARY_VALUE);

	value = invariant->v.primary.v.value;

	phi_assign = alloc_stmt();

	phi_assign->type = STMT_PHI_ASSIGN;
	phi_assign->v.assign.lhs = make_value_copy(value);
	phi_assign->v.assign.rhs = make_value_rhs(current_value(value->compvar));
	phi_assign->v.assign.rhs2 = make_value_rhs(current_value(value->compvar));

	add_use(current_value(value->compvar), phi_assign);
	add_use(current_value(value->compvar), phi_assign);

	phi_assign->v.assign.lhs->def = phi_assign;

	UNSAFE_EMIT_STMT(phi_assign, stmt->v.while_loop.entry);

	assign_value_index_and_make_current(phi_assign->v.assign.lhs);
 
	stmt->v.while_loop.invariant = make_value_rhs(current_value(value->compvar));
    }

    emit_stmt(stmt);
    stmt_stack[stmt_stackp++] = stmt;

    emit_loc = &stmt->v.while_loop.body;
}

void
end_while_loop (void)
{
    statement_t *stmt, *phi;

    assert(stmt_stackp > 0);

    stmt = stmt_stack[--stmt_stackp];

    assert(stmt->type == STMT_WHILE_LOOP);

    if (stmt->v.while_loop.body == 0)
	emit_nil();

    reset_values_for_phis(stmt->v.while_loop.entry, 1);

    for (phi = stmt->v.while_loop.entry; phi != 0; phi = phi->next)
    {
	assert(phi->type == STMT_PHI_ASSIGN);

	commit_assign(phi);
    }

    emit_loc = &stmt->next;
}

#define GAMMA(a)              (((a) > 171.0) ? 0.0 : gsl_sf_gamma((a)))
#define PRINT(a)              (printf("%f ", (float)(a)), 0)
#define NEWLINE()             (printf("\n"))
#define MAKE_M2X2(a,b,c,d)           ({ gsl_matrix *m = gsl_matrix_alloc(2,2); \
                                        gsl_matrix_set(m,0,0,(a)); gsl_matrix_set(m,0,1,(b)); gsl_matrix_set(m,1,0,(c)); gsl_matrix_set(m,1,1,(d)); m; })
#define MAKE_M3X3(a,b,c,d,e,f,g,h,i) ({ gsl_matrix *m = gsl_matrix_alloc(3,3); \
                                        gsl_matrix_set(m,0,0,(a)); gsl_matrix_set(m,0,1,(b)); gsl_matrix_set(m,0,2,(c)); \
                                        gsl_matrix_set(m,1,0,(d)); gsl_matrix_set(m,1,1,(e)); gsl_matrix_set(m,1,2,(f)); \
                                        gsl_matrix_set(m,2,0,(g)); gsl_matrix_set(m,2,1,(h)); gsl_matrix_set(m,2,2,(i)); m; })
#define MAKE_V2(a,b)          ({ gsl_vector *v = gsl_vector_alloc(2); gsl_vector_set(v,0,(a)); gsl_vector_set(v,1,(b)); v; })
#define MAKE_V3(a,b,c)        ({ gsl_vector *v = gsl_vector_alloc(3); gsl_vector_set(v,0,(a)); gsl_vector_set(v,1,(b)); gsl_vector_set(v,2,(c)); v; })
#define SOLVE_LINEAR_2(m,v)   ({ gsl_vector *r = gsl_vector_alloc(2); gsl_linalg_HH_solve(m,v,r); r; })
#define SOLVE_LINEAR_3(m,v)   ({ gsl_vector *r = gsl_vector_alloc(3); gsl_linalg_HH_solve(m,v,r); r; })
#define RAND(a,b)             ((rand() / (float)RAND_MAX) * ((b) - (a)) + (a))

#define STK                   invocation->stack
#define STKP                  invocation->stackp

#include <complex.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_specfunc.h>

#include "spec_func.h"
#include "builtins.h"
#include "noise.h"

#include "new_builtins.c"

/*** debug printing ***/

static void
print_indent (int indent)
{
    int i;

    for (i = 0; i < indent; ++i)
	fputs("  ", stdout);
}

static void
print_value (value_t *val)
{
    if (val->compvar->var != 0)
	printf("%s[%d]_%d", val->compvar->var->name, val->compvar->n, val->index);
    else
	printf("$t%d_%d", val->compvar->temp->number, val->index);
}

static void
print_primary (primary_t *primary)
{
    switch (primary->type)
    {
	case PRIMARY_VALUE :
	    print_value(primary->v.value);
	    break;

	case PRIMARY_INT_CONST :
	    printf("%d", primary->v.int_const);
	    break;

	case PRIMARY_FLOAT_CONST :
	    printf("%f", primary->v.float_const);
	    break;

	default :
	    assert(0);
    }
}

static void
print_rhs (rhs_t *rhs)
{
    switch (rhs->type)
    {
	case RHS_PRIMARY :
	    print_primary(&rhs->v.primary);
	    break;

	case RHS_INTERNAL :
	    printf("%s", rhs->v.internal->name);
	    break;

	case RHS_OP :
	    {
		int i;

		printf("%s", rhs->v.op.op->name);
		for (i = 0; i < rhs->v.op.op->num_args; ++i)
		{
		    printf(" ");
		    print_primary(&rhs->v.op.args[i]);
		}
	    }
	    break;

	default :
	    assert(0);
    }
}

static void
output_const_type (FILE *out, unsigned int const_type)
{
    if (const_type & CONST_ROW)
	fputs("x", out);
    if (const_type & CONST_COL)
	fputs("y", out);
    if (const_type == CONST_NONE)
	fputs("-", out);
}

static int
count_uses (value_t *val)
{
    statement_list_t *lst;
    int num_uses = 0;

    for (lst = val->uses; lst != 0; lst = lst->next)
	++num_uses;

    return num_uses;
}

void
dump_code (statement_t *stmt, int indent)
{
    while (stmt != 0)
    {
	switch (stmt->type)
	{
	    case STMT_NIL :
		/*
		print_indent(indent);
		printf("nil (%p)\n", stmt);
		*/
		break;

	    case STMT_ASSIGN :
		print_indent(indent);
		print_value(stmt->v.assign.lhs);
		printf(" (%d) = ", count_uses(stmt->v.assign.lhs));
		print_rhs(stmt->v.assign.rhs);
		printf("   ");
		output_const_type(stdout, stmt->v.assign.lhs->const_type);
		printf(" ");
		output_const_type(stdout, stmt->v.assign.lhs->least_const_type_directly_used_in);
		printf(" ");
		output_const_type(stdout, stmt->v.assign.lhs->least_const_type_multiply_used_in);
		printf(" (%p)\n", stmt);
		break;

	    case STMT_PHI_ASSIGN :
		print_indent(indent);
		print_value(stmt->v.assign.lhs);
		printf(" (%d) = phi(", count_uses(stmt->v.assign.lhs));
		print_rhs(stmt->v.assign.rhs);
		printf(", ");
		print_rhs(stmt->v.assign.rhs2);
		printf(")   ");
		output_const_type(stdout, stmt->v.assign.lhs->const_type);
		printf(" ");
		output_const_type(stdout, stmt->v.assign.lhs->least_const_type_directly_used_in);
		printf(" ");
		output_const_type(stdout, stmt->v.assign.lhs->least_const_type_multiply_used_in);
		printf(" (%p)\n", stmt);
		break;

	    case STMT_IF_COND :
		print_indent(indent);
		printf("if ");
		print_rhs(stmt->v.if_cond.condition);
		printf(" (%p)\n", stmt);
		dump_code(stmt->v.if_cond.consequent, indent + 1);
		print_indent(indent);
		printf("else\n");
		dump_code(stmt->v.if_cond.alternative, indent + 1);
		print_indent(indent);
		printf("exit\n");
		dump_code(stmt->v.if_cond.exit, indent + 1);
		break;

	    case STMT_WHILE_LOOP :
		print_indent(indent);
		printf("start while (%p)\n", stmt);
		dump_code(stmt->v.while_loop.entry, indent + 1);
		print_indent(indent);
		printf("while ");
		print_rhs(stmt->v.while_loop.invariant);
		printf("\n");
		dump_code(stmt->v.while_loop.body, indent + 1);
		break;

	    default :
		assert(0);
	}

	stmt = stmt->next;
    }
}

/*** ssa generation from tree code ***/

static void
alloc_var_compvars_if_needed (variable_t *var)
{
    int i;

    for (i = 0; i < var->type.length; ++i)
	if (var->compvar[i] == 0)
	    var->compvar[i] = make_variable(var, i);
}

static void
gen_code (exprtree *tree, compvar_t **dest, int is_alloced)
{
    int i;

    switch (tree->type)
    {
	case EXPR_INT_CONST :
	    if (!is_alloced)
		dest[0] = make_temporary();
	    emit_assign(make_lhs(dest[0]), make_int_const_rhs(tree->val.int_const));
	    break;

	case EXPR_FLOAT_CONST :
	    if (!is_alloced)
		dest[0] = make_temporary();
	    emit_assign(make_lhs(dest[0]), make_float_const_rhs(tree->val.float_const));
	    break;

	case EXPR_TUPLE_CONST :
	    for (i = 0; i < tree->val.tuple_const.length; ++i)
	    {
		if (!is_alloced)
		    dest[i] = make_temporary();
		emit_assign(make_lhs(dest[i]), make_float_const_rhs(tree->val.tuple_const.data[i]));
	    }
	    break;

	case EXPR_TUPLE :
	    {
		exprtree *elem;

		for (i = 0, elem = tree->val.tuple.elems; elem != 0; ++i, elem = elem->next)
		    gen_code(elem, dest + i, is_alloced);
	    }
	    break;

	case EXPR_SELECT :
	    {
		compvar_t *temps[tree->val.select.tuple->result.length];
		exprtree *sub;
		int i;

		gen_code(tree->val.select.tuple, temps, 0);

		for (sub = tree->val.select.subscripts, i = 0; sub != 0; sub = sub->next, ++i)
		{
		    int subscript;

		    if (is_exprtree_single_const(sub, &subscript, 0))
		    {
			if (subscript < 0)
			    subscript = 0;
			if (subscript >= tree->val.select.tuple->result.length)
			    subscript = tree->val.select.tuple->result.length - 1;

			if (!is_alloced)
			    dest[i] = temps[subscript];
			else
			    emit_assign(make_lhs(dest[i]), make_compvar_rhs(temps[subscript]));
		    }
		    else
		    {
			compvar_t *subscript;
			int length = tree->val.select.tuple->result.length;
			int j;

			if (!is_alloced)
			    dest[i] = make_temporary();

			gen_code(sub, &subscript, 0);

			for (j = 1; j < length; ++j)
			{
			    start_if_cond(make_op_rhs(OP_LESS, make_compvar_primary(subscript), make_int_const_primary(j)));
			    emit_assign(make_lhs(dest[i]), make_compvar_rhs(temps[j - 1]));
			    switch_if_branch();
			}
			emit_assign(make_lhs(dest[i]), make_compvar_rhs(temps[length - 1]));
			for (j = 0; j < length - 1; ++j)
			    end_if_cond();
		    }
		}
	    }
	    break;

	case EXPR_VARIABLE :
	    alloc_var_compvars_if_needed(tree->val.var);
	    for (i = 0; i < tree->val.var->type.length; ++i)
		if (!is_alloced)
		    dest[i] = tree->val.var->compvar[i];
		else
		    emit_assign(make_lhs(dest[i]), make_compvar_rhs(tree->val.var->compvar[i]));
	    break;

	case EXPR_INTERNAL :
	    if (!is_alloced)
		dest[0] = make_temporary();
	    emit_assign(make_lhs(dest[0]), make_internal_rhs(tree->val.internal));
	    break;

	case EXPR_ASSIGNMENT :
	    alloc_var_compvars_if_needed(tree->val.assignment.var);
	    gen_code(tree->val.assignment.value, tree->val.assignment.var->compvar, 1);
	    for (i = 0; i < tree->result.length; ++i)
		if (is_alloced)
		    emit_assign(make_lhs(dest[i]), make_compvar_rhs(tree->val.assignment.var->compvar[i]));
		else
		    dest[i] = tree->val.assignment.var->compvar[i];
	    break;

	case EXPR_SUB_ASSIGNMENT :
	    {
		compvar_t *temps[tree->val.sub_assignment.value->result.length];
		exprtree *sub;
		int i;

		alloc_var_compvars_if_needed(tree->val.sub_assignment.var);

		gen_code(tree->val.sub_assignment.value, temps, 0);

		for (sub = tree->val.sub_assignment.subscripts, i = 0; sub != 0; sub = sub->next, ++i)
		{
		    int subscript;
		    
		    if (is_exprtree_single_const(sub, &subscript, 0))
		    {
			if (subscript < 0)
			    subscript = 0;
			if (subscript >= tree->result.length)
			    subscript = tree->result.length - 1;

			emit_assign(make_lhs(tree->val.sub_assignment.var->compvar[subscript]), make_compvar_rhs(temps[i]));
		    }
		    else
		    {
			compvar_t *subscript;
			int length = tree->val.sub_assignment.var->type.length;
			int j;

			if (!is_alloced)
			    dest[i] = make_temporary();

			gen_code(sub, &subscript, 0);

			for (j = 1; j < length; ++j)
			{
			    start_if_cond(make_op_rhs(OP_LESS, make_compvar_primary(subscript), make_int_const_primary(j)));
			    emit_assign(make_lhs(tree->val.sub_assignment.var->compvar[j - 1]), make_compvar_rhs(temps[i]));
			    switch_if_branch();
			}
			emit_assign(make_lhs(tree->val.sub_assignment.var->compvar[length - 1]), make_compvar_rhs(temps[i]));
			for (j = 0; j < length - 1; ++j)
			    end_if_cond();
		    }

		    if (is_alloced)
			emit_assign(make_lhs(dest[i]), make_compvar_rhs(temps[i]));
		    else
			dest[i] = temps[i];
		}
	    }
	    break;

	case EXPR_CAST :
	    gen_code(tree->val.cast.tuple, dest, is_alloced);
	    break;

	case EXPR_FUNC :
	    {
		exprtree *arg;
		int num_args = 0;
		compvar_t ***args;
		int *arglengths;

		for (arg = tree->val.func.args; arg != 0; arg = arg->next)
		    ++num_args;

		args = (compvar_t***)alloca(num_args * sizeof(compvar_t**));
		arglengths = (int*)alloca(num_args * sizeof(int));

		for (i = 0, arg = tree->val.func.args; i < num_args; ++i, arg = arg->next)
		{
		    args[i] = (compvar_t**)alloca(arg->result.length * sizeof(compvar_t*));
		    arglengths[i] = arg->result.length;
		    gen_code(arg, args[i], 0);
		}

		if (!is_alloced)
		    for (i = 0; i < tree->result.length; ++i)
			dest[i] = make_temporary();

		tree->val.func.entry->v.builtin.generator(args, arglengths, dest);
	    }
	    break;
 
	case EXPR_SEQUENCE :
	    {
		compvar_t **left_result;

		left_result = (compvar_t**)alloca(tree->val.operator.left->result.length * sizeof(compvar_t*));
		gen_code(tree->val.operator.left, left_result, 0);

		gen_code(tree->val.operator.right, dest, is_alloced);
	    }
	    break;

	case EXPR_IF_THEN :
	case EXPR_IF_THEN_ELSE :
	    {
		compvar_t *condition;
		compvar_t **result = (compvar_t**)alloca(tree->result.length * sizeof(compvar_t*));

		for (i = 0; i < tree->result.length; ++i)
		    result[i] = make_temporary();

		gen_code(tree->val.ifExpr.condition, &condition, 0);

		start_if_cond(make_compvar_rhs(condition));

		gen_code(tree->val.ifExpr.consequent, result, 1);

		switch_if_branch();

		if (tree->type == EXPR_IF_THEN_ELSE)
		    gen_code(tree->val.ifExpr.alternative, result, 1);

		end_if_cond();

		for (i = 0; i < tree->result.length; ++i)
		    if (is_alloced)
			emit_assign(make_lhs(dest[i]), make_compvar_rhs(result[i]));
		    else
			dest[i] = result[i];
	    }
	    break;

	case EXPR_DO_WHILE :
	case EXPR_WHILE :
	    {
		compvar_t *invariant = make_temporary();
		compvar_t **body_result = (compvar_t**)alloca(tree->val.whileExpr.body->result.length * sizeof(compvar_t*));

		if (tree->type == EXPR_DO_WHILE)
		    gen_code(tree->val.whileExpr.body, body_result, 0);

		gen_code(tree->val.whileExpr.invariant, &invariant, 1);
		start_while_loop(make_compvar_rhs(invariant));
		gen_code(tree->val.whileExpr.body, body_result, 0);
		gen_code(tree->val.whileExpr.invariant, &invariant, 1);
		end_while_loop();

		if (!is_alloced)
		    dest[0] = make_temporary();
		emit_assign(make_lhs(dest[0]), make_int_const_rhs(0));
	    }
	    break;

	case EXPR_USERVAL :
	    switch (tree->val.userval.info->type)
	    {
		case USERVAL_INT_CONST :
		    if (!is_alloced)
			dest[0] = make_temporary();
		    emit_assign(make_lhs(dest[0]), make_op_rhs(OP_USERVAL_INT, make_int_const_primary(tree->val.userval.info->index)));
		    break;

		case USERVAL_FLOAT_CONST :
		    if (!is_alloced)
			dest[0] = make_temporary();
		    emit_assign(make_lhs(dest[0]), make_op_rhs(OP_USERVAL_FLOAT, make_int_const_primary(tree->val.userval.info->index)));
		    break;

		case USERVAL_BOOL_CONST :
		    if (!is_alloced)
			dest[0] = make_temporary();
		    emit_assign(make_lhs(dest[0]), make_op_rhs(OP_USERVAL_BOOL, make_int_const_primary(tree->val.userval.info->index)));
		    break;

		case USERVAL_CURVE :
		    {
			compvar_t *pos;

			if (!is_alloced)
			    dest[0] = make_temporary();

			gen_code(tree->val.userval.args, &pos, 0);

			emit_assign(make_lhs(dest[0]), make_op_rhs(OP_USERVAL_CURVE, make_int_const_primary(tree->val.userval.info->index),
								   make_compvar_primary(pos)));
		    }
		    break;

		case USERVAL_COLOR :
		case USERVAL_GRADIENT :
		    {
			compvar_t *pos;
			compvar_t *temp = make_temporary();
			int i;

			if (!is_alloced)
			    for (i = 0; i < 4; ++i)
				dest[i] = make_temporary();

			if (tree->val.userval.info->type == USERVAL_COLOR)
			    emit_assign(make_lhs(temp), make_op_rhs(OP_USERVAL_COLOR, make_int_const_primary(tree->val.userval.info->index)));
			else
			{
			    gen_code(tree->val.userval.args, &pos, 0);
			    emit_assign(make_lhs(temp), make_op_rhs(OP_USERVAL_GRADIENT, make_int_const_primary(tree->val.userval.info->index),
								    make_compvar_primary(pos)));
			}

			emit_assign(make_lhs(dest[0]), make_op_rhs(OP_RED, make_compvar_primary(temp)));
			emit_assign(make_lhs(dest[1]), make_op_rhs(OP_GREEN, make_compvar_primary(temp)));
			emit_assign(make_lhs(dest[2]), make_op_rhs(OP_BLUE, make_compvar_primary(temp)));
			emit_assign(make_lhs(dest[3]), make_op_rhs(OP_ALPHA, make_compvar_primary(temp)));
		    }
		    break;

		case USERVAL_IMAGE :
		    if (!is_alloced)
			dest[0] = make_temporary();
		    emit_assign(make_lhs(dest[0]), make_int_const_rhs(tree->val.userval.info->index));
		    break;

		default :
		    assert(0);
	    }
	    break;

	default :
	    assert(0);
   }
}

/*** dfa ***/

static statement_list_t*
prepend_value_statements (value_t *value, statement_list_t *rest)
{
    statement_list_t *lst;

    for (lst = value->uses; lst != 0; lst = lst->next)
	rest = prepend_statement(lst->stmt, rest);

    return rest;
}

static statement_list_t*
prepend_compvar_statements (compvar_t *compvar, statement_list_t *rest)
{
    value_t *value;

    for (value = compvar->values; value != 0; value = value->next)
	rest = prepend_value_statements(value, rest);

    return rest;
}

static void
perform_worklist_dfa (statement_t *stmts,
		      statement_list_t* (*build_worklist) (statement_t *stmt, statement_list_t *worklist),
		      statement_list_t* (*work_statement) (statement_t *stmt, statement_list_t *worklist))
{
    statement_list_t *worklist = 0;

    void builder (statement_t *stmt)
	{ worklist = build_worklist(stmt, worklist); }

    for_each_assign_statement(stmts, &builder);

    do
    {
	statement_list_t *new_worklist = 0;

	while (worklist != 0)
	{
	    new_worklist = work_statement(worklist->stmt, new_worklist);
	    worklist = worklist->next;
	}

	worklist = new_worklist;
    } while (worklist != 0);
}


/*** type propagation ***/

static type_t
primary_type (primary_t *primary)
{
    switch (primary->type)
    {
	case PRIMARY_VALUE :
	    return primary->v.value->compvar->type;

	case PRIMARY_INT_CONST :
	    return TYPE_INT;

	case PRIMARY_FLOAT_CONST :
	    return TYPE_FLOAT;

	default :
	    assert(0);
    }
}

static type_t
rhs_type (rhs_t *rhs)
{
    switch (rhs->type)
    {
	case RHS_PRIMARY :
	    return primary_type(&rhs->v.primary);

	case RHS_INTERNAL :
	    return TYPE_FLOAT;	/* FIXME: actually, most internals are int */

	case RHS_OP :
	    if (rhs->v.op.op->type_prop == TYPE_PROP_CONST)
		return rhs->v.op.op->const_type;
	    else
	    {
		int max = TYPE_INT;
		int i;

		assert(rhs->v.op.op->type_prop == TYPE_PROP_MAX);

		for (i = 0; i < rhs->v.op.op->num_args; ++i)
		{
		    int type = primary_type(&rhs->v.op.args[i]);

		    if (type > max)
			max = type;
		}

		return max;
	    }
	    break;

	default :
	    assert(0);
    }

    return 0;
}

static statement_list_t*
propagate_types_builder (statement_t *stmt, statement_list_t *worklist)
{
    int type, type2;

    switch (stmt->type)
    {
	case STMT_ASSIGN :
	    type = rhs_type(stmt->v.assign.rhs);
	    if (type != stmt->v.assign.lhs->compvar->type)
	    {
		stmt->v.assign.lhs->compvar->type = type;
		worklist = prepend_compvar_statements(stmt->v.assign.lhs->compvar, worklist);
	    }
	    break;

	case STMT_PHI_ASSIGN :
	    type = rhs_type(stmt->v.assign.rhs);
	    type2 = rhs_type(stmt->v.assign.rhs2);
	    if (type != type2)
	    {
		assert(type <= MAX_PROMOTABLE_TYPE && type2 <= MAX_PROMOTABLE_TYPE);
		if (type2 > type)
		    type = type2;
	    }
	    if (type != stmt->v.assign.lhs->compvar->type)
	    {
		stmt->v.assign.lhs->compvar->type = type;
		worklist = prepend_compvar_statements(stmt->v.assign.lhs->compvar, worklist);
	    }
	    break;

	default :
	    assert(0);
    }

    return worklist;
}

static statement_list_t*
propagate_types_worker (statement_t *stmt, statement_list_t *worklist)
{
    switch (stmt->type)
    {
	case STMT_ASSIGN :
	case STMT_PHI_ASSIGN :
	{
	    int type, type2;
	    type = rhs_type(stmt->v.assign.rhs);
	    if (stmt->type == STMT_PHI_ASSIGN)
	    {
		type2 = rhs_type(stmt->v.assign.rhs2);
		if (type != type2)
		{
		    assert(type <= MAX_PROMOTABLE_TYPE && type2 <= MAX_PROMOTABLE_TYPE);
		    if (type2 > type)
			type = type2;
		}
	    }

	    if (type != stmt->v.assign.lhs->compvar->type)
	    {
		stmt->v.assign.lhs->compvar->type = type;
		worklist = prepend_compvar_statements(stmt->v.assign.lhs->compvar, worklist);
	    }
	}
	break;

	case STMT_IF_COND :
	case STMT_WHILE_LOOP :
	    break;

	default :
	    assert(0);
    }

    return worklist;
}

static void
propagate_types (void)
{
    perform_worklist_dfa(first_stmt, &propagate_types_builder, &propagate_types_worker);
}

/*** constants analysis ***/

#define LEAST_CONST_TYPE(v)        ((v)->const_type & (v)->least_const_type_multiply_used_in)

int
primary_constant (primary_t *primary)
{
    switch (primary->type)
    {
	case PRIMARY_VALUE :
	    return LEAST_CONST_TYPE(primary->v.value);

	case PRIMARY_INT_CONST :
	case PRIMARY_FLOAT_CONST :
	    return CONST_MAX;

	default :
	    assert(0);
    }
}

int
rhs_constant (rhs_t *rhs)
{
    switch (rhs->type)
    {
	case RHS_PRIMARY :
	    return primary_constant(&rhs->v.primary);

	case RHS_INTERNAL :
	    return rhs->v.internal->const_type;

	case RHS_OP :
	    if (rhs->v.op.op->is_pure)
	    {
		int i;
		int const_type_max = CONST_MAX;

		for (i = 0; i < rhs->v.op.op->num_args; ++i)
		{
		    int const_type = primary_constant(&rhs->v.op.args[i]);

		    const_type_max &= const_type;
		}

		return const_type_max;
	    }
	    else
		return CONST_NONE;
	    break;

	default :
	    assert(0);
    }
}

void
analyze_phis_constant (statement_t *phis, int const_max, int *changed)
{
    while (phis != 0)
    {
	int const_type, const_type2;

	if (phis->type == STMT_NIL)
	{
	    phis = phis->next;
	    continue;
	}

	assert(phis->type == STMT_PHI_ASSIGN);

	const_type = rhs_constant(phis->v.assign.rhs);
	const_type2 = rhs_constant(phis->v.assign.rhs2);

	const_type = const_type & const_type2 & const_max;

	if (phis->v.assign.lhs->const_type != const_type)
	{
	    phis->v.assign.lhs->const_type = const_type;
	    *changed = 1;
	}

	phis = phis->next;
    }
}

static void
analyze_stmts_constants (statement_t *stmt, int *changed, unsigned int inherited_max_const)
{
    int const_type;

    while (stmt != 0)
    {
	switch (stmt->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_ASSIGN :
		const_type = rhs_constant(stmt->v.assign.rhs);
		if (stmt->v.assign.lhs->const_type != (const_type & inherited_max_const))
		{
		    stmt->v.assign.lhs->const_type = (const_type & inherited_max_const);
		    *changed = 1;
		}
		break;

	    case STMT_PHI_ASSIGN :
		assert(0);
		break;

	    case STMT_IF_COND :
		const_type = rhs_constant(stmt->v.if_cond.condition);

		analyze_stmts_constants(stmt->v.if_cond.consequent, changed, const_type & inherited_max_const);
		analyze_stmts_constants(stmt->v.if_cond.alternative, changed, const_type & inherited_max_const);
		analyze_phis_constant(stmt->v.if_cond.exit, const_type & inherited_max_const, changed);
		break;

	    case STMT_WHILE_LOOP :
		const_type = rhs_constant(stmt->v.while_loop.invariant);

		analyze_phis_constant(stmt->v.while_loop.entry, const_type & inherited_max_const, changed);
		analyze_stmts_constants(stmt->v.while_loop.body, changed, const_type & inherited_max_const);
		break;

	    default :
		assert(0);
	}

	stmt = stmt->next;
    }
}

static unsigned int
analyze_least_const_type_directly_used_in (statement_t *stmt)
{
    void clear_const_bits_from_assignment (value_t *val)
	{ val->least_const_type_directly_used_in &= LEAST_CONST_TYPE(stmt->v.assign.lhs); }

    unsigned int least_const_type = CONST_MAX;

    while (stmt != 0)
    {
	switch (stmt->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_PHI_ASSIGN :
		for_each_value_in_rhs(stmt->v.assign.rhs2, &clear_const_bits_from_assignment);
	    case STMT_ASSIGN :
		for_each_value_in_rhs(stmt->v.assign.rhs, &clear_const_bits_from_assignment);
		least_const_type &= LEAST_CONST_TYPE(stmt->v.assign.lhs);
		break;

	    case STMT_IF_COND :
	    {
		unsigned int sub_least_const_type = CONST_MAX;

		void clear_const_bits (value_t *val)
		    { val->least_const_type_directly_used_in &= sub_least_const_type; }

		sub_least_const_type &= analyze_least_const_type_directly_used_in(stmt->v.if_cond.consequent);
		sub_least_const_type &= analyze_least_const_type_directly_used_in(stmt->v.if_cond.alternative);
		sub_least_const_type &= analyze_least_const_type_directly_used_in(stmt->v.if_cond.exit);

		for_each_value_in_rhs(stmt->v.if_cond.condition, &clear_const_bits);

		least_const_type &= sub_least_const_type;

		break;
	    }

	    case STMT_WHILE_LOOP :
	    {
		unsigned int sub_least_const_type = CONST_MAX;

		void clear_const_bits (value_t *val)
		    { val->least_const_type_directly_used_in &= sub_least_const_type; }

		sub_least_const_type &= analyze_least_const_type_directly_used_in(stmt->v.while_loop.entry);
		sub_least_const_type &= analyze_least_const_type_directly_used_in(stmt->v.while_loop.body);

		for_each_value_in_rhs(stmt->v.while_loop.invariant, &clear_const_bits);

		least_const_type &= sub_least_const_type;

		break;
	    }

	    default :
		assert(0);
	}

	stmt = stmt->next;
    }

    return least_const_type;
}

#undef LEAST_CONST_TYPE

static int
analyze_least_const_type_multiply_used_in (statement_t *stmt, int in_loop, value_set_t *multiply_assigned_set, int *changed)
{
    int update_const_mask;
    value_set_t *update_const_set = multiply_assigned_set;
    void update_const (value_t *val)
	{
	    if (value_set_contains(update_const_set, val)
		&& ((val->least_const_type_multiply_used_in & update_const_mask)
		    != val->least_const_type_multiply_used_in))
	    {
		val->least_const_type_multiply_used_in &= update_const_mask;
		*changed = 1;
	    }
	}

    int least_const = CONST_MAX;

    while (stmt != 0)
    {
	switch (stmt->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_PHI_ASSIGN :
		update_const_mask = stmt->v.assign.lhs->least_const_type_multiply_used_in;
		for_each_value_in_rhs(stmt->v.assign.rhs2, &update_const);
	    case STMT_ASSIGN :
		update_const_mask = stmt->v.assign.lhs->least_const_type_multiply_used_in;
		for_each_value_in_rhs(stmt->v.assign.rhs, &update_const);
		if (in_loop)
		    value_set_add(multiply_assigned_set, stmt->v.assign.lhs);
		least_const &= stmt->v.assign.lhs->least_const_type_multiply_used_in;
		break;

	    case STMT_IF_COND :
	    {
		int sub_least_const;

		sub_least_const = analyze_least_const_type_multiply_used_in(stmt->v.if_cond.consequent,
									    in_loop, multiply_assigned_set, changed);
		sub_least_const &= analyze_least_const_type_multiply_used_in(stmt->v.if_cond.alternative,
									     in_loop, multiply_assigned_set, changed);
		sub_least_const &= analyze_least_const_type_multiply_used_in(stmt->v.if_cond.exit,
									     in_loop, multiply_assigned_set, changed);

		update_const_mask = sub_least_const;
		for_each_value_in_rhs(stmt->v.if_cond.condition, &update_const);

		least_const &= sub_least_const;

		break;
	    }

	    case STMT_WHILE_LOOP :
	    {
		int sub_least_const;
		value_set_t *copy;

		sub_least_const = analyze_least_const_type_multiply_used_in(stmt->v.while_loop.entry,
									    in_loop, multiply_assigned_set, changed);

		copy = value_set_copy(multiply_assigned_set);
		assert(copy != 0);

		/* we have to process the body twice because
		 * multiply_assigned_to information flows from the body to the
		 * entry as well as from the entry to the body (we could just
		 * as well have processed the entry first, then the body, and
		 * then the entry again) */
		sub_least_const &= analyze_least_const_type_multiply_used_in(stmt->v.while_loop.body,
									     1, copy, changed);
		sub_least_const &= analyze_least_const_type_multiply_used_in(stmt->v.while_loop.entry,
									     1, copy, changed);
		sub_least_const &= analyze_least_const_type_multiply_used_in(stmt->v.while_loop.body,
									     1, copy, changed);

		update_const_set = copy;
		update_const_mask = sub_least_const;
		for_each_value_in_rhs(stmt->v.while_loop.invariant, &update_const);
		update_const_set = multiply_assigned_set;

		free_value_set(copy);

		least_const &= sub_least_const;

		break;
	    }

	    default:
		assert(0);
	}

	stmt = stmt->next;
    }

    return least_const;
}

static void
analyze_constants (void)
{
    void init_const_type (value_t *value)
	{ value->const_type = CONST_MAX; }

    void init_least_const_types (value_t *value)
	{
	    value->least_const_type_multiply_used_in = value->const_type;
	    value->least_const_type_directly_used_in = value->const_type;
	}

    int changed;

    for_each_value_in_statements(first_stmt, &init_const_type);

    do
    {
	changed = 0;
	analyze_stmts_constants(first_stmt, &changed, CONST_MAX);
    } while (changed);

    for_each_value_in_statements(first_stmt, &init_least_const_types);

    do
    {
	value_set_t *set = new_value_set();

	changed = 0;
	analyze_least_const_type_multiply_used_in(first_stmt, 0, set, &changed);

	free_value_set(set);
    } while (changed);

    analyze_least_const_type_directly_used_in(first_stmt);
}

/*** make_color optimization ***/

static int
is_color_def (statement_t *stmt, int op, value_t **value)
{
    if (stmt->type == STMT_ASSIGN
	&& stmt->v.assign.rhs->type == RHS_OP
	&& op_index(stmt->v.assign.rhs->v.op.op) == op
	&& stmt->v.assign.rhs->v.op.args[0].type == PRIMARY_VALUE)
    {
	*value = stmt->v.assign.rhs->v.op.args[0].v.value;
	return 1;
    }
    return 0;
}

static void
optimize_make_color (statement_t *stmt)
{
    while (stmt != 0)
    {
	switch (stmt->type)
	{
	    case STMT_NIL :
		stmt = stmt->next;
		break;

	    case STMT_ASSIGN :
		if (stmt->v.assign.rhs->type == RHS_OP
		    && op_index(stmt->v.assign.rhs->v.op.op) == OP_MAKE_COLOR)
		{
		    static int ops[] = { OP_RED, OP_GREEN, OP_BLUE, OP_ALPHA };

		    value_t *vals[4];
		    int i;

		    for (i = 0; i < 4; ++i)
			if (stmt->v.assign.rhs->v.op.args[i].type != PRIMARY_VALUE
			    || !is_color_def(stmt->v.assign.rhs->v.op.args[i].v.value->def, ops[i], &vals[i]))
			    break;

		    if (i == 4 && vals[0] == vals[1] && vals[0] == vals[2] && vals[0] == vals[3]) /* successful? */
		    {
			for (i = 0; i < 4; ++i)
			    remove_use(stmt->v.assign.rhs->v.op.args[i].v.value, stmt);

			stmt->v.assign.rhs = make_value_rhs(vals[0]);
			add_use(vals[0], stmt);
		    }
		}
		stmt = stmt->next;
		break;

	    case STMT_PHI_ASSIGN :
		stmt = stmt->next;
		break;

	    case STMT_IF_COND :
		optimize_make_color(stmt->v.if_cond.consequent);
		optimize_make_color(stmt->v.if_cond.alternative);
		stmt = stmt->next;
		break;

	    case STMT_WHILE_LOOP :
		optimize_make_color(stmt->v.while_loop.body);
		stmt = stmt->next;
		break;

	    default :
		assert(0);
	}
    }
}

/*** constant propagation ***/

/*** copy propagation ***/

static void
copy_propagation (void)
{
    int changed;

    void propagate_copy (statement_t *stmt)
	{
	    if (stmt->type == STMT_ASSIGN
		&& stmt->v.assign.lhs->uses != 0
		&& stmt->v.assign.rhs->type == RHS_PRIMARY
		&& (stmt->v.assign.rhs->v.primary.type != PRIMARY_VALUE
		    || stmt->v.assign.rhs->v.primary.v.value != stmt->v.assign.lhs))
	    {
		rewrite_uses(stmt->v.assign.lhs, stmt->v.assign.rhs->v.primary, 0);
		changed = 1;
	    }
	}

    do
    {
	changed = 0;
	for_each_assign_statement(first_stmt, &propagate_copy);
    } while (changed);
}

/*** dead code removal ***/

static value_list_t*
add_value_if_new (value_list_t *list, value_t *value)
{
    value_list_t *l;

    for (l = list; l != 0; l = l->next)
	if (l->value == value)
	    return list;

    l = (value_list_t*)pool_alloc(sizeof(value_list_t));
    l->value = value;
    l->next = list;

    return l;
}

static void
remove_values_from_rhs (statement_t *stmt, rhs_t *rhs, value_list_t **worklist)
{
    switch (rhs->type)
    {
	case RHS_PRIMARY :
	    if (rhs->v.primary.type == PRIMARY_VALUE)
	    {
		assert(rhs->v.primary.v.value->index < 0
		       || rhs->v.primary.v.value->def->type != STMT_NIL);
		remove_use(rhs->v.primary.v.value, stmt);
		if (rhs->v.primary.v.value->uses == 0)
		    *worklist = add_value_if_new(*worklist, rhs->v.primary.v.value);
	    }
	    break;

	case RHS_OP :
	    {
		int i;

		for (i = 0; i < rhs->v.op.op->num_args; ++i)
		    if (rhs->v.op.args[i].type == PRIMARY_VALUE)
		    {
			assert(rhs->v.op.args[i].v.value->index < 0
			       || rhs->v.op.args[i].v.value->def->type != STMT_NIL);
			remove_use(rhs->v.op.args[i].v.value, stmt);
			if (rhs->v.op.args[i].v.value->uses == 0)
			    *worklist = add_value_if_new(*worklist, rhs->v.op.args[i].v.value);
		    }
	    }
	    break;
    }
}

static void
remove_assign_stmt_if_pure (statement_t *stmt, value_list_t **worklist)
{
    assert(stmt->v.assign.lhs->uses == 0);

    if ((stmt->v.assign.rhs->type == RHS_OP
	 && !stmt->v.assign.rhs->v.op.op->is_pure)
	|| (stmt->type == STMT_PHI_ASSIGN
	    && stmt->v.assign.rhs2->type == RHS_OP
	    && !stmt->v.assign.rhs2->v.op.op->is_pure))
	return;

    remove_values_from_rhs(stmt, stmt->v.assign.rhs, worklist);
    if (stmt->type == STMT_PHI_ASSIGN)
	remove_values_from_rhs(stmt, stmt->v.assign.rhs2, worklist);

    stmt->type = STMT_NIL;
}

static void
remove_dead_code_initially (statement_t *stmt, value_list_t **worklist)
{
    while (stmt != 0)
    {
	switch (stmt->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_ASSIGN :
	    case STMT_PHI_ASSIGN :
		if (stmt->v.assign.lhs->uses == 0)
		    *worklist = add_value_if_new(*worklist, stmt->v.assign.lhs);
		break;

	    case STMT_IF_COND :
		remove_dead_code_initially(stmt->v.if_cond.consequent, worklist);
		remove_dead_code_initially(stmt->v.if_cond.alternative, worklist);
		remove_dead_code_initially(stmt->v.if_cond.exit, worklist);
		break;

	    case STMT_WHILE_LOOP :
		remove_dead_code_initially(stmt->v.while_loop.entry, worklist);
		remove_dead_code_initially(stmt->v.while_loop.body, worklist);
		break;

	    default :
		assert(0);
	}

	stmt = stmt->next;
    }
}

static void
remove_dead_code_from_worklist (value_list_t *worklist, value_list_t **new_worklist)
{
    while (worklist != 0)
    {
	assert(worklist->value->uses == 0);

	if (worklist->value->def->type == STMT_NIL)
	    assert(worklist->value->def == &dummy_stmt);
	else
	{
	    assert(worklist->value->def->type == STMT_ASSIGN
		   || worklist->value->def->type == STMT_PHI_ASSIGN);

	    remove_assign_stmt_if_pure(worklist->value->def, new_worklist);
	}

	worklist = worklist->next;
    }
}

static void
remove_dead_code (void)
{
    value_list_t *worklist = 0;

    remove_dead_code_initially(first_stmt, &worklist);
    do
    {
	value_list_t *new_worklist = 0;

	remove_dead_code_from_worklist(worklist, &new_worklist);
	worklist = new_worklist;
    } while (worklist != 0);
}

/*** ssa well-formedness check ***/

static void
check_rhs_defined (rhs_t *rhs, value_set_t *defined_set)
{
    void check_value (value_t *value)
	{ assert(value_set_contains(defined_set, value)); }

    for_each_value_in_rhs(rhs, check_value);
}

static value_t*
last_assignment_to_compvar (statement_t *stmts, compvar_t *compvar)
{
    value_t *last = 0;

    while (stmts != 0)
    {
	switch (stmts->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_ASSIGN :
	    case STMT_PHI_ASSIGN :
		if (stmts->v.assign.lhs->compvar == compvar)
		    last = stmts->v.assign.lhs;
		break;

	    case STMT_IF_COND :
	    {
		value_t *new_last = last_assignment_to_compvar(stmts->v.if_cond.exit, compvar);

		if (new_last != 0)
		    last = new_last;

		break;
	    }

	    case STMT_WHILE_LOOP :
	    {
		value_t *new_last = last_assignment_to_compvar(stmts->v.while_loop.entry, compvar);

		if (new_last != 0)
		    last = new_last;

		break;
	    }

	    default :
		assert(0);
	}

	stmts = stmts->next;
    }

    return last;
}

static void
set_value_defined_and_current_for_checking (value_t *value, GHashTable *current_value_hash, value_set_t *defined_set)
{
    value_set_add(defined_set, value);
    g_hash_table_insert(current_value_hash, value->compvar, value);
}

static void
check_phis (statement_t *stmts, statement_t *body1, statement_t *body2,
	    GHashTable *current_value_hash, value_set_t *defined_set)
{
    while (stmts != 0)
    {
	assert(stmts->type == STMT_NIL || stmts->type == STMT_PHI_ASSIGN);

	if (stmts->type == STMT_PHI_ASSIGN)
	{
	    void check_value (value_t *value)
		{
		    if (value->index >= 0)
		    {
			value_t *value1, *value2;
			value_t *current_value = (value_t*)g_hash_table_lookup(current_value_hash, value->compvar);

			value1 = last_assignment_to_compvar(body1, value->compvar);
			value2 = last_assignment_to_compvar(body2, value->compvar);
			if (value1 == 0)
			    value1 = current_value;
			if (value2 == 0)
			    value2 = current_value;

			assert(value == value1 || value == value2);
		    }
		    else
			assert(g_hash_table_lookup(current_value_hash, value->compvar) == 0);
		}

	    for_each_value_in_rhs(stmts->v.assign.rhs, check_value);
	    for_each_value_in_rhs(stmts->v.assign.rhs2, check_value);

	    set_value_defined_and_current_for_checking(stmts->v.assign.lhs, current_value_hash, defined_set);
	}

	stmts = stmts->next;
    }
}

static void check_ssa_recursively (statement_t *stmts, GHashTable *current_value_hash, value_set_t *defined_set);

static void
check_ssa_with_undo (statement_t *stmts, GHashTable *current_value_hash, value_set_t *defined_set)
{
    GHashTable *current_value_hash_copy;
    value_set_t *defined_set_copy;

    current_value_hash_copy = direct_hash_table_copy(current_value_hash);
    defined_set_copy = value_set_copy(defined_set);

    check_ssa_recursively(stmts, current_value_hash_copy, defined_set_copy);

    free_value_set(defined_set_copy);
    g_hash_table_destroy(current_value_hash_copy);
}

static void
check_ssa_recursively (statement_t *stmts, GHashTable *current_value_hash, value_set_t *defined_set)
{
    while (stmts != 0)
    {
	switch (stmts->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_ASSIGN:
		check_rhs_defined(stmts->v.assign.rhs, defined_set);
		assert(!value_set_contains(defined_set, stmts->v.assign.lhs));
		set_value_defined_and_current_for_checking(stmts->v.assign.lhs, current_value_hash, defined_set);
		break;

	    case STMT_PHI_ASSIGN :
		assert(0);
		break;

	    case STMT_IF_COND :
		check_rhs_defined(stmts->v.if_cond.condition, defined_set);

		check_ssa_with_undo(stmts->v.if_cond.consequent, current_value_hash, defined_set);
		check_ssa_with_undo(stmts->v.if_cond.alternative, current_value_hash, defined_set);

		check_phis(stmts->v.if_cond.exit, stmts->v.if_cond.consequent, stmts->v.if_cond.alternative,
			   current_value_hash, defined_set);
		break;

	    case STMT_WHILE_LOOP :
		check_phis(stmts->v.while_loop.entry, stmts->v.while_loop.body, 0,
			   current_value_hash, defined_set);

		check_rhs_defined(stmts->v.while_loop.invariant, defined_set);

		check_ssa_with_undo(stmts->v.while_loop.body, current_value_hash, defined_set);
		break;

	    default :
		assert(0);
	}

	stmts = stmts->next;
    }
}

static void
check_ssa (statement_t *stmts)
{
    GHashTable *current_value_hash;
    value_set_t *defined_set;

    current_value_hash = g_hash_table_new(&g_direct_hash, &g_direct_equal);
    defined_set = new_value_set();

    check_ssa_recursively(stmts, current_value_hash, defined_set);

    free_value_set(defined_set);
    g_hash_table_destroy(current_value_hash);
}

/*** code slicing ***/

/* returns whether the slice is non-empty */
static int
slice_code (statement_t *stmt, unsigned int slice_flag, int (*predicate) (statement_t *stmt))
{
    int non_empty = 0;

    while (stmt != 0)
    {
	switch (stmt->type)
	{
	    case STMT_NIL :
		break;

	    case STMT_ASSIGN :
	    case STMT_PHI_ASSIGN :
		if (predicate(stmt))
		{
		    stmt->slice_flags |= slice_flag;
		    non_empty = 1;
		}
		break;

	    case STMT_IF_COND :
	    {
		int result;

		result = slice_code(stmt->v.if_cond.consequent, slice_flag, predicate);
		result = slice_code(stmt->v.if_cond.alternative, slice_flag, predicate) || result;
		result = slice_code(stmt->v.if_cond.exit, slice_flag, predicate) || result;

		if (result)
		{
		    slice_code(stmt->v.if_cond.exit, slice_flag, predicate);

		    stmt->slice_flags |= slice_flag;
		    non_empty = 1;
		}
		break;
	    }

	    case STMT_WHILE_LOOP :
	    {
		if (slice_code(stmt->v.while_loop.body, slice_flag, predicate))
		{
		    slice_code(stmt->v.while_loop.entry, slice_flag, predicate);

		    stmt->slice_flags |= slice_flag;
		    non_empty = 1;
		}
		else
		    assert(!slice_code(stmt->v.while_loop.entry, slice_flag, predicate));
		break;
	    }

	    default:
		assert(0);
	}

	stmt = stmt->next;
    }

    return non_empty;
}

/*** c code output ***/

/* permanent const values must be calculated once and then available for the
 * calculation of all less const values */
static int
is_permanent_const_value (value_t *value)
{
    return value->least_const_type_multiply_used_in == value->const_type
	&& value->least_const_type_directly_used_in != value->const_type;
}

/* temporary const values must be defined for the calculation of all const
 * types up to the least const type they are used in */
static int
is_temporary_const_value (value_t *value)
{
    return !is_permanent_const_value(value);
}

/* returns whether const_type is at least as const as lower_bound but not more
 * const than upper_bound */
static int
is_const_type_within (int const_type, int lower_bound, int upper_bound)
{
    assert((lower_bound & upper_bound) == lower_bound);

    return (const_type & lower_bound) == lower_bound
	&& (const_type & upper_bound) == const_type;
}

void
output_value_name (FILE *out, value_t *value, int for_decl)
{
    if (value->index < 0)
	fputs("0 /* uninitialized */ ", out);
    else
    {
#ifndef NO_CONSTANTS_ANALYSIS
	if (!for_decl && is_permanent_const_value(value))
	{
	    if (value->const_type == (CONST_ROW | CONST_COL))
		fprintf(out, "xy_vars->");
	    else if (value->const_type == CONST_COL)
		fprintf(out, "y_vars->");
	}
#endif

	if (value->compvar->var != 0)
	    fprintf(out, "var_%s_%d_%d", value->compvar->var->name, value->compvar->n, value->index);
	else
	    fprintf(out, "tmp_%d_%d", value->compvar->temp->number, value->index);
    }
}

void
output_value_decl (FILE *out, value_t *value)
{
    if (!value->have_defined && value->index >= 0)
    {
	switch (value->compvar->type)
	{
	    case TYPE_INT :
		fputs("int ", out);
		break;

	    case TYPE_FLOAT :
		fputs("float ", out);
		break;

	    case TYPE_COLOR :
		fputs("color_t ", out);
		break;

	    case TYPE_COMPLEX :
		fputs("complex float ", out);
		break;

	    case TYPE_MATRIX :
		fputs("gsl_matrix *", out);
		break;

	    case TYPE_VECTOR :
		fputs("gsl_vector *", out);
		break;
	}

	output_value_name(out, value, 1);
	fputs(";\n", out);
	value->have_defined = 1;
    }
}

static void
reset_have_defined (statement_t *stmt)
{
    void reset_value_have_defined (value_t *value)
	{ value->have_defined = 0; }

    for_each_value_in_statements(stmt, &reset_value_have_defined);
}

void
output_primary (FILE *out, primary_t *prim)
{
    switch (prim->type)
    {
	case PRIMARY_VALUE :
	    output_value_name(out, prim->v.value, 0);
	    break;

	case PRIMARY_INT_CONST :
	    fprintf(out, "%d", prim->v.int_const);
	    break;

	case PRIMARY_FLOAT_CONST :
	    fprintf(out, "%f", prim->v.float_const);
	    break;
    }
}

void
output_rhs (FILE *out, rhs_t *rhs)
{
    switch (rhs->type)
    {
	case RHS_PRIMARY :
	    output_primary(out, &rhs->v.primary);
	    break;

	case RHS_INTERNAL :
	    fputs(rhs->v.internal->name, out);
	    /* fprintf(out, "invocation->internals[%d].data[0]", rhs->v.internal->index); */
	    break;

	case RHS_OP :
	    {
		int i;

		fprintf(out, "%s(", rhs->v.op.op->name);
		for (i = 0; i < rhs->v.op.op->num_args; ++i)
		{
		    if (i > 0)
			fputs(",", out);
		    output_primary(out, &rhs->v.op.args[i]);
		}
		fputs(")", out);
	    }
	    break;

	default :
	    assert(0);
    }
}

void
output_phis (FILE *out, statement_t *phis, int branch, unsigned int slice_flag)
{
    while (phis != 0)
    {
	rhs_t *rhs;

#ifndef NO_CONSTANTS_ANALYSIS
	if ((phis->slice_flags & slice_flag) == 0)
#else
        if (phis->type == STMT_NIL)
#endif
	{
	    phis = phis->next;
	    continue;
	}

	assert(phis->type == STMT_PHI_ASSIGN);

	rhs = ((branch == 0) ? phis->v.assign.rhs : phis->v.assign.rhs2);

	if (rhs->type != RHS_PRIMARY
	    || rhs->v.primary.type != PRIMARY_VALUE
	    || rhs->v.primary.v.value != phis->v.assign.lhs)
	{
	    output_value_name(out, phis->v.assign.lhs, 0);
	    fputs(" = ", out);
	    output_rhs(out, rhs);
	    fputs(";\n", out);
	}

	phis = phis->next;
    }
}

void
output_stmts (FILE *out, statement_t *stmt, unsigned int slice_flag)
{
    while (stmt != 0)
    {
#ifndef NO_CONSTANTS_ANALYSIS
	if (stmt->slice_flags & slice_flag)
#endif
	    switch (stmt->type)
	    {
		case STMT_NIL :
#ifndef NO_CONSTANTS_ANALYSIS
		    assert(0);
#endif
		    break;

		case STMT_ASSIGN :
		    output_value_name(out, stmt->v.assign.lhs, 0);
		    fputs(" = ", out);
		    output_rhs(out, stmt->v.assign.rhs);
		    fputs(";\n", out);
		    break;

		case STMT_PHI_ASSIGN :
		    assert(0);
		    break;

		case STMT_IF_COND :
		    fputs("if (", out);
		    output_rhs(out, stmt->v.if_cond.condition);
		    fputs(")\n{\n", out);
		    output_stmts(out, stmt->v.if_cond.consequent, slice_flag);
		    output_phis(out, stmt->v.if_cond.exit, 0, slice_flag);
		    fputs("}\nelse\n{\n", out);
		    output_stmts(out, stmt->v.if_cond.alternative, slice_flag);
		    output_phis(out, stmt->v.if_cond.exit, 1, slice_flag);
		    fputs("}\n", out);
		    break;

		case STMT_WHILE_LOOP :
		    output_phis(out, stmt->v.while_loop.entry, 0, slice_flag);
		    fputs("while (", out);
		    output_rhs(out, stmt->v.while_loop.invariant);
		    fputs(")\n{\n", out);
		    output_stmts(out, stmt->v.while_loop.body, slice_flag);
		    output_phis(out, stmt->v.while_loop.entry, 1, slice_flag);
		    fputs("}\n", out);
		    break;

		default :
		    assert(0);
	    }

	stmt = stmt->next;
    }
}

static void
output_permanent_const_declarations (FILE *out, int const_type)
{
    void output_value_if_needed (value_t *value)
	{
	    if (value->const_type == const_type
		&& is_permanent_const_value(value))
		output_value_decl(out, value);
	}

    reset_have_defined(first_stmt);

    for_each_value_in_statements(first_stmt, &output_value_if_needed);
}

static void
output_permanent_const_code (FILE *out, int const_type)
{
    int is_value_needed (value_t *value)
	{
	    return value->const_type == const_type
		|| (is_const_type_within(const_type,
					 value->least_const_type_multiply_used_in,
					 value->const_type));
	}

    void output_value_if_needed (value_t *value)
	{
	    if ((is_temporary_const_value(value) || const_type == 0)
		&& is_value_needed(value))
		output_value_decl(out, value);
	}

    int const_predicate (statement_t *stmt)
	{
	    assert(stmt->type == STMT_ASSIGN || stmt->type == STMT_PHI_ASSIGN);

	    return is_value_needed(stmt->v.assign.lhs);
	}

    unsigned int slice_flag;

    /* declarations */
    reset_have_defined(first_stmt);
    for_each_value_in_statements(first_stmt, &output_value_if_needed);

    /* code */
    if (const_type == (CONST_ROW | CONST_COL))
	slice_flag = SLICE_XY_CONST;
    else if (const_type == CONST_ROW)
	slice_flag = SLICE_X_CONST;
    else if (const_type == CONST_COL)
	slice_flag = SLICE_Y_CONST;
    else
	slice_flag = SLICE_NO_CONST;

    slice_code(first_stmt, slice_flag, &const_predicate);

    output_stmts(out, first_stmt, slice_flag);
}

/*** compiling and loading ***/

#ifdef OPENSTEP
#ifndef MAX
#define	MAX(a,b)	(((a)<(b))?(b):(a))
#endif
#define	CGEN_CC		"cc -c -fPIC -faltivec -o"
#define	CGEN_LD		"cc -bundle -flat_namespace -undefined suppress -o"
#endif

#ifdef OPENSTEP
#include <sys/param.h>
#include <sys/sysctl.h>

int
has_altivec (void)
{
    int mib[2], gHasAltivec;
    size_t len;

    mib[0] = CTL_HW;
    mib[1] = HW_VECTORUNIT;
    len = sizeof(gHasAltivec);
    sysctl(mib, 2, &gHasAltivec, &len, NULL, 0);

    if (gHasAltivec)
	printf("has altivec\n");
    else
	printf("no altivec\n");

    return (gHasAltivec != 0);
}
#endif

#define MAX_TEMPLATE_VAR_LENGTH       64
#define is_word_character(c)          (isalnum((c)) || (c) == '_')

initfunc_t
gen_and_load_c_code (mathmap_t *mathmap, void **module_info, FILE *template)
{
    static int last_mathfunc = 0;

    FILE *out;
    int numtmpvars = 0, i;
    variable_t *var;
    char *buf;
    int pid = getpid();
    int c;
    initfunc_t initfunc;
    compvar_t *result[MAX_TUPLE_LENGTH];
#ifndef OPENSTEP
    GModule *module = 0;
#endif

    first_stmt = 0;
    emit_loc = &first_stmt;
    next_temp_number = 1;
    next_value_global_index = 0;

    assert(template != 0);

    gen_code(mathmap->exprtree, result, 0);
    {
	compvar_t *color_tmp = make_temporary(), *dummy = make_temporary();

	emit_assign(make_lhs(color_tmp), make_op_rhs(OP_MAKE_COLOR,
						     make_compvar_primary(result[0]), make_compvar_primary(result[1]),
						     make_compvar_primary(result[2]), make_compvar_primary(result[3])));
	emit_assign(make_lhs(dummy), make_op_rhs(OP_OUTPUT_COLOR, make_compvar_primary(color_tmp)));
    }

    propagate_types();

    dump_code(first_stmt, 0);
    check_ssa(first_stmt);

    optimize_make_color(first_stmt);
    copy_propagation();
    remove_dead_code();

#ifndef NO_CONSTANTS_ANALYSIS
    analyze_constants();
#endif
    dump_code(first_stmt, 0);
    check_ssa(first_stmt);

    buf = (char*)alloca(MAX(strlen(CGEN_CC), strlen(CGEN_LD)) + 512);
    assert(buf != 0);

    sprintf(buf, "/tmp/mathfunc%d_%d.c", pid, ++last_mathfunc);
    out = fopen(buf, "w");
    if (out == 0)
    {
	fprintf(stderr, "cannot write temporary file %s\n", buf);
	return 0;
    }

    while ((c = fgetc(template)) != EOF)
    {
	if (c == '$')
	{
	    c = fgetc(template);
	    assert(c != EOF);

	    if (!is_word_character(c))
		putc(c, out);
	    else
	    {
		char name[MAX_TEMPLATE_VAR_LENGTH + 1];
		int length = 1;

		name[0] = c;

		do
		{
		    c = fgetc(template);

		    if (is_word_character(c))
		    {
			assert(length < MAX_TEMPLATE_VAR_LENGTH);
			name[length++] = c;
		    }
		    else
			if (c != EOF)
			    ungetc(c, template);
		} while (is_word_character(c));

		assert(length > 0 && length <= MAX_TEMPLATE_VAR_LENGTH);

		name[length] = '\0';

		if (strcmp(name, "l") == 0)
		    fprintf(out, "%d", MAX_TUPLE_LENGTH);
		else if (strcmp(name, "g") == 0)
		{
#ifdef GIMP
		    putc('1', out);
#else
		    putc('0', out);
#endif
		}
		else if (strcmp(name, "2") == 0)
		{
#ifdef GIMP2
		    putc('1', out);
#else
		    putc('0', out);
#endif
		}
		else if (strcmp(name, "m") == 0)
		    output_permanent_const_code(out, 0);
		else if (strcmp(name, "p") == 0)
		    fprintf(out, "%d", USER_CURVE_POINTS);
		else if (strcmp(name, "q") == 0)
		    fprintf(out, "%d", USER_GRADIENT_POINTS);
		else if (strcmp(name, "o") == 0)
		{
#ifdef OPENSTEP
		    putc('1', out);
#else
		    putc('0', out);
#endif
		}
		else if (strcmp(name, "a") == 0)
		{
#ifdef OPENSTEP
		    putc(has_altivec() ? '1' : '0', out);
#else
		    putc('0', out);
#endif
		}
		else if (strcmp(name, "xy_decls") == 0)
		{
#ifndef NO_CONSTANTS_ANALYSIS
		    output_permanent_const_declarations(out, CONST_ROW | CONST_COL);
#endif
		}
		else if (strcmp(name, "x_decls") == 0)
		{
#ifndef NO_CONSTANTS_ANALYSIS
		    output_permanent_const_declarations(out, CONST_ROW);
#endif
		}
		else if (strcmp(name, "y_decls") == 0)
		{
#ifndef NO_CONSTANTS_ANALYSIS
		    output_permanent_const_declarations(out, CONST_COL);
#endif
		}
		else if (strcmp(name, "xy_code") == 0)
		{
#ifndef NO_CONSTANTS_ANALYSIS
		    output_permanent_const_code(out, CONST_ROW | CONST_COL);
#endif
		}
		else if (strcmp(name, "x_code") == 0)
		{
#ifndef NO_CONSTANTS_ANALYSIS
		    output_permanent_const_code(out, CONST_ROW);
#endif
		}
		else if (strcmp(name, "y_code") == 0)
		{
#ifndef NO_CONSTANTS_ANALYSIS
		    output_permanent_const_code(out, CONST_COL);
#endif
		}
		else
		    assert(0);
	    }
	}
	else
	    putc(c, out);
    }

    fclose(out);

    sprintf(buf, "%s /tmp/mathfunc%d_%d.o /tmp/mathfunc%d_%d.c", CGEN_CC, pid, last_mathfunc, pid, last_mathfunc);
    if (system(buf) != 0)
    {
	fprintf(stderr, "compiler failed\n");
	return 0;
    }

    sprintf(buf, "%s /tmp/mathfunc%d_%d.so /tmp/mathfunc%d_%d.o", CGEN_LD, pid, last_mathfunc, pid, last_mathfunc);
    if (system(buf) != 0)
    {
	fprintf(stderr, "linker failed\n");
	return 0;
    }

    sprintf(buf, "/tmp/mathfunc%d_%d.so", pid, last_mathfunc);

#ifndef OPENSTEP
    module = g_module_open(buf, 0);
    if (module == 0)
    {
	fprintf(stderr, "could not load module: %s\n", g_module_error());
	return 0;
    }

    printf("loaded %p\n", module);

    assert(g_module_symbol(module, "mathmapinit", (void**)&initfunc));

    /* unlink(buf); */

    sprintf(buf, "/tmp/mathfunc%d_%d.o", pid, last_mathfunc);
    unlink(buf);

    sprintf(buf, "/tmp/mathfunc%d_%d.c", pid, last_mathfunc);
    /* unlink(buf); */

    *module_info = module;
#else
    {
        NSObjectFileImage objectFileImage;
        NSModule module;
        const char *moduleName = "Johnny";
        NSSymbol symbol;
        
        NSCreateObjectFileImageFromFile(buf, &objectFileImage);
	if (objectFileImage == 0)
	{
	    fprintf(stderr, "NSCreateObjectFileImageFromFile() failed\n");
	    return 0;
	}

        module = NSLinkModule(objectFileImage, moduleName,
			      NSLINKMODULE_OPTION_PRIVATE | NSLINKMODULE_OPTION_BINDNOW);
	if (module == 0)
	{
	    fprintf(stderr, "NSLinkModule() failed\n");
	    return 0;
	}
        NSDestroyObjectFileImage(objectFileImage);

	symbol = NSLookupSymbolInModule(module, "__init");
	if (symbol != 0)
	{
	    void (*init) (void) = NSAddressOfSymbol(symbol);
	    init();
	}

        symbol = NSLookupSymbolInModule(module, "_mathmapinit");
	assert(symbol != 0);
        initfunc = NSAddressOfSymbol(symbol);

	*module_info = module;
    }
#endif

    free_pools();

    return initfunc;
}

void
unload_c_code (void *module_info)
{
#ifndef OPENSTEP
    GModule *module = module_info;

    printf("unloading %p\n", module);
    assert(g_module_close(module));
#else
    /* FIXME */
#endif
}

/*** inits ***/

static void
init_op (int index, char *name, int num_args, type_prop_t type_prop, type_t const_type, int is_pure)
{
    assert(num_args <= MAX_OP_ARGS);

    ops[index].name = name;
    ops[index].num_args = num_args;
    ops[index].type_prop = type_prop;
    ops[index].const_type = const_type;
    ops[index].is_pure = is_pure;
}

#define PURE          1
#define NONPURE       0

void
init_compiler (void)
{
    init_op(OP_NOP, "NOP", 0, TYPE_PROP_CONST, TYPE_INT, PURE);
    init_op(OP_ADD, "ADD", 2, TYPE_PROP_MAX, 0, PURE);
    init_op(OP_SUB, "SUB", 2, TYPE_PROP_MAX, 0, PURE);
    init_op(OP_NEG, "NEG", 1, TYPE_PROP_MAX, 0, PURE);
    init_op(OP_MUL, "MUL", 2, TYPE_PROP_MAX, 0, PURE);
    init_op(OP_DIV, "DIV", 2, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_MOD, "MOD", 2, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ABS, "fabs", 1, TYPE_PROP_MAX, 0, PURE);
    init_op(OP_MIN, "MIN", 2, TYPE_PROP_MAX, 0, PURE);
    init_op(OP_MAX, "MAX", 2, TYPE_PROP_MAX, 0, PURE);

    init_op(OP_SQRT, "sqrt", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_HYPOT, "hypot", 2, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_SIN, "sin", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_COS, "cos", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_TAN, "tan", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ASIN, "asin", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ACOS, "acos", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ATAN, "atan", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ATAN2, "atan2", 2, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_POW, "pow", 2, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_EXP, "exp", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_LOG, "log", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_SINH, "sinh", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_COSH, "cosh", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_TANH, "tanh", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ASINH, "asinh", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ACOSH, "acosh", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ATANH, "atanh", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_GAMMA, "GAMMA", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);

    init_op(OP_FLOOR, "floor", 1, TYPE_PROP_CONST, TYPE_INT, PURE);
    init_op(OP_EQ, "EQ", 2, TYPE_PROP_CONST, TYPE_INT, PURE);
    init_op(OP_LESS, "LESS", 2, TYPE_PROP_CONST, TYPE_INT, PURE);
    init_op(OP_LEQ, "LEQ", 2, TYPE_PROP_CONST, TYPE_INT, PURE);
    init_op(OP_NOT, "NOT", 1, TYPE_PROP_CONST, TYPE_INT, PURE);

    init_op(OP_PRINT, "PRINT", 1, TYPE_PROP_CONST, TYPE_INT, NONPURE);
    init_op(OP_NEWLINE, "NEWLINE", 0, TYPE_PROP_CONST, TYPE_INT, NONPURE);

    init_op(OP_ORIG_VAL, "ORIG_VAL", 4, TYPE_PROP_CONST, TYPE_COLOR, PURE);
    init_op(OP_RED, "RED_FLOAT", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_GREEN, "GREEN_FLOAT", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_BLUE, "BLUE_FLOAT", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_ALPHA, "ALPHA_FLOAT", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);

    init_op(OP_COMPLEX, "COMPLEX", 2, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_REAL, "crealf", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_C_IMAG, "cimagf", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_C_SQRT, "csqrtf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_SIN, "csinf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_COS, "ccosf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_TAN, "ctanf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_ASIN, "casinf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_ACOS, "cacosf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_ATAN, "catanf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_POW, "cpowf", 2, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_EXP, "cexpf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_LOG, "clogf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_ARG, "cargf", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_C_SINH, "csinhf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_COSH, "ccoshf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_TANH, "ctanhf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_ASINH, "casinhf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_ACOSH, "cacoshf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_ATANH, "catanhf", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);
    init_op(OP_C_GAMMA, "cgamma", 1, TYPE_PROP_CONST, TYPE_COMPLEX, PURE);

    init_op(OP_MAKE_M2X2, "MAKE_M2X2", 4, TYPE_PROP_CONST, TYPE_MATRIX, NONPURE);
    init_op(OP_MAKE_M3X3, "MAKE_M3X3", 9, TYPE_PROP_CONST, TYPE_MATRIX, NONPURE);
    init_op(OP_FREE_MATRIX, "FREE_MATRIX", 1, TYPE_PROP_CONST, TYPE_INT, NONPURE);
    init_op(OP_MAKE_V2, "MAKE_V2", 2, TYPE_PROP_CONST, TYPE_VECTOR, NONPURE);
    init_op(OP_MAKE_V3, "MAKE_V3", 3, TYPE_PROP_CONST, TYPE_VECTOR, NONPURE);
    init_op(OP_FREE_VECTOR, "FREE_VECTOR", 1, TYPE_PROP_CONST, TYPE_INT, NONPURE);
    init_op(OP_VECTOR_NTH, "VECTOR_NTH", 2, TYPE_PROP_CONST, TYPE_FLOAT, NONPURE);
    init_op(OP_SOLVE_LINEAR_2, "SOLVE_LINEAR_2", 2, TYPE_PROP_CONST, TYPE_VECTOR, NONPURE);
    init_op(OP_SOLVE_LINEAR_3, "SOLVE_LINEAR_3", 2, TYPE_PROP_CONST, TYPE_VECTOR, NONPURE);
    init_op(OP_NOISE, "noise", 3, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_RAND, "RAND", 2, TYPE_PROP_CONST, TYPE_FLOAT, NONPURE);
    init_op(OP_USERVAL_INT, "USERVAL_INT", 1, TYPE_PROP_CONST, TYPE_INT, PURE);
    init_op(OP_USERVAL_FLOAT, "USERVAL_FLOAT", 1, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_USERVAL_BOOL, "USERVAL_BOOL", 1, TYPE_PROP_CONST, TYPE_INT, PURE);
    init_op(OP_USERVAL_CURVE, "USERVAL_CURVE", 2, TYPE_PROP_CONST, TYPE_FLOAT, PURE);
    init_op(OP_USERVAL_COLOR, "USERVAL_COLOR", 1, TYPE_PROP_CONST, TYPE_COLOR, PURE);
    init_op(OP_USERVAL_GRADIENT, "USERVAL_GRADIENT", 2, TYPE_PROP_CONST, TYPE_COLOR, PURE);
    init_op(OP_MAKE_COLOR, "MAKE_COLOR", 4, TYPE_PROP_CONST, TYPE_COLOR, PURE);
    init_op(OP_OUTPUT_COLOR, "OUTPUT_COLOR", 1, TYPE_PROP_CONST, TYPE_INT, NONPURE);
}