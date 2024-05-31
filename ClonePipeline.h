/// clone a pipleline

#pragma once

extern pipeline clone_pipeline(
	device* device,
	reshade::api::pipeline_layout layout,
	uint32_t subobjectCount,
	const reshade::api::pipeline_subobject* subobjects,
	reshade::api::pipeline pipeline,
	std::vector<std::vector<uint8_t>>& ReplaceshaderCode
);
