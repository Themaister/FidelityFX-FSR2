/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// Heavily adapted from raw Vulkan backend implementation.

#include "ffx_fsr2_granite.h"
#include "device.hpp"
#include "image.hpp"
#include "buffer.hpp"
#include "command_buffer.hpp"

static constexpr unsigned FSR2_MAX_RESOURCE_COUNT = 64;
static constexpr unsigned FSR2_MAX_GPU_JOBS = 32;
static constexpr unsigned FSR2_MAX_BARRIERS = 16;

namespace Granite
{
namespace FSR2
{
enum Fs2ShaderPermutationOptions
{
	FSR2_SHADER_PERMUTATION_LANCZOS_LUT = (1 << 0),    // FFX_FSR2_OPTION_USE_LANCZOS_LUT
	FSR2_SHADER_PERMUTATION_HDR_COLOR_INPUT = (1 << 1),    // FFX_FSR2_OPTION_HDR_COLOR_INPUT
	FSR2_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS = (1 << 2),    // FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS
	FSR2_SHADER_PERMUTATION_JITTER_MOTION_VECTORS = (1 << 3),    // FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS
	FSR2_SHADER_PERMUTATION_DEPTH_INVERTED = (1 << 4),    // FFX_FSR2_OPTION_INVERTED_DEPTH
	FSR2_SHADER_PERMUTATION_ENABLE_SHARPENING = (1 << 5),    // FFX_FSR2_OPTION_APPLY_SHARPENING
	FSR2_SHADER_PERMUTATION_FORCE_WAVE64 = (1 << 6),    // doesn't map to a define, selects different table
	FSR2_SHADER_PERMUTATION_ALLOW_FP16 = (1 << 7),    // FFX_USE_16BIT
};

struct Resource
{
	FfxResourceDescription resourceDescription;
	Vulkan::BufferHandle buffer;
	Vulkan::ImageHandle image;
	VkImageAspectFlags aspectFlags;
	Vulkan::ImageViewHandle singleMipImageViews[16];
	FfxResourceStates state;
	bool undefined;
};

struct Pipeline
{
	Vulkan::ShaderProgramVariant *variant;
	bool force_wave64;
};

struct Context
{
	Vulkan::Device *device = nullptr;
	int32_t nextStaticResource = 0;
	int32_t nextDynamicResource = FSR2_MAX_RESOURCE_COUNT - 1;
	int32_t nextPipelineIndex = 0;
	Resource resources[FSR2_MAX_RESOURCE_COUNT] = {};
	Pipeline pipelines[FFX_FSR2_PASS_COUNT] = {};

	unsigned renderJobCount = 0;
	FfxGpuJobDescription renderJobs[FSR2_MAX_GPU_JOBS] = {};

	// Barrier batching.
	VkImageMemoryBarrier imageBarriers[FSR2_MAX_BARRIERS];
	VkBufferMemoryBarrier bufferBarriers[FSR2_MAX_BARRIERS];
	unsigned imageBarrierCount = 0;
	unsigned bufferBarrierCount = 0;
	VkPipelineStageFlags srcStageMask = 0;
	VkPipelineStageFlags dstStageMask = 0;
};

static FfxErrorCode GetDeviceCapabilities(FfxFsr2Interface* backendInterface, FfxDeviceCapabilities* deviceCapabilities, FfxDevice device);
static FfxErrorCode CreateDevice(FfxFsr2Interface* backendInterface, FfxDevice device);
static FfxErrorCode DestroyDevice(FfxFsr2Interface* backendInterface);
static FfxErrorCode CreateResource(FfxFsr2Interface* backendInterface, const FfxCreateResourceDescription* desc, FfxResourceInternal* outResource);
static FfxErrorCode RegisterResource(FfxFsr2Interface* backendInterface, const FfxResource* inResource, FfxResourceInternal* outResourceInternal);
static FfxErrorCode UnregisterResources(FfxFsr2Interface* backendInterface);
static FfxResourceDescription GetResourceDescriptor(FfxFsr2Interface* backendInterface, FfxResourceInternal resource);
static FfxErrorCode DestroyResource(FfxFsr2Interface* backendInterface, FfxResourceInternal resource);
static FfxErrorCode CreatePipeline(FfxFsr2Interface* backendInterface, FfxFsr2Pass passId, const FfxPipelineDescription* desc, FfxPipelineState* outPass);
static FfxErrorCode DestroyPipeline(FfxFsr2Interface* backendInterface, FfxPipelineState* pipeline);
static FfxErrorCode ScheduleRenderJob(FfxFsr2Interface* backendInterface, const FfxGpuJobDescription* job);
static FfxErrorCode ExecuteRenderJobs(FfxFsr2Interface* backendInterface, FfxCommandList commandList);
}
}

static VkFormat getVKFormatFromSurfaceFormat(FfxSurfaceFormat fmt)
{
	switch (fmt)
	{
	case FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
	case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT:
		return VK_FORMAT_R32G32B32A32_SFLOAT;
	case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:
		return VK_FORMAT_R16G16B16A16_SFLOAT;
	case FFX_SURFACE_FORMAT_R32G32_FLOAT:
		return VK_FORMAT_R32G32_SFLOAT;
	case FFX_SURFACE_FORMAT_R32_UINT:
		return VK_FORMAT_R32_UINT;
	case FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
	case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:
		return VK_FORMAT_R8G8B8A8_UNORM;
	case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:
		return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	case FFX_SURFACE_FORMAT_R16G16_FLOAT:
		return VK_FORMAT_R16G16_SFLOAT;
	case FFX_SURFACE_FORMAT_R16G16_UINT:
		return VK_FORMAT_R16G16_UINT;
	case FFX_SURFACE_FORMAT_R16_FLOAT:
		return VK_FORMAT_R16_SFLOAT;
	case FFX_SURFACE_FORMAT_R16_UINT:
		return VK_FORMAT_R16_UINT;
	case FFX_SURFACE_FORMAT_R16_UNORM:
		return VK_FORMAT_R16_UNORM;
	case FFX_SURFACE_FORMAT_R16_SNORM:
		return VK_FORMAT_R16_SNORM;
	case FFX_SURFACE_FORMAT_R8_UNORM:
		return VK_FORMAT_R8_UNORM;
	case FFX_SURFACE_FORMAT_R32_FLOAT:
		return VK_FORMAT_R32_SFLOAT;
	default:
		return VK_FORMAT_UNDEFINED;
	}
}

static FfxSurfaceFormat ffxGetSurfaceFormatVK(VkFormat fmt)
{
	switch (fmt)
	{
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	case VK_FORMAT_R32G32_SFLOAT:
		return FFX_SURFACE_FORMAT_R32G32_FLOAT;
	case VK_FORMAT_R32_UINT:
		return FFX_SURFACE_FORMAT_R32_UINT;
	case VK_FORMAT_R8G8B8A8_UNORM:
		return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
		return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
	case VK_FORMAT_R16G16_SFLOAT:
		return FFX_SURFACE_FORMAT_R16G16_FLOAT;
	case VK_FORMAT_R16G16_UINT:
		return FFX_SURFACE_FORMAT_R16G16_UINT;
	case VK_FORMAT_R16_SFLOAT:
		return FFX_SURFACE_FORMAT_R16_FLOAT;
	case VK_FORMAT_R16_UINT:
		return FFX_SURFACE_FORMAT_R16_UINT;
	case VK_FORMAT_R16_UNORM:
		return FFX_SURFACE_FORMAT_R16_UNORM;
	case VK_FORMAT_R16_SNORM:
		return FFX_SURFACE_FORMAT_R16_SNORM;
	case VK_FORMAT_R8_UNORM:
		return FFX_SURFACE_FORMAT_R8_UNORM;
	case VK_FORMAT_R32_SFLOAT:
		return FFX_SURFACE_FORMAT_R32_FLOAT;
	default:
		return FFX_SURFACE_FORMAT_UNKNOWN;
	}
}

static VkBufferUsageFlags getVKBufferUsageFlagsFromResourceUsage(FfxResourceUsage flags)
{
	if (flags & FFX_RESOURCE_USAGE_UAV)
		return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	else
		return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
}

static VkImageType getVKImageTypeFromResourceType(FfxResourceType type)
{
	switch (type)
	{
	case FFX_RESOURCE_TYPE_TEXTURE1D:
		return VK_IMAGE_TYPE_1D;
	case FFX_RESOURCE_TYPE_TEXTURE2D:
		return VK_IMAGE_TYPE_2D;
	case FFX_RESOURCE_TYPE_TEXTURE3D:
		return VK_IMAGE_TYPE_3D;
	default:
		return VK_IMAGE_TYPE_MAX_ENUM;
	}
}

static VkImageUsageFlags getVKImageUsageFlagsFromResourceUsage(FfxResourceUsage flags)
{
	VkImageUsageFlags ret = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (flags & FFX_RESOURCE_USAGE_RENDERTARGET)
		ret |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (flags & FFX_RESOURCE_USAGE_UAV)
		ret |= (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	return ret;
}

// Create a FfxFsr2Device from a VkDevice
FfxDevice ffxGetDeviceGranite(Vulkan::Device *device)
{
	FFX_ASSERT(device);
	return static_cast<FfxDevice>(device);
}

FfxCommandList ffxGetCommandListGranite(Vulkan::CommandBuffer *cmd)
{
	FFX_ASSERT(cmd);
	return static_cast<FfxCommandList>(cmd);
}

FFX_API size_t ffxFsr2GetScratchMemorySizeGranite()
{
	return FFX_ALIGN_UP(sizeof(Granite::FSR2::Context), sizeof(uint64_t));
}

FFX_API FfxErrorCode ffxFsr2GetInterfaceGranite(
	FfxFsr2Interface *outInterface,
	void *scratchBuffer,
	size_t scratchBufferSize)
{
	FFX_RETURN_ON_ERROR(
		outInterface,
		FFX_ERROR_INVALID_POINTER);
	FFX_RETURN_ON_ERROR(
		scratchBuffer,
		FFX_ERROR_INVALID_POINTER);
	FFX_RETURN_ON_ERROR(
		scratchBufferSize >= ffxFsr2GetScratchMemorySizeGranite(),
		FFX_ERROR_INSUFFICIENT_MEMORY);

	outInterface->fpGetDeviceCapabilities = Granite::FSR2::GetDeviceCapabilities;
	outInterface->fpCreateBackendContext = Granite::FSR2::CreateDevice;
	outInterface->fpDestroyBackendContext = Granite::FSR2::DestroyDevice;
	outInterface->fpCreateResource = Granite::FSR2::CreateResource;
	outInterface->fpRegisterResource = Granite::FSR2::RegisterResource;
	outInterface->fpUnregisterResources = Granite::FSR2::UnregisterResources;
	outInterface->fpGetResourceDescription = Granite::FSR2::GetResourceDescriptor;
	outInterface->fpDestroyResource = Granite::FSR2::DestroyResource;
	outInterface->fpCreatePipeline = Granite::FSR2::CreatePipeline;
	outInterface->fpDestroyPipeline = Granite::FSR2::DestroyPipeline;
	outInterface->fpScheduleGpuJob = Granite::FSR2::ScheduleRenderJob;
	outInterface->fpExecuteGpuJobs = Granite::FSR2::ExecuteRenderJobs;

	auto *context = new(scratchBuffer) Granite::FSR2::Context();
	FFX_ASSERT(context == scratchBuffer);
	outInterface->scratchBuffer = context;
	outInterface->scratchBufferSize = scratchBufferSize;

	return FFX_OK;
}

FfxResource ffxGetTextureResourceGranite(FfxFsr2Context *,
                                         const Vulkan::Image *image, const Vulkan::ImageView *view,
                                         FfxResourceStates state)
{
	FfxResource resource = {};
	resource.resource = const_cast<Vulkan::Image *>(image);
	resource.state = state;
	resource.descriptorData = reinterpret_cast<uintptr_t>(view ? view : &image->get_view());
	resource.description.flags = FFX_RESOURCE_FLAGS_NONE;
	resource.description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
	resource.description.width = view ? view->get_view_width() : image->get_width();
	resource.description.height = view ? view->get_view_height() : image->get_height();
	resource.description.depth = 1;
	resource.description.mipCount = 1;
	resource.description.format = ffxGetSurfaceFormatVK(image->get_format());

	switch (image->get_format())
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
	{
		resource.isDepth = true;
		break;
	}
	default:
	{
		resource.isDepth = false;
		break;
	}
	}

	return resource;
}

FfxResource ffxGetBufferResourceGranite(FfxFsr2Context *, const Vulkan::Buffer *buffer, FfxResourceStates state)
{
	FfxResource resource = {};
	resource.resource = const_cast<Vulkan::Buffer *>(buffer);
	resource.state = state;
	resource.descriptorData = 0;
	resource.description.flags = FFX_RESOURCE_FLAGS_NONE;
	resource.description.type = FFX_RESOURCE_TYPE_BUFFER;
	resource.description.width = buffer->get_create_info().size;
	resource.description.height = 1;
	resource.description.depth = 1;
	resource.description.mipCount = 1;
	resource.description.format = FFX_SURFACE_FORMAT_UNKNOWN;
	resource.isDepth = false;

	return resource;
}

FfxErrorCode Granite::FSR2::CreateDevice(FfxFsr2Interface *backendInterface, FfxDevice device_)
{
	FFX_ASSERT(backendInterface);

	auto *device = static_cast<Vulkan::Device *>(device_);
	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);
	backendContext->device = device;
	return FFX_OK;
}

FfxErrorCode Granite::FSR2::DestroyDevice(FfxFsr2Interface* backendInterface)
{
	FFX_ASSERT(backendInterface);
	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);
	backendContext->~Context();
	backendContext->device = nullptr;
	return FFX_OK;
}

// Crude, but it's obviously just using ASCII here.
// Why on earth would anyone use wchar here ... :<
static std::string convert_to_utf8(const wchar_t *name)
{
	std::string str;
	while (*name != '\0')
	{
		str.push_back(*name < 128 ? char(*name) : '?');
		name++;
	}
	return str;
}

// create a internal resource that will stay alive until effect gets shut down
FfxErrorCode Granite::FSR2::CreateResource(
    FfxFsr2Interface *backendInterface,
    const FfxCreateResourceDescription *createResourceDescription,
    FfxResourceInternal *outResource)
{
	FFX_ASSERT(backendInterface);
	FFX_ASSERT(createResourceDescription);
	FFX_ASSERT(outResource);

	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);
	auto *device = backendContext->device;

	FFX_ASSERT(backendContext->nextStaticResource + 1 < backendContext->nextDynamicResource);
	outResource->internalIndex = backendContext->nextStaticResource++;
	auto &res = backendContext->resources[outResource->internalIndex];
	res.resourceDescription = createResourceDescription->resourceDescription;
	res.undefined = true; // A flag to make sure the first barrier for this image resource always uses an src layout of undefined
	res.state = createResourceDescription->initalState;

	auto name = convert_to_utf8(createResourceDescription->name);

	switch (createResourceDescription->resourceDescription.type)
	{
	case FFX_RESOURCE_TYPE_BUFFER:
	{
		Vulkan::BufferCreateInfo info = {};
		info.size = createResourceDescription->resourceDescription.width;
		info.usage = getVKBufferUsageFlagsFromResourceUsage(createResourceDescription->usage);
		info.domain = createResourceDescription->heapType == FFX_HEAP_TYPE_UPLOAD ?
		              Vulkan::BufferDomain::LinkedDeviceHost : Vulkan::BufferDomain::Device;

		// TODO: Could make this work if I have to.
		if (createResourceDescription->initDataSize < info.size)
			return FFX_ERROR_BACKEND_API_ERROR;

		res.buffer = device->create_buffer(info, createResourceDescription->initData);
		if (!res.buffer)
			return FFX_ERROR_OUT_OF_MEMORY;

		device->set_name(*res.buffer, name.c_str());
		break;
	}

	case FFX_RESOURCE_TYPE_TEXTURE1D:
	case FFX_RESOURCE_TYPE_TEXTURE2D:
	case FFX_RESOURCE_TYPE_TEXTURE3D:
	{
		Vulkan::ImageCreateInfo info = {};
		info.type = getVKImageTypeFromResourceType(createResourceDescription->resourceDescription.type);
		info.width = createResourceDescription->resourceDescription.width;
		info.height = createResourceDescription->resourceDescription.type == FFX_RESOURCE_TYPE_TEXTURE1D ?
		              1 : createResourceDescription->resourceDescription.height;
		info.depth = createResourceDescription->resourceDescription.type == FFX_RESOURCE_TYPE_TEXTURE3D ?
		             createResourceDescription->resourceDescription.depth : 1;
		info.levels = res.resourceDescription.mipCount;
		info.layers = 1;
		info.format = getVKFormatFromSurfaceFormat(createResourceDescription->resourceDescription.format);
		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage = getVKImageUsageFlagsFromResourceUsage(createResourceDescription->usage);
		info.samples = VK_SAMPLE_COUNT_1_BIT;

		Vulkan::ImageInitialData init = { createResourceDescription->initData };
		FFX_ASSERT(!init.data || (info.layers == 1 && info.levels == 1));
		if (init.data)
		{
			info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			res.undefined = false;
			res.state = FFX_RESOURCE_STATE_COMPUTE_READ;
			info.misc =
			    Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT |
			    Vulkan::IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT;
		}

		res.image = device->create_image(info, init.data ? &init : nullptr);
		if (!res.image)
			return FFX_ERROR_OUT_OF_MEMORY;

		res.resourceDescription.mipCount = res.image->get_create_info().levels;
		res.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;

		// create image views of individual mip levels for use as a uav
		uint32_t num_levels = res.image->get_create_info().levels;
		for (uint32_t mip = 0; mip < num_levels; mip++)
		{
			Vulkan::ImageViewCreateInfo view = {};
			view.levels = 1;
			view.layers = 1;
			view.base_level = mip;
			view.view_type = res.image->get_view().get_create_info().view_type;
			view.image = res.image.get();
			res.singleMipImageViews[mip] = device->create_image_view(view);
			if (!res.singleMipImageViews[mip])
				return FFX_ERROR_OUT_OF_MEMORY;
		}

		device->set_name(*res.image, name.c_str());
		break;
	}

	default:
		break;
	}

	return FFX_OK;
}

FfxResourceDescription Granite::FSR2::GetResourceDescriptor(FfxFsr2Interface *backendInterface,
                                                            FfxResourceInternal resource)
{
	FFX_ASSERT(backendInterface);
	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);

	if (resource.internalIndex != -1)
	{
		auto desc = backendContext->resources[resource.internalIndex].resourceDescription;
		return desc;
	}
	else
	{
		FfxResourceDescription desc = {};
		return desc;
	}
}

FfxErrorCode Granite::FSR2::RegisterResource(
	FfxFsr2Interface *backendInterface,
	const FfxResource *inFfxResource,
	FfxResourceInternal *outFfxResourceInternal)
{
	FFX_ASSERT(backendInterface);
	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);

	if (inFfxResource->resource == nullptr)
	{
		outFfxResourceInternal->internalIndex = FFX_FSR2_RESOURCE_IDENTIFIER_NULL;
		return FFX_OK;
	}

	FFX_ASSERT(backendContext->nextDynamicResource > backendContext->nextStaticResource);
	outFfxResourceInternal->internalIndex = backendContext->nextDynamicResource--;

	auto &backendResource = backendContext->resources[outFfxResourceInternal->internalIndex];

	backendResource.resourceDescription = inFfxResource->description;
	backendResource.state = inFfxResource->state;
	backendResource.undefined = false;

	if (inFfxResource->description.type == FFX_RESOURCE_TYPE_BUFFER)
	{
		auto *buffer = static_cast<Vulkan::Buffer *>(inFfxResource->resource);
		buffer->add_reference();
		backendResource.buffer = Vulkan::BufferHandle(buffer);
	}
	else
	{
		auto *image = static_cast<Vulkan::Image *>(inFfxResource->resource);
		if (image)
		{
			image->add_reference();
			backendResource.image = Vulkan::ImageHandle(image);

			if (inFfxResource->descriptorData && inFfxResource->isDepth)
				backendResource.aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
			else if (inFfxResource->descriptorData)
				backendResource.aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
			else
				backendResource.aspectFlags = 0;
		}
	}

	return FFX_OK;
}

FfxErrorCode Granite::FSR2::UnregisterResources(FfxFsr2Interface *backendInterface)
{
	FFX_ASSERT(backendInterface);
	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);

	while (backendContext->nextDynamicResource < int(FSR2_MAX_RESOURCE_COUNT - 1))
	{
		backendContext->nextDynamicResource++;
		backendContext->resources[backendContext->nextDynamicResource] = {};
	}

	return FFX_OK;
}

FfxErrorCode Granite::FSR2::GetDeviceCapabilities(FfxFsr2Interface *,
                                                  FfxDeviceCapabilities *deviceCapabilities,
                                                  FfxDevice device_)
{
	auto *device = static_cast<Vulkan::Device *>(device_);
	auto &features = device->get_device_features();

	// no shader model in vulkan so assume the minimum
	deviceCapabilities->raytracingSupported = false;
	deviceCapabilities->fp16Supported = features.float16_int8_features.shaderFloat16 &&
	                                    features.storage_16bit_features.storageBuffer16BitAccess;

	if (features.subgroup_size_control_features.subgroupSizeControl)
	{
		deviceCapabilities->waveLaneCountMin = features.subgroup_size_control_properties.minSubgroupSize;
		deviceCapabilities->waveLaneCountMax = features.subgroup_size_control_properties.maxSubgroupSize;
	}
	else
	{
		deviceCapabilities->waveLaneCountMin = features.subgroup_properties.subgroupSize;
		deviceCapabilities->waveLaneCountMax = features.subgroup_properties.subgroupSize;
	}

	if (features.subgroup_size_control_features.computeFullSubgroups &&
	    (features.subgroup_size_control_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0 &&
	    deviceCapabilities->fp16Supported)
	{
		deviceCapabilities->minimumSupportedShaderModel = FFX_SHADER_MODEL_6_6;
	}
	else if (deviceCapabilities->fp16Supported)
	{
		deviceCapabilities->minimumSupportedShaderModel = FFX_SHADER_MODEL_6_2;
	}
	else if (features.subgroup_properties.subgroupSize >= 4 &&
	         (features.subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT))
	{
		deviceCapabilities->minimumSupportedShaderModel = FFX_SHADER_MODEL_6_0;
	}
	else
	{
		deviceCapabilities->minimumSupportedShaderModel = FFX_SHADER_MODEL_5_1;
	}

	return FFX_OK;
}

FfxErrorCode Granite::FSR2::DestroyResource(FfxFsr2Interface *backendInterface, FfxResourceInternal resource)
{
	FFX_ASSERT(backendInterface);

	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);

	if (resource.internalIndex != -1)
	{
		auto &res = backendContext->resources[resource.internalIndex];
		res = {};
	}

	return FFX_OK;
}

#include "ffx_fsr2_granite_static_reflection.h"

FfxErrorCode Granite::FSR2::CreatePipeline(FfxFsr2Interface *backendInterface, FfxFsr2Pass pass,
                                           const FfxPipelineDescription *pipelineDescription,
                                           FfxPipelineState *outPipeline)
{
	FFX_ASSERT(backendInterface);
	FFX_ASSERT(pipelineDescription);

	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);
	auto *device = backendContext->device;

	// check if we can force wave64
	bool canForceWave64 = device->supports_subgroup_size_log2(false, 6, 6);
	bool useLut = canForceWave64;

	// check if we have 16bit floating point.
	//bool supportedFP16 = device->get_device_features().float16_int8_features.shaderFloat16;
	// TODO: Figure out rendering glitches with FP32.
	bool supportedFP16 = false;

	if (pass == FFX_FSR2_PASS_ACCUMULATE || pass == FFX_FSR2_PASS_ACCUMULATE_SHARPEN)
	{
		// Workaround: Disable FP16 path for the accumulate pass on NVIDIA due to reduced occupancy and high VRAM throughput.
		if (device->get_device_features().driver_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
			supportedFP16 = false;
	}

	// work out what permutation to load.
	uint32_t flags = 0;
	flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE) ? FSR2_SHADER_PERMUTATION_HDR_COLOR_INPUT : 0;
	flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) ? 0 : FSR2_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS;
	flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) ? FSR2_SHADER_PERMUTATION_JITTER_MOTION_VECTORS : 0;
	flags |= (pipelineDescription->contextFlags & FFX_FSR2_ENABLE_DEPTH_INVERTED) ? FSR2_SHADER_PERMUTATION_DEPTH_INVERTED : 0;
	flags |= pass == FFX_FSR2_PASS_ACCUMULATE_SHARPEN ? FSR2_SHADER_PERMUTATION_ENABLE_SHARPENING : 0;
	flags |= useLut ? FSR2_SHADER_PERMUTATION_LANCZOS_LUT : 0;
	flags |= supportedFP16 ? FSR2_SHADER_PERMUTATION_ALLOW_FP16 : 0;

	Fsr2ShaderBlobGranite shaderBlob = {};
	fsr2GetPermutationBlobByIndex(pass, flags, &shaderBlob);
	FFX_ASSERT(!shaderBlob.path.empty());

	Pipeline pipeline = {};
	auto *program = device->get_shader_manager().register_compute(shaderBlob.path);
	if (!program)
		return FFX_ERROR_BACKEND_API_ERROR;

	pipeline.variant = program->register_variant(shaderBlob.defines);
	pipeline.force_wave64 = canForceWave64;

	FFX_ASSERT(backendContext->nextPipelineIndex < FFX_FSR2_PASS_COUNT);
	backendContext->pipelines[backendContext->nextPipelineIndex] = pipeline;

	outPipeline->pipeline = &backendContext->pipelines[backendContext->nextPipelineIndex];
	outPipeline->rootSignature = {};

	auto &set_layout = pipeline.variant->get_program()->get_pipeline_layout()->get_resource_layout().sets[1];
	outPipeline->srvCount = 0;
	outPipeline->uavCount = 0;
	outPipeline->constCount = 0;

	Util::for_each_bit(set_layout.separate_image_mask |
	                   set_layout.sampled_texel_buffer_mask, [&](unsigned bit) {
		outPipeline->srvResourceBindings[outPipeline->srvCount].slotIndex = bit;
		wcscpy(outPipeline->srvResourceBindings[outPipeline->srvCount].name,
		       shaderBlob.name_table[bit]);
		outPipeline->srvCount++;
	});

	Util::for_each_bit(set_layout.storage_image_mask |
	                   set_layout.storage_buffer_mask |
	                   set_layout.storage_texel_buffer_mask, [&](unsigned bit) {
		outPipeline->uavResourceBindings[outPipeline->uavCount].slotIndex = bit;
		wcscpy(outPipeline->uavResourceBindings[outPipeline->uavCount].name,
		       shaderBlob.name_table[bit]);
		outPipeline->uavCount++;
	});

	Util::for_each_bit(set_layout.uniform_buffer_mask, [&](unsigned bit) {
		outPipeline->cbResourceBindings[outPipeline->constCount].slotIndex = bit;
		wcscpy(outPipeline->cbResourceBindings[outPipeline->constCount].name,
		       shaderBlob.name_table[bit]);
		outPipeline->constCount++;
	});

	backendContext->nextPipelineIndex++;

	return FFX_OK;
}

FfxErrorCode Granite::FSR2::DestroyPipeline(FfxFsr2Interface *backendInterface, FfxPipelineState *pipeline)
{
	FFX_ASSERT(backendInterface);
	if (!pipeline)
		return FFX_OK;
	*static_cast<Pipeline *>(pipeline->pipeline) = {};
	return FFX_OK;
}

FfxErrorCode Granite::FSR2::ScheduleRenderJob(FfxFsr2Interface *backendInterface, const FfxGpuJobDescription *job)
{
	FFX_ASSERT(backendInterface);
	FFX_ASSERT(job);

	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);
	FFX_ASSERT(backendContext->renderJobCount < FSR2_MAX_GPU_JOBS);
	backendContext->renderJobs[backendContext->renderJobCount] = *job;

	if (job->jobType == FFX_GPU_JOB_COMPUTE)
	{
		// needs to copy SRVs and UAVs in case they are on the stack only
		auto *computeJob = &backendContext->renderJobs[backendContext->renderJobCount].computeJobDescriptor;
		const uint32_t numConstBuffers = job->computeJobDescriptor.pipeline.constCount;
		for (uint32_t currentRootConstantIndex = 0; currentRootConstantIndex < numConstBuffers; currentRootConstantIndex++)
		{
			computeJob->cbs[currentRootConstantIndex].uint32Size = job->computeJobDescriptor.cbs[currentRootConstantIndex].uint32Size;
			memcpy(computeJob->cbs[currentRootConstantIndex].data,
			       job->computeJobDescriptor.cbs[currentRootConstantIndex].data,
			       computeJob->cbs[currentRootConstantIndex].uint32Size * sizeof(uint32_t));
		}
	}

	backendContext->renderJobCount++;
	return FFX_OK;
}

static VkImageLayout getVKImageLayoutFromResourceState(FfxResourceStates state)
{
	switch (state)
	{
	case FFX_RESOURCE_STATE_GENERIC_READ:
	case FFX_RESOURCE_STATE_UNORDERED_ACCESS:
		return VK_IMAGE_LAYOUT_GENERAL;
	case FFX_RESOURCE_STATE_COMPUTE_READ:
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	case FFX_RESOURCE_STATE_COPY_SRC:
		return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	case FFX_RESOURCE_STATE_COPY_DEST:
		return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	default:
		return VK_IMAGE_LAYOUT_GENERAL;
	}
}

static VkPipelineStageFlags getVKPipelineStageFlagsFromResourceState(FfxResourceStates state)
{
	switch (state)
	{
	case FFX_RESOURCE_STATE_GENERIC_READ:
	case FFX_RESOURCE_STATE_UNORDERED_ACCESS:
	case FFX_RESOURCE_STATE_COMPUTE_READ:
		return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case FFX_RESOURCE_STATE_COPY_SRC:
	case FFX_RESOURCE_STATE_COPY_DEST:
		return VK_PIPELINE_STAGE_TRANSFER_BIT;
	default:
		return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}
}

static VkAccessFlags getVKAccessFlagsFromResourceState(FfxResourceStates state)
{
	switch (state)
	{
	case FFX_RESOURCE_STATE_GENERIC_READ:
		return VK_ACCESS_SHADER_READ_BIT;
	case FFX_RESOURCE_STATE_UNORDERED_ACCESS:
		return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	case FFX_RESOURCE_STATE_COMPUTE_READ:
		return VK_ACCESS_SHADER_READ_BIT;
	case FFX_RESOURCE_STATE_COPY_SRC:
		return VK_ACCESS_TRANSFER_READ_BIT;
	case FFX_RESOURCE_STATE_COPY_DEST:
		return VK_ACCESS_TRANSFER_WRITE_BIT;
	default:
		return VK_ACCESS_SHADER_READ_BIT;
	}
}

static void flushBarriers(Granite::FSR2::Context *backendContext, Vulkan::CommandBuffer &cmd)
{
	FFX_ASSERT(backendContext);

	if (backendContext->bufferBarrierCount > 0 || backendContext->imageBarrierCount > 0)
	{
		cmd.barrier(backendContext->srcStageMask, backendContext->dstStageMask,
		            0, nullptr,
		            backendContext->bufferBarrierCount, backendContext->bufferBarriers,
		            backendContext->imageBarrierCount, backendContext->imageBarriers);
		backendContext->imageBarrierCount = 0;
		backendContext->bufferBarrierCount = 0;
		backendContext->srcStageMask = 0;
		backendContext->dstStageMask = 0;
	}
}

static void addBarrier(Granite::FSR2::Context *backendContext, FfxResourceInternal *resource, FfxResourceStates newState)
{
	FFX_ASSERT(backendContext);
	FFX_ASSERT(resource);

	auto &ffxResource = backendContext->resources[resource->internalIndex];
	auto &curState = backendContext->resources[resource->internalIndex].state;

	if (curState == newState && newState != FFX_RESOURCE_STATE_UNORDERED_ACCESS &&
	    newState != FFX_RESOURCE_STATE_COPY_DEST && !ffxResource.undefined)
	{
		return;
	}

	if (ffxResource.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
	{
		auto &buffer = *ffxResource.buffer;
		auto *barrier = &backendContext->bufferBarriers[backendContext->bufferBarrierCount];

		barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier->pNext = nullptr;
		barrier->srcAccessMask = getVKAccessFlagsFromResourceState(curState);
		barrier->dstAccessMask = getVKAccessFlagsFromResourceState(newState);
		barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->buffer = buffer.get_buffer();
		barrier->offset = 0;
		barrier->size = VK_WHOLE_SIZE;

		backendContext->srcStageMask |= getVKPipelineStageFlagsFromResourceState(curState);
		backendContext->dstStageMask |= getVKPipelineStageFlagsFromResourceState(newState);
		backendContext->bufferBarrierCount++;
	}
	else
	{
		auto &image = *ffxResource.image;
		auto *barrier = &backendContext->imageBarriers[backendContext->imageBarrierCount];

		VkImageSubresourceRange range;
		range.aspectMask = backendContext->resources[resource->internalIndex].aspectFlags;
		range.baseMipLevel = 0;
		range.levelCount = backendContext->resources[resource->internalIndex].resourceDescription.mipCount;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier->pNext = nullptr;
		barrier->srcAccessMask = getVKAccessFlagsFromResourceState(curState);
		barrier->dstAccessMask = getVKAccessFlagsFromResourceState(newState);
		barrier->oldLayout = ffxResource.undefined ? VK_IMAGE_LAYOUT_UNDEFINED : getVKImageLayoutFromResourceState(curState);
		barrier->newLayout = getVKImageLayoutFromResourceState(newState);
		barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier->image = image.get_image();
		barrier->subresourceRange = range;

		backendContext->srcStageMask |= getVKPipelineStageFlagsFromResourceState(curState);
		backendContext->dstStageMask |= getVKPipelineStageFlagsFromResourceState(newState);
		backendContext->imageBarrierCount++;
	}

	curState = newState;
	if (ffxResource.undefined)
		ffxResource.undefined = false;
}

static FfxErrorCode executeRenderJobClearFloat(Granite::FSR2::Context *backendContext,
                                               FfxGpuJobDescription* job, Vulkan::CommandBuffer &cmd)
{
	uint32_t idx = job->clearJobDescriptor.target.internalIndex;
	const auto &ffxResource = backendContext->resources[idx];

	if (ffxResource.resourceDescription.type != FFX_RESOURCE_TYPE_BUFFER)
	{
		addBarrier(backendContext, &job->clearJobDescriptor.target, FFX_RESOURCE_STATE_COPY_DEST);
		flushBarriers(backendContext, cmd);

		VkClearValue clearValue = {};
		auto &clearColorValue = clearValue.color;
		clearColorValue.float32[0] = job->clearJobDescriptor.color[0];
		clearColorValue.float32[1] = job->clearJobDescriptor.color[1];
		clearColorValue.float32[2] = job->clearJobDescriptor.color[2];
		clearColorValue.float32[3] = job->clearJobDescriptor.color[3];

		cmd.clear_image(*ffxResource.image, clearValue);
	}

	return FFX_OK;
}

static FfxErrorCode executeRenderJobCopy(Granite::FSR2::Context *backendContext,
                                         FfxGpuJobDescription* job, Vulkan::CommandBuffer &cmd)
{
	auto &ffxResourceSrc = backendContext->resources[job->copyJobDescriptor.src.internalIndex];
	auto &ffxResourceDst = backendContext->resources[job->copyJobDescriptor.dst.internalIndex];

	addBarrier(backendContext, &job->copyJobDescriptor.src, FFX_RESOURCE_STATE_COPY_SRC);
	addBarrier(backendContext, &job->copyJobDescriptor.dst, FFX_RESOURCE_STATE_COPY_DEST);
	flushBarriers(backendContext, cmd);

	if (ffxResourceSrc.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER &&
	    ffxResourceDst.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER)
	{
		auto &src = *ffxResourceSrc.buffer;
		auto &dst = *ffxResourceDst.buffer;
		cmd.copy_buffer(dst, src);
	}
	else if (ffxResourceSrc.resourceDescription.type == FFX_RESOURCE_TYPE_BUFFER &&
	         ffxResourceDst.resourceDescription.type != FFX_RESOURCE_TYPE_BUFFER)
	{
		auto &src = *ffxResourceSrc.buffer;
		auto &dst = *ffxResourceDst.image;

		VkImageSubresourceLayers subresourceLayers = {};
		VkExtent3D extent = {};

		subresourceLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceLayers.baseArrayLayer = 0;
		subresourceLayers.layerCount = 1;
		subresourceLayers.mipLevel = 0;

		extent.width = ffxResourceDst.resourceDescription.width;
		extent.height = ffxResourceDst.resourceDescription.height;
		extent.depth = ffxResourceDst.resourceDescription.depth;

		cmd.copy_buffer_to_image(dst, src, 0, {}, extent, 0, 0, subresourceLayers);
	}
	else
	{
		auto &src = *ffxResourceSrc.image;
		auto &dst = *ffxResourceDst.image;
		cmd.copy_image(dst, src);
	}

	return FFX_OK;
}

static FfxErrorCode executeRenderJobCompute(Granite::FSR2::Context *backendContext,
                                            FfxGpuJobDescription *job,
                                            Vulkan::CommandBuffer &cmd)
{
	// bind uavs
	for (uint32_t uav = 0; uav < job->computeJobDescriptor.pipeline.uavCount; uav++)
	{
		addBarrier(backendContext, &job->computeJobDescriptor.uavs[uav], FFX_RESOURCE_STATE_UNORDERED_ACCESS);
		auto &ffxResource = backendContext->resources[job->computeJobDescriptor.uavs[uav].internalIndex];
		auto *single_mip_view = ffxResource.singleMipImageViews[job->computeJobDescriptor.uavMip[uav]].get();
		cmd.set_storage_texture(1, job->computeJobDescriptor.pipeline.uavResourceBindings[uav].slotIndex,
		                        single_mip_view ? *single_mip_view : ffxResource.image->get_view());
	}

	// bind srvs
	for (uint32_t srv = 0; srv < job->computeJobDescriptor.pipeline.srvCount; srv++)
	{
		addBarrier(backendContext, &job->computeJobDescriptor.srvs[srv], FFX_RESOURCE_STATE_COMPUTE_READ);
		auto &ffxResource = backendContext->resources[job->computeJobDescriptor.srvs[srv].internalIndex];
		cmd.set_texture(1, job->computeJobDescriptor.pipeline.srvResourceBindings[srv].slotIndex,
		                ffxResource.image->get_view());
	}

	// update ubos
	for (uint32_t i = 0; i < job->computeJobDescriptor.pipeline.constCount; i++)
	{
		void *data = cmd.allocate_constant_data(1, job->computeJobDescriptor.pipeline.cbResourceBindings[i].slotIndex,
		                                        job->computeJobDescriptor.cbs[i].uint32Size * sizeof(uint32_t));
		memcpy(data, job->computeJobDescriptor.cbs[i].data,
		       job->computeJobDescriptor.cbs[i].uint32Size * sizeof(uint32_t));
	}

	// insert all the barriers
	flushBarriers(backendContext, cmd);

	auto *pipeline = static_cast<const Granite::FSR2::Pipeline *>(job->computeJobDescriptor.pipeline.pipeline);
	cmd.set_program(pipeline->variant->get_program());

	if (pipeline->force_wave64)
	{
		cmd.enable_subgroup_size_control(true);
		cmd.set_subgroup_size_log2(false, 6, 6);
	}

	cmd.set_sampler(0, 0, Vulkan::StockSampler::NearestClamp);
	cmd.set_sampler(0, 1, Vulkan::StockSampler::LinearClamp);

	cmd.dispatch(job->computeJobDescriptor.dimensions[0], job->computeJobDescriptor.dimensions[1], job->computeJobDescriptor.dimensions[2]);
	cmd.enable_subgroup_size_control(false);

	return FFX_OK;
}

FfxErrorCode Granite::FSR2::ExecuteRenderJobs(FfxFsr2Interface *backendInterface, FfxCommandList commandList)
{
	FFX_ASSERT(backendInterface);
	auto *backendContext = static_cast<Granite::FSR2::Context *>(backendInterface->scratchBuffer);
	FfxErrorCode errorCode = FFX_OK;

	// execute all renderjobs
	for (uint32_t i = 0; i < backendContext->renderJobCount; i++)
	{
		FfxGpuJobDescription* renderJob = &backendContext->renderJobs[i];
		auto *cmd = static_cast<Vulkan::CommandBuffer *>(commandList);
		FFX_ASSERT(cmd);

		switch (renderJob->jobType)
		{
		case FFX_GPU_JOB_CLEAR_FLOAT:
			errorCode = executeRenderJobClearFloat(backendContext, renderJob, *cmd);
			break;

		case FFX_GPU_JOB_COPY:
			errorCode = executeRenderJobCopy(backendContext, renderJob, *cmd);
			break;

		case FFX_GPU_JOB_COMPUTE:
			errorCode = executeRenderJobCompute(backendContext, renderJob, *cmd);
			break;

		default:
		    break;
		}
	}

	// check the execute function returned cleanly.
	FFX_RETURN_ON_ERROR(errorCode == FFX_OK, FFX_ERROR_BACKEND_API_ERROR);
	backendContext->renderJobCount = 0;
	return FFX_OK;
}
