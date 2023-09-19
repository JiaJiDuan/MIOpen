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

#include <vector>
#include <cstdint>

#include <miopen/solver.hpp>
#include <miopen/generic_search.hpp>
#include <miopen/conv/data_invoke_params.hpp>
#include <miopen/solver/problem_description_interpreter.hpp>
#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
#include <ck/library/tensor_operation_instance/gpu/grouped_convolution_backward_data.hpp>
#endif
#include <miopen/solver/implicitgemm_ck_util.hpp>
MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEBUG_3D_CONV_IMPLICIT_GEMM_HIP_BWD_XDLOPS)

namespace miopen {
namespace solver {

#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
template <typename DataType>
using DeviceOpGBwd = ck::tensor_operation::device::DeviceGroupedConvBwdDataMultipleD<
    3,
    ck::tensor_layout::convolution::NDHWGK,
    ck::tensor_layout::convolution::GKZYXC,
    ck::Tuple<>,
    ck::tensor_layout::convolution::NDHWGC,
    DataType,
    DataType,
    ck::Tuple<>,
    DataType,
    ck::tensor_operation::element_wise::PassThrough,
    ck::tensor_operation::element_wise::PassThrough,
    ck::tensor_operation::element_wise::PassThrough>;

template <typename DataType>
using DeviceOpGBwdPtrs =
    ck::tensor_operation::device::instance::DeviceOperationInstanceFactory<DeviceOpGBwd<DataType>>;

namespace {

struct CKArgs
{
    CKArgs(const ProblemDescription& problem)
    {
        G  = ProblemInterpreter::GetGroupCountG(problem);
        N  = ProblemInterpreter::GetBatchN(problem);
        K1 = ProblemInterpreter::GetOutputChannelK(problem);
        C1 = ProblemInterpreter::GetInputChannelC(problem);
        C  = C1 / G; // Number of input Channel per group
        K  = K1 / G; // Number of output Channel per group
        Hi = ProblemInterpreter::GetInputHeightHi(problem);
        Wi = ProblemInterpreter::GetInputWidthWi(problem);
        Ho = ProblemInterpreter::GetOutputHeightHo(problem);
        Wo = ProblemInterpreter::GetOutputWidthWo(problem);
        Y  = ProblemInterpreter::GetFilterHeightY(problem);
        X  = ProblemInterpreter::GetFilterWidthX(problem);
        Di = ProblemInterpreter::GetInputDepthDi(problem);
        Do = ProblemInterpreter::GetOutputDepthDo(problem);
        Z  = ProblemInterpreter::GetFilterDepthZ(problem);

        input  = {G, N, C, Di, Hi, Wi};
        output = {G, N, K, Do, Ho, Wo};
        weight = {G, K, C, Z, Y, X};

        // strides from NHWGC to GNCHW laout
        in_strides  = {C, Di * Hi * Wi * G * C, 1, Hi * Wi * G * C, Wi * G * C, G * C};
        out_strides = {K, Do * Ho * Wo * G * K, 1, Ho * Wo * G * K, Wo * G * K, G * K};
        wei_strides = {K * Z * Y * X * C, Z * Y * X * C, 1, Y * X * C, X * C, C};

        strides  = {ProblemInterpreter::GetAdjustedConvolutionStrideD(problem),
                   ProblemInterpreter::GetAdjustedConvolutionStrideH(problem),
                   ProblemInterpreter::GetAdjustedConvolutionStrideW(problem)};
        dilation = {ProblemInterpreter::GetAdjustedConvolutionDilationD(problem),
                    ProblemInterpreter::GetAdjustedConvolutionDilationH(problem),
                    ProblemInterpreter::GetAdjustedConvolutionDilationW(problem)};
        lPadding = {ProblemInterpreter::GetInputLeftPadD(problem),
                    ProblemInterpreter::GetInputLeftPadH(problem),
                    ProblemInterpreter::GetInputLeftPadW(problem)};
        rPadding = {ProblemInterpreter::GetAdjustedInputRightPadD(problem),
                    ProblemInterpreter::GetAdjustedInputRightPadH(problem),
                    ProblemInterpreter::GetAdjustedInputRightPadW(problem)};
    }

    CKArgs(const CKArgs&) = default;
    CKArgs(CKArgs&&)      = default;
    CKArgs& operator=(const CKArgs&) = default;

    template <typename ConvPtr>
    auto MakeArgPtr(const ConvPtr& conv_ptr, ConstData_t in, ConstData_t w, Data_t out) const
    {
        return conv_ptr->MakeArgumentPointer(in,
                                             w,
                                             {},
                                             out,
                                             output,
                                             out_strides,
                                             weight,
                                             wei_strides,
                                             {},
                                             {},
                                             input,
                                             in_strides,
                                             strides,
                                             dilation,
                                             lPadding,
                                             rPadding,
                                             {},
                                             {},
                                             {});
    }

    template <typename ConvPtr>
    auto MakeArgPtr(const ConvPtr& conv_ptr, const ConvDataTensors& tensors) const
    {
        return MakeArgPtr(conv_ptr, tensors.in, tensors.w, tensors.out);
    }

    template <typename ConvPtr>
    bool IsSupportedBy(const ConvPtr& conv_ptr) const
    {
        auto arg_ptr = MakeArgPtr(conv_ptr, nullptr, nullptr, nullptr);
        return conv_ptr->IsSupportedArgument(arg_ptr.get());
    }

    int G;
    int N;
    int K;
    int C;
    int C1;
    int K1;
    int Hi;
    int Wi;
    int Di;
    int Ho;
    int Wo;
    int Do;
    int Y;
    int X;
    int Z;
    std::array<ck::index_t, 6> input;
    std::array<ck::index_t, 6> in_strides;
    std::array<ck::index_t, 6> output;
    std::array<ck::index_t, 6> out_strides;
    std::array<ck::index_t, 6> weight;
    std::array<ck::index_t, 6> wei_strides;
    std::array<ck::index_t, 3> strides;
    std::array<ck::index_t, 3> dilation;
    std::array<ck::index_t, 3> lPadding;
    std::array<ck::index_t, 3> rPadding;
};
} // namespace

template <typename DataType>
void PerformanceConfigHipImplicitGemm3DGroupBwdXdlops::Init(const ProblemDescription& problem)
{
    valid_kernels = FillValidKernelsIDs<DeviceOpGBwdPtrs<DataType>, CKArgs>(problem);
    index         = 0;
    kernel_id     = valid_kernels[index];
}

template <typename DataType>
bool PerformanceConfigHipImplicitGemm3DGroupBwdXdlops::CheckIsSupportCKArgs(
    const ProblemDescription& problem) const
{
    return IsCKArgsSupported<DeviceOpGBwdPtrs<DataType>, CKArgs>(problem, kernel_id);
}

template <typename DataType>
bool ConvHipImplicitGemm3DGroupBwdXdlops::CheckCKApplicability(
    const ProblemDescription& problem) const
{
    return IsCKApplicable<DeviceOpGBwdPtrs<DataType>, CKArgs>(problem);
}
#endif

void PerformanceConfigHipImplicitGemm3DGroupBwdXdlops::HeuristicInit(
    [[maybe_unused]] const ProblemDescription& problem)
{
    index     = 0;
    kernel_id = "";

#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
    switch(problem.GetInDataType())
    {
    case miopenHalf: Init<ck::half_t>(problem); break;
    case miopenFloat: Init<float>(problem); break;
    case miopenInt8: Init<int8_t>(problem); break;
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenFloat8:
    case miopenBFloat8:
    case miopenDouble: break;
    }
#endif
}

bool PerformanceConfigHipImplicitGemm3DGroupBwdXdlops::SetNextValue(
    const ProblemDescription& problem)
{
    if(valid_kernels.empty())
    {
        HeuristicInit(problem);
        assert(!valid_kernels.empty());
        return true;
    }
    if((index + 1) < valid_kernels.size())
    {
        ++index;
        kernel_id = valid_kernels[index];
        return true;
    }
    else
        return false;
}

bool PerformanceConfigHipImplicitGemm3DGroupBwdXdlops::IsValidValue() const
{
    return index < valid_kernels.size();
}

bool PerformanceConfigHipImplicitGemm3DGroupBwdXdlops::IsValid(
    [[maybe_unused]] const ProblemDescription& problem) const
{
#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
    switch(problem.GetInDataType())
    {
    case miopenHalf: return CheckIsSupportCKArgs<ck::half_t>(problem);
    case miopenFloat: return CheckIsSupportCKArgs<float>(problem);
    case miopenInt8: return CheckIsSupportCKArgs<int8_t>(problem);
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenFloat8:
    case miopenBFloat8:
    case miopenDouble: break;
    }
#endif
    return false;
}

bool PerformanceConfigHipImplicitGemm3DGroupBwdXdlops::operator==(
    const PerformanceConfigHipImplicitGemm3DGroupBwdXdlops& other) const
{
    return kernel_id == other.kernel_id;
}

PerformanceConfigHipImplicitGemm3DGroupBwdXdlops
ConvHipImplicitGemm3DGroupBwdXdlops::GetDefaultPerformanceConfig(
    const ConvolutionContext&, const ProblemDescription& problem) const
{
    PerformanceConfigHipImplicitGemm3DGroupBwdXdlops pp;
    pp.HeuristicInit(problem);
    return pp;
}

bool ConvHipImplicitGemm3DGroupBwdXdlops::IsValidPerformanceConfig(
    const ConvolutionContext&,
    const ProblemDescription& problem,
    const PerformanceConfigHipImplicitGemm3DGroupBwdXdlops& config) const
{
    return config.IsValid(problem);
}

PerformanceConfigHipImplicitGemm3DGroupBwdXdlops
ConvHipImplicitGemm3DGroupBwdXdlops::Search(const ConvolutionContext& ctx,
                                            const ProblemDescription& problem,
                                            const AnyInvokeParams& invoke_ctx) const
{
    return GenericSearch(*this, ctx, problem, invoke_ctx);
}

bool ConvHipImplicitGemm3DGroupBwdXdlops::IsApplicable(
    [[maybe_unused]] const ConvolutionContext& ctx,
    [[maybe_unused]] const ProblemDescription& problem) const
{
#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
    if(miopen::IsDisabled(MIOPEN_DEBUG_3D_CONV_IMPLICIT_GEMM_HIP_BWD_XDLOPS{}))
        return false;
    if(miopen::IsEnabled(MIOPEN_DEBUG_CONVOLUTION_DETERMINISTIC{}))
        return false;
    if(problem.GetInDataType() != problem.GetWeightsDataType() ||
       problem.GetWeightsDataType() != problem.GetOutDataType() ||
       problem.GetInDataType() != problem.GetOutDataType())
        return false;
    if(problem.IsTensorsCasted())
        return false;
    if(!problem.direction.IsBackwardData())
        return false;
    if(!problem.Is3d())
        return false;
    if(!problem.IsLayoutNHWC())
        return false;
    const std::string& arch = ctx.GetStream().GetDeviceName();
    if(miopen::StartsWith(arch, "gfx11") || miopen::StartsWith(arch, "gfx10"))
        return false;
    switch(problem.GetInDataType())
    {
    case miopenHalf: return CheckCKApplicability<ck::half_t>(problem);
    case miopenFloat: return CheckCKApplicability<float>(problem);
    case miopenInt8: return CheckCKApplicability<int8_t>(problem);
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenFloat8:
    case miopenBFloat8:
    case miopenDouble: break;
    }
#endif
    return false;
}

ConvSolution ConvHipImplicitGemm3DGroupBwdXdlops::GetSolution(
    [[maybe_unused]] const ConvolutionContext& ctx,
    [[maybe_unused]] const ProblemDescription& problem,
    [[maybe_unused]] const PerformanceConfigHipImplicitGemm3DGroupBwdXdlops& config) const
{
#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
    switch(problem.GetInDataType())
    {
    case miopenInt8:
        return InitInvokerFactory<DeviceOpGBwdPtrs<int8_t>, CKArgs, conv::DataInvokeParams>(
            problem, config.kernel_id);
    case miopenHalf:
        return InitInvokerFactory<DeviceOpGBwdPtrs<ck::half_t>, CKArgs, conv::DataInvokeParams>(
            problem, config.kernel_id);
    case miopenFloat:
        return InitInvokerFactory<DeviceOpGBwdPtrs<float>, CKArgs, conv::DataInvokeParams>(
            problem, config.kernel_id);
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenDouble:
    case miopenFloat8:
    case miopenBFloat8:
    default:
        MIOPEN_THROW(miopenStatusInternalError,
                     "ConvHipImplicitGemmBwdXdlops operation not implemented for this data type");
    }
#endif
    return {};
}

} // namespace solver
} // namespace miopen