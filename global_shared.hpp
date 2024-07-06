
// a class to host all global variables shared between reshade on_* functions. 
// This class is to be added in private_data of the reshade device.

#include "reshade.hpp"
#include "mod_parameters.h"

struct __declspec(uuid("a2d72286-a857-4e4a-8ca1-ef42faf4e340")) global_shared
{
	// index of constant buffers in global DX11 pipepline layout, should be 2
	uint32_t CBIndex = 0;
	// index of resource views in global DX11 pipepline layout, should be 1
	uint32_t RVIndex = 0;

	// working : float array containing the constant buffer to be injected
	// float cb_inject_values[CBSIZE] = { 1.0, 2.0, 3.0, 4.0 };
	//to test : struct
	struct ShaderInjectData cb_inject_values;
	uint64_t cb_inject_size = CBSIZE;

	// reference of unique DX11 pipeline_layout -if push_descriptor not working or of a new dedicated pipeline layout if push_constant
	reshade::api::pipeline_layout saved_pipeline_layout;

	// to be used in push_descriptors
	reshade::api::device *saved_device;
	reshade::api::descriptor_table_update CB_desc_table_update;
	reshade::api::descriptor_table_update RV_desc_table_update;
	// for push_descriptor - not working :-(
	// reshade::api::resource resource_desc_CB;
	// not working
	// reshade::api::buffer_range resource_desc_CB;
	reshade::api::resource resource_desc_RV;

	// to be tested :  contains shader code to override ouput
	static std::vector<std::vector<uint8_t>> s_constant_color;

	void* shaderInjection;

} shared_data;