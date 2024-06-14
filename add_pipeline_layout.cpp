/// create a pipleline_layout and initialize it to handle 1 constant buffer, code parts given by Crossire

#include <reshade.hpp>

using namespace reshade::api;

// See if it should be better to have the layout global => no function to avoid pointer question...

reshade::api::pipeline_layout add_pipeline_layout(
    device* device)
{

    // create descriptor for a constant buffer in DX mapping starting from 0
    reshade::api::descriptor_range srv_range;
    srv_range.dx_register_index = 0; // start at 0
    srv_range.count = UINT32_MAX; // unbounded
    srv_range.visibility = reshade::api::shader_stage::all;
    srv_range.type = reshade::api::descriptor_type::constant_buffer;

    // create pipeline_layout using the descriptor
    const reshade::api::pipeline_layout_param params[1] = { srv_range };
    reshade::api::pipeline_layout layout;
    device->create_pipeline_layout(1, params, &layout);

    return layout;
}


