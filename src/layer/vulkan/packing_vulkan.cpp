// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2019 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "packing_vulkan.h"

namespace ncnn {

DEFINE_LAYER_CREATOR(Packing_vulkan)

Packing_vulkan::Packing_vulkan()
{
    support_vulkan = false;

    pipeline_packing_1to4 = 0;
    pipeline_packing_4to1 = 0;
}

int Packing_vulkan::create_pipeline(const Option& opt)
{
    std::vector<vk_specialization_type> specializations;

    if (out_packing == 4)
    {
        pipeline_packing_1to4 = new Pipeline(vkdev);
        pipeline_packing_1to4->set_optimal_local_size_xyz();
        pipeline_packing_1to4->create("packing_1to4", specializations, 2, 10);
    }

    if (out_packing == 1)
    {
        pipeline_packing_4to1 = new Pipeline(vkdev);
        pipeline_packing_4to1->set_optimal_local_size_xyz();
        pipeline_packing_4to1->create("packing_4to1", specializations, 2, 10);
    }

    return 0;
}

int Packing_vulkan::destroy_pipeline(const Option& opt)
{
    delete pipeline_packing_1to4;
    pipeline_packing_1to4 = 0;

    delete pipeline_packing_4to1;
    pipeline_packing_4to1 = 0;

    return 0;
}

int Packing_vulkan::forward(const VkMat& bottom_blob, VkMat& top_blob, VkCompute& cmd, const Option& opt) const
{
    int packing = bottom_blob.packing;

    if (packing == out_packing)
    {
        top_blob = bottom_blob;
        return 0;
    }

    int w = bottom_blob.w;
    int h = bottom_blob.h;
    int channels = bottom_blob.c;
    int dims = bottom_blob.dims;
    size_t elemsize = bottom_blob.elemsize;

    if (!use_padding)
    {
        // identity if use_padding not allowed
        if (dims == 1 && w * packing % out_packing != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
        if (dims == 2 && h * packing % out_packing != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
        if (dims == 3 && channels * packing % out_packing != 0)
        {
            top_blob = bottom_blob;
            return 0;
        }
    }

    if (dims == 1)
    {
        if (vkdev->info.support_fp16_storage && out_packing == 1)
        {
            top_blob = bottom_blob;
            top_blob.w = w * packing;
            top_blob.cstep = w * packing;
            top_blob.elemsize = elemsize / packing;
            top_blob.packing = out_packing;
            return 0;
        }

        int outw = (w * packing + out_packing - 1) / out_packing;
        size_t out_elemsize = elemsize / packing * out_packing;
        if (vkdev->info.support_fp16_packed && !vkdev->info.support_fp16_storage)
        {
            if (out_packing == 4) out_elemsize = 4*2u;
            if (out_packing == 1) out_elemsize = 4u;
        }

        top_blob.create(outw, out_elemsize, out_packing, opt.blob_vkallocator, opt.staging_vkallocator);
        if (top_blob.empty())
            return -100;
    }

    if (dims == 2)
    {
        int outh = (h * packing + out_packing - 1) / out_packing;
        size_t out_elemsize = elemsize / packing * out_packing;
        if (vkdev->info.support_fp16_packed && !vkdev->info.support_fp16_storage)
        {
            if (out_packing == 4) out_elemsize = 4*2u;
            if (out_packing == 1) out_elemsize = 4u;
        }

        top_blob.create(w, outh, out_elemsize, out_packing, opt.blob_vkallocator, opt.staging_vkallocator);
        if (top_blob.empty())
            return -100;
    }

    if (dims == 3)
    {
        int outc = (channels * packing + out_packing - 1) / out_packing;
        size_t out_elemsize = elemsize / packing * out_packing;
        if (vkdev->info.support_fp16_packed && !vkdev->info.support_fp16_storage)
        {
            if (out_packing == 4) out_elemsize = 4*2u;
            if (out_packing == 1) out_elemsize = 4u;
        }

        top_blob.create(w, h, outc, out_elemsize, out_packing, opt.blob_vkallocator, opt.staging_vkallocator);
        if (top_blob.empty())
            return -100;
    }

    std::vector<VkMat> bindings(2);
    bindings[0] = bottom_blob;
    bindings[1] = top_blob;

    std::vector<vk_constant_type> constants(10);
    constants[0].i = bottom_blob.dims;
    constants[1].i = bottom_blob.w;
    constants[2].i = bottom_blob.h;
    constants[3].i = bottom_blob.c;
    constants[4].i = bottom_blob.cstep;
    constants[5].i = top_blob.dims;
    constants[6].i = top_blob.w;
    constants[7].i = top_blob.h;
    constants[8].i = top_blob.c;
    constants[9].i = top_blob.cstep;

    if (packing == 1 && out_packing == 4)
    {
        cmd.record_pipeline(pipeline_packing_1to4, bindings, constants, top_blob);
    }

    if (packing == 4 && out_packing == 1)
    {
        cmd.record_pipeline(pipeline_packing_4to1, bindings, constants, bottom_blob);
    }

    return 0;
}

} // namespace ncnn
