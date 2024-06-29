///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for Reshade 5+ which allows you
// to define groups of shaders to toggle them on/off with one key press
// 
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/ShaderToggler
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////

#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID unsigned long long // Change ImGui texture ID type to that of a 'reshade::api::resource_view' handle

#define COLOR_SHADER_NAME L"red.cso"

#include <imgui.h>
#include <reshade.hpp>
#include "crc32_hash.hpp"
#include "ShaderManager.h"
#include "CDataFile.h"
#include "ToggleGroup.h"
#include <vector>
#include <filesystem>

#include <unordered_map>

#include "global_shared.hpp"

#include "api_trace.cpp"
#include "config.hpp"

#include "load_shader.cpp"
#include "ClonePipeline.cpp"

// #include "renodx_format.hpp"

// using namespace reshade::api;
using namespace ShaderToggler;

extern "C" __declspec(dllexport) const char *NAME = "Shader Hunter";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Add-on which allows you to define groups of game shaders to toggle on/off or mark them with one key press. Write infos of frame in reshade log";

struct __declspec(uuid("038B03AA-4C75-443B-A695-752D80797037")) CommandListDataContainer {
    uint64_t activePixelShaderPipeline;
    uint64_t activeVertexShaderPipeline;
	uint64_t activeComputeShaderPipeline;
};

#define FRAMECOUNT_COLLECTION_PHASE_DEFAULT 250;
#define HASH_FILE_NAME	"ShaderToggler.ini"

static ShaderToggler::ShaderManager g_pixelShaderManager;
static ShaderToggler::ShaderManager g_vertexShaderManager;
static ShaderToggler::ShaderManager g_computeShaderManager;
static KeyData g_keyCollector;
static atomic_uint32_t g_activeCollectorFrameCounter = 0;
static std::vector<ToggleGroup> g_toggleGroups;
static atomic_int g_toggleGroupIdKeyBindingEditing = -1;
static atomic_int g_toggleGroupIdShaderEditing = -1;
static float g_overlayOpacity = 1.0f;
static int g_startValueFramecountCollectionPhase = FRAMECOUNT_COLLECTION_PHASE_DEFAULT;
static std::string g_iniFileName = "";

/// contains shader code to override ouput
static thread_local std::vector<std::vector<uint8_t>> s_constant_color;

/// resource for injecting cb13
reshade::api::pipeline_layout cb_inject_layout;
resource cb_inject_ressource;

uint64_t cb_inject_size = CBSIZE;
float cb_inject_values[CBSIZE];


/// contains cloned pipeline, used to replace the selected pipeline by the "constant color" PS shader
std::unordered_map<uint64_t, reshade::api::pipeline> pipelineCloneMap;

struct ClonedShader {
	reshade::api::pipeline pipeline;
	bool initialized = false;
} colorPSShader;

// GUI variable to pass to cb (or not)
static int constant_color = false;
static int draw_to_trace = 1;


/// <summary>
/// Calculates a crc32 hash from the passed in shader bytecode. The hash is used to identity the shader in future runs.
/// </summary>
/// <param name="shaderData"></param>
/// <returns></returns>

static uint32_t calculateShaderHash(void* shaderData)
{
	if(nullptr==shaderData)
	{
		return 0;
	}

	const auto shaderDesc = *static_cast<shader_desc *>(shaderData);
	return compute_crc32(static_cast<const uint8_t *>(shaderDesc.code), shaderDesc.code_size);
}


/// <summary>
/// Adds a default group with VK_CAPITAL as toggle key. Only used if there aren't any groups defined in the ini file.
/// </summary>
void addDefaultGroup()
{
	ToggleGroup toAdd("Default", ToggleGroup::getNewGroupId());
	toAdd.setToggleKey(VK_CAPITAL, false, false, false);
	g_toggleGroups.push_back(toAdd);
}


/// <summary>
/// Loads the defined hashes and groups from the shaderToggler.ini file.
/// </summary>
void loadShaderTogglerIniFile()
{
	// Will assume it's started at the start of the application and therefore no groups are present.
	CDataFile iniFile;
	if(!iniFile.Load(g_iniFileName))
	{
		// not there
		return;
	}
	int groupCounter = 0;
	const int numberOfGroups = iniFile.GetInt("AmountGroups", "General");
	if(numberOfGroups==INT_MIN)
	{
		// old format file?
		addDefaultGroup();
		groupCounter=-1;	// enforce old format read for pre 1.0 ini file.
	}
	else
	{
		for(int i=0;i<numberOfGroups;i++)
		{
			g_toggleGroups.push_back(ToggleGroup("", ToggleGroup::getNewGroupId()));
		}
	}
	for(auto& group: g_toggleGroups)
	{
		group.loadState(iniFile, groupCounter);		// groupCounter is normally 0 or greater. For when the old format is detected, it's -1 (and there's 1 group).
		groupCounter++;
	}
}


/// <summary>
/// Saves the currently known toggle groups with their shader hashes to the shadertoggler.ini file
/// </summary>
void saveShaderTogglerIniFile()
{
	// format: first section with # of groups, then per group a section with pixel and vertex shaders, as well as their name and key value.
	// groups are stored with "Group" + group counter, starting with 0.
	CDataFile iniFile;
	iniFile.SetInt("AmountGroups", g_toggleGroups.size(), "",  "General");

	int groupCounter = 0;
	for(const auto& group: g_toggleGroups)
	{
		group.saveState(iniFile, groupCounter);
		groupCounter++;
	}
	iniFile.SetFileName(g_iniFileName);
	iniFile.Save();
}


static void onInitCommandList(command_list *commandList)
{
	commandList->create_private_data<CommandListDataContainer>();
}


static void onDestroyCommandList(command_list *commandList)
{
	commandList->destroy_private_data<CommandListDataContainer>();
}

static void onResetCommandList(command_list *commandList)
{
	CommandListDataContainer &commandListData = commandList->get_private_data<CommandListDataContainer>();
	commandListData.activePixelShaderPipeline = -1;
	commandListData.activeVertexShaderPipeline = -1;
	commandListData.activeComputeShaderPipeline = -1;
}


static void onInitPipeline(device *device, pipeline_layout layout, uint32_t subobjectCount, const pipeline_subobject *subobjects, pipeline pipelineHandle)
{
	
	bool isPixelShader = false;
	
	// logging infos
	std::stringstream s;
	s << "onInitPipeline, pipelineHandle: " << (void*)pipelineHandle.handle << "), ";
	s << "layout =  " << reinterpret_cast<void*>(layout.handle) << " ;";
	// always "FFFF*" for pipelineHandle.handle...
	reshade::log_message(reshade::log_level::info, s.str().c_str());

	// shader has been created, we will now create a hash and store it with the handle we got.
	for (uint32_t i = 0; i < subobjectCount; ++i)
	{
		switch (subobjects[i].type)
		{
			case pipeline_subobject_type::vertex_shader:
				g_vertexShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
				break;
			case pipeline_subobject_type::pixel_shader:
				g_pixelShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
				isPixelShader = true;
				break;
			case pipeline_subobject_type::compute_shader:
				g_computeShaderManager.addHashHandlePair(calculateShaderHash(subobjects[i].data), pipelineHandle.handle);
				break;
		}
	}

	//create a new pipeline by cloning the first PS shader
	if (isPixelShader) {

		// read color shader if not done
		if (s_constant_color.size() == 0) {
			// Shader code loaded here
			wchar_t filename[] = COLOR_SHADER_NAME;
			bool status = load_shader_code(s_constant_color, filename);
		}

		// if not done, clone the pipeline to have a new version with fixed color for PS
		clone_pipeline(device, cb_inject_layout, subobjectCount, subobjects, pipelineHandle, s_constant_color, pipelineCloneMap);

		/* s << "onInitPipeline, color pipeline : " << (void*)colorPSShader.pipeline.handle << ") cloned from ("
			<< (void*)pipelineHandle.handle << ")";
		reshade::log_message(reshade::log_level::info, s.str().c_str()); */
	}
}

static void onDestroyPipeline(device *device, pipeline pipelineHandle)
{
	g_pixelShaderManager.removeHandle(pipelineHandle.handle);
	g_vertexShaderManager.removeHandle(pipelineHandle.handle);
	g_computeShaderManager.removeHandle(pipelineHandle.handle);

	std::stringstream s;
	s << "on_destroy_pipeline("
		<< reinterpret_cast<void*>(pipelineHandle.handle)
		<< ")";
	//reshade::log_message(reshade::log_level::info, s.str().c_str());

	// suppress cloned pipeline 
	if (colorPSShader.initialized) {
		device->destroy_pipeline(colorPSShader.pipeline);
		colorPSShader.initialized = false;

		s << "suppress cloned pipeline";
		reshade::log_message(reshade::log_level::info, s.str().c_str());
	}

}

/// <summary>
/// Imported from reshade example shader_dump_addon.cpp
/// </summary>

static void save_shader_code(device_api device_type, const shader_desc &desc)
{
	if (desc.code_size == 0)
		return;

	uint32_t shader_hash = compute_crc32(static_cast<const uint8_t *>(desc.code), desc.code_size);

	const wchar_t *extension = L".cso";
	if (device_type == device_api::vulkan || (
		device_type == device_api::opengl && desc.code_size > sizeof(uint32_t) && *static_cast<const uint32_t *>(desc.code) == 0x07230203 /* SPIR-V magic value */))
		extension = L".spv"; // Vulkan uses SPIR-V (and sometimes OpenGL does too)
	else if (device_type == device_api::opengl)
		extension = L".glsl"; // OpenGL otherwise uses plain text GLSL

	// Prepend executable directory to image files
	wchar_t file_prefix[MAX_PATH] = L"";
	GetModuleFileNameW(nullptr, file_prefix, ARRAYSIZE(file_prefix));

	std::filesystem::path dump_path = file_prefix;
	dump_path  = dump_path.parent_path();
	dump_path /= RESHADE_ADDON_SHADER_SAVE_DIR;

	if (std::filesystem::exists(dump_path) == false)
		std::filesystem::create_directory(dump_path);

	wchar_t hash_string[11];
	swprintf_s(hash_string, L"0x%08X", shader_hash);

	dump_path /= hash_string;
	dump_path += extension;

	std::ofstream file(dump_path, std::ios::binary);
	file.write(static_cast<const char *>(desc.code), desc.code_size);
}

// to display debug infos, taken from clshortfuse renodx repo

// to display infos on pipeline_layout, taken from clshortfuse renodx repo
static void logLayout(
	const uint32_t paramCount,
	const reshade::api::pipeline_layout_param* params,
	const reshade::api::pipeline_layout layout
) {
	
	for (uint32_t paramIndex = 0; paramIndex < paramCount; ++paramIndex) {
		auto param = params[paramIndex];
		if (param.type == reshade::api::pipeline_layout_param_type::descriptor_table) {
			for (uint32_t rangeIndex = 0; rangeIndex < param.descriptor_table.count; ++rangeIndex) {
				auto range = param.descriptor_table.ranges[rangeIndex];
				std::stringstream s;
				s << "logPipelineLayout("
					<< reinterpret_cast<void*>(layout.handle) << "[" << paramIndex << "]"
					<< " | TBL"
					<< " | " << reinterpret_cast<void*>(&param.descriptor_table.ranges)
					<< " | ";
				switch (range.type) {
				case reshade::api::descriptor_type::sampler:
					s << "SMP";
					break;
				case reshade::api::descriptor_type::sampler_with_resource_view:
					s << "SMPRV";
					break;
				case reshade::api::descriptor_type::texture_shader_resource_view:
					s << "TSRV";
					break;
				case reshade::api::descriptor_type::texture_unordered_access_view:
					s << "TUAV";
					break;
				case reshade::api::descriptor_type::constant_buffer:
					s << "CBV";
					break;
				case reshade::api::descriptor_type::shader_storage_buffer:
					s << "SSB";
					break;
				case reshade::api::descriptor_type::acceleration_structure:
					s << "ACC";
					break;
				default:
					s << "??? (0x" << std::hex << (uint32_t)range.type << std::dec << ")";
				}

				s << ", array_size: " << range.array_size
					<< ", binding: " << range.binding
					<< ", count: " << range.count
					<< ", register: " << range.dx_register_index
					<< ", space: " << range.dx_register_space
					<< ", visibility: " << to_string(range.visibility)
					<< ")"
					<< " [" << rangeIndex << "/" << param.descriptor_table.count << "]";
				reshade::log_message(reshade::log_level::info, s.str().c_str());
			}
		}
		else if (param.type == reshade::api::pipeline_layout_param_type::push_constants) {
			std::stringstream s;
			s << "logPipelineLayout("
				<< reinterpret_cast<void*>(layout.handle) << "[" << paramIndex << "]"
				<< " | PC"
				<< ", binding: " << param.push_constants.binding
				<< ", count " << param.push_constants.count
				<< ", register: " << param.push_constants.dx_register_index
				<< ", space: " << param.push_constants.dx_register_space
				<< ", visibility " << to_string(param.push_constants.visibility)
				<< ")";
			reshade::log_message(reshade::log_level::info, s.str().c_str());
		}
		else if (param.type == reshade::api::pipeline_layout_param_type::push_descriptors) {
			std::stringstream s;
			s << "logPipelineLayout("
				<< reinterpret_cast<void*>(layout.handle) << "[" << paramIndex << "]"
				<< " | PD |"
				<< " array_size: " << param.push_descriptors.array_size
				<< ", binding: " << param.push_descriptors.binding
				<< ", count " << param.push_descriptors.count
				<< ", register: " << param.push_descriptors.dx_register_index
				<< ", space: " << param.push_descriptors.dx_register_space
				<< ", type: " << to_string(param.push_descriptors.type)
				<< ", visibility " << to_string(param.push_descriptors.visibility)
				<< ")";
			reshade::log_message(reshade::log_level::info, s.str().c_str());
		}
		else if (param.type == reshade::api::pipeline_layout_param_type::push_descriptors_with_ranges) {
			std::stringstream s;
			s << "logPipelineLayout("
				<< reinterpret_cast<void*>(layout.handle) << "[" << paramIndex << "]"
				<< " | PDR?? | "
				<< ")";
			reshade::log_message(reshade::log_level::info, s.str().c_str());
#if RESHADE_API_VERSION >= 13
		}
		else if (param.type == reshade::api::pipeline_layout_param_type::descriptor_table_with_static_samplers) {
			for (uint32_t rangeIndex = 0; rangeIndex < param.descriptor_table_with_static_samplers.count; ++rangeIndex) {
				auto range = param.descriptor_table_with_static_samplers.ranges[rangeIndex];
				std::stringstream s;
				s << "logPipelineLayout("
					<< reinterpret_cast<void*>(layout.handle) << "[" << paramIndex << "]"
					<< " | TBLSS"
					<< " | " << reinterpret_cast<void*>(&param.descriptor_table.ranges)
					<< " | ";
				if (range.static_samplers == nullptr) {
					s << " null ";
				}
				else {
					s << ", filter: " << (uint32_t)range.static_samplers->filter;
					s << ", address_u: " << (uint32_t)range.static_samplers->address_u;
					s << ", address_v: " << (uint32_t)range.static_samplers->address_v;
					s << ", address_w: " << (uint32_t)range.static_samplers->address_w;
					s << ", mip_lod_bias: " << (uint32_t)range.static_samplers->mip_lod_bias;
					s << ", max_anisotropy: " << (uint32_t)range.static_samplers->max_anisotropy;
					s << ", compare_op: " << (uint32_t)range.static_samplers->compare_op;
					s << ", border_color: [" << range.static_samplers->border_color[0] << ", " << range.static_samplers->border_color[1] << ", " << range.static_samplers->border_color[2] << ", " << range.static_samplers->border_color[3] << "]";
					s << ", min_lod: " << range.static_samplers->min_lod;
					s << ", max_lod: " << range.static_samplers->max_lod;
				}
				reshade::log_message(reshade::log_level::info, s.str().c_str());
			}
		}
		else if (param.type == reshade::api::pipeline_layout_param_type::push_descriptors_with_static_samplers) {
			for (uint32_t rangeIndex = 0; rangeIndex < param.descriptor_table.count; ++rangeIndex) {
				auto range = param.descriptor_table_with_static_samplers.ranges[rangeIndex];
				std::stringstream s;
				s << "logPipelineLayout("
					<< reinterpret_cast<void*>(layout.handle) << "[" << paramIndex << "]"
					<< " | PDSS"
					<< " | " << reinterpret_cast<void*>(&range)
					<< " | ";
				if (range.static_samplers == nullptr) {
					s << "not";
				}
				else {
					s << "filter: " << (uint32_t)range.static_samplers->filter
						<< ", address_u: " << (uint32_t)range.static_samplers->address_u
						<< ", address_v: " << (uint32_t)range.static_samplers->address_v
						<< ", address_w: " << (uint32_t)range.static_samplers->address_w
						<< ", mip_lod_bias: " << (uint32_t)range.static_samplers->mip_lod_bias
						<< ", max_anisotropy: " << (uint32_t)range.static_samplers->max_anisotropy
						<< ", compare_op: " << (uint32_t)range.static_samplers->compare_op
						<< ", border_color: [" << range.static_samplers->border_color[0] << ", " << range.static_samplers->border_color[1] << ", " << range.static_samplers->border_color[2] << ", " << range.static_samplers->border_color[3] << "]"
						<< ", min_lod: " << range.static_samplers->min_lod
						<< ", max_lod: " << range.static_samplers->max_lod;
				}
				s << ")"
					<< " [" << rangeIndex << "/" << param.descriptor_table.count << "]";
				reshade::log_message(reshade::log_level::info, s.str().c_str());
			}
#endif
		}
		else {
			std::stringstream s;
			s << "logPipelineLayout("
				<< reinterpret_cast<void*>(layout.handle) << "[" << paramIndex << "]"
				<< " | ??? (0x" << std::hex << (uint32_t)param.type << std::dec << ")"
				<< " | " << to_string(param.type)
				<< ")";
			reshade::log_message(reshade::log_level::info, s.str().c_str());
		}
	}
}

// initialize the resources to be injected in shaders
// log infos and setup the global variables for CB and RV
static void on_init_pipeline_layout(
		reshade::api::device * device,
		const uint32_t paramCount,
		const reshade::api::pipeline_layout_param * params,
		reshade::api::pipeline_layout layout
	) {
		
	    // auto &global_data = device->get_private_data<global_shared>();

		logLayout(paramCount, params, layout);

		std::stringstream s;

		bool foundVisiblity = false;
		uint32_t cbvIndex = 0;
		uint32_t pcCount = 0;

		// log infos and setup index for CB and SV
		for (uint32_t paramIndex = 0; paramIndex < paramCount; ++paramIndex) {
			auto param = params[paramIndex];
			if (param.type == reshade::api::pipeline_layout_param_type::descriptor_table) {
				for (uint32_t rangeIndex = 0; rangeIndex < param.descriptor_table.count; ++rangeIndex) {
					auto range = param.descriptor_table.ranges[rangeIndex];
					if (range.type == reshade::api::descriptor_type::constant_buffer) {
						if (cbvIndex < range.dx_register_index + range.count) {
							cbvIndex = range.dx_register_index + range.count;
						}
					}
				}
			}
			else if (param.type == reshade::api::pipeline_layout_param_type::push_constants) {
				pcCount++;
				if (cbvIndex < param.push_constants.dx_register_index + param.push_constants.count) {
					cbvIndex = param.push_constants.dx_register_index + param.push_constants.count;
				}
			}
			else if (param.type == reshade::api::pipeline_layout_param_type::push_descriptors) {
				if (param.push_descriptors.type == reshade::api::descriptor_type::constant_buffer) {
					if (cbvIndex < param.push_descriptors.dx_register_index + param.push_descriptors.count) {
						cbvIndex = param.push_descriptors.dx_register_index + param.push_descriptors.count;
					}
				}
			}
		}
		uint32_t maxCount = 64u - (paramCount + 1u) + 1u;


		// ********************************
		// generate infos for CB injections
		// 
		// store pipeline layout to re use it in "push descriptor"
		shared_data.saved_pipeline_layout = layout;
		shared_data.saved_device = device;

		// generate data for constant_buffer or shader_resource_view
		for (uint32_t paramIndex = 0; paramIndex < paramCount; ++paramIndex) {
			auto param = params[paramIndex];
			s << "* looping on  paramCount : param = " << to_string(paramIndex) << ", param.type = " << to_string(param.type) <<", param.push_descriptors.type = " << to_string(param.push_descriptors.type);
			reshade::log_message(reshade::log_level::info, s.str().c_str());
			s.str("");
			s.clear();
			if (param.push_descriptors.type == descriptor_type::constant_buffer)
			{	
				// store info for CB injection
				shared_data.CBIndex = paramIndex;
				
				// create descriptor table to re use it in "push descriptor"
				//this line make map_buffer_region() output at 0
				//bool create_result = device->create_resource(resource_desc(CBSIZE * sizeof(float), memory_heap::cpu_to_gpu, resource_usage::constant_buffer), nullptr, resource_usage::cpu_access, &shared_data.resource_desc_CB);
				
				bool create_result = device->create_resource(resource_desc(CBSIZE * sizeof(float), memory_heap::cpu_to_gpu, resource_usage::cpu_access), nullptr, resource_usage::cpu_access, &shared_data.resource_desc_CB);
				s << "!!! create_ressource=" << to_string(create_result);
				reshade::log_message(reshade::log_level::info, s.str().c_str());
				s.str("");
				s.clear();

				shared_data.CB_desc_table_update.binding = CBINDEX;
				shared_data.CB_desc_table_update.count = 1;
				shared_data.CB_desc_table_update.type = descriptor_type::constant_buffer;
				shared_data.CB_desc_table_update.descriptors = &shared_data.resource_desc_CB;
				// shared_data.CB_desc_table_update.table = param.descriptor_table.ranges;

				// map the constant buffer to the resource
				uint64_t offset = 0;
				uint64_t size = CBSIZE * sizeof(float);

				//associate the desc_table_update with the mapping
				/*
				subresource_data subres = {};
				// bool map_result = device->map_buffer_region(test, 0, UINT64_MAX, map_access::write_only, &subres.data);
				bool map_result = device->map_buffer_region(shared_data.resource_desc_CB, 0, UINT64_MAX, map_access::write_only, &subres.data);
				s << "!!! map_ressource=" << to_string(map_result);
				reshade::log_message(reshade::log_level::info, s.str().c_str());
				s.str("");
				s.clear();

				// try to write things in the buffer
				auto mapped_data = static_cast<float*>(subres.data);
				mapped_data[0] = 1.0;
				mapped_data[1] = 1.0;

				device->unmap_buffer_region(shared_data.resource_desc_CB);

				s << "!!!  data written to buffer !!! ";
				reshade::log_message(reshade::log_level::info, s.str().c_str());
				s.str("");
				s.clear(); */

			}
			else if (param.push_descriptors.type == descriptor_type::shader_resource_view)
			{
				// store info Ressource View injection
				shared_data.RVIndex = paramIndex;
			}

		}

		// std::stringstream s;
		s << "on_init_pipeline_layout++("
			<< reinterpret_cast<void*>(layout.handle)
			<< " , max injections: " << (maxCount)
			<< " )";
		reshade::log_message(reshade::log_level::info, s.str().c_str());
}

static bool on_create_pipeline_layout(
	reshade::api::device* device,
	uint32_t& param_count,
	reshade::api::pipeline_layout_param*& params) 
{
	std::stringstream s;

	// s << "on_create_pipeline_layout(" << " param count: " << param_count << ")";
	s << "on_create_pipeline_layout()";
	reshade::log_message(reshade::log_level::info, s.str().c_str());
}

// create the container for global shared data in private_data of device
static void on_init_device(device* device)
{
	
	std::stringstream s;
	s << "init_device("
		<< reinterpret_cast<void*>(device)
		<< ")";
	reshade::log_message(reshade::log_level::info, s.str().c_str());
	
	//to be defined if usefull...
	device->create_private_data<global_shared>();
}
static void on_destroy_device(device* device)
{
	std::stringstream s;
	s << "destroy_device("
		<< reinterpret_cast<void*>(device)
		<< ")";
	reshade::log_message(reshade::log_level::info, s.str().c_str());

	device->destroy_private_data<global_shared>();
}

static void on_init_swapchain(reshade::api::swapchain* swapchain) {
	const size_t backBufferCount = swapchain->get_back_buffer_count();

	for (uint32_t index = 0; index < backBufferCount; index++) {
		auto buffer = swapchain->get_back_buffer(index);

		std::stringstream s;
		s << "init_swapchain("
			<< "buffer:" << reinterpret_cast<void*>(buffer.handle)
			<< ")";
		reshade::log_message(reshade::log_level::info, s.str().c_str());
	}

	std::stringstream s;
	s << "init_swapchain"
		// << "(colorspace: " << to_string(swapchain->get_color_space())
		<< "(colorspace: "
		<< ")";
	reshade::log_message(reshade::log_level::info, s.str().c_str());
}

static void on_init_resource(
	reshade::api::device* device,
	const reshade::api::resource_desc& desc,
	const reshade::api::subresource_data* initial_data,
	reshade::api::resource_usage initial_state,
	reshade::api::resource resource
) {

	bool warn = false;
	std::stringstream s;
	s << "init_resource(";
	s << reinterpret_cast<void*>(resource.handle);
	s << ", flags: " << std::hex << (uint32_t)desc.flags << std::dec
		<< ", state: " << std::hex << (uint32_t)initial_state << std::dec
		<< ", type: " << to_string(desc.type)
		<< ", usage: " << std::hex << (uint32_t)desc.usage << std::dec;

	switch (desc.type) {
	case reshade::api::resource_type::buffer:
		s << ", size: " << desc.buffer.size;
		s << ", stride: " << desc.buffer.stride;
		break;
	case reshade::api::resource_type::texture_1d:
	case reshade::api::resource_type::texture_2d:
	case reshade::api::resource_type::texture_3d:
	case reshade::api::resource_type::surface:
		s << ", width: " << desc.texture.width
			<< ", height: " << desc.texture.height
			<< ", levels: " << desc.texture.levels
		    << ", format: " << to_string(desc.texture.format);
			//<< ", format: " ;
		if (desc.texture.format == reshade::api::format::unknown) {
			warn = true;
		}
		break;
	default:
	case reshade::api::resource_type::unknown:
		break;
	}

	s << ")";
	reshade::log_message(
		warn
		? reshade::log_level::warning
		: reshade::log_level::info,
		s.str().c_str()
	);
}

static void on_init_resource_view(
	reshade::api::device* device,
	reshade::api::resource resource,
	reshade::api::resource_usage usage_type,
	const reshade::api::resource_view_desc& desc,
	reshade::api::resource_view view
) {
	/* auto& data = device->get_private_data<device_data>();
	std::unique_lock lock(data.mutex); 
	if (data.resourceViews.contains(view.handle)) {
		if (traceRunning || presentCount < MAX_PRESENT_COUNT) {
			std::stringstream s;
			s << "init_resource_view(reused view: "
				<< reinterpret_cast<void*>(view.handle)
				<< ")";
			reshade::log_message(reshade::log_level::info, s.str().c_str());
		}
		if (!resource.handle) {
			data.resourceViews.erase(view.handle);
			return;
		}
	}
	if (resource.handle) {
		data.resourceViews.emplace(view.handle, resource.handle);
	}

	if (!forceAll && !traceRunning && presentCount >= MAX_PRESENT_COUNT) return; */
	std::stringstream s;
	s << "init_resource_view("
		<< reinterpret_cast<void*>(view.handle)
		<< ", view type: " << to_string(desc.type) << " (0x" << std::hex << (uint32_t)desc.type << std::dec << ")"
		<< ", view format: " << to_string(desc.format) << " (0x" << std::hex << (uint32_t)desc.format << std::dec << ")"
		<< ", resource: " << reinterpret_cast<void*>(resource.handle)
		<< ", resource usage: " << to_string(usage_type) << " 0x" << std::hex << (uint32_t)usage_type << std::dec;
	// if (desc.type == reshade::api::resource_view_type::buffer) return;
	if (resource.handle) {
		const auto resourceDesc = device->get_resource_desc(resource);
		s << ", resource type: " << to_string(resourceDesc.type);

		switch (resourceDesc.type) {
		default:
		case reshade::api::resource_type::unknown:
			break;
		case reshade::api::resource_type::buffer:
			// if (!traceRunning) return;
			return;
			s << ", buffer offset: " << desc.buffer.offset;
			s << ", buffer size: " << desc.buffer.size;
			break;
		case reshade::api::resource_type::texture_1d:
		case reshade::api::resource_type::texture_2d:
		case reshade::api::resource_type::surface:
			s << ", texture format: " << to_string(resourceDesc.texture.format);
			s << ", texture width: " << resourceDesc.texture.width;
			s << ", texture height: " << resourceDesc.texture.height;
			break;
		case reshade::api::resource_type::texture_3d:
			s << ", texture format: " << to_string(resourceDesc.texture.format);
			s << ", texture width: " << resourceDesc.texture.width;
			s << ", texture height: " << resourceDesc.texture.height;
			s << ", texture depth: " << resourceDesc.texture.depth_or_layers;
			break;
		}
	}
	s << ")";
	reshade::log_message(reshade::log_level::info, s.str().c_str());
}

static void on_destroy_resource(reshade::api::device* device, reshade::api::resource resource) {
	std::stringstream s;
	s << "on_destroy_resource("
		<< reinterpret_cast<void*>(resource.handle)
		<< ")";
	reshade::log_message(reshade::log_level::debug, s.str().c_str());
}

static void on_destroy_resource_view(reshade::api::device* device, reshade::api::resource_view view) {
	std::stringstream s;
	s << "on_destroy_resource_view("
		<< reinterpret_cast<void*>(view.handle)
		<< ")";
	reshade::log_message(reshade::log_level::debug, s.str().c_str());

	/* auto& data = device->get_private_data<device_data>();
	std::unique_lock lock(data.mutex);
	data.resourceViews.erase(view.handle); */
}

//********************************

void on_bind_descriptor_tables(
	reshade::api::command_list* cmd_list,
	reshade::api::shader_stage stages,
	reshade::api::pipeline_layout layout,
	uint32_t first,
	uint32_t count,
	const reshade::api::descriptor_table* tables
) {
	std::stringstream s;
	s << "on_bind_descriptor_tables()";
	reshade::log_message(reshade::log_level::info, s.str().c_str());
}

static bool on_create_pipeline(device *device, pipeline_layout, uint32_t subobject_count, const pipeline_subobject *subobjects)
{
	const device_api device_type = device->get_api();

	// Go through all shader stages that are in this pipeline and dump the associated shader code
	for (uint32_t i = 0; i < subobject_count; ++i)
	{
		switch (subobjects[i].type)
		{
		case pipeline_subobject_type::vertex_shader:
		case pipeline_subobject_type::hull_shader:
		case pipeline_subobject_type::domain_shader:
		case pipeline_subobject_type::geometry_shader:
		case pipeline_subobject_type::pixel_shader:
		case pipeline_subobject_type::compute_shader:
		case pipeline_subobject_type::amplification_shader:
		case pipeline_subobject_type::mesh_shader:
		case pipeline_subobject_type::raygen_shader:
		case pipeline_subobject_type::any_hit_shader:
		case pipeline_subobject_type::closest_hit_shader:
		case pipeline_subobject_type::miss_shader:
		case pipeline_subobject_type::intersection_shader:
		case pipeline_subobject_type::callable_shader:
			save_shader_code(device_type, *static_cast<const shader_desc *>(subobjects[i].data));
			break;
		}
	}

	// logging infos
	// std::stringstream s;
	// s << "On_create_pipeline  " ;
	// reshade::log_message(reshade::log_level::info, s.str().c_str());


	return false;
}
/// End of example shader_dump_addon.cpp

/// <summary>
/// This function will return true if the command list specified has one or more shader hashes which are currently marked to be hidden. Otherwise false.
/// </summary>
/// <param name="commandList"></param>
/// <returns>true if the draw call has to be blocked</returns>
bool blockDrawCallForCommandList(command_list* commandList)
{
	if (nullptr == commandList)
	{
		return false;
	}

	const CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
	uint32_t shaderHash = g_pixelShaderManager.getShaderHash(commandListData.activePixelShaderPipeline);
	bool blockCall = g_pixelShaderManager.isBlockedShader(shaderHash);
	for (auto& group : g_toggleGroups)
	{
		blockCall |= group.isBlockedPixelShader(shaderHash);
	}
	shaderHash = g_vertexShaderManager.getShaderHash(commandListData.activeVertexShaderPipeline);
	blockCall |= g_vertexShaderManager.isBlockedShader(shaderHash);
	for (auto& group : g_toggleGroups)
	{
		blockCall |= group.isBlockedVertexShader(shaderHash);
	}
	shaderHash = g_computeShaderManager.getShaderHash(commandListData.activeComputeShaderPipeline);
	blockCall |= g_computeShaderManager.isBlockedShader(shaderHash);
	for (auto& group : g_toggleGroups)
	{
		blockCall |= group.isBlockedComputeShader(shaderHash);
	}
	return blockCall;
}




static void onBindPipeline(command_list* commandList, pipeline_stage stages, pipeline pipelineHandle)
{
	
	uint64_t shaderHash; 
	
	if(nullptr != commandList && pipelineHandle.handle != 0)
	{
		const bool handleHasPixelShaderAttached = g_pixelShaderManager.isKnownHandle(pipelineHandle.handle);
		const bool handleHasVertexShaderAttached = g_vertexShaderManager.isKnownHandle(pipelineHandle.handle);
		const bool handleHasComputeShaderAttached = g_computeShaderManager.isKnownHandle(pipelineHandle.handle);
		if(!handleHasPixelShaderAttached && !handleHasVertexShaderAttached && !handleHasComputeShaderAttached)
		{
			// draw call with unknown handle, don't collect it
			return;
		}
		CommandListDataContainer& commandListData = commandList->get_private_data<CommandListDataContainer>();
		

		if (handleHasPixelShaderAttached) shaderHash = g_pixelShaderManager.getShaderHash(pipelineHandle.handle);		
		if (handleHasVertexShaderAttached) shaderHash = g_vertexShaderManager.getShaderHash(pipelineHandle.handle);
		if (handleHasComputeShaderAttached) shaderHash = g_computeShaderManager.getShaderHash(pipelineHandle.handle);


		// always do the following code as that has to run for every bind on a pipeline:
		if(g_activeCollectorFrameCounter > 0)
		{
			// in collection mode
			if(handleHasPixelShaderAttached)
			{
				g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
			}
			if(handleHasVertexShaderAttached)
			{
				g_vertexShaderManager.addActivePipelineHandle(pipelineHandle.handle);
			}
			if(handleHasComputeShaderAttached)
			{
				g_computeShaderManager.addActivePipelineHandle(pipelineHandle.handle);
			}
		}
		else
		{
			commandListData.activePixelShaderPipeline = handleHasPixelShaderAttached ? pipelineHandle.handle : commandListData.activePixelShaderPipeline;
			commandListData.activeVertexShaderPipeline = handleHasVertexShaderAttached ? pipelineHandle.handle : commandListData.activeVertexShaderPipeline;
			commandListData.activeComputeShaderPipeline = handleHasComputeShaderAttached ? pipelineHandle.handle : commandListData.activeComputeShaderPipeline;
		}
		if ((stages & pipeline_stage::pixel_shader) == pipeline_stage::pixel_shader)
		{
			if(handleHasPixelShaderAttached)
			{
				if(g_activeCollectorFrameCounter > 0)
				{
					// in collection mode
					g_pixelShaderManager.addActivePipelineHandle(pipelineHandle.handle);
				}
				commandListData.activePixelShaderPipeline = pipelineHandle.handle;
			}
		}
		if((stages & pipeline_stage::vertex_shader) == pipeline_stage::vertex_shader)
		{
			if(handleHasVertexShaderAttached)
			{
				if(g_activeCollectorFrameCounter > 0)
				{
					// in collection mode
					g_vertexShaderManager.addActivePipelineHandle(pipelineHandle.handle);
				}
				commandListData.activeVertexShaderPipeline = pipelineHandle.handle;
			}
		}
		if((stages & pipeline_stage::compute_shader) == pipeline_stage::compute_shader)
		{
			if(handleHasComputeShaderAttached)
			{
				if(g_activeCollectorFrameCounter > 0)
				{
					// in collection mode
					g_computeShaderManager.addActivePipelineHandle(pipelineHandle.handle);
				}
				commandListData.activeComputeShaderPipeline = pipelineHandle.handle;
			}
		}

		// replace the shader by the cloned one if it is in the blocked list and a PS and modify its parameter to include cb13
		if (blockDrawCallForCommandList(commandList) && handleHasPixelShaderAttached && constant_color) 
		{
			std::stringstream s;
			//clone pipeline
			auto pipelineCloned = pipelineCloneMap.find(pipelineHandle.handle);
			if (pipelineCloned != pipelineCloneMap.end()) {

				subresource_data subres = {};
				bool map_result = commandList->get_device()->map_buffer_region(shared_data.resource_desc_CB, 0, UINT64_MAX, map_access::write_only, &subres.data);
				s << "!!! map_ressource=" << to_string(map_result);
				reshade::log_message(reshade::log_level::info, s.str().c_str());
				s.str("");
				s.clear();

				// try to write things in the buffer
				auto mapped_data = static_cast<float*>(subres.data);
				mapped_data[0] = 1.0;
				mapped_data[1] = 1.0;

				commandList->get_device()->unmap_buffer_region(shared_data.resource_desc_CB);

				s << "!!!  data written to buffer !!! ";
				reshade::log_message(reshade::log_level::info, s.str().c_str());
				s.str("");
				s.clear(); 


				//push value for cb13
				reshade::api::shader_stage stage = reshade::api::shader_stage::pixel;
				commandList->push_descriptors(
					shader_stage::pixel,
					shared_data.saved_pipeline_layout,
					shared_data.CBIndex,
					shared_data.CB_desc_table_update);

				s << "!!! new descriptor pushed !!! "
					<< reinterpret_cast<void*>(pipelineHandle.handle)
					<< ")";
				reshade::log_message(reshade::log_level::info, s.str().c_str());
				s.str("");
				s.clear();
			
				//replace pipeline by the clone
				auto newPipeline = pipelineCloned->second;
				commandList->bind_pipeline(stages, newPipeline);
				s << "pipeline Pixel replaced ("
					<< reinterpret_cast<void*>(pipelineHandle.handle)
					<< ")";

				reshade::log_message(reshade::log_level::info, s.str().c_str());
				s.str("");
				s.clear();
			}
		}
	}

	if (s_do_capture) {
		std::stringstream s;
		s << "bind_pipeline(" << to_string(stages)<< " : " << (void *)shaderHash << ", pipelineHandle: " << (void *)pipelineHandle.handle << ")";

		reshade::log_message(reshade::log_level::info, s.str().c_str());
	}
}




static bool onDraw(command_list* commandList, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	
	if (s_do_capture)
	{
		std::stringstream s;
		s << "draw(" << vertex_count << ", " << instance_count << ", " << first_vertex << ", " << first_instance << ")";

		reshade::log_message(reshade::log_level::info, s.str().c_str());
	}
	// check if for this command list the active shader handles are part of the blocked set. If so, return true
	if (!constant_color) 
		return blockDrawCallForCommandList(commandList);
	else
		return false;
}


static bool onDrawIndexed(command_list* commandList, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	if (s_do_capture)
	{
		std::stringstream s;
		s << "draw_indexed(" << index_count << ", " << instance_count << ", " << first_index << ", " << vertex_offset << ", " << first_instance << ")";

		reshade::log_message(reshade::log_level::info, s.str().c_str());
	}
	// same as onDraw
	if (!constant_color)
		return blockDrawCallForCommandList(commandList);
	else
		return false;
}


static bool onDrawOrDispatchIndirect(command_list* commandList, indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride)
{
	std::stringstream s;
	switch(type)
	{
		case indirect_command::unknown:
			if (s_do_capture) 
				s << "draw_or_dispatch_indirect(" << (void *)buffer.handle << ", " << offset << ", " << draw_count << ", " << stride << ")";
			break;
		case indirect_command::draw:
			if (s_do_capture) 
				s << "draw_indirect(" << (void *)buffer.handle << ", " << offset << ", " << draw_count << ", " << stride << ")";
			break;
		case indirect_command::draw_indexed:
			if (s_do_capture) 
				s << "draw_indexed_indirect(" << (void *)buffer.handle << ", " << offset << ", " << draw_count << ", " << stride << ")";
			break;
		case indirect_command::dispatch:
			if (s_do_capture) 
				s << "dispatch_indirect(" << (void *)buffer.handle << ", " << offset << ", " << draw_count << ", " << stride << ")";
			// same as OnDraw
			if (!constant_color)
				return blockDrawCallForCommandList(commandList);
		// the rest aren't blocked
		case indirect_command::dispatch_mesh:
			if (s_do_capture) 
				s << "dispatch_mesh_indirect(" << (void *)buffer.handle << ", " << offset << ", " << draw_count << ", " << stride << ")";
			break;
			
		case indirect_command::dispatch_rays:
			if (s_do_capture) 
					s << "dispatch_rays_indirect(" << (void *)buffer.handle << ", " << offset << ", " << draw_count << ", " << stride << ")";
			break;
	}
	if (s_do_capture) reshade::log_message(reshade::log_level::info, s.str().c_str());
	return false;
}


static void displayIsPartOfToggleGroup()
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
	ImGui::SameLine();
	ImGui::Text(" Shader is part of this toggle group.");
	ImGui::PopStyleColor();
}


static void displayShaderManagerInfo(ShaderManager& toDisplay, const char* shaderType)
{
	if(toDisplay.isInHuntingMode())
	{
		ImGui::Text("# of %s shaders active: %d. # of %s shaders in group: %d", shaderType, toDisplay.getAmountShaderHashesCollected(), shaderType, toDisplay.getMarkedShaderCount());
		ImGui::Text("Current selected %s shader: %d / %d.", shaderType, toDisplay.getActiveHuntedShaderIndex(), toDisplay.getAmountShaderHashesCollected());
		if(toDisplay.isHuntedShaderMarked())
		{
			displayIsPartOfToggleGroup();
		}
	}
}

static void displayShaderManagerStats(ShaderManager& toDisplay, const char* shaderType)
{
	ImGui::Text("# of pipelines with %s shaders: %d. # of different %s shaders gathered: %d.", shaderType, toDisplay.getPipelineCount(), shaderType, toDisplay.getShaderCount());
}


static void onReshadeOverlay(reshade::api::effect_runtime *runtime)
{
	if(g_toggleGroupIdShaderEditing>=0)
	{
		ImGui::SetNextWindowBgAlpha(g_overlayOpacity);
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		if (!ImGui::Begin("ShaderTogglerInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | 
														ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			return;
		}
		string editingGroupName = "";
		for(auto& group:g_toggleGroups)
		{
			if(group.getId()==g_toggleGroupIdShaderEditing)
			{
				editingGroupName = group.getName();
				break;
			}
		}
		
		displayShaderManagerStats(g_vertexShaderManager, "vertex");
		displayShaderManagerStats(g_pixelShaderManager, "pixel");
		displayShaderManagerStats(g_computeShaderManager, "compute");

		if(g_activeCollectorFrameCounter > 0)
		{
			const uint32_t counterValue = g_activeCollectorFrameCounter;
			ImGui::Text("Collecting active shaders... frames to go: %d", counterValue);
		}
		else
		{
			if(g_vertexShaderManager.isInHuntingMode() || g_pixelShaderManager.isInHuntingMode() || g_computeShaderManager.isInHuntingMode())
			{
				ImGui::Text("Editing the shaders for group: %s", editingGroupName.c_str());
			}
			displayShaderManagerInfo(g_vertexShaderManager, "vertex");
			displayShaderManagerInfo(g_pixelShaderManager, "pixel");
			displayShaderManagerInfo(g_computeShaderManager, "compute");
		}
		ImGui::End();
	}
}



static void onReshadePresent(effect_runtime* runtime)
{

	if (s_do_capture)
	{
		reshade::log_message(reshade::log_level::info, "present()");
		reshade::log_message(reshade::log_level::info, "--- End Frame ---");
		s_do_capture = false;
	}
	else
	{
		// The keyboard shortcut to trigger logging
		if (runtime->is_key_pressed(VK_F1))
		{
			s_do_capture = true;
			reshade::log_message(reshade::log_level::info, "--- Frame ---");
		}
	}


	if(g_activeCollectorFrameCounter>0)
	{
		--g_activeCollectorFrameCounter;
	}

	for(auto& group: g_toggleGroups)
	{
		if(group.isToggleKeyPressed(runtime))
		{
			group.toggleActive();
			// if the group's shaders are being edited, it should toggle the ones currently marked.
			if(group.getId() == g_toggleGroupIdShaderEditing)
			{
				g_vertexShaderManager.toggleHideMarkedShaders();
				g_pixelShaderManager.toggleHideMarkedShaders();
				g_computeShaderManager.toggleHideMarkedShaders();
			}
		}
	}

	// hardcoded hunting keys.
	// If Ctrl is pressed too, it'll step to the next marked shader (if any)
	// Numpad 1: previous pixel shader
	// Numpad 2: next pixel shader
	// Numpad 3: mark current pixel shader as part of the toggle group
	// Numpad 4: previous vertex shader
	// Numpad 5: next vertex shader
	// Numpad 6: mark current vertex shader as part of the toggle group
	// Numpad 7: previous compute shader
	// Numpad 8: next compute shader
	// Numpad 9: mark current compute shader as part of the toggle group
	if(runtime->is_key_pressed(VK_NUMPAD1))
	{
		g_pixelShaderManager.huntPreviousShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD2))
	{
		g_pixelShaderManager.huntNextShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD3))
	{
		g_pixelShaderManager.toggleMarkOnHuntedShader();
	}
	if(runtime->is_key_pressed(VK_NUMPAD4))
	{
		g_vertexShaderManager.huntPreviousShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD5))
	{
		g_vertexShaderManager.huntNextShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD6))
	{
		g_vertexShaderManager.toggleMarkOnHuntedShader();
	}
	if(runtime->is_key_pressed(VK_NUMPAD7))
	{
		g_computeShaderManager.huntPreviousShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD8))
	{
		g_computeShaderManager.huntNextShader(runtime->is_key_down(VK_CONTROL));
	}
	if(runtime->is_key_pressed(VK_NUMPAD9))
	{
		g_computeShaderManager.toggleMarkOnHuntedShader();
	}

	//TODO map Gui variable with cb13
	// cb_inject_values[0] = draw_to_trace;
}


/// <summary>
/// Function which marks the end of a keybinding editing cycle
/// </summary>
/// <param name="acceptCollectedBinding"></param>
/// <param name="groupEditing"></param>
void endKeyBindingEditing(bool acceptCollectedBinding, ToggleGroup& groupEditing)
{
	if (acceptCollectedBinding && g_toggleGroupIdKeyBindingEditing == groupEditing.getId() && g_keyCollector.isValid())
	{
		groupEditing.setToggleKey(g_keyCollector);
	}
	g_toggleGroupIdKeyBindingEditing = -1;
	g_keyCollector.clear();
}


/// <summary>
/// Function which marks the start of a keybinding editing cycle for the passed in toggle group
/// </summary>
/// <param name="groupEditing"></param>
void startKeyBindingEditing(ToggleGroup& groupEditing)
{
	if (g_toggleGroupIdKeyBindingEditing == groupEditing.getId())
	{
		return;
	}
	if (g_toggleGroupIdKeyBindingEditing >= 0)
	{
		endKeyBindingEditing(false, groupEditing);
	}
	g_toggleGroupIdKeyBindingEditing = groupEditing.getId();
}


/// <summary>
/// Function which marks the end of a shader editing cycle for a given toggle group
/// </summary>
/// <param name="acceptCollectedShaderHashes"></param>
/// <param name="groupEditing"></param>
void endShaderEditing(bool acceptCollectedShaderHashes, ToggleGroup& groupEditing)
{
	if(acceptCollectedShaderHashes && g_toggleGroupIdShaderEditing == groupEditing.getId())
	{
		groupEditing.storeCollectedHashes(g_pixelShaderManager.getMarkedShaderHashes(), g_vertexShaderManager.getMarkedShaderHashes(), g_computeShaderManager.getMarkedShaderHashes());
		g_pixelShaderManager.stopHuntingMode();
		g_vertexShaderManager.stopHuntingMode();
		g_computeShaderManager.stopHuntingMode();
	}
	g_toggleGroupIdShaderEditing = -1;
}


/// <summary>
/// Function which marks the start of a shader editing cycle for a given toggle group.
/// </summary>
/// <param name="groupEditing"></param>
void startShaderEditing(ToggleGroup& groupEditing)
{
	if(g_toggleGroupIdShaderEditing==groupEditing.getId())
	{
		return;
	}
	if(g_toggleGroupIdShaderEditing >= 0)
	{
		endShaderEditing(false, groupEditing);
	}
	g_toggleGroupIdShaderEditing = groupEditing.getId();
	g_activeCollectorFrameCounter = g_startValueFramecountCollectionPhase;
	g_pixelShaderManager.startHuntingMode(groupEditing.getPixelShaderHashes());
	g_vertexShaderManager.startHuntingMode(groupEditing.getVertexShaderHashes());
	g_computeShaderManager.startHuntingMode(groupEditing.getComputeShaderHashes());

	// after copying them to the managers, we can now clear the group's shader.
	groupEditing.clearHashes();
}


static void showHelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(450.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}


static void displaySettings(reshade::api::effect_runtime* runtime)
{
	if(g_toggleGroupIdKeyBindingEditing >= 0)
	{
		// a keybinding is being edited. Read current pressed keys into the collector, cumulatively;
		g_keyCollector.collectKeysPressed(runtime);
	}

	if(ImGui::CollapsingHeader("General info and help"))
	{
		ImGui::PushTextWrapPos();
		ImGui::TextUnformatted("The Shader Toggler allows you to create one or more groups with shaders to toggle on/off. You can assign a keyboard shortcut (including using keys like Shift, Alt and Control) to each group, including a handy name. Each group can have one or more vertex or pixel shaders assigned to it. When you press the assigned keyboard shortcut, any draw calls using these shaders will be disabled, effectively hiding the elements in the 3D scene.");
		ImGui::TextUnformatted("\nThe following (hardcoded) keyboard shortcuts are used when you click a group's 'Change Shaders' button:");
		ImGui::TextUnformatted("* Numpad 1 and Numpad 2: previous/next pixel shader");
		ImGui::TextUnformatted("* Ctrl + Numpad 1 and Ctrl + Numpad 2: previous/next marked pixel shader in the group");
		ImGui::TextUnformatted("* Numpad 3: mark/unmark the current pixel shader as being part of the group");
		ImGui::TextUnformatted("* Numpad 4 and Numpad 5: previous/next vertex shader");
		ImGui::TextUnformatted("* Ctrl + Numpad 4 and Ctrl + Numpad 5: previous/next marked vertex shader in the group");
		ImGui::TextUnformatted("* Numpad 6: mark/unmark the current vertex shader as being part of the group");
		ImGui::TextUnformatted("* Numpad 7 and Numpad 8: previous/next compute shader");
		ImGui::TextUnformatted("* Ctrl + Numpad 7 and Ctrl + Numpad 8: previous/next marked compute shader in the group");
		ImGui::TextUnformatted("* Numpad 9: mark/unmark the current compute shader as being part of the group");
		ImGui::TextUnformatted("\nWhen you step through the shaders, the current shader is disabled in the 3D scene so you can see if that's the shader you were looking for.");
		ImGui::TextUnformatted("When you're done, make sure you click 'Save all toggle groups' to preserve the groups you defined so next time you start your game they're loaded in and you can use them right away.");
		ImGui::PopTextWrapPos();
	}

	ImGui::AlignTextToFramePadding();
	if(ImGui::CollapsingHeader("Shader selection parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::AlignTextToFramePadding();
		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
		ImGui::SliderFloat("Overlay opacity", &g_overlayOpacity, 0.2f, 1.0f);
		ImGui::AlignTextToFramePadding();
		ImGui::SliderInt("# of frames to collect", &g_startValueFramecountCollectionPhase, 10, 1000);
		ImGui::SameLine();
		showHelpMarker("This is the number of frames the addon will collect active shaders. Set this to a high number if the shader you want to mark is only used occasionally. Only shaders that are used in the frames collected can be marked.");
		ImGui::PopItemWidth();
	}
	ImGui::Separator();

	// GUI for shaderHunter options
	if (ImGui::CollapsingHeader("Pixel shader toggling parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// choose if color or disabled
		ImGui::RadioButton("Disable", &constant_color, 0); ImGui::SameLine();
		ImGui::RadioButton("Color", &constant_color, 1);

		// define the number of draw to differentiate
		// ImGui::SliderInt("# of draws to differentiate", &draw_to_trace, 1, 5);
		ImGui::SliderFloat("# of draws to differentiate", &cb_inject_values[0], 0.0f, 5.0f, "ratio = %.0f");
	}

	ImGui::Separator();


	if(ImGui::CollapsingHeader("List of Toggle Groups", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if(ImGui::Button(" New "))
		{
			addDefaultGroup();
		}
		ImGui::Separator();

		std::vector<ToggleGroup> toRemove;
		for(auto& group : g_toggleGroups)
		{
			ImGui::PushID(group.getId());
			ImGui::AlignTextToFramePadding();
			if(ImGui::Button("X"))
			{
				toRemove.push_back(group);
			}
			ImGui::SameLine();
			ImGui::Text(" %d ", group.getId());
			ImGui::SameLine();
			if(ImGui::Button("Edit"))
			{
				group.setEditing(true);
			}

			ImGui::SameLine();
			if(g_toggleGroupIdShaderEditing >= 0)
			{
				if(g_toggleGroupIdShaderEditing == group.getId())
				{
					if(ImGui::Button(" Done "))
					{
						endShaderEditing(true, group);
					}
				}
				else
				{
					ImGui::BeginDisabled(true);
					ImGui::Button("      ");
					ImGui::EndDisabled();
				}
			}
			else
			{
				if(ImGui::Button("Change shaders"))
				{
					ImGui::SameLine();
					startShaderEditing(group);
				}
			}
			ImGui::SameLine();
			ImGui::Text(" %s (%s%s)", group.getName().c_str(), group.getToggleKeyAsString().c_str(), group.isActive() ? ", is active" : "");
			if(group.isActiveAtStartup())
			{
				ImGui::SameLine();
				ImGui::Text(" (Active at startup)");
			}
			if(group.isEditing())
			{
				ImGui::Separator();
				ImGui::Text("Edit group %d", group.getId());

				// Name of group
				char tmpBuffer[150];
				const string& name = group.getName();
				strncpy_s(tmpBuffer, 150, name.c_str(), name.size());
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Name");
				ImGui::SameLine(ImGui::GetWindowWidth() * 0.25f);
				ImGui::InputText("##Name", tmpBuffer, 149);
				group.setName(tmpBuffer);
				ImGui::PopItemWidth();

				// Key binding of group
				bool isKeyEditing = false;
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Key shortcut");
				ImGui::SameLine(ImGui::GetWindowWidth() * 0.25f);
				string textBoxContents = (g_toggleGroupIdKeyBindingEditing == group.getId()) ? g_keyCollector.getKeyAsString() : group.getToggleKeyAsString();	// The 'press a key' is inside keycollector
				string toggleKeyName = group.getToggleKeyAsString();
				ImGui::InputText("##Key shortcut", (char*)textBoxContents.c_str(), textBoxContents.size(), ImGuiInputTextFlags_ReadOnly);
				if(ImGui::IsItemClicked())
				{
					startKeyBindingEditing(group);
				}
				if(g_toggleGroupIdKeyBindingEditing == group.getId())
				{
					isKeyEditing = true;
					ImGui::SameLine();
					if(ImGui::Button("OK"))
					{
						endKeyBindingEditing(true, group);
					}
					ImGui::SameLine();
					if(ImGui::Button("Cancel"))
					{
						endKeyBindingEditing(false, group);
					}
				}
				ImGui::PopItemWidth();

				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
				ImGui::Text(" ");
				ImGui::SameLine(ImGui::GetWindowWidth() * 0.25f);
				bool isDefaultActive = group.isActiveAtStartup();
				ImGui::Checkbox("Is active at startup", &isDefaultActive);
				group.setIsActiveAtStartup(isDefaultActive);
				ImGui::PopItemWidth();

				if(!isKeyEditing)
				{
					if(ImGui::Button("OK"))
					{
						group.setEditing(false);
						g_toggleGroupIdKeyBindingEditing = -1;
						g_keyCollector.clear();
					}
				}
				ImGui::Separator();
			}

			ImGui::PopID();
		}
		if(toRemove.size() > 0)
		{
			// switch off keybinding editing or shader editing, if in progress
			g_toggleGroupIdKeyBindingEditing = -1;
			g_keyCollector.clear();
			g_toggleGroupIdShaderEditing = -1;
			g_pixelShaderManager.stopHuntingMode();
			g_vertexShaderManager.stopHuntingMode();
		}
		for(const auto& group : toRemove)
		{
			std::erase(g_toggleGroups, group);
		}

		ImGui::Separator();
		if(g_toggleGroups.size() > 0)
		{
			if(ImGui::Button("Save all Toggle Groups"))
			{
				saveShaderTogglerIniFile();
			}
		}
	}
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		{
			if(!reshade::register_addon(hModule))
			{
				return FALSE;
			}

			// We'll pass a nullptr for the module handle so we get the containing process' executable + path. We can't use the reshade's api as we don't have the runtime
			// and we can't use reshade's handle because under vulkan reshade is stored in a central space and therefore it won't get the folder of the exe (where the reshade dll is located as well).
			WCHAR buf[MAX_PATH];
			const std::filesystem::path dllPath = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf)) ? buf : std::filesystem::path();		// <installpath>/shadertoggler.addon64
			const std::filesystem::path basePath = dllPath.parent_path();																// <installpath>
			const std::string& hashFileName = HASH_FILE_NAME;
			g_iniFileName = (basePath / hashFileName).string();																			// <installpath>/shadertoggler.ini

			reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
			reshade::register_event<reshade::addon_event::init_command_list>(onInitCommandList);
			reshade::register_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
			reshade::register_event<reshade::addon_event::reset_command_list>(onResetCommandList);
			reshade::register_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
			reshade::register_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
			
			//updated
			reshade::register_event<reshade::addon_event::reshade_present>(onReshadePresent);
			reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
			reshade::register_event<reshade::addon_event::draw>(onDraw);
			reshade::register_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
			reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);

			// added to dump shaders
			reshade::register_event<reshade::addon_event::create_pipeline>(on_create_pipeline);
			reshade::register_event<reshade::addon_event::init_pipeline_layout>(on_init_pipeline_layout);
			reshade::register_event<reshade::addon_event::create_pipeline_layout>(on_create_pipeline_layout);
			reshade::register_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables);

			// added (coming from API_trace and used by DCS. other calls from API trace should miss for other games)
			reshade::register_event<reshade::addon_event::push_descriptors>(on_push_descriptors);
			reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
			reshade::register_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
			reshade::register_event<reshade::addon_event::clear_render_target_view>(on_clear_render_target_view);
			reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(on_clear_depth_stencil_view);
			reshade::register_event<reshade::addon_event::bind_pipeline_states>(on_bind_pipeline_states);
			reshade::register_event<reshade::addon_event::bind_vertex_buffers>(on_bind_vertex_buffers);
			reshade::register_event<reshade::addon_event::bind_index_buffer>(on_bind_index_buffer);

			// coming from RenoDx
			reshade::register_event<reshade::addon_event::init_device>(on_init_device);
			reshade::register_event<reshade::addon_event::destroy_device>(on_destroy_device);
			reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
			reshade::register_event<reshade::addon_event::init_resource>(on_init_resource);
			reshade::register_event<reshade::addon_event::destroy_resource>(on_destroy_resource);
			reshade::register_event<reshade::addon_event::init_resource_view>(on_init_resource_view);
			reshade::register_event<reshade::addon_event::destroy_resource_view>(on_destroy_resource_view);

			reshade::register_overlay(nullptr, &displaySettings);
			loadShaderTogglerIniFile();

		}
		break;
	case DLL_PROCESS_DETACH:
		reshade::unregister_event<reshade::addon_event::reshade_present>(onReshadePresent);
		reshade::unregister_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
		reshade::unregister_event<reshade::addon_event::init_pipeline>(onInitPipeline);
		reshade::unregister_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
		reshade::unregister_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
		reshade::unregister_event<reshade::addon_event::draw>(onDraw);
		reshade::unregister_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
		reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);
		reshade::unregister_event<reshade::addon_event::init_command_list>(onInitCommandList);
		reshade::unregister_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
		reshade::unregister_event<reshade::addon_event::reset_command_list>(onResetCommandList);

		reshade::unregister_event<reshade::addon_event::create_pipeline>(on_create_pipeline);
		reshade::unregister_event<reshade::addon_event::init_pipeline_layout>(on_init_pipeline_layout);
		reshade::unregister_event<reshade::addon_event::create_pipeline_layout>(on_create_pipeline_layout);
		reshade::unregister_event<reshade::addon_event::bind_descriptor_tables>(on_bind_descriptor_tables);

		reshade::unregister_event<reshade::addon_event::push_descriptors>(on_push_descriptors);
		reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_render_targets_and_depth_stencil);
		reshade::unregister_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
		reshade::unregister_event<reshade::addon_event::clear_render_target_view>(on_clear_render_target_view);
		reshade::unregister_event<reshade::addon_event::clear_depth_stencil_view>(on_clear_depth_stencil_view);
		reshade::unregister_event<reshade::addon_event::bind_pipeline_states>(on_bind_pipeline_states);
		reshade::unregister_event<reshade::addon_event::bind_vertex_buffers>(on_bind_vertex_buffers);
		reshade::unregister_event<reshade::addon_event::bind_index_buffer>(on_bind_index_buffer);

		reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
		reshade::unregister_event<reshade::addon_event::destroy_device>(on_destroy_device);
		reshade::unregister_event<reshade::addon_event::init_swapchain>(on_init_swapchain);

		reshade::unregister_overlay(nullptr, &displaySettings);
		reshade::unregister_addon(hModule);
		break;
	}

	return TRUE;
}


// auto& global_data = device->get_private_data<global_shared>();
