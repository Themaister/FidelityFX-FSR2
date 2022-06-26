/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
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

#include <string>
#include <vector>
#include <utility>

// Hardcode the reflection information.
// Quite awkward to link this by name, but oh well ...
static const char FSR2_BIND_SRV_INPUT_COLOR[] = "r_input_color_jittered";
static const char FSR2_BIND_SRV_EXPOSURE[] = "r_exposure";
static const char FSR2_BIND_SRV_DILATED_DEPTH[] = "r_dilatedDepth";
static const char FSR2_BIND_SRV_DILATED_MOTION_VECTORS[] = "r_dilated_motion_vectors";
static const char FSR2_BIND_SRV_RECONSTRUCTED_PREV_NEAREST_DEPTH[] = "r_ReconstructedPrevNearestDepth";
static const char FSR2_BIND_SRV_MOTION_VECTORS[] = "r_motion_vectors";
static const char FSR2_BIND_SRV_DEPTH[] = "r_depth";
static const char FSR2_BIND_SRV_REACTIVE_MASK[] = "r_reactive_mask";
static const char FSR2_BIND_SRV_LOCK_STATUS[] = "r_lock_status";
static const char FSR2_BIND_SRV_PREPARED_INPUT_COLOR[] = "r_prepared_input_color";
static const char FSR2_BIND_SRV_TRANSPARENCY_AND_COMPOSITION_MASK[] = "r_transparency_and_composition_mask";
static const char FSR2_BIND_SRV_INTERNAL_UPSCALED[] = "r_internal_upscaled_color";
static const char FSR2_BIND_SRV_DEPTH_CLIP[] = "r_depth_clip";
static const char FSR2_BIND_SRV_LUMA_HISTORY[] = "r_luma_history";
static const char FSR2_BIND_SRV_LANCZOS_LUT[] = "r_lanczos_lut";
static const char FSR2_BIND_SRV_UPSCALE_MAXIMUM_BIAS_LUT[] = "r_upsample_maximum_bias_lut";
static const char FSR2_BIND_SRV_REACTIVE_MAX[] = "r_reactive_max";
static const char FSR2_BIND_SRV_EXPOSURE_MIPS[] = "r_imgMips";
static const char FSR2_BIND_SRV_RCAS_INPUT[] = "r_rcas_input";
static const char FSR2_BIND_SRV_PRE_ALPHA_COLOR[] = "r_input_color_pre_alpha";
static const char FSR2_BIND_SRV_POST_ALPHA_COLOR[] = "r_input_color_post_alpha";

static const char FSR2_BIND_UAV_DEPTH_CLIP[] = "rw_depth_clip";
static const char FSR2_BIND_UAV_RECONSTRUCTED_PREV_NEAREST_DEPTH[] = "rw_ReconstructedPrevNearestDepth";
static const char FSR2_BIND_UAV_PREPARED_INPUT_COLOR[] =  "rw_prepared_input_color";
static const char FSR2_BIND_UAV_LUMA_HISTORY[] = "rw_luma_history";
static const char FSR2_BIND_UAV_DILATED_MOTION_VECTORS[] = "rw_dilated_motion_vectors";
static const char FSR2_BIND_UAV_DILATED_DEPTH[] = "rw_dilatedDepth";
static const char FSR2_BIND_UAV_LOCK_STATUS[] = "rw_lock_status";
static const char FSR2_BIND_UAV_REACTIVE_MASK_MAX[] = "rw_reactive_max";
static const char FSR2_BIND_UAV_INTERNAL_UPSCALED[] = "rw_internal_upscaled_color";
static const char FSR2_BIND_UAV_UPSCALED_OUTPUT[] = "rw_upscaled_output";
static const char FSR2_BIND_UAV_SPD_GLOBAL_ATOMIC[] = "rw_spd_global_atomic";
static const char FSR2_BIND_UAV_EXPOSURE_MIP_LUMA_CHANGE[] = "rw_img_mip_shading_change";
static const char FSR2_BIND_UAV_EXPOSURE_MIP_5[] = "rw_img_mip_5";
static const char FSR2_BIND_UAV_EXPOSURE[] = "rw_exposure";
static const char FSR2_BIND_UAV_REACTIVE[] = "rw_output_reactive_mask";

static const char FSR2_BIND_CB_FSR2[] = "cbFSR2";
static const char FSR2_BIND_CB_RCAS[] = "cbRCAS";
static const char FSR2_BIND_CB_SPD[] = "cbSPD";
static const char FSR2_BIND_CB_REACTIVE[] = "cbGenerateReactive";

static const char *input_color_table[] = {
	FSR2_BIND_SRV_INPUT_COLOR,
	FSR2_BIND_SRV_EXPOSURE,
	FSR2_BIND_UAV_RECONSTRUCTED_PREV_NEAREST_DEPTH,
	FSR2_BIND_UAV_PREPARED_INPUT_COLOR,
	FSR2_BIND_UAV_LUMA_HISTORY,
	FSR2_BIND_CB_FSR2,
};

static const char *depth_clip_table[] = {
	FSR2_BIND_SRV_RECONSTRUCTED_PREV_NEAREST_DEPTH,
	FSR2_BIND_SRV_DILATED_MOTION_VECTORS,
	FSR2_BIND_SRV_DILATED_DEPTH,
	FSR2_BIND_UAV_DEPTH_CLIP,
	FSR2_BIND_CB_FSR2,
};

static const char *reconstruct_previous_depth_table[] = {
	FSR2_BIND_SRV_MOTION_VECTORS,
	FSR2_BIND_SRV_DEPTH,
	FSR2_BIND_UAV_RECONSTRUCTED_PREV_NEAREST_DEPTH,
	FSR2_BIND_UAV_DILATED_MOTION_VECTORS,
	FSR2_BIND_UAV_DILATED_DEPTH,
	FSR2_BIND_CB_FSR2,
};

static const char *lock_table[] = {
	FSR2_BIND_SRV_REACTIVE_MASK,
	FSR2_BIND_SRV_LOCK_STATUS,
	FSR2_BIND_SRV_PREPARED_INPUT_COLOR,
	FSR2_BIND_UAV_LOCK_STATUS,
	FSR2_BIND_UAV_REACTIVE_MASK_MAX,
	FSR2_BIND_CB_FSR2,
};

static const char *accumulate_table[] = {
	FSR2_BIND_SRV_EXPOSURE,
	FSR2_BIND_SRV_TRANSPARENCY_AND_COMPOSITION_MASK,
	FSR2_BIND_SRV_DILATED_MOTION_VECTORS,
	FSR2_BIND_SRV_INTERNAL_UPSCALED,
	FSR2_BIND_SRV_LOCK_STATUS,
	FSR2_BIND_SRV_DEPTH_CLIP,
	FSR2_BIND_SRV_PREPARED_INPUT_COLOR,
	FSR2_BIND_SRV_LUMA_HISTORY,
	FSR2_BIND_SRV_LANCZOS_LUT,
	FSR2_BIND_SRV_UPSCALE_MAXIMUM_BIAS_LUT,
	FSR2_BIND_SRV_REACTIVE_MAX,
	FSR2_BIND_SRV_EXPOSURE_MIPS,
	FSR2_BIND_UAV_INTERNAL_UPSCALED,
	FSR2_BIND_UAV_LOCK_STATUS,
	FSR2_BIND_UAV_UPSCALED_OUTPUT,
	FSR2_BIND_CB_FSR2,
};

static const char *rcas_table[] = {
	FSR2_BIND_SRV_EXPOSURE,
	FSR2_BIND_SRV_RCAS_INPUT,
	FSR2_BIND_UAV_UPSCALED_OUTPUT,
	FSR2_BIND_CB_FSR2,
	FSR2_BIND_CB_RCAS,
};

static const char *compute_luminance_pyramid_table[] = {
	FSR2_BIND_SRV_INPUT_COLOR,
	FSR2_BIND_UAV_SPD_GLOBAL_ATOMIC,
	FSR2_BIND_UAV_EXPOSURE_MIP_LUMA_CHANGE,
	FSR2_BIND_UAV_EXPOSURE_MIP_5,
	FSR2_BIND_UAV_EXPOSURE,
	FSR2_BIND_CB_FSR2,
	FSR2_BIND_CB_SPD,
};

static const char *generate_reactive_table[] = {
	FSR2_BIND_SRV_PRE_ALPHA_COLOR,
	FSR2_BIND_SRV_POST_ALPHA_COLOR,
	FSR2_BIND_UAV_REACTIVE,
	FSR2_BIND_CB_REACTIVE,
	FSR2_BIND_CB_FSR2,
};

struct Fsr2ShaderBlobGranite
{
	std::string path;
	std::vector<std::pair<std::string, int>> defines;
	const char * const *name_table;
};

static FfxErrorCode fsr2GetPermutationBlobByIndex(FfxFsr2Pass passId, uint32_t permutationOptions, Fsr2ShaderBlobGranite *outBlob)
{
	switch (passId)
	{
	case FFX_FSR2_PASS_PREPARE_INPUT_COLOR:
		outBlob->path = "fsr2://ffx_fsr2_prepare_input_color_pass.glsl";
		outBlob->name_table = input_color_table;
		break;

	case FFX_FSR2_PASS_DEPTH_CLIP:
		outBlob->path = "fsr2://ffx_fsr2_depth_clip_pass.glsl";
		outBlob->name_table = depth_clip_table;
		break;

	case FFX_FSR2_PASS_RECONSTRUCT_PREVIOUS_DEPTH:
		outBlob->path = "fsr2://ffx_fsr2_reconstruct_previous_depth_pass.glsl";
		outBlob->name_table = reconstruct_previous_depth_table;
		break;

	case FFX_FSR2_PASS_LOCK:
		outBlob->path = "fsr2://ffx_fsr2_lock_pass.glsl";
		outBlob->name_table = lock_table;
		break;

	case FFX_FSR2_PASS_ACCUMULATE:
	case FFX_FSR2_PASS_ACCUMULATE_SHARPEN:
		outBlob->path = "fsr2://ffx_fsr2_accumulate_pass.glsl";
		outBlob->name_table = accumulate_table;
		break;

	case FFX_FSR2_PASS_RCAS:
		outBlob->path = "fsr2://ffx_fsr2_rcas_pass.glsl";
		outBlob->name_table = rcas_table;
		break;

	case FFX_FSR2_PASS_COMPUTE_LUMINANCE_PYRAMID:
		outBlob->path = "fsr2://ffx_fsr2_compute_luminance_pyramid_pass.glsl";
		// From FSR2 build system. FP16 is explicitly disabled.
		permutationOptions &= ~Granite::FSR2::FSR2_SHADER_PERMUTATION_ALLOW_FP16;
		outBlob->name_table = compute_luminance_pyramid_table;
		break;

	case FFX_FSR2_PASS_GENERATE_REACTIVE:
		outBlob->path = "fsr2://ffx_fsr2_autogen_reactive_pass.glsl";
		outBlob->name_table = generate_reactive_table;
		break;

	default:
		return FFX_ERROR_INVALID_ALIGNMENT;
	}

	outBlob->defines.emplace_back(std::string("FFX_GLSL"), 1);
	outBlob->defines.emplace_back(std::string("FFX_GPU"), 1);
	outBlob->defines.emplace_back(std::string("FFX_HALF"),
	                              (permutationOptions & Granite::FSR2::FSR2_SHADER_PERMUTATION_ALLOW_FP16) ? 1 : 0);
	outBlob->defines.emplace_back(std::string("FFX_FSR2_OPTION_USE_LANCZOS_LUT"),
	                              (permutationOptions & Granite::FSR2::FSR2_SHADER_PERMUTATION_LANCZOS_LUT) ? 1 : 0);
	outBlob->defines.emplace_back(std::string("FFX_FSR2_OPTION_HDR_COLOR_INPUT"),
	                              (permutationOptions & Granite::FSR2::FSR2_SHADER_PERMUTATION_HDR_COLOR_INPUT) ? 1 : 0);
	outBlob->defines.emplace_back(std::string("FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS"),
	                              (permutationOptions & Granite::FSR2::FSR2_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS) ? 1 : 0);
	outBlob->defines.emplace_back(std::string("FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS"),
	                              (permutationOptions & Granite::FSR2::FSR2_SHADER_PERMUTATION_JITTER_MOTION_VECTORS) ? 1 : 0);
	outBlob->defines.emplace_back(std::string("FFX_FSR2_OPTION_INVERTED_DEPTH"),
	                              (permutationOptions & Granite::FSR2::FSR2_SHADER_PERMUTATION_DEPTH_INVERTED) ? 1 : 0);
	outBlob->defines.emplace_back(std::string("FFX_FSR2_OPTION_APPLY_SHARPENING"),
	                              (permutationOptions & Granite::FSR2::FSR2_SHADER_PERMUTATION_ENABLE_SHARPENING) ? 1 : 0);
	return FFX_OK;
}
