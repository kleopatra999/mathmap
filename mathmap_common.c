/* -*- c -*- */

/*
 * mathmap_common.c
 *
 * MathMap
 *
 * Copyright (C) 1997-2007 Mark Probst
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

#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "internals.h"
#include "tags.h"
#include "jump.h"
#include "scanner.h"
#include "compiler.h"
#include "mathmap.h"

int cmd_line_mode = 0; 

mathmap_t *the_mathmap = 0;
int scanner_line_num;

/* from parser.y */
int yyparse (void);

static unsigned int
image_flags_from_options (option_t *options)
{
    unsigned int flags = 0;
    option_t *unit_option = find_option_with_name(options, "unit");

    if (unit_option != 0)
    {
	flags |= IMAGE_FLAG_UNIT;
	if (find_option_with_name(unit_option->suboptions, "stretched") == 0)
	    flags |= IMAGE_FLAG_SQUARE;
    }

    return flags;
}

void
register_args_as_uservals (mathmap_t *mathmap, arg_decl_t *arg_decls)
{
    while (arg_decls != 0)
    {
	userval_info_t *result = 0;

	//printf("registering %s  type %d\n", arg_decls->name, arg_decls->type);

	switch (arg_decls->type)
	{
	    case ARG_TYPE_INT :
		if (arg_decls->v.integer.have_limits)
		    result = register_int_const(&mathmap->userval_infos, arg_decls->name,
						arg_decls->v.integer.min, arg_decls->v.integer.max,
						arg_decls->v.integer.default_value);
		else
		    result = register_int_const(&mathmap->userval_infos, arg_decls->name, -100000, 100000, 0);
		break;

	    case ARG_TYPE_FLOAT :
		if (arg_decls->v.floating.have_limits)
		    result = register_float_const(&mathmap->userval_infos, arg_decls->name,
						  arg_decls->v.floating.min, arg_decls->v.floating.max,
						  arg_decls->v.floating.default_value);
		else
		    result = register_float_const(&mathmap->userval_infos, arg_decls->name, -1.0, 1.0, 0.0);
		break;

	    case ARG_TYPE_BOOL :
		result = register_bool(&mathmap->userval_infos, arg_decls->name, arg_decls->v.boolean.default_value);
		break;

	    case ARG_TYPE_COLOR :
		result = register_color(&mathmap->userval_infos, arg_decls->name);
		break;

	    case ARG_TYPE_GRADIENT :
		result = register_gradient(&mathmap->userval_infos, arg_decls->name);
		break;

	    case ARG_TYPE_CURVE :
		result = register_curve(&mathmap->userval_infos, arg_decls->name);
		break;

	    case ARG_TYPE_FILTER :
		assert(0);

	    case ARG_TYPE_IMAGE :
		result = register_image(&mathmap->userval_infos, arg_decls->name,
					image_flags_from_options(arg_decls->options));
		break;

	    default :
		assert(0);
	}

	if (result == 0)
	{
	    sprintf(error_string, "Conflict for argument %s.", arg_decls->name);
	    JUMP(1);
	}

	arg_decls = arg_decls->next;
    }
}

void
unload_mathmap (mathmap_t *mathmap)
{
    if ((mathmap->flags & MATHMAP_FLAG_NATIVE) && mathmap->module_info != 0)
    {
	unload_c_code(mathmap->module_info);
	mathmap->module_info = 0;
    }
}

void
free_mathmap (mathmap_t *mathmap)
{
    if (mathmap->variables != 0)
	free_variables(mathmap->variables);
    if (mathmap->top_level_decls != 0)
	free_top_level_decls(mathmap->top_level_decls);
    if (mathmap->userval_infos != 0)
	free_userval_infos(mathmap->userval_infos);
    if (mathmap->interpreter_insns != 0)
	free(mathmap->interpreter_insns);
    if (mathmap->interpreter_values != 0)
	g_array_free(mathmap->interpreter_values, 1);
    unload_mathmap(mathmap);

    free(mathmap);
}

void
free_invocation (mathmap_invocation_t *invocation)
{
    userval_info_t *info;

    /*
    if (invocation->internals != 0)
	free(invocation->internals);
    */
    if (invocation->variables != 0)
	free(invocation->variables);
    if (invocation->uservals != 0)
    {
	for (info = invocation->mathmap->userval_infos; info != 0; info = info->next)
	{
	    switch (info->type)
	    {
		case USERVAL_CURVE :
		    free(invocation->uservals[info->index].v.curve.values);
		    break;

		case USERVAL_GRADIENT :
		    free(invocation->uservals[info->index].v.gradient.values);
		    break;

		case USERVAL_IMAGE :
		    if (!cmd_line_mode)
			if (invocation->uservals[info->index].v.image.index > 0)
			    free_input_drawable(invocation->uservals[info->index].v.image.index);
		    break;
	    }
	}
	free(invocation->uservals);
    }
    if (invocation->xy_vars != 0)
	free(invocation->xy_vars);
    if (invocation->y_vars != 0)
	free(invocation->y_vars);
    /*
    if (invocation->interpreter_values != 0)
	g_array_free(invocation->interpreter_values, TRUE);
    */
    free(invocation);
}

#define X_INTERNAL_INDEX         0
#define Y_INTERNAL_INDEX         1
#define R_INTERNAL_INDEX         2
#define A_INTERNAL_INDEX         3

void
init_internals (mathmap_t *mathmap)
{
    register_internal(&mathmap->internals, "x", CONST_Y | CONST_T);
    register_internal(&mathmap->internals, "y", CONST_X | CONST_T);
    register_internal(&mathmap->internals, "r", CONST_T);
    register_internal(&mathmap->internals, "a", CONST_T);
    register_internal(&mathmap->internals, "t", CONST_X | CONST_Y);
    register_internal(&mathmap->internals, "X", CONST_X | CONST_Y | CONST_T);
    register_internal(&mathmap->internals, "Y", CONST_X | CONST_Y | CONST_T);
    register_internal(&mathmap->internals, "W", CONST_X | CONST_Y | CONST_T);
    register_internal(&mathmap->internals, "H", CONST_X | CONST_Y | CONST_T);
    register_internal(&mathmap->internals, "R", CONST_X | CONST_Y | CONST_T);
    register_internal(&mathmap->internals, "frame", CONST_X | CONST_Y);
}

int
does_mathmap_use_ra (mathmap_t *mathmap)
{
    internal_t *r_internal = lookup_internal(mathmap->internals, "r", 1);
    internal_t *a_internal = lookup_internal(mathmap->internals, "a", 1);

    assert(r_internal != 0 && a_internal != 0);

    return r_internal->is_used || a_internal->is_used;
}

int
does_mathmap_use_t (mathmap_t *mathmap)
{
    internal_t *t_internal = lookup_internal(mathmap->internals, "t", 1);

    assert(t_internal != 0);

    return t_internal->is_used;
}

mathmap_t*
parse_mathmap (char *expression)
{
    static mathmap_t *mathmap;	/* this is static to avoid problems with longjmp.  */
    userval_info_t *info;
    exprtree *expr;

    mathmap = (mathmap_t*)malloc(sizeof(mathmap_t));
    assert(mathmap != 0);

    memset(mathmap, 0, sizeof(mathmap_t));

    init_internals(mathmap);

    the_mathmap = mathmap;

    DO_JUMP_CODE {
	scanner_line_num = 0;
	scanFromString(expression);
	yyparse();
	endScanningFromString();

	mathmap->top_level_decls = the_top_level_decls;
	the_top_level_decls = 0;

	if (mathmap->top_level_decls == 0
	    || mathmap->top_level_decls->next != 0
	    || mathmap->top_level_decls->type != TOP_LEVEL_FILTER)
	{
	    free_top_level_decls(mathmap->top_level_decls);
	    mathmap->top_level_decls = 0;

	    sprintf(error_string, "Exactly one filter must be defined.");
	    JUMP(1);
	}

	expr = mathmap->top_level_decls->v.filter.body;

	if (expr->result.number != rgba_tag_number
	    || expr->result.length != 4)
	{
	    free_top_level_decls(mathmap->top_level_decls);
	    mathmap->top_level_decls = 0;

	    sprintf(error_string, "The expression must have the result type rgba:4.");
	    JUMP(1);
	}

	mathmap->flags = image_flags_from_options(mathmap->top_level_decls->v.filter.options);
    } WITH_JUMP_HANDLER {
	the_top_level_decls = 0;

	free_mathmap(mathmap);
	mathmap = 0;
    } END_JUMP_HANDLER;

    the_mathmap = 0;

    if (mathmap != 0)
    {
	mathmap->num_uservals = 0;
	for (info = mathmap->userval_infos; info != 0; info = info->next)
	    ++mathmap->num_uservals;
    }

    return mathmap;
}

int
check_mathmap (char *expression)
{
    mathmap_t *mathmap = parse_mathmap(expression);

    if (mathmap != 0)
    {
	free_mathmap(mathmap);
	return 1;
    }
    else
	return 0;
}

mathmap_t*
compile_mathmap (char *expression, FILE *template, char *opmacros_filename)
{
    static mathmap_t *mathmap;	/* this is static to avoid problems with longjmp.  */
    static int try_compiler = 1;

    DO_JUMP_CODE {
	if (try_compiler)
	{
	    mathmap = parse_mathmap(expression);

	    if (mathmap == 0)
	    {
		JUMP(1);
	    }

	    mathmap->initfunc = gen_and_load_c_code(mathmap, &mathmap->module_info, template, opmacros_filename);
	    if (mathmap->initfunc == 0 && !cmd_line_mode)
	    {
		char *message = g_strdup_printf("The MathMap compiler failed.  This is not a fatal error,\n"
						"because MathMap has a fallback interpreter which can do\n"
						"everything the compiler does, but it is much slower, so it\n"
						"is recommended that you use the compiler.\n"
						"This is the reason why the compiler failed:\n%s", error_string);

		gimp_message(message);

		g_free(message);
	    }
	}

	if (!try_compiler || mathmap->initfunc == 0)
	{
	    mathmap = parse_mathmap(expression);

	    if (mathmap == 0)
	    {
		JUMP(1);
	    }

	    generate_interpreter_code(mathmap);

	    mathmap->flags &= ~MATHMAP_FLAG_NATIVE;

	    if (try_compiler)
		fprintf(stderr, "falling back to interpreter\n");

	    try_compiler = 0;
	}
	else
	    mathmap->flags |= MATHMAP_FLAG_NATIVE;
    } WITH_JUMP_HANDLER {
	if (mathmap != 0)
	{
	    free_mathmap(mathmap);
	    mathmap = 0;
	}
    } END_JUMP_HANDLER;

    return mathmap;
}

static void
init_invocation (mathmap_invocation_t *invocation)
{
    if (invocation->mathmap->flags & MATHMAP_FLAG_NATIVE)
	invocation->mathfuncs = invocation->mathmap->initfunc(invocation);
    else
    {
	/*
	int i;

	invocation->interpreter_values = g_array_new(FALSE, TRUE, sizeof(runtime_value_t));
	g_array_set_size(invocation->interpreter_values, invocation->mathmap->interpreter_values->len);
	for (i = 0; i < invocation->mathmap->interpreter_values->len; ++i)
	    g_array_index(invocation->interpreter_values, runtime_value_t, i)
		= g_array_index(invocation->mathmap->interpreter_values, runtime_value_t, i);
	*/
    }
}

/* The scale factors computed by this function are to get from virtual
   coordinates to pixel coordinates. */
void
calc_scale_factors (unsigned int flags, int pixel_width, int pixel_height, float *scale_x, float *scale_y)
{
    float virt_width, virt_height;

    switch (flags & (IMAGE_FLAG_UNIT | IMAGE_FLAG_SQUARE))
    {
	case 0 :
	    virt_width = pixel_width;
	    virt_height = pixel_height;
	    break;

	case IMAGE_FLAG_UNIT :
	    virt_width = virt_height = 2.0;
	    break;

	case IMAGE_FLAG_UNIT | IMAGE_FLAG_SQUARE :
	    if (pixel_width > pixel_height)
	    {
		virt_width = 2.0;
		virt_height = 2.0 * pixel_height / pixel_width;
	    }
	    else
	    {
		virt_height = 2.0;
		virt_width = 2.0 * pixel_width / pixel_height;
	    }
	    break;

	default :
	    assert(0);
    }

    *scale_x = pixel_width / virt_width;
    *scale_y = pixel_height / virt_height;
}

void
calc_middle_values (int img_width, int img_height, float scale_x, float scale_y, float *middle_x, float *middle_y)
{
    *middle_x = (img_width - 1) / 2.0 * scale_x;
    *middle_y = (img_height - 1) / 2.0 * scale_y;
}

mathmap_invocation_t*
invoke_mathmap (mathmap_t *mathmap, mathmap_invocation_t *template, int img_width, int img_height)
{
    mathmap_invocation_t *invocation = (mathmap_invocation_t*)malloc(sizeof(mathmap_invocation_t));

    assert(invocation != 0);
    memset(invocation, 0, sizeof(mathmap_invocation_t));

    invocation->mathmap = mathmap;

    invocation->variables = instantiate_variables(mathmap->variables);

    invocation->antialiasing = 0;
    invocation->supersampling = 0;

    invocation->output_bpp = 4;

    invocation->origin_x = invocation->origin_y = 0;

    invocation->edge_behaviour_x = invocation->edge_behaviour_y = EDGE_BEHAVIOUR_COLOR;

    invocation->img_width = invocation->calc_img_width = img_width;
    invocation->img_height = invocation->calc_img_height = img_height;

    calc_scale_factors(mathmap->flags, img_width, img_height, &invocation->scale_x, &invocation->scale_y);
    invocation->scale_x = 1.0 / invocation->scale_x;
    invocation->scale_y = 1.0 / invocation->scale_y;

    invocation->image_W = img_width * invocation->scale_x;
    invocation->image_H = img_height * invocation->scale_y;

    calc_middle_values(img_width, img_height,
		       invocation->scale_x, invocation->scale_y,
		       &invocation->middle_x, &invocation->middle_y);

    invocation->sampling_offset_x = invocation->sampling_offset_y = 0.0;

    invocation->image_X = invocation->middle_x;
    invocation->image_Y = invocation->middle_y;
    
    invocation->image_R = hypot(invocation->image_X, invocation->image_Y);

    invocation->current_r = invocation->current_a = 0.0;

    invocation->row_stride = img_width * 4;
    invocation->num_rows_finished = 0;

    invocation->do_debug = 0;

    invocation->uservals = instantiate_uservals(mathmap->userval_infos, invocation);

    if (!cmd_line_mode)
    {
	userval_info_t *info;

	// we give the original image as the first input image
	for (info = mathmap->userval_infos; info != 0; info = info->next)
	    if (info->type == USERVAL_IMAGE)
	    {
		invocation->uservals[info->index].v.image.index = 0;
		break;
	    }
    }

    if (template != 0)
	carry_over_uservals_from_template(invocation, template);

    init_invocation(invocation);

    return invocation;
}

void
enable_debugging (mathmap_invocation_t *invocation)
{
    invocation->do_debug = 1;
}

void
disable_debugging (mathmap_invocation_t *invocation)
{
    invocation->do_debug = 0;
}

static void
set_float_internal (mathmap_invocation_t *invocation, int index, float value)
{
    g_array_index(invocation->mathmap->interpreter_values, runtime_value_t, index).float_value = value;
}

void
update_image_internals (mathmap_invocation_t *invocation)
{
    internal_t *internal;

    if (invocation->mathmap->flags & MATHMAP_FLAG_NATIVE)
	return;

    internal = lookup_internal(invocation->mathmap->internals, "t", 1);
    set_float_internal(invocation, internal->index, invocation->current_t);

    internal = lookup_internal(invocation->mathmap->internals, "X", 1);
    set_float_internal(invocation, internal->index, invocation->image_X);
    internal = lookup_internal(invocation->mathmap->internals, "Y", 1);
    set_float_internal(invocation, internal->index, invocation->image_Y);
    
    internal = lookup_internal(invocation->mathmap->internals, "W", 1);
    set_float_internal(invocation, internal->index, invocation->image_W);
    internal = lookup_internal(invocation->mathmap->internals, "H", 1);
    set_float_internal(invocation, internal->index, invocation->image_H);
    
    internal = lookup_internal(invocation->mathmap->internals, "R", 1);
    set_float_internal(invocation, internal->index, invocation->image_R);

    internal = lookup_internal(invocation->mathmap->internals, "frame", 1);
    set_float_internal(invocation, internal->index, invocation->current_frame);
}

static void
update_pixel_internals (mathmap_invocation_t *invocation, float x, float y, float r, float a)
{
    set_float_internal(invocation, X_INTERNAL_INDEX, x);
    set_float_internal(invocation, Y_INTERNAL_INDEX, y);

    set_float_internal(invocation, R_INTERNAL_INDEX, r);
    set_float_internal(invocation, A_INTERNAL_INDEX, a);
}

static void
write_color_to_pixel (color_t color, guchar *dest, int output_bpp)
{
    if (output_bpp == 1 || output_bpp == 2)
	dest[0] = (RED(color) * 299 + GREEN(color) * 587 + BLUE(color) * 114) / 1000;
    else if (output_bpp == 3 || output_bpp == 4)
    {
	dest[0] = RED(color);
	dest[1] = GREEN(color);
	dest[2] = BLUE(color);
    }
    else
	assert(0);

    if (output_bpp == 2 || output_bpp == 4)
	dest[output_bpp - 1] = ALPHA(color);
}

void
init_frame (mathmap_invocation_t *invocation)
{
    if (invocation->mathmap->flags & MATHMAP_FLAG_NATIVE)
	invocation->mathfuncs.init_frame(invocation);
}

static void
run_interpreter (mathmap_invocation_t *invocation)
{
    invocation->interpreter_ip = 0;
    do
    {
	interpreter_insn_t *insn = &invocation->mathmap->interpreter_insns[invocation->interpreter_ip];

	++invocation->interpreter_ip;
	insn->func(invocation, insn->arg_indexes);
    } while (invocation->interpreter_ip >= 0);
}

static void
calc_lines (mathmap_invocation_t *invocation, int first_row, int last_row, unsigned char *q)
{
    if (invocation->mathmap->flags & MATHMAP_FLAG_NATIVE)
	invocation->mathfuncs.calc_lines(invocation, first_row, last_row, q);
    else
    {
	int row, col;
	int output_bpp = invocation->output_bpp;
	int origin_x = invocation->origin_x, origin_y = invocation->origin_y;
	float middle_x = invocation->middle_x, middle_y = invocation->middle_y;
	float sampling_offset_x = invocation->sampling_offset_x, sampling_offset_y = invocation->sampling_offset_y;
	float scale_x = invocation->scale_x, scale_y = invocation->scale_y;
	int uses_ra = does_mathmap_use_ra(invocation->mathmap);

	for (row = first_row; row < last_row; ++row)
	{
	    float y = CALC_VIRTUAL_Y(row, origin_y, scale_y, middle_y, sampling_offset_y);
	    unsigned char *p = q;

	    for (col = 0; col < invocation->calc_img_width; ++col)
	    {
		float x = CALC_VIRTUAL_X(col, origin_x, scale_x, middle_x, sampling_offset_x);
		float r, a;

		if (uses_ra)
		{
		    r = hypot(x, y);
		    if (r == 0.0)
			a = 0.0;
		    else
			a = acos(x / r);

		    if (y < 0)
			a = 2 * M_PI - a;
		}
		else
		    r = a = 0.0; /* to make the compiler happy */

		update_pixel_internals(invocation, x, y, r, a);

		run_interpreter(invocation);

		write_color_to_pixel(invocation->interpreter_output_color, p, output_bpp);

		p += output_bpp;
	    }

	    q += invocation->row_stride;

	    if (!invocation->supersampling)
		invocation->num_rows_finished = row + 1;
	}
    }
}

void
call_invocation (mathmap_invocation_t *invocation, int first_row, int last_row, unsigned char *q)
{
    if (invocation->supersampling)
    {
	guchar *line1, *line2, *line3;
	int row, col;
	int img_width = invocation->calc_img_width;

	line1 = (guchar*)malloc((img_width + 1) * invocation->output_bpp);
	line2 = (guchar*)malloc(img_width * invocation->output_bpp);
	line3 = (guchar*)malloc((img_width + 1) * invocation->output_bpp);

	invocation->calc_img_width = img_width + 1;
	invocation->sampling_offset_x = -0.5;
	invocation->sampling_offset_y = -0.5;
	init_frame(invocation);
	calc_lines(invocation, first_row, first_row + 1, line1);

	for (row = first_row; row < last_row; ++row)
	{
	    unsigned char *p = q;

	    invocation->calc_img_width = img_width;
	    invocation->sampling_offset_x = 0;
	    invocation->sampling_offset_y = 0;
	    init_frame(invocation);
	    calc_lines(invocation, row, row + 1, line2);

	    invocation->calc_img_width = img_width + 1;
	    invocation->sampling_offset_x = -0.5;
	    invocation->sampling_offset_y = -0.5;
	    init_frame(invocation);
	    calc_lines(invocation, row + 1, row + 2, line3);

	    for (col = 0; col < img_width; ++col)
	    {
		int i;

		for (i = 0; i < invocation->output_bpp; ++i)
		    p[i] = (line1[col*invocation->output_bpp+i]
			    + line1[(col+1)*invocation->output_bpp+i]
			    + 2*line2[col*invocation->output_bpp+i]
			    + line3[col*invocation->output_bpp+i]
			    + line3[(col+1)*invocation->output_bpp+i]) / 6;
		p += invocation->output_bpp;
	    }
	    memcpy(line1, line3, (img_width + 1) * invocation->output_bpp);

	    q += invocation->row_stride;

	    invocation->num_rows_finished = row + 1;
	}

	free(line1);
	free(line2);
	free(line3);
    }
    else
    {
	init_frame(invocation);
	calc_lines(invocation, first_row, last_row, q);
    }
}

typedef struct
{
    mathmap_invocation_t *invocation;
    int first_row;
    int last_row;
    unsigned char *q;
} thread_data_t;

static gpointer
call_invocation_thread_func (gpointer _data)
{
    thread_data_t *data = (thread_data_t*)_data;

    calc_lines(data->invocation, data->first_row, data->last_row, data->q);

    return NULL;
}

void
call_invocation_parallel (mathmap_invocation_t *invocation, int first_row, int last_row,
			  unsigned char *q, int num_threads)
{
    GThread *threads[num_threads];
    thread_data_t datas[num_threads];
    int i;

    if (invocation->supersampling || num_threads < 2)
    {
	call_invocation (invocation, first_row, last_row, q);
	return;
    }

    if (!g_thread_supported())
	g_thread_init(NULL);

    init_frame(invocation);

    for (i = 0; i < num_threads; ++i)
    {
	datas[i].invocation = invocation;
	datas[i].first_row = first_row + (last_row - first_row) * i / num_threads;
	datas[i].last_row = first_row + (last_row - first_row) * (i + 1) / num_threads;
	datas[i].q = q + (datas[i].first_row - first_row) * invocation->row_stride;

	/*if (i > 0)*/ {
	    threads[i] = g_thread_create(call_invocation_thread_func, &datas[i], TRUE, NULL);

	    g_assert (threads[i] != NULL);
	}
    }

    //call_invocation_thread_func (&datas[0]);

    for (i = 0; i < num_threads; ++i)
	g_thread_join(threads[i]);
}

void
carry_over_uservals_from_template (mathmap_invocation_t *invocation, mathmap_invocation_t *template)
{
    userval_info_t *info;

    for (info = invocation->mathmap->userval_infos; info != 0; info = info->next)
    {
	userval_info_t *template_info = lookup_matching_userval(template->mathmap->userval_infos, info);

	if (template_info != 0)
	    copy_userval(&invocation->uservals[info->index], &template->uservals[template_info->index], info->type);
    }
}

#define MAX_TEMPLATE_VAR_LENGTH       64
#define is_word_character(c)          (isalnum((c)) || (c) == '_')

void
process_template_file (mathmap_t *mathmap, FILE *template, FILE *out, template_processor_func_t template_processor)
{
    int c;

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
			if (c != EOF && c != '$')
			    ungetc(c, template);
		} while (is_word_character(c));

		assert(length > 0 && length <= MAX_TEMPLATE_VAR_LENGTH);

		name[length] = '\0';

		if (!template_processor(mathmap, name, out))
		    assert(0);
	    }
	}
	else
	    putc(c, out);
    }
}
