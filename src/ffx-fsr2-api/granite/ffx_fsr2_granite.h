/* Copyright (c) 2017-2023 Hans-Kristian Arntzen
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

#pragma once

#include "ffx_fsr2_interface.h"
#include "ffx_fsr2.h"

namespace Vulkan
{
class Device;
class CommandBuffer;
class Image;
class ImageView;
class Buffer;
}

extern "C"
{
FFX_API size_t ffxFsr2GetScratchMemorySizeGranite();
FFX_API FfxErrorCode ffxFsr2GetInterfaceGranite(
	FfxFsr2Interface *outInterface,
	void *scratchBuffer,
	size_t scratchBufferSize);
FFX_API FfxDevice ffxGetDeviceGranite(Vulkan::Device *device);
FFX_API FfxCommandList ffxGetCommandListGranite(Vulkan::CommandBuffer *cmd);
FFX_API FfxResource ffxGetTextureResourceGranite(
	FfxFsr2Context* context,
	const Vulkan::Image *img, const Vulkan::ImageView *view,
	FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ);
FFX_API FfxResource ffxGetBufferResourceGranite(
	FfxFsr2Context *context,
	const Vulkan::Buffer *buffer,
	FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ);
}
