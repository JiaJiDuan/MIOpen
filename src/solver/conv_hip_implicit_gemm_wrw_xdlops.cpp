/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2022 Advanced Micro Devices, Inc.
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
#include <miopen/conv/wrw_invoke_params.hpp>
#include <miopen/solver/problem_description_interpreter.hpp>
#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
#include <ck/library/tensor_operation_instance/gpu/convolution_backward_weight.hpp>
#endif
MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_HIP_WRW_XDLOPS)

namespace miopen {
namespace solver {

#if MIOPEN_BACKEND_HIP && MIOPEN_USE_COMPOSABLEKERNEL
template <typename DataType>
using DeviceOp = ck::tensor_operation::device::DeviceConvBwdWeight<
    2,
    ck::tensor_layout::convolution::NHWC,
    ck::tensor_layout::convolution::KYXC,
    ck::tensor_layout::convolution::NHWK,
    DataType,
    DataType,
    DataType,
    ck::tensor_operation::element_wise::PassThrough,
    ck::tensor_operation::element_wise::PassThrough,
    ck::tensor_operation::element_wise::PassThrough>;

template <typename DataType>
using DeviceOpPtrs =
    ck::tensor_operation::device::instance::DeviceOperationInstanceFactory<DeviceOp<DataType>>;

struct CKArgs
{
    CKArgs(const ProblemDescription& problem)
    {
        N        = ProblemInterpreter::GetBatchN(problem);
        K        = ProblemInterpreter::GetOutputChannelK(problem);
        C        = ProblemInterpreter::GetInputChannelC(problem);
        input    = {ProblemInterpreter::GetInputHeightHi(problem),
                 ProblemInterpreter::GetInputWidthWi(problem)};
        output   = {ProblemInterpreter::GetOutputHeightHo(problem),
                  ProblemInterpreter::GetOutputWidthWo(problem)};
        filter   = {ProblemInterpreter::GetFilterHeightY(problem),
                  ProblemInterpreter::GetFilterWidthX(problem)};
        strides  = {ProblemInterpreter::GetAdjustedConvolutionStrideH(problem),
                   ProblemInterpreter::GetAdjustedConvolutionStrideW(problem)};
        dilation = {ProblemInterpreter::GetAdjustedConvolutionDilationH(problem),
                    ProblemInterpreter::GetAdjustedConvolutionDilationW(problem)};
        lPadding = {ProblemInterpreter::GetInputLeftPadH(problem),
                    ProblemInterpreter::GetInputLeftPadW(problem)};
        rPadding = {ProblemInterpreter::GetAdjustedInputRightPadH(problem),
                    ProblemInterpreter::GetAdjustedInputRightPadW(problem)};
        split_k  = 1;
    }
    int N;
    int K;
    int C;
    std::vector<int> input;
    std::vector<int> output;
    std::vector<int> filter;
    std::vector<int> strides;
    std::vector<int> dilation;
    std::vector<int> lPadding;
    std::vector<int> rPadding;
    ck::index_t split_k;
};

template <typename DataType>
void PerformanceConfigHipImplicitGemmWrwXdlops::Init(const ProblemDescription& problem)
{
    const auto args      = CKArgs{problem};
    const auto conv_ptrs = DeviceOpPtrs<DataType>::GetInstances();
    assert(!conv_ptrs.empty());
    this->total_size = conv_ptrs.size();
    for(int i = 0; i < conv_ptrs.size(); i++)
    {
        auto argument_ptr = conv_ptrs[i]->MakeArgumentPointer(nullptr,
                                                              nullptr,
                                                              nullptr,
                                                              args.N,
                                                              args.K,
                                                              args.C,
                                                              args.input,
                                                              args.filter,
                                                              args.output,
                                                              args.strides,
                                                              args.dilation,
                                                              args.lPadding,
                                                              args.rPadding,
                                                              {},
                                                              {},
                                                              {},
                                                              args.split_k);
        if(conv_ptrs[i]->IsSupportedArgument(argument_ptr.get()))
        {
            this->kernel_id = conv_ptrs[i]->GetTypeString();
            break;
        }
        ++this->index;
    }
}

template <typename DataType>
bool PerformanceConfigHipImplicitGemmWrwXdlops::CheckIsSupportCKArgs(
    const ProblemDescription& problem) const
{
    const auto args      = CKArgs{problem};
    const auto conv_ptrs = DeviceOpPtrs<DataType>::GetInstances();
    auto argument_ptr    = conv_ptrs[this->index]->MakeArgumentPointer(nullptr,
                                                                    nullptr,
                                                                    nullptr,
                                                                    args.N,
                                                                    args.K,
                                                                    args.C,
                                                                    args.input,
                                                                    args.filter,
                                                                    args.output,
                                                                    args.strides,
                                                                    args.dilation,
                                                                    args.lPadding,
                                                                    args.rPadding,
                                                                    {},
                                                                    {},
                                                                    {},
                                                                    args.split_k);
    return conv_ptrs[this->index]->IsSupportedArgument(argument_ptr.get());
}

template <typename DataType>
bool ConvHipImplicitGemmWrwXdlops::CheckCKApplicability(const ProblemDescription& problem) const
{
    const auto conv_ptrs = DeviceOpPtrs<DataType>::GetInstances();
    assert(!conv_ptrs.empty());
    const auto args = CKArgs{problem};
    if(!std::all_of(args.strides.begin(), args.strides.end(), [&](auto x) { return x == 1; }))
        return false;
    for(int i = 0; i < conv_ptrs.size(); i++)
    {
        auto argument_ptr = conv_ptrs[i]->MakeArgumentPointer(nullptr,
                                                              nullptr,
                                                              nullptr,
                                                              args.N,
                                                              args.K,
                                                              args.C,
                                                              args.input,
                                                              args.filter,
                                                              args.output,
                                                              args.strides,
                                                              args.dilation,
                                                              args.lPadding,
                                                              args.rPadding,
                                                              {},
                                                              {},
                                                              {},
                                                              args.split_k);
        if(conv_ptrs[i]->IsSupportedArgument(argument_ptr.get()))
            return true;
    }
    return false;
}

template <typename DataType>
void ConvHipImplicitGemmWrwXdlops::RunCKSolution(
    const Handle& handle,
    const AnyInvokeParams& primitive_parameters,
    const ProblemDescription& problem,
    const PerformanceConfigHipImplicitGemmWrwXdlops& config) const
{
    const auto args      = CKArgs{problem};
    const auto conv_ptrs = DeviceOpPtrs<DataType>::GetInstances();
    auto& conv_ptr       = conv_ptrs.at(config.index);
    const auto& data_ctx = primitive_parameters.CastTo<conv::WrWInvokeParams>();
    const auto& tensors  = data_ctx.tensors;
    auto argument_ptr    = conv_ptr->MakeArgumentPointer(
        const_cast<void*>( // NOLINT (cppcoreguidelines-pro-type-const-cast)
            static_cast<const void*>(tensors.x)),
        static_cast<void*>(tensors.dw),
        const_cast<void*>( // NOLINT (cppcoreguidelines-pro-type-const-cast)
            static_cast<const void*>(tensors.dy)),
        args.N,
        args.K,
        args.C,
        args.input,
        args.filter,
        args.output,
        args.strides,
        args.dilation,
        args.lPadding,
        args.rPadding,
        {},
        {},
        {},
        args.split_k);
    auto invoker_ptr            = conv_ptr->MakeInvokerPointer();
    const auto enable_profiling = handle.IsProfilingEnabled();

    float elapsed_time =
        invoker_ptr->Run(argument_ptr.get(), {handle.GetStream(), enable_profiling});
    if(enable_profiling)
    {
        handle.ResetKernelTime();
        handle.AccumKernelTime(elapsed_time);
    }
}
#endif

void PerformanceConfigHipImplicitGemmWrwXdlops::HeuristicInit(const ProblemDescription& problem)
{
    this->index = 0;
#if !MIOPEN_BACKEND_HIP || !MIOPEN_USE_COMPOSABLEKERNEL
    std::ignore = problem;
#else
    this->index      = 0;
    this->total_size = 0;
    this->kernel_id  = "";
    switch(problem.conv_problem.GetInDataType())
    {
    case miopenHalf: Init<ck::half_t>(problem); break;
    case miopenFloat: Init<float>(problem); break;
    case miopenInt8:
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenDouble: break;
    }
#endif
}

bool PerformanceConfigHipImplicitGemmWrwXdlops::SetNextValue(const ProblemDescription& problem)
{
    if(total_size == -1)
        this->HeuristicInit(problem);
    assert(total_size != -1);
    if((index + 1) < total_size)
    {
        ++index;
        return true;
    }
    else
        return false;
}

bool PerformanceConfigHipImplicitGemmWrwXdlops::IsValidValue() const { return index < total_size; }

bool PerformanceConfigHipImplicitGemmWrwXdlops::IsValid(const ProblemDescription& problem) const
{
#if !MIOPEN_BACKEND_HIP || !MIOPEN_USE_COMPOSABLEKERNEL
    std::ignore = problem;
    return false;
#else
    switch(problem.conv_problem.GetInDataType())
    {
    case miopenHalf: return CheckIsSupportCKArgs<ck::half_t>(problem);
    case miopenFloat: return CheckIsSupportCKArgs<float>(problem);
    case miopenInt8:
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenDouble: break;
    }
    return false;
#endif
}

bool PerformanceConfigHipImplicitGemmWrwXdlops::operator==(
    const PerformanceConfigHipImplicitGemmWrwXdlops& other) const
{
    return this->index == other.index;
}

PerformanceConfigHipImplicitGemmWrwXdlops
ConvHipImplicitGemmWrwXdlops::GetDefaultPerformanceConfig(const ProblemDescription& problem) const
{
    PerformanceConfigHipImplicitGemmWrwXdlops pp;
    pp.HeuristicInit(problem);
    return pp;
}

bool ConvHipImplicitGemmWrwXdlops::IsValidPerformanceConfig(
    const ProblemDescription& problem, const PerformanceConfigHipImplicitGemmWrwXdlops& config) const
{
    return config.IsValid(problem);
}

PerformanceConfigHipImplicitGemmWrwXdlops
ConvHipImplicitGemmWrwXdlops::Search(const ConvolutionContext& ctx,
                                     const ProblemDescription& problem,
                                     const AnyInvokeParams& invoke_ctx) const
{
    return GenericSearch(*this, ctx, problem, invoke_ctx);
}

size_t ConvHipImplicitGemmWrwXdlops::GetWorkspaceSize(const ConvolutionContext& ctx) const
{
    std::ignore = ctx;
    return 0;
}

bool ConvHipImplicitGemmWrwXdlops::IsApplicable(const ProblemDescription& problem) const
{
#if !MIOPEN_BACKEND_HIP || !MIOPEN_USE_COMPOSABLEKERNEL
    std::ignore = problem;
    return false;
#else
    if(miopen::IsDisabled(MIOPEN_DEBUG_CONV_IMPLICIT_GEMM_HIP_WRW_XDLOPS{}))
        return false;
    if(miopen::IsEnabled(MIOPEN_DEBUG_CONVOLUTION_DETERMINISTIC{}))
        return false;
    if(problem.conv_problem.GetInDataType() != problem.conv_problem.GetWeightsDataType() ||
       problem.conv_problem.GetWeightsDataType() != problem.conv_problem.GetOutDataType() ||
       problem.conv_problem.GetInDataType() != problem.conv_problem.GetOutDataType())
        return false;
    if(!problem.direction.IsBackwardWrW())
        return false;
    if(!problem.Is2d())
        return false;
    if(!problem.IsLayoutNHWC())
        return false;
    switch(problem.conv_problem.GetInDataType())
    {
    case miopenHalf: return CheckCKApplicability<ck::half_t>(problem);
    case miopenFloat: return CheckCKApplicability<float>(problem);
    case miopenInt8:
    case miopenInt32:
    case miopenInt8x4:
    case miopenBFloat16:
    case miopenDouble: break;
    }
    return false;
#endif
}

ConvSolution ConvHipImplicitGemmWrwXdlops::GetSolution(
    const ConvolutionContext& ctx,
    const ProblemDescription& problem,
    const PerformanceConfigHipImplicitGemmWrwXdlops& config) const
{
    std::ignore = ctx;
#if !MIOPEN_BACKEND_HIP || !MIOPEN_USE_COMPOSABLEKERNEL
    std::ignore = problem;
    std::ignore = config;
    return {};
#else
    ConvSolution result;
    result.invoker_factory = [=](const std::vector<Kernel>& kernels) {
        std::ignore = kernels;
        return [=](const Handle& handle, const AnyInvokeParams& primitive_parameters) {
            switch(problem.conv_problem.GetInDataType())
            {
            case miopenHalf:
                RunCKSolution<ck::half_t>(handle, primitive_parameters, problem, config);
                break;
            case miopenFloat:
                RunCKSolution<float>(handle, primitive_parameters, problem, config);
                break;
            case miopenInt8:
            case miopenInt32:
            case miopenInt8x4:
            case miopenBFloat16:
            case miopenDouble: break;
            }
        };
    };
    return result;
#endif
}

} // namespace solver
} // namespace miopen