/*
image-source-sliced
Copyright (C) 2023 nitz - chris marc dailey https://cmd.wtf

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>

#include "plugin-macros.generated.h"

#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <sys/stat.h>

#define info_no_ctx(format, ...) \
	blog(LOG_INFO, "[" PLUGIN_NAME "] " format, ##__VA_ARGS__)

#define ctx_blog(log_level, format, ...)                   \
	blog(log_level, "[" PLUGIN_NAME ": '%s'] " format, \
	     obs_source_get_name(context->source), ##__VA_ARGS__)

#define debug(format, ...) ctx_blog(LOG_DEBUG, format, ##__VA_ARGS__)
#define info(format, ...) ctx_blog(LOG_INFO, format, ##__VA_ARGS__)
#define warn(format, ...) ctx_blog(LOG_WARNING, format, ##__VA_ARGS__)
#define error(format, ...) ctx_blog(LOG_ERROR, format, ##__VA_ARGS__)

struct filter_9slice {
	obs_source_t *source;
	gs_effect_t *effect;
	struct vec4 border;
	struct vec2 output_pixel_scale;
	struct vec2 last_source_size;
	bool show_uvs;
	bool uniform_scale;
	bool use_linear_filtering;
	struct filter_9slice_params_t {
		gs_eparam_t *border;
		gs_eparam_t *output_size;
		gs_eparam_t *source_size;
		gs_eparam_t *show_uvs;
		gs_eparam_t *use_linear_filtering;
	} params;
};

static const char *filter_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("NineSlice.Name");
}

static void filter_destroy(void *data)
{
	struct filter_9slice *context = data;

	if (context) {
		obs_enter_graphics();

		if (context->effect) {
			gs_effect_destroy(context->effect);
		}

		bfree(context);

		obs_leave_graphics();
	}
}

static bool filter_reload_effect(void *data)
{
	struct filter_9slice *context = data;

	obs_enter_graphics();

	// cleanup any existing effect.
	if (context->effect) {
		gs_effect_destroy(context->effect);
		context->effect = NULL;
		info("Destroyed existing effect.");
	}

	char *effect_file = obs_module_file("sliced.hlsl");
	context->effect = gs_effect_create_from_file(effect_file, NULL);
	bfree(effect_file);

	struct filter_9slice_params_t *params = &context->params;
	params->border = NULL;
	params->output_size = NULL;
	params->source_size = NULL;
	params->use_linear_filtering = NULL;
	params->show_uvs = NULL;

	if (!context->effect) {
		error("Failed to create effect.");
	} else {

		const gs_effect_t *effect = context->effect;

		params->show_uvs =
			gs_effect_get_param_by_name(effect, "show_uvs");
		params->use_linear_filtering = gs_effect_get_param_by_name(
			effect, "use_linear_filtering");
		params->border = gs_effect_get_param_by_name(effect, "border");
		params->output_size =
			gs_effect_get_param_by_name(effect, "output_size");
		params->source_size =
			gs_effect_get_param_by_name(effect, "source_size");

		if (params->use_linear_filtering == NULL) {
			warn("Failed to get use_linear_filtering param.");
		}
		if (params->show_uvs == NULL) {
			warn("Failed to get show_uvs param.");
		}
		if (params->border == NULL) {
			warn("Failed to get border param.");
		}
		if (params->output_size == NULL) {
			warn("Failed to get output_size param.");
		}
		if (params->source_size == NULL) {
			warn("Failed to get source_size param.");
		}
	}

	obs_leave_graphics();

	return context->effect != NULL;
}

static void filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct filter_9slice *context = data;

	if (!context)
		return;

	obs_source_t *parent = obs_filter_get_parent(context->source);
	const uint32_t width = obs_source_get_width(parent);
	const uint32_t height = obs_source_get_height(parent);

	context->last_source_size.x = (float)width;
	context->last_source_size.y = (float)height;

	const struct vec2 output_size = {
		.x = width * context->output_pixel_scale.x,
		.y = height * context->output_pixel_scale.y};

	if (!obs_source_process_filter_begin(context->source, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;

	struct filter_9slice_params_t *params = &context->params;

	gs_effect_set_vec4(params->border, &context->border);
	gs_effect_set_vec2(params->source_size, &context->last_source_size);
	gs_effect_set_vec2(params->output_size, &output_size);
	gs_effect_set_bool(params->show_uvs, context->show_uvs);
	gs_effect_set_bool(params->use_linear_filtering,
			   context->use_linear_filtering);

	obs_source_process_filter_end(context->source, context->effect, width,
				      height);
}

static void filter_update(void *data, obs_data_t *settings)
{
	struct filter_9slice *context = data;

	const bool show_uvs = obs_data_get_bool(settings, "show_uvs");
	const bool uniform_scale = obs_data_get_bool(settings, "uniform_scale");
	const bool use_linear_filtering =
		obs_data_get_bool(settings, "use_linear_filtering");

	const double output_scale_x =
		obs_data_get_double(settings, "output_scale_x");
	const double output_scale_y =
		obs_data_get_double(settings, "output_scale_y");

	const double border_top = obs_data_get_double(settings, "border_top");
	const double border_left = obs_data_get_double(settings, "border_left");
	const double border_bottom =
		obs_data_get_double(settings, "border_bottom");
	const double border_right =
		obs_data_get_double(settings, "border_right");

	context->show_uvs = show_uvs;
	context->uniform_scale = uniform_scale;
	context->use_linear_filtering = use_linear_filtering;

	context->output_pixel_scale.x = (float)output_scale_x;
	context->output_pixel_scale.y = uniform_scale ? (float)output_scale_x
						      : (float)output_scale_y;

	if (uniform_scale) {
		obs_data_set_double(settings, "output_scale_y", output_scale_x);
	}

	context->border.x = (float)border_top;
	context->border.y = (float)border_left;
	context->border.z = (float)border_bottom;
	context->border.w = (float)border_right;
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct filter_9slice *context = bzalloc(sizeof(struct filter_9slice));

	context->source = source;

	if (!filter_reload_effect(context)) {
		filter_destroy(context);
		context = NULL;
	}

	if (context != NULL) {
		filter_update(context, settings);
	}

	UNUSED_PARAMETER(settings);
	return context;
}

static void filter_get_defaults(obs_data_t *settings)
{
	// arbitrary numbers mostly chosen for no reason in particular.
	const double ScaleDefault = 4.0;
	const double BorderDefault = 8.0;

	obs_data_set_default_bool(settings, "show_uvs", false);
	obs_data_set_default_bool(settings, "uniform_scale", true);
	obs_data_set_default_bool(settings, "use_linear_filtering", false);

	obs_data_set_default_double(settings, "output_scale_x", ScaleDefault);
	obs_data_set_default_double(settings, "output_scale_y", ScaleDefault);

	obs_data_set_default_double(settings, "border_top", BorderDefault);
	obs_data_set_default_double(settings, "border_left", BorderDefault);
	obs_data_set_default_double(settings, "border_bottom", BorderDefault);
	obs_data_set_default_double(settings, "border_right", BorderDefault);
}

static obs_properties_t *filter_get_properties(void *data)
{
	// arbitrary numbers mostly chosen for no reason in particular.
	const double ScaleMin = 0.0;
	const double ScaleStep = 0.1;
	const double ScaleMax = 20.0;

	const double SliceMin = 0.0;
	const double SliceStep = 1.0;

	struct filter_9slice *context = data;
	obs_properties_t *props = obs_properties_create();

	// limit the border sizes to half of the source size
	const double SliceWidthMax =
		context == NULL ? ScaleMin : context->last_source_size.x - 1.0;
	const double SliceHeightMax =
		context == NULL ? ScaleMin : context->last_source_size.y - 1.0;

	// debug options
	obs_properties_add_bool(props, "show_uvs",
				obs_module_text("NineSlice.ShowUVs"));

	obs_properties_add_bool(props, "uniform_scale",
				obs_module_text("NineSlice.UniformScale"));

	obs_properties_add_bool(props, "use_linear_filtering",
				obs_module_text("NineSlice.LinearFiltering"));

	// output scale x/y
	obs_properties_add_float_slider(props, "output_scale_x",
					obs_module_text("NineSlice.ScaleX"),
					ScaleMin, ScaleMax, ScaleStep);

	obs_properties_add_float_slider(props, "output_scale_y",
					obs_module_text("NineSlice.ScaleY"),
					ScaleMin, ScaleMax, ScaleStep);

	// border sizes
	obs_properties_add_float_slider(props, "border_top",
					obs_module_text("NineSlice.Top"),
					SliceMin, SliceHeightMax, SliceStep);
	obs_properties_add_float_slider(props, "border_left",
					obs_module_text("NineSlice.Left"),
					SliceMin, SliceWidthMax, SliceStep);
	obs_properties_add_float_slider(props, "border_bottom",
					obs_module_text("NineSlice.Bottom"),
					SliceMin, SliceHeightMax, SliceStep);
	obs_properties_add_float_slider(props, "border_right",
					obs_module_text("NineSlice.Right"),
					SliceMin, SliceWidthMax, SliceStep);

	return props;
}

struct obs_source_info filter_9slice = {
	.id = "filter_9slice",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_getname,
	.create = filter_create,
	.destroy = filter_destroy,
	.video_render = filter_render,
	.update = filter_update,
	.get_defaults = filter_get_defaults,
	.get_properties = filter_get_properties,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("NineSlice.Description");
}

bool obs_module_load(void)
{
	info_no_ctx("v" PLUGIN_VERSION " loaded successfully");
	obs_register_source(&filter_9slice);
	return true;
}

void obs_module_unload()
{
	info_no_ctx("unloaded");
}
