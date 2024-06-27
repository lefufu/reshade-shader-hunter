
// a class to host all global variables shared between reshade on_* functions. 
// This class is to be added in private_data of the reshade device.

#include "reshade.hpp"

// size of the constant buffer containing all mod parameters, to be injected in shaders
constexpr auto CBSIZE = 4;

// CB number to be injected in the shaders
constexpr auto CBINDEX = 13;


struct __declspec(uuid("a2d72286-a857-4e4a-8ca1-ef42faf4e340")) global_shared
{
	// index of constant buffers in global DX11 pipepline layout, should be 2
	uint32_t CBIndex = 0;
	// index of resource views in global DX11 pipepline layout, should be 1
	uint32_t RVIndex = 0;

	// float array containing the constant buffer to be injected
	float cb_inject_values[CBSIZE] = { 1.0, 2.0, 3.0, 4.0 };
	uint64_t cb_inject_size = CBSIZE;

	// reference of unique DX11 pipeline_layout
	reshade::api::pipeline_layout saved_pipeline_layout;

	// to be used in push_descriptors
	reshade::api::device *saved_device;
	reshade::api::descriptor_table_update CB_desc_table_update;
	reshade::api::descriptor_table_update RV_desc_table_update;
	reshade::api::resource resource_desc_CB;
	reshade::api::resource resource_desc_RV;

	// to be tested :  contains shader code to override ouput
	static std::vector<std::vector<uint8_t>> s_constant_color;

} shared_data;