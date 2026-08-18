// kat_state_json.cpp -- KAT probe for fuzz_state_json's exact code path (SPEC 6.3).
//
// Drives Fossilize's own recorder to produce a real, valid serialized blob (a sampler +
// a shader module, fixed fields -- never hand-crafted JSON), then feeds it through
// StateReplayer::parse() with the same no-driver NullStateCreator shape the fuzz harness uses, and
// prints exact counts. See kat_foz_db.cpp for why an unconditional printed assertion is required
// on top of Fossilize's own abort()-only test/ suite.
#include "fossilize.hpp"

#include <cstdio>
#include <cstdint>

using namespace Fossilize;

namespace
{
struct CountingCreator : StateCreatorInterface
{
	int sampler_calls = 0;
	int shader_module_calls = 0;

	template <typename T>
	static T FakeHandle(Hash hash) { return reinterpret_cast<T>(static_cast<uintptr_t>(hash | 1)); }

	bool enqueue_create_sampler(Hash hash, const VkSamplerCreateInfo *, VkSampler *out) override
	{
		sampler_calls++;
		*out = FakeHandle<VkSampler>(hash);
		return true;
	}
	bool enqueue_create_descriptor_set_layout(Hash hash, const VkDescriptorSetLayoutCreateInfo *,
	                                          VkDescriptorSetLayout *out) override
	{
		*out = FakeHandle<VkDescriptorSetLayout>(hash);
		return true;
	}
	bool enqueue_create_pipeline_layout(Hash hash, const VkPipelineLayoutCreateInfo *, VkPipelineLayout *out) override
	{
		*out = FakeHandle<VkPipelineLayout>(hash);
		return true;
	}
	bool enqueue_create_shader_module(Hash hash, const VkShaderModuleCreateInfo *, VkShaderModule *out) override
	{
		shader_module_calls++;
		*out = FakeHandle<VkShaderModule>(hash);
		return true;
	}
	bool enqueue_create_render_pass(Hash hash, const VkRenderPassCreateInfo *, VkRenderPass *out) override
	{
		*out = FakeHandle<VkRenderPass>(hash);
		return true;
	}
	bool enqueue_create_render_pass2(Hash hash, const VkRenderPassCreateInfo2 *, VkRenderPass *out) override
	{
		*out = FakeHandle<VkRenderPass>(hash);
		return true;
	}
	bool enqueue_create_compute_pipeline(Hash hash, const VkComputePipelineCreateInfo *, VkPipeline *out) override
	{
		*out = FakeHandle<VkPipeline>(hash);
		return true;
	}
	bool enqueue_create_graphics_pipeline(Hash hash, const VkGraphicsPipelineCreateInfo *, VkPipeline *out) override
	{
		*out = FakeHandle<VkPipeline>(hash);
		return true;
	}
	bool enqueue_create_raytracing_pipeline(Hash hash, const VkRayTracingPipelineCreateInfoKHR *,
	                                        VkPipeline *out) override
	{
		*out = FakeHandle<VkPipeline>(hash);
		return true;
	}
};
}

int main()
{
	VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_NEAREST;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.maxAnisotropy = 1.0f;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

	static const uint32_t code[] = { 0x07230203u, 0x00010000u, 0u, 4u, 0u };
	VkShaderModuleCreateInfo module = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	module.codeSize = sizeof(code);
	module.pCode = code;

	StateRecorder recorder;
	bool record_ok = recorder.record_sampler(reinterpret_cast<VkSampler>(uintptr_t(100)), sampler) &&
	                  recorder.record_shader_module(reinterpret_cast<VkShaderModule>(uintptr_t(200)), module);
	if (!record_ok)
	{
		fprintf(stderr, "KAT_STATE: record failed\n");
		return 1;
	}

	uint8_t *serialized = nullptr;
	size_t serialized_size = 0;
	if (!recorder.serialize(&serialized, &serialized_size))
	{
		fprintf(stderr, "KAT_STATE: serialize failed\n");
		return 1;
	}

	CountingCreator creator;
	StateReplayer replayer;
	bool parse_ok = replayer.parse(creator, nullptr, serialized, serialized_size);
	StateRecorder::free_serialized(serialized);

	printf("KAT_STATE_PARSE_OK=%d\n", parse_ok ? 1 : 0);
	printf("KAT_STATE_SAMPLERS=%d\n", creator.sampler_calls);
	printf("KAT_STATE_SHADER_MODULES=%d\n", creator.shader_module_calls);
	fflush(stdout);

	bool ok = parse_ok && creator.sampler_calls == 1 && creator.shader_module_calls == 1;
	return ok ? 0 : 1;
}
