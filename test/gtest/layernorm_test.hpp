/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#include <miopen/miopen.h>
#ifdef MIOPEN_BETA_API
#include <gtest/gtest.h>
#include <miopen/layernorm.hpp>

#include "tensor_holder.hpp"
#include "cpu_layernorm.hpp"
#include "get_handle.hpp"
#include "../driver/tensor_driver.hpp"
#include "verify.hpp"
#include <random>

struct LayerNormTestCase
{
    size_t N;
    size_t C;
    size_t D;
    size_t H;
    size_t W;
    size_t nomalized_dim;
    float eps;
    miopenLayerNormMode_t ln_mode;
    friend std::ostream& operator<<(std::ostream& os, const LayerNormTestCase& tc)
    {
        return os << " N:" << tc.N << " C:" << tc.C << " D:" << tc.D << " H:" << tc.H
                  << " W:" << tc.W << " dim:" << tc.nomalized_dim << " eps:" << tc.eps
                  << " LayerNorm_mode:" << tc.ln_mode;
    }

    std::vector<size_t> GetInput() { return {N, C, D, H, W}; }
};

std::vector<LayerNormTestCase> LayerNormTestConfigs()
{ // n c h d w nomalized_dim eps ln_mode
    // clang-format off
    return {
	{ 32,   1,   32,  32,  32  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},       // 32x32x32 based on VoxNet arch
        { 32,   1,   14,  14,  14  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32,  32,   14,  14,  14  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32,  32,   12,  12,  12  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32,  32,    6,   6,   6  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 256,  1,   32,  32,  32  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},      // 32x32x32 based on VoxNet arch
        { 256, 32,   14,  14,  14  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 256, 32,   12,  12,  12  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 256, 32,    6,   6,   6  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 512,  1,   32,  32,  32  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},      // 32x32x32 based on VoxNet arch
        { 512, 32,   14,  14,  14  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 512, 32,   12,  12,  12  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 512, 32,    6,   6,   6  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32,  2,   32,  57, 125  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},       // Hand-gesture recognition CVPR 2015 paper High Res Net Path
        { 32, 32,   14,  25,  59  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32, 32,    6,  10,  27  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32, 32,    4,   6,  11  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32, 32,    2,   2,   3  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32, 32,   32,  28,  62  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},       // Hand-gesture recognition CVPR 2015 paper Low Res Net Path
        { 32, 32,   14,  12,  29  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32, 32,    6,   4,  12  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 32, 32,    4,   2,   2  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},
        { 16, 32,    6,  50,  50  ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},       // Multi-view 3D convnet
        { 1, 3,     8,  240, 320 ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},      // 3D convet on video
        { 1, 3,    16,  240, 320 ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},      // 3D convet on video
        { 1, 3,     8,  128, 171 ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},      // 3D convet on video
        { 1, 3,    16,  128, 171 ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},      // 3D convet on video
        { 1, 3,     8,  112, 112 ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE},      // 3D convet on video
        { 1, 3,    16,  112, 112 ,4 , 1e-5, MIOPEN_ELEMENTWISE_AFFINE}      // 3D convet on video
      };
    // clang-format on
}

inline int SetTensorLayout(miopen::TensorDescriptor& desc)
{
    std::vector<std::size_t> lens = desc.GetLengths();
    std::vector<int> int_lens(lens.begin(), lens.end());

    // set the strides for the tensor
    return SetTensorNd(&desc, int_lens, desc.GetType());
}

template <typename T = float>
struct LayerNormSolverTest : public ::testing::TestWithParam<LayerNormTestCase>
{
protected:
    void SetUp() override
    {
        auto&& handle    = get_handle();
        test_skipped     = false;
        layernorm_config = GetParam();
        std::mt19937 gen(0);
        std::uniform_real_distribution<> d{-3, 3};
        auto gen_value = [&](auto...) { return d(gen); };

        nomalized_dim = layernorm_config.nomalized_dim;
        eps           = layernorm_config.eps;
        ln_mode       = layernorm_config.ln_mode;

        auto in_dim = layernorm_config.GetInput();

        input = tensor<T>{in_dim}.generate(gen_value);

        if(ln_mode == MIOPEN_ELEMENTWISE_AFFINE)
        {
            std::vector<size_t> inner_dim;
            if(nomalized_dim == in_dim.size())
                inner_dim = {1};
            else
                inner_dim = {in_dim.begin() + nomalized_dim, in_dim.end()};
            weight = tensor<T>{inner_dim}.generate(gen_value);
            bias   = tensor<T>{inner_dim}.generate(gen_value);
            SetTensorLayout(weight.desc);
            SetTensorLayout(bias.desc);
        }

        std::vector<size_t> outer_dim;
        if(nomalized_dim == 0)
            outer_dim = {1};
        else
            outer_dim = {in_dim.begin(), in_dim.end() - (in_dim.size() - nomalized_dim)};

        SetTensorLayout(input.desc);

        output = tensor<T>{in_dim};
        mean   = tensor<T>{outer_dim};
        rstd   = tensor<T>{outer_dim};
        SetTensorLayout(output.desc);
        SetTensorLayout(mean.desc);
        SetTensorLayout(rstd.desc);
        std::fill(output.begin(), output.end(), std::numeric_limits<T>::quiet_NaN());
        std::fill(mean.begin(), mean.end(), std::numeric_limits<T>::quiet_NaN());
        std::fill(rstd.begin(), rstd.end(), std::numeric_limits<T>::quiet_NaN());

        ref_output = tensor<T>{in_dim};
        ref_mean   = tensor<T>{outer_dim};
        ref_rstd   = tensor<T>{outer_dim};
        std::fill(ref_output.begin(), ref_output.end(), std::numeric_limits<T>::quiet_NaN());
        std::fill(ref_mean.begin(), ref_mean.end(), std::numeric_limits<T>::quiet_NaN());
        std::fill(ref_rstd.begin(), ref_rstd.end(), std::numeric_limits<T>::quiet_NaN());

        input_dev  = handle.Write(input.data);
        weight_dev = handle.Write(weight.data);
        bias_dev   = handle.Write(bias.data);
        output_dev = handle.Write(output.data);
        mean_dev   = handle.Write(mean.data);
        rstd_dev   = handle.Write(rstd.data);

        doutput = tensor<T>{in_dim}.generate(gen_value);

        if(ln_mode == MIOPEN_ELEMENTWISE_AFFINE)
        {
            std::vector<int> inner_dim;
            if(nomalized_dim == in_dim.size())
                inner_dim = {1};
            else
                inner_dim = {in_dim.begin() + nomalized_dim, in_dim.end()};
            dweight = tensor<T>{inner_dim}.generate(gen_value);
            dbias   = tensor<T>{inner_dim}.generate(gen_value);
            SetTensorLayout(dweight.desc);
            SetTensorLayout(dbias.desc);
            std::fill(dweight.begin(), dweight.end(), std::numeric_limits<T>::quiet_NaN());
            std::fill(dbias.begin(), dbias.end(), std::numeric_limits<T>::quiet_NaN());

            ref_dweight = tensor<T>{inner_dim}.generate(gen_value);
            ref_dbias   = tensor<T>{inner_dim}.generate(gen_value);
            std::fill(ref_dweight.begin(), ref_dweight.end(), std::numeric_limits<T>::quiet_NaN());
            std::fill(ref_dbias.begin(), ref_dbias.end(), std::numeric_limits<T>::quiet_NaN());
        }

        SetTensorLayout(doutput.desc);

        dinput = tensor<T>{in_dim};
        SetTensorLayout(dinput.desc);
        std::fill(dinput.begin(), dinput.end(), std::numeric_limits<T>::quiet_NaN());
        ref_dinput = tensor<T>{in_dim};
        std::fill(ref_dinput.begin(), ref_dinput.end(), std::numeric_limits<T>::quiet_NaN());

        doutput_dev = handle.Write(doutput.data);
        dinput_dev  = handle.Write(dinput.data);
        dweight_dev = handle.Write(dweight.data);
        dbias_dev   = handle.Write(dbias.data);
    }
    void TearDown() override
    {
        if(test_skipped)
            return;

        auto&& handle = get_handle();

        cpu_layernorm_forward<T>(
            input, weight, bias, ref_output, ref_mean, ref_rstd, eps, nomalized_dim, ln_mode);
        miopenStatus_t status;

        status = miopen::LayerNormForward(handle,
                                          input.desc,
                                          input_dev.get(),
                                          weight.desc,
                                          weight_dev.get(),
                                          bias.desc,
                                          bias_dev.get(),
                                          output.desc,
                                          output_dev.get(),
                                          mean.desc,
                                          mean_dev.get(),
                                          rstd.desc,
                                          rstd_dev.get(),
                                          ln_mode,
                                          eps,
                                          nomalized_dim);

        EXPECT_EQ(status, miopenStatusSuccess);

        output.data = handle.Read<T>(output_dev, output.data.size());
        mean.data   = handle.Read<T>(mean_dev, mean.data.size());
        rstd.data   = handle.Read<T>(rstd_dev, rstd.data.size());

        double threshold = std::numeric_limits<T>::epsilon();
        auto error       = miopen::rms_range(ref_output, output);

        EXPECT_TRUE(miopen::range_distance(ref_output) == miopen::range_distance(output));
        EXPECT_TRUE(error < threshold * 1000) << "Error output beyond tolerance Error:" << error
                                              << ",  Thresholdx1000: " << threshold * 1000;

        error = miopen::rms_range(ref_mean, mean);
        EXPECT_TRUE(miopen::range_distance(ref_mean) == miopen::range_distance(mean));
        EXPECT_TRUE(error < threshold)
            << "Error mean beyond tolerance Error:" << error << ",  Threshold: " << threshold;

        error = miopen::rms_range(ref_rstd, rstd);
        EXPECT_TRUE(miopen::range_distance(ref_rstd) == miopen::range_distance(rstd));
        EXPECT_TRUE(error < threshold * 2000) << "Error rstd beyond tolerance Error:" << error
                                              << ",  Thresholdx2000: " << threshold * 2000;

        cpu_layernorm_backward<T>(input,
                                  doutput,
                                  weight,
                                  mean,
                                  rstd,
                                  ref_dinput,
                                  ref_dweight,
                                  ref_dbias,
                                  nomalized_dim,
                                  ln_mode);

        status = miopen::LayerNormBackward(handle,
                                           input.desc,
                                           input_dev.get(),
                                           doutput.desc,
                                           doutput_dev.get(),
                                           weight.desc,
                                           weight_dev.get(),
                                           mean.desc,
                                           mean_dev.get(),
                                           rstd.desc,
                                           rstd_dev.get(),
                                           dinput.desc,
                                           dinput_dev.get(),
                                           dweight.desc,
                                           dweight_dev.get(),
                                           dbias.desc,
                                           dbias_dev.get(),
                                           ln_mode,
                                           nomalized_dim);

        EXPECT_EQ(status, miopenStatusSuccess);

        dinput.data  = handle.Read<T>(dinput_dev, dinput.data.size());
        dweight.data = handle.Read<T>(dweight_dev, dweight.data.size());
        dbias.data   = handle.Read<T>(dbias_dev, dbias.data.size());

        error = miopen::rms_range(ref_dinput, dinput);
        EXPECT_TRUE(miopen::range_distance(ref_dinput) == miopen::range_distance(dinput));
        EXPECT_TRUE(error < threshold * 10) << "Error dinput beyond tolerance Error:" << error
                                            << ",  Thresholdx10: " << threshold * 10;

        error = miopen::rms_range(ref_dweight, dweight);
        EXPECT_TRUE(miopen::range_distance(ref_dweight) == miopen::range_distance(dweight));
        EXPECT_TRUE(error < threshold * 1000) << "Error dweight beyond tolerance Error:" << error
                                              << ",  Thresholdx1000: " << threshold * 1000;

        error = miopen::rms_range(ref_dbias, dbias);
        EXPECT_TRUE(miopen::range_distance(ref_dbias) == miopen::range_distance(dbias));
        EXPECT_TRUE(error < threshold * 1000) << "Error dbias beyond tolerance Error:" << error
                                              << ",  Thresholdx1000: " << threshold * 1000;
    }
    LayerNormTestCase layernorm_config;

    tensor<T> input;
    tensor<T> weight;
    tensor<T> bias;
    tensor<T> output;
    tensor<T> mean;
    tensor<T> rstd;
    tensor<T> doutput;
    tensor<T> dinput;
    tensor<T> dweight;
    tensor<T> dbias;

    tensor<T> ref_output;
    tensor<T> ref_mean;
    tensor<T> ref_rstd;
    tensor<T> ref_dinput;
    tensor<T> ref_dweight;
    tensor<T> ref_dbias;

    miopen::Allocator::ManageDataPtr input_dev;
    miopen::Allocator::ManageDataPtr weight_dev;
    miopen::Allocator::ManageDataPtr bias_dev;
    miopen::Allocator::ManageDataPtr output_dev;
    miopen::Allocator::ManageDataPtr mean_dev;
    miopen::Allocator::ManageDataPtr rstd_dev;
    miopen::Allocator::ManageDataPtr doutput_dev;
    miopen::Allocator::ManageDataPtr dinput_dev;
    miopen::Allocator::ManageDataPtr dweight_dev;
    miopen::Allocator::ManageDataPtr dbias_dev;

    size_t nomalized_dim;
    float eps;
    miopenLayerNormMode_t ln_mode;

    bool test_skipped = false;
};
#endif