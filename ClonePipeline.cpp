/// clone a PS pipleline, code parts taken from RenoDX - DevKit

#include <reshade.hpp>
#include "config.hpp"
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>


using namespace reshade::api;

// input : ReplaceshaderCode is hosting the code to be used for the cloned PS (read previously)
// output : pipelineCloneMap containing the links between original pipeline Handles and the cloned pipelines

static void clone_pipeline(
    device* device,
    reshade::api::pipeline_layout layout,
    uint32_t subobjectCount,
    const reshade::api::pipeline_subobject* subobjects,
    reshade::api::pipeline pipeline,
    std::vector<std::vector<uint8_t>>& ReplaceshaderCode,
    std::unordered_map<uint64_t, reshade::api::pipeline>& pipelineCloneMap)
{

    struct CachedShader {
        void* data = nullptr;
        size_t size = 0;
    };
    std::stringstream s;

    // flag to know if a code replacement has been done
    bool code_replace = false;

    // check if one subobject is pixel shader
    bool PScheck = false;
    for (uint32_t i = 0; i < subobjectCount; ++i) {
        if (subobjects[i].type == pipeline_subobject_type::pixel_shader && !PScheck) {
            PScheck = true;
        }
    }

    if (PScheck)
    {
        //log beginning of copy
        s << "CLONING PIPELINE("
            << reinterpret_cast<void*>(pipeline.handle)
            << ") Layout : " << reinterpret_cast<void*>(layout.handle)
            << ", subobjects counts: " << (subobjectCount)
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
                s << "pipeline_subobject Pixel cloned with code replacement ("
                    << ", object Number: " << std::to_string(i)
                    << ", pipeline: " << reinterpret_cast<void*>(pipeline.handle)
                    << ")";
                // reshade::log_message(reshade::log_level::info, s.str().c_str());
            }

        }
        // create cloned pipeline
        reshade::api::pipeline pipelineClone;

        bool builtPipelineOK = device->create_pipeline(
            layout,
            subobjectCount,
            &newSubobjects[0],
            &pipelineClone
        );

        if (builtPipelineOK) {
            // Add cloned Pipeline to pipelineCloneMap
            pipelineCloneMap.emplace(pipeline.handle, pipelineClone);

            s << "pipeline  cloned  ("
                << ", orig pipeline: " << reinterpret_cast<void*>(pipeline.handle)
                << ", cloned pipeline: " << reinterpret_cast<void*>(pipelineClone.handle)
                << ")";
            // reshade::log_message(reshade::log_level::info, s.str().c_str());
        }
        else
        {
            // log error
            s << "********** Error : pipeline not cloned !! ("
                << ", orig pipeline: " << reinterpret_cast<void*>(pipeline.handle)
                << ")";
            reshade::log_message(reshade::log_level::info, s.str().c_str());
        }
    }
}

