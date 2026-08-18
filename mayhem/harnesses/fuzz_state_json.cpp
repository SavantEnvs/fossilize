// fuzz_state_json.cpp -- the Fossilize STATE DESERIALIZER surface.
//
// A Fossilize "blob" (one database entry, or the direct StateReplayer::parse() argument) is a
// JSON document -- optionally followed by a NUL byte and a binary varint payload -- describing
// Vulkan *CreateInfo structs (samplers, descriptor set layouts, pipeline layouts, shader modules,
// render passes, pipelines, ...). fossilize.cpp's StateReplayer walks that JSON with rapidjson and
// reconstructs the C structs field-by-field; that reconstruction is the target here.
//
// NO VULKAN DEVICE, NO FILE I/O. StateReplayer::parse() takes the fuzzer's buffer directly (the
// buffer/fd-based form the brief asks to prefer over a path-based API), and StateCreatorInterface
// is an abstract callback interface: NullStateCreator below implements every pure virtual as a
// trivial "accept it, hand back a fake non-null handle" stub -- it never touches a real Vulkan
// instance/device, so parsing runs identically with or without a GPU/driver present. Passing
// resolver=nullptr to parse() is an explicitly supported mode (fossilize.cpp checks `!resolver`
// before every dereference) for exactly this no-database, no-driver case.
#include "fossilize.hpp"

#include <cstdint>
#include <cstddef>

using namespace Fossilize;

// Deeply/widely nested JSON (or a huge inlined base64 "code" blob) is a classic parser hang/OOM
// vector; cap the raw input so one pathological document cannot stall the whole campaign (SPEC 6b).
// rapidjson's own recursive descent is the risk here, not anything we can bound after the fact, so
// the input-size cap is the mitigation -- genuine crashes/OOM under it are real findings, not masked.
static constexpr size_t kMaxInputSize = 4 * 1024 * 1024;

namespace
{

// Records nothing, validates nothing beyond what StateReplayer itself does; every callback just
// reports success with a fake non-null handle, so parsing proceeds through every resource type
// exactly as it would with a real device recording -- the interface's job is to keep the parser
// from short-circuiting because a callback thinks it "failed" to create the object.
struct NullStateCreator : StateCreatorInterface
{
	template <typename T>
	static T FakeHandle(Hash hash)
	{
		return reinterpret_cast<T>(static_cast<uintptr_t>(hash | 1));
	}

	bool enqueue_create_sampler(Hash hash, const VkSamplerCreateInfo *, VkSampler *out) override
	{
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

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size == 0 || size > kMaxInputSize)
		return 0;

	NullStateCreator creator;
	StateReplayer replayer;
	// resolver=nullptr: no external database to resolve hash-referenced immutable samplers/blob
	// links against -- fossilize.cpp treats that as "resource not found" and rejects the document,
	// it does not dereference a null resolver.
	(void)replayer.parse(creator, nullptr, data, size);
	return 0;
}
