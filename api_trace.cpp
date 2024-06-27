/*
 * Copyright (C) 2021 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include <reshade.hpp>
#include <cassert>
#include <sstream>
#include <shared_mutex>
#include <unordered_set>

using namespace reshade::api;

namespace
{
	bool s_do_capture = false;
	std::shared_mutex s_mutex;
	std::unordered_set<uint64_t> s_samplers;
	std::unordered_set<uint64_t> s_resources;
	std::unordered_set<uint64_t> s_resource_views;
	std::unordered_set<uint64_t> s_pipelines;

	inline auto to_string(shader_stage value)
	{
		switch (value)
		{
		case shader_stage::vertex:
			return "vertex";
		case shader_stage::hull:
			return "hull";
		case shader_stage::domain:
			return "domain";
		case shader_stage::geometry:
			return "geometry";
		case shader_stage::pixel:
			return "pixel";
		case shader_stage::compute:
			return "compute";
		case shader_stage::amplification:
			return "amplification";
		case shader_stage::mesh:
			return "mesh";
		case shader_stage::raygen:
			return "raygen";
		case shader_stage::any_hit:
			return "any_hit";
		case shader_stage::closest_hit:
			return "closest_hit";
		case shader_stage::miss:
			return "miss";
		case shader_stage::intersection:
			return "intersection";
		case shader_stage::callable:
			return "callable";
		case shader_stage::all:
			return "all";
		case shader_stage::all_graphics:
			return "all_graphics";
		case shader_stage::all_ray_tracing:
			return "all_raytracing";
		default:
			return "unknown";
		}
	}
	inline auto to_string(pipeline_stage value)
	{
		switch (value)
		{
		case pipeline_stage::vertex_shader:
			return "vertex_shader";
		case pipeline_stage::hull_shader:
			return "hull_shader";
		case pipeline_stage::domain_shader:
			return "domain_shader";
		case pipeline_stage::geometry_shader:
			return "geometry_shader";
		case pipeline_stage::pixel_shader:
			return "pixel_shader";
		case pipeline_stage::compute_shader:
			return "compute_shader";
		case pipeline_stage::amplification_shader:
			return "amplification_shader";
		case pipeline_stage::mesh_shader:
			return "mesh_shader";
		case pipeline_stage::input_assembler:
			return "input_assembler";
		case pipeline_stage::stream_output:
			return "stream_output";
		case pipeline_stage::rasterizer:
			return "rasterizer";
		case pipeline_stage::depth_stencil:
			return "depth_stencil";
		case pipeline_stage::output_merger:
			return "output_merger";
		case pipeline_stage::all:
			return "all";
		case pipeline_stage::all_graphics:
			return "all_graphics";
		case pipeline_stage::all_ray_tracing:
			return "all_ray_tracing";
		case pipeline_stage::all_shader_stages:
			return "all_shader_stages";
		default:
			return "unknown";
		}
	}
	inline auto to_string(descriptor_type value)
	{
		switch (value)
		{
		case descriptor_type::sampler:
			return "sampler";
		case descriptor_type::sampler_with_resource_view:
			return "sampler_with_resource_view";
		case descriptor_type::shader_resource_view:
			return "shader_resource_view";
		case descriptor_type::unordered_access_view:
			return "unordered_access_view";
		case descriptor_type::constant_buffer:
			return "constant_buffer";
		case descriptor_type::acceleration_structure:
			return "acceleration_structure";
		default:
			return "unknown";
		}
	}
	inline auto to_string(dynamic_state value)
	{
		switch (value)
		{
		default:
		case dynamic_state::unknown:
			return "unknown";
		case dynamic_state::alpha_test_enable:
			return "alpha_test_enable";
		case dynamic_state::alpha_reference_value:
			return "alpha_reference_value";
		case dynamic_state::alpha_func:
			return "alpha_func";
		case dynamic_state::srgb_write_enable:
			return "srgb_write_enable";
		case dynamic_state::primitive_topology:
			return "primitive_topology";
		case dynamic_state::sample_mask:
			return "sample_mask";
		case dynamic_state::alpha_to_coverage_enable:
			return "alpha_to_coverage_enable";
		case dynamic_state::blend_enable:
			return "blend_enable";
		case dynamic_state::logic_op_enable:
			return "logic_op_enable";
		case dynamic_state::color_blend_op:
			return "color_blend_op";
		case dynamic_state::source_color_blend_factor:
			return "src_color_blend_factor";
		case dynamic_state::dest_color_blend_factor:
			return "dst_color_blend_factor";
		case dynamic_state::alpha_blend_op:
			return "alpha_blend_op";
		case dynamic_state::source_alpha_blend_factor:
			return "src_alpha_blend_factor";
		case dynamic_state::dest_alpha_blend_factor:
			return "dst_alpha_blend_factor";
		case dynamic_state::logic_op:
			return "logic_op";
		case dynamic_state::blend_constant:
			return "blend_constant";
		case dynamic_state::render_target_write_mask:
			return "render_target_write_mask";
		case dynamic_state::fill_mode:
			return "fill_mode";
		case dynamic_state::cull_mode:
			return "cull_mode";
		case dynamic_state::front_counter_clockwise:
			return "front_counter_clockwise";
		case dynamic_state::depth_bias:
			return "depth_bias";
		case dynamic_state::depth_bias_clamp:
			return "depth_bias_clamp";
		case dynamic_state::depth_bias_slope_scaled:
			return "depth_bias_slope_scaled";
		case dynamic_state::depth_clip_enable:
			return "depth_clip_enable";
		case dynamic_state::scissor_enable:
			return "scissor_enable";
		case dynamic_state::multisample_enable:
			return "multisample_enable";
		case dynamic_state::antialiased_line_enable:
			return "antialiased_line_enable";
		case dynamic_state::depth_enable:
			return "depth_enable";
		case dynamic_state::depth_write_mask:
			return "depth_write_mask";
		case dynamic_state::depth_func:
			return "depth_func";
		case dynamic_state::stencil_enable:
			return "stencil_enable";
		case dynamic_state::front_stencil_read_mask:
			return "front_stencil_read_mask";
		case dynamic_state::front_stencil_write_mask:
			return "front_stencil_write_mask";
		case dynamic_state::front_stencil_reference_value:
			return "front_stencil_reference_value";
		case dynamic_state::front_stencil_func:
			return "front_stencil_func";
		case dynamic_state::front_stencil_pass_op:
			return "front_stencil_pass_op";
		case dynamic_state::front_stencil_fail_op:
			return "front_stencil_fail_op";
		case dynamic_state::front_stencil_depth_fail_op:
			return "front_stencil_depth_fail_op";
		case dynamic_state::back_stencil_read_mask:
			return "back_stencil_read_mask";
		case dynamic_state::back_stencil_write_mask:
			return "back_stencil_write_mask";
		case dynamic_state::back_stencil_reference_value:
			return "back_stencil_reference_value";
		case dynamic_state::back_stencil_func:
			return "back_stencil_func";
		case dynamic_state::back_stencil_pass_op:
			return "back_stencil_pass_op";
		case dynamic_state::back_stencil_fail_op:
			return "back_stencil_fail_op";
		case dynamic_state::back_stencil_depth_fail_op:
			return "back_stencil_depth_fail_op";
		}
	}
	inline auto to_string(resource_usage value)
	{
		switch (value)
		{
		default:
		case resource_usage::undefined:
			return "undefined";
		case resource_usage::index_buffer:
			return "index_buffer";
		case resource_usage::vertex_buffer:
			return "vertex_buffer";
		case resource_usage::constant_buffer:
			return "constant_buffer";
		case resource_usage::stream_output:
			return "stream_output";
		case resource_usage::indirect_argument:
			return "indirect_argument";
		case resource_usage::depth_stencil:
		case resource_usage::depth_stencil_read:
		case resource_usage::depth_stencil_write:
			return "depth_stencil";
		case resource_usage::render_target:
			return "render_target";
		case resource_usage::shader_resource:
		case resource_usage::shader_resource_pixel:
		case resource_usage::shader_resource_non_pixel:
			return "shader_resource";
		case resource_usage::unordered_access:
			return "unordered_access";
		case resource_usage::copy_dest:
			return "copy_dest";
		case resource_usage::copy_source:
			return "copy_source";
		case resource_usage::resolve_dest:
			return "resolve_dest";
		case resource_usage::resolve_source:
			return "resolve_source";
		case resource_usage::acceleration_structure:
			return "acceleration_structure";
		case resource_usage::general:
			return "general";
		case resource_usage::present:
			return "present";
		case resource_usage::cpu_access:
			return "cpu_access";
		}
	}
	inline auto to_string(query_type value)
	{
		switch (value)
		{
		case query_type::occlusion:
			return "occlusion";
		case query_type::binary_occlusion:
			return "binary_occlusion";
		case query_type::timestamp:
			return "timestamp";
		case query_type::pipeline_statistics:
			return "pipeline_statistics";
		case query_type::stream_output_statistics_0:
			return "stream_output_statistics_0";
		case query_type::stream_output_statistics_1:
			return "stream_output_statistics_1";
		case query_type::stream_output_statistics_2:
			return "stream_output_statistics_2";
		case query_type::stream_output_statistics_3:
			return "stream_output_statistics_3";
		default:
			return "unknown";
		}
	}
	inline auto to_string(acceleration_structure_type value)
	{
		switch (value)
		{
		case acceleration_structure_type::top_level:
			return "top_level";
		case acceleration_structure_type::bottom_level:
			return "bottom_level";
		default:
		case acceleration_structure_type::generic:
			return "generic";
		}
	}
	inline auto to_string(acceleration_structure_copy_mode value)
	{
		switch (value)
		{
		case acceleration_structure_copy_mode::clone:
			return "clone";
		case acceleration_structure_copy_mode::compact:
			return "compact";
		case acceleration_structure_copy_mode::serialize:
			return "serialize";
		case acceleration_structure_copy_mode::deserialize:
			return "deserialize";
		default:
			return "unknown";
		}
	}
	inline auto to_string(acceleration_structure_build_mode value)
	{
		switch (value)
		{
		case acceleration_structure_build_mode::build:
			return "build";
		case acceleration_structure_build_mode::update:
			return "update";
		default:
			return "unknown";
		}
	}
}

inline auto to_string(reshade::api::pipeline_layout_param_type value) {
	switch (value) {
	case reshade::api::pipeline_layout_param_type::push_constants:               return "push_constants";
	case reshade::api::pipeline_layout_param_type::descriptor_table:             return "descriptor_table";
	case reshade::api::pipeline_layout_param_type::push_descriptors:             return "push_descriptors";
	case reshade::api::pipeline_layout_param_type::push_descriptors_with_ranges: return "push_descriptors_with_ranges";
	default:                                                                     return "unknown";
	}
}

inline auto to_string(reshade::api::resource_type value) {
	switch (value) {
	case reshade::api::resource_type::buffer:     return "buffer";
	case reshade::api::resource_type::texture_1d: return "texture_1d";
	case reshade::api::resource_type::texture_2d: return "texture_2d";
	case reshade::api::resource_type::texture_3d: return "texture_3d";
	case reshade::api::resource_type::surface:    return "surface";
	default:
	case reshade::api::resource_type::unknown:    return "unknown";
	}
}

inline auto to_string(reshade::api::format value) {
	switch (value) {
	case reshade::api::format::r1_unorm:              return "r1_unorm";
	case reshade::api::format::l8_unorm:              return "l8_unorm";
	case reshade::api::format::a8_unorm:              return "a8_unorm";
	case reshade::api::format::r8_typeless:           return "r8_typeless";
	case reshade::api::format::r8_uint:               return "r8_uint";
	case reshade::api::format::r8_sint:               return "r8_sint";
	case reshade::api::format::r8_unorm:              return "r8_unorm";
	case reshade::api::format::r8_snorm:              return "r8_snorm";
	case reshade::api::format::l8a8_unorm:            return "l8a8_unorm";
	case reshade::api::format::r8g8_typeless:         return "r8g8_typeless";
	case reshade::api::format::r8g8_uint:             return "r8g8_uint";
	case reshade::api::format::r8g8_sint:             return "r8g8_sint";
	case reshade::api::format::r8g8_unorm:            return "r8g8_unorm";
	case reshade::api::format::r8g8_snorm:            return "r8g8_snorm";
	case reshade::api::format::r8g8b8a8_typeless:     return "r8g8b8a8_typeless";
	case reshade::api::format::r8g8b8a8_uint:         return "r8g8b8a8_uint";
	case reshade::api::format::r8g8b8a8_sint:         return "r8g8b8a8_sint";
	case reshade::api::format::r8g8b8a8_unorm:        return "r8g8b8a8_unorm";
	case reshade::api::format::r8g8b8a8_unorm_srgb:   return "r8g8b8a8_unorm_srgb";
	case reshade::api::format::r8g8b8a8_snorm:        return "r8g8b8a8_snorm";
	case reshade::api::format::r8g8b8x8_unorm:        return "r8g8b8x8_unorm";
	case reshade::api::format::r8g8b8x8_unorm_srgb:   return "r8g8b8x8_unorm_srgb";
	case reshade::api::format::b8g8r8a8_typeless:     return "b8g8r8a8_typeless";
	case reshade::api::format::b8g8r8a8_unorm:        return "b8g8r8a8_unorm";
	case reshade::api::format::b8g8r8a8_unorm_srgb:   return "b8g8r8a8_unorm_srgb";
	case reshade::api::format::b8g8r8x8_typeless:     return "b8g8r8x8_typeless";
	case reshade::api::format::b8g8r8x8_unorm:        return "b8g8r8x8_unorm";
	case reshade::api::format::b8g8r8x8_unorm_srgb:   return "b8g8r8x8_unorm_srgb";
	case reshade::api::format::r10g10b10a2_typeless:  return "r10g10b10a2_typeless";
	case reshade::api::format::r10g10b10a2_uint:      return "r10g10b10a2_uint";
	case reshade::api::format::r10g10b10a2_unorm:     return "r10g10b10a2_unorm";
	case reshade::api::format::r10g10b10a2_xr_bias:   return "r10g10b10a2_xr_bias";
	case reshade::api::format::b10g10r10a2_typeless:  return "b10g10r10a2_typeless";
	case reshade::api::format::b10g10r10a2_uint:      return "b10g10r10a2_uint";
	case reshade::api::format::b10g10r10a2_unorm:     return "b10g10r10a2_unorm";
	case reshade::api::format::l16_unorm:             return "l16_unorm";
	case reshade::api::format::r16_typeless:          return "r16_typeless";
	case reshade::api::format::r16_uint:              return "r16_uint";
	case reshade::api::format::r16_sint:              return "r16_sint";
	case reshade::api::format::r16_unorm:             return "r16_unorm";
	case reshade::api::format::r16_snorm:             return "r16_snorm";
	case reshade::api::format::r16_float:             return "r16_float";
	case reshade::api::format::l16a16_unorm:          return "l16a16_unorm";
	case reshade::api::format::r16g16_typeless:       return "r16g16_typeless";
	case reshade::api::format::r16g16_uint:           return "r16g16_uint";
	case reshade::api::format::r16g16_sint:           return "r16g16_sint";
	case reshade::api::format::r16g16_unorm:          return "r16g16_unorm";
	case reshade::api::format::r16g16_snorm:          return "r16g16_snorm";
	case reshade::api::format::r16g16_float:          return "r16g16_float";
	case reshade::api::format::r16g16b16a16_typeless: return "r16g16b16a16_typeless";
	case reshade::api::format::r16g16b16a16_uint:     return "r16g16b16a16_uint";
	case reshade::api::format::r16g16b16a16_sint:     return "r16g16b16a16_sint";
	case reshade::api::format::r16g16b16a16_unorm:    return "r16g16b16a16_unorm";
	case reshade::api::format::r16g16b16a16_snorm:    return "r16g16b16a16_snorm";
	case reshade::api::format::r16g16b16a16_float:    return "r16g16b16a16_float";
	case reshade::api::format::r32_typeless:          return "r32_typeless";
	case reshade::api::format::r32_uint:              return "r32_uint";
	case reshade::api::format::r32_sint:              return "r32_sint";
	case reshade::api::format::r32_float:             return "r32_float";
	case reshade::api::format::r32g32_typeless:       return "r32g32_typeless";
	case reshade::api::format::r32g32_uint:           return "r32g32_uint";
	case reshade::api::format::r32g32_sint:           return "r32g32_sint";
	case reshade::api::format::r32g32_float:          return "r32g32_float";
	case reshade::api::format::r32g32b32_typeless:    return "r32g32b32_typeless";
	case reshade::api::format::r32g32b32_uint:        return "r32g32b32_uint";
	case reshade::api::format::r32g32b32_sint:        return "r32g32b32_sint";
	case reshade::api::format::r32g32b32_float:       return "r32g32b32_float";
	case reshade::api::format::r32g32b32a32_typeless: return "r32g32b32a32_typeless";
	case reshade::api::format::r32g32b32a32_uint:     return "r32g32b32a32_uint";
	case reshade::api::format::r32g32b32a32_sint:     return "r32g32b32a32_sint";
	case reshade::api::format::r32g32b32a32_float:    return "r32g32b32a32_float";
	case reshade::api::format::r9g9b9e5:              return "r9g9b9e5";
	case reshade::api::format::r11g11b10_float:       return "r11g11b10_float";
	case reshade::api::format::b5g6r5_unorm:          return "b5g6r5_unorm";
	case reshade::api::format::b5g5r5a1_unorm:        return "b5g5r5a1_unorm";
	case reshade::api::format::b5g5r5x1_unorm:        return "b5g5r5x1_unorm";
	case reshade::api::format::b4g4r4a4_unorm:        return "b4g4r4a4_unorm";
	case reshade::api::format::a4b4g4r4_unorm:        return "a4b4g4r4_unorm";
	case reshade::api::format::s8_uint:               return "s8_uint";
	case reshade::api::format::d16_unorm:             return "d16_unorm";
	case reshade::api::format::d16_unorm_s8_uint:     return "d16_unorm_s8_uint";
	case reshade::api::format::d24_unorm_x8_uint:     return "d24_unorm_x8_uint";
	case reshade::api::format::d24_unorm_s8_uint:     return "d24_unorm_s8_uint";
	case reshade::api::format::d32_float:             return "d32_float";
	case reshade::api::format::d32_float_s8_uint:     return "d32_float_s8_uint";
	case reshade::api::format::r24_g8_typeless:       return "r24_g8_typeless";
	case reshade::api::format::r24_unorm_x8_uint:     return "r24_unorm_x8_uint";
	case reshade::api::format::x24_unorm_g8_uint:     return "x24_unorm_g8_uint";
	case reshade::api::format::r32_g8_typeless:       return "r32_g8_typeless";
	case reshade::api::format::r32_float_x8_uint:     return "r32_float_x8_uint";
	case reshade::api::format::x32_float_g8_uint:     return "x32_float_g8_uint";
	case reshade::api::format::bc1_typeless:          return "bc1_typeless";
	case reshade::api::format::bc1_unorm:             return "bc1_unorm";
	case reshade::api::format::bc1_unorm_srgb:        return "bc1_unorm_srgb";
	case reshade::api::format::bc2_typeless:          return "bc2_typeless";
	case reshade::api::format::bc2_unorm:             return "bc2_unorm";
	case reshade::api::format::bc2_unorm_srgb:        return "bc2_unorm_srgb";
	case reshade::api::format::bc3_typeless:          return "bc3_typeless";
	case reshade::api::format::bc3_unorm:             return "bc3_unorm";
	case reshade::api::format::bc3_unorm_srgb:        return "bc3_unorm_srgb";
	case reshade::api::format::bc4_typeless:          return "bc4_typeless";
	case reshade::api::format::bc4_unorm:             return "bc4_unorm";
	case reshade::api::format::bc4_snorm:             return "bc4_snorm";
	case reshade::api::format::bc5_typeless:          return "bc5_typeless";
	case reshade::api::format::bc5_unorm:             return "bc5_unorm";
	case reshade::api::format::bc5_snorm:             return "bc5_snorm";
	case reshade::api::format::bc6h_typeless:         return "bc6h_typeless";
	case reshade::api::format::bc6h_ufloat:           return "bc6h_ufloat";
	case reshade::api::format::bc6h_sfloat:           return "bc6h_sfloat";
	case reshade::api::format::bc7_typeless:          return "bc7_typeless";
	case reshade::api::format::bc7_unorm:             return "bc7_unorm";
	case reshade::api::format::bc7_unorm_srgb:        return "bc7_unorm_srgb";
	case reshade::api::format::r8g8_b8g8_unorm:       return "r8g8_b8g8_unorm";
	case reshade::api::format::g8r8_g8b8_unorm:       return "g8r8_g8b8_unorm";
	case reshade::api::format::intz:                  return "intz";
	case reshade::api::format::unknown:
	default:                                          return "unknown";
	}
}

inline auto to_string(reshade::api::resource_view_type value) {
	switch (value) {
	case reshade::api::resource_view_type::buffer:                       return "buffer";
	case reshade::api::resource_view_type::texture_1d:                   return "texture_1d";
	case reshade::api::resource_view_type::texture_1d_array:             return "texture_1d_array";
	case reshade::api::resource_view_type::texture_2d:                   return "texture_2d";
	case reshade::api::resource_view_type::texture_2d_array:             return "texture_2d_array";
	case reshade::api::resource_view_type::texture_2d_multisample:       return "texture_2d_multisample";
	case reshade::api::resource_view_type::texture_2d_multisample_array: return "texture_2d_multisample_array";
	case reshade::api::resource_view_type::texture_3d:                   return "texture_3d";
	case reshade::api::resource_view_type::texture_cube:                 return "texture_cube";
	case reshade::api::resource_view_type::texture_cube_array:           return "texture_cube_array";
	case reshade::api::resource_view_type::acceleration_structure:       return "acceleration_structure";
	case reshade::api::resource_view_type::unknown:
	default:                                                             return "unknown";
	}
}

static void on_push_descriptors(command_list*, shader_stage stages, pipeline_layout layout, uint32_t param_index, const descriptor_table_update& update)
{
	if (!s_do_capture)
		return;

#ifndef NDEBUG
	{	const std::shared_lock<std::shared_mutex> lock(s_mutex);

	switch (update.type)
	{
	case descriptor_type::sampler:
		for (uint32_t i = 0; i < update.count; ++i)
			assert(static_cast<const sampler*>(update.descriptors)[i].handle == 0 || s_samplers.find(static_cast<const sampler*>(update.descriptors)[i].handle) != s_samplers.end());
		break;
	case descriptor_type::sampler_with_resource_view:
		for (uint32_t i = 0; i < update.count; ++i)
			assert(static_cast<const sampler_with_resource_view*>(update.descriptors)[i].view.handle == 0 || s_resource_views.find(static_cast<const sampler_with_resource_view*>(update.descriptors)[i].view.handle) != s_resource_views.end());
		break;
	case descriptor_type::shader_resource_view:
	case descriptor_type::unordered_access_view:
	case descriptor_type::acceleration_structure:
		for (uint32_t i = 0; i < update.count; ++i)
			assert(static_cast<const resource_view*>(update.descriptors)[i].handle == 0 || s_resource_views.find(static_cast<const resource_view*>(update.descriptors)[i].handle) != s_resource_views.end());
		break;
	case descriptor_type::constant_buffer:
		for (uint32_t i = 0; i < update.count; ++i)
			assert(static_cast<const buffer_range*>(update.descriptors)[i].buffer.handle == 0 || s_resources.find(static_cast<const buffer_range*>(update.descriptors)[i].buffer.handle) != s_resources.end());
		break;
	default:
		break;
	}
	}
#endif

	std::stringstream s;
	s << "push_descriptors(" << to_string(stages) << ", " << (void*)layout.handle << ", " << param_index << ", { " << to_string(update.type) << ", " << update.binding << ", " << update.count << " })";

	reshade::log_message(reshade::log_level::info, s.str().c_str());
}

static void on_bind_render_targets_and_depth_stencil(command_list *, uint32_t count, const resource_view *rtvs, resource_view dsv)
{
	if (!s_do_capture)
		return;

#ifndef NDEBUG
	{	const std::shared_lock<std::shared_mutex> lock(s_mutex);

		for (uint32_t i = 0; i < count; ++i)
			assert(rtvs[i] == 0 || s_resource_views.find(rtvs[i].handle) != s_resource_views.end());
		assert(dsv == 0 || s_resource_views.find(dsv.handle) != s_resource_views.end());
	}
#endif

	std::stringstream s;
	s << "bind_render_targets_and_depth_stencil(" << count << ", { ";
	for (uint32_t i = 0; i < count; ++i)
		s << (void *)rtvs[i].handle << ", ";
	s << " }, " << (void *)dsv.handle << ")";

	reshade::log_message(reshade::log_level::info, s.str().c_str());
}

static void on_bind_viewports(command_list *, uint32_t first, uint32_t count, const viewport *viewports)
{
	if (!s_do_capture)
		return;

	std::stringstream s;
	s << "bind_viewports(" << first << ", " << count << ", { ... })";

	reshade::log_message(reshade::log_level::info, s.str().c_str());
}

static bool on_clear_render_target_view(command_list *, resource_view rtv, const float color[4], uint32_t, const rect *)
{
	if (!s_do_capture)
		return false;

#ifndef NDEBUG
	{	const std::shared_lock<std::shared_mutex> lock(s_mutex);

		assert(s_resource_views.find(rtv.handle) != s_resource_views.end());
	}
#endif

	std::stringstream s;
	s << "clear_render_target_view(" << (void *)rtv.handle << ", { " << color[0] << ", " << color[1] << ", " << color[2] << ", " << color[3] << " })";

	reshade::log_message(reshade::log_level::info, s.str().c_str());

	return false;
}

static bool on_clear_depth_stencil_view(command_list *, resource_view dsv, const float *depth, const uint8_t *stencil, uint32_t, const rect *)
{
	if (!s_do_capture)
		return false;

#ifndef NDEBUG
	{	const std::shared_lock<std::shared_mutex> lock(s_mutex);

		assert(s_resource_views.find(dsv.handle) != s_resource_views.end());
	}
#endif

	std::stringstream s;
	s << "clear_depth_stencil_view(" << (void *)dsv.handle << ", " << (depth != nullptr ? *depth : 0.0f) << ", " << (stencil != nullptr ? *stencil : 0) << ")";

	reshade::log_message(reshade::log_level::info, s.str().c_str());

	return false;
}

static void on_bind_pipeline_states(command_list *, uint32_t count, const dynamic_state *states, const uint32_t *values)
{
	if (!s_do_capture)
		return;

	for (uint32_t i = 0; i < count; ++i)
	{
		std::stringstream s;
		s << "bind_pipeline_state(" << to_string(states[i]) << ", " << values[i] << ")";
		reshade::log_message(reshade::log_level::info, s.str().c_str());
	}
}


static void on_bind_vertex_buffers(command_list *, uint32_t first, uint32_t count, const resource *buffers, const uint64_t *offsets, const uint32_t *strides)
{
	if (!s_do_capture)
		return;

#ifndef NDEBUG
	{	const std::shared_lock<std::shared_mutex> lock(s_mutex);

		for (uint32_t i = 0; i < count; ++i)
			assert(buffers[i].handle == 0 || s_resources.find(buffers[i].handle) != s_resources.end());
	}
#endif

	for (uint32_t i = 0; i < count; ++i)
	{
		std::stringstream s;
		s << "bind_vertex_buffer(" << (first + i) << ", " << (void *)buffers[i].handle << ", " << (offsets != nullptr ? offsets[i] : 0) << ", " << (strides != nullptr ? strides[i] : 0) << ")";
		reshade::log_message(reshade::log_level::info, s.str().c_str());
	}
}


static void on_bind_index_buffer(command_list *, resource buffer, uint64_t offset, uint32_t index_size)
{
	if (!s_do_capture)
		return;

#ifndef NDEBUG
	{	const std::shared_lock<std::shared_mutex> lock(s_mutex);

		assert(buffer.handle == 0 || s_resources.find(buffer.handle) != s_resources.end());
	}
#endif

	std::stringstream s;
	s << "bind_index_buffer(" << (void *)buffer.handle << ", " << offset << ", " << index_size << ")";

	reshade::log_message(reshade::log_level::info, s.str().c_str());
}


