/// clone a pipleline, code parts taken from RenoDX - DevKit

#include <reshade.hpp>
#include "config.hpp"
#include <sstream>
#include <vector>
#include <string>


using namespace reshade::api;

struct CachedShader {
    void* data = nullptr;
    size_t size = 0;
};

static pipeline clone_pipeline(
    device* device,
	reshade::api::pipeline_layout layout,
	uint32_t subobjectCount,
	const reshade::api::pipeline_subobject* subobjects,
	reshade::api::pipeline pipeline,
    std::vector<std::vector<uint8_t>>& ReplaceshaderCode
) {

    // flag to know if a code replacement has been done
    bool code_replace = false;

	//log beginning of copy
	std::stringstream s;
	s << "on_init_pipeline("
		<< reinterpret_cast<void*>(pipeline.handle)
		<< " on " << reinterpret_cast<void*>(layout.handle)
		<< ", subobjects: " << (subobjectCount)
		<< " )";
	// reshade::log_message(reshade::log_level::info, s.str().c_str());

	// clone subobjects
	reshade::api::pipeline_subobject* newSubobjects = new reshade::api::pipeline_subobject[subobjectCount];
	memcpy(newSubobjects, subobjects, sizeof(reshade::api::pipeline_subobject) * subobjectCount);

    // clone the desc and change code source if PS
    for (uint32_t i = 0; i < subobjectCount; ++i) {
        auto clonedSubObject = &newSubobjects[i];
        CachedShader* cache;

        //original Desc
        shader_desc desc = *static_cast<shader_desc*>(subobjects[i].data);

        // Clone desc
        reshade::api::shader_desc clonedDesc;
        memcpy(&clonedDesc, &desc, sizeof(reshade::api::shader_desc));

        // Point to cloned desc
        clonedSubObject->data = &clonedDesc;

        // change code source to use the new one if a PS is used, otherwise keep things as they are
        if (subobjects[i].type == pipeline_subobject_type::pixel_shader) {

            // clone ReplaceshaderCode
            cache = new CachedShader{
              malloc(ReplaceshaderCode.back().size()),
              ReplaceshaderCode.back().size()
            };
            memcpy(cache->data, ReplaceshaderCode.back().data(), cache->size);
            clonedDesc.code = cache->data;
            clonedDesc.code_size = cache->size;

            code_replace = true;

            //log operation
            std::stringstream s;
            s << "pipeline_subobject Pixel cloned with code replacement ("
                << ", object Number: " << std::to_string(i)
                << ", pipeline: " << reinterpret_cast<void*>(pipeline.handle)
                << ")";
            reshade::log_message(reshade::log_level::info, s.str().c_str());
        }
        /* else {
            switch (subobjects[i].type)
            {
            case pipeline_subobject_type::vertex_shader:
            case pipeline_subobject_type::hull_shader:
            case pipeline_subobject_type::domain_shader:
            case pipeline_subobject_type::geometry_shader:
            case pipeline_subobject_type::compute_shader:
            case pipeline_subobject_type::amplification_shader:
            case pipeline_subobject_type::mesh_shader:
            case pipeline_subobject_type::raygen_shader:
            case pipeline_subobject_type::any_hit_shader:
            case pipeline_subobject_type::closest_hit_shader:
            case pipeline_subobject_type::miss_shader:
            case pipeline_subobject_type::intersection_shader:
            case pipeline_subobject_type::callable_shader:
                // clone original shader code
                cache = new CachedShader{
                 malloc(desc.code_size),
                 desc.code_size
                };
                memcpy(cache->data, desc.code, cache->size);
                clonedDesc.code = cache->data;
                clonedDesc.code_size = cache->size;

                //log operation
                std::stringstream s;
                s << "pipeline_subobject cloned without code replacement ("
                    << ", object Number: " << std::to_string(i)
                    << ", pipeline: " << reinterpret_cast<void*>(pipeline.handle)
                    << ")";
                //reshade::log_message(reshade::log_level::info, s.str().c_str());
                break;
            }
        } */
    }
    // create cloned pipeline
    // if (code_replace) 
    //{
        reshade::api::pipeline pipelineClone;

        bool builtPipelineOK = device->create_pipeline(
            layout,
            subobjectCount,
            &newSubobjects[0],
            &pipelineClone
        );

        if (builtPipelineOK) {
            // Cloned pipeline created
            return(pipelineClone);

            std::stringstream s;
            s << "pipeline  cloned  ("
                << ", orig pipeline: " << reinterpret_cast<void*>(pipeline.handle)
                << ", cloned pipeline: " << reinterpret_cast<void*>(pipelineClone.handle)
                << ")";
            //reshade::log_message(reshade::log_level::info, s.str().c_str());
        }

        else
        {
            // error : return original pipeline. need to free allocated objects ? 
            return(pipeline);

            std::stringstream s;
            s << "********** Error : pipeline not cloned !! ("
                << ", orig pipeline: " << reinterpret_cast<void*>(pipeline.handle)
                << ")";
            reshade::log_message(reshade::log_level::info, s.str().c_str());
        }
    /* }
    else {
        // no PS : return original pipeline. need to free allocated objects ? 
        // free_allocated_objects(newSubobjects, subobjectCount);
        return(pipeline);

        std::stringstream s;
        s << "!! pipeline not cloned because no PS !! ("
            << ", orig pipeline: " << reinterpret_cast<void*>(pipeline.handle)
            << ")";
        reshade::log_message(reshade::log_level::info, s.str().c_str());
    } */
}

// free memory allocated in case of error or no need to clone
static void free_allocated_objects(
    reshade::api::pipeline_subobject* newSubobjects,
    uint32_t subobjectCount)
{
    for (uint32_t i = 0; i < subobjectCount; ++i) {
        shader_desc desc = *static_cast<shader_desc*>(newSubobjects[i].data);
        if (desc.code != NULL) free ((void*)desc.code);

    }
}