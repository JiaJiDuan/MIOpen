#include <tuple>

#include <miopen/miopen.h>
#include <gtest/gtest.h>
#include <miopen/miopen.h>
#include <miopen/env.hpp>
#include "conv_2d.hpp"
#include "get_handle.hpp"

using TestCase = std::tuple<std::vector<std::string>, std::string>;

std::string GetFloatArg()
{
    static const auto tmp = miopen::GetEnv("MIOPEN_TEST_FLOAT_ARG");
    if(tmp.empty())
    {
        return "";
    }
    return tmp.front();
};

void GetArgs(const TestCase& param, std::vector<std::string>& tokens)
{
    auto env_vars = std::get<0>(param);
    for(auto& elem : env_vars)
    {
        putenv(elem.data());
    }

    auto cmd = std::get<1>(param);

    std::stringstream ss(cmd);
    std::istream_iterator<std::string> begin(ss);
    std::istream_iterator<std::string> end;
    while(begin != end)
        tokens.push_back(*begin++);
}

class Conv2dHalf : public testing::TestWithParam<std::vector<TestCase>>
{
};
class Conv2dInt8 : public testing::TestWithParam<std::vector<TestCase>>
{
};

void Run2dDriver(miopenDataType_t prec)
{

    std::vector<TestCase> params;
    switch(prec)
    {
    case miopenHalf: params = Conv2dHalf::GetParam(); break;
    case miopenInt8: params = Conv2dInt8::GetParam(); break;
    case miopenBFloat16:
    case miopenFloat:
    case miopenInt8x4:
    case miopenInt32:
    case miopenDouble:
        MIOPEN_THROW(miopenStatusBadParm,
                     "miopenBFloat16, miopenFloat, miopenInt8x4, miopenInt32, miopenDouble data "
                     "type not supported by "
                     "conv_igemm_mlir test");

    default: params = Conv2dHalf::GetParam();
    }

    for(const auto& test_value : params)
    {
        std::vector<std::string> tokens;
        GetArgs(test_value, tokens);
        std::vector<const char*> ptrs;

        for(std::string const& str : tokens)
            ptrs.push_back(str.data());

        testing::internal::CaptureStderr();
        test_drive<conv2d_driver>(ptrs.size(), ptrs.data());
        auto capture = testing::internal::GetCapturedStderr();
        EXPECT_FALSE(capture.find("Perf Db: record not found") != std::string::npos);
    }
};

TEST_P(Conv2dHalf, HalfTest)
{
#if MIOPEN_USE_MLIR

    const auto& handle = get_handle();
    if(miopen::StartsWith(handle.GetDeviceName(), "gfx900") || // Explicitly disabled archs
       miopen::StartsWith(handle.GetDeviceName(), "gfx908") ||
       miopen::StartsWith(handle.GetDeviceName(), "gfx90a") ||
       !miopen::IsEnvvarValueEnabled("MIOPEN_TEST_ALL") || GetFloatArg() != "--half")
    {
        GTEST_SKIP();
    }
    else if(miopen::StartsWith(handle.GetDeviceName(), "gfx103x") ||
            miopen::StartsWith(handle.GetDeviceName(), "gfx906")) // Implicited enabled arch
    {
        Run2dDriver(miopenHalf);
    }
    else // Implicitly disabled archs
    {
        GTEST_SKIP();
    }

#else
    GTEST_SKIP();
#endif
};

TEST_P(Conv2dInt8, Int8Test)
{
#if MIOPEN_USE_MLIR

    const auto& handle = get_handle();
    if(miopen::StartsWith(handle.GetDeviceName(), "gfx900") || // Explicitly disabled archs
       miopen::StartsWith(handle.GetDeviceName(), "gfx908") ||
       miopen::StartsWith(handle.GetDeviceName(), "gfx90a") ||
       !miopen::IsEnvvarValueEnabled("MIOPEN_TEST_ALL") || GetFloatArg() != "--int8")
    {
        GTEST_SKIP();
    }
    else if(miopen::StartsWith(handle.GetDeviceName(), "gfx103x") ||
            miopen::StartsWith(handle.GetDeviceName(), "gfx906")) // Implicited enabled arch
    {
        Run2dDriver(miopenInt8);
    }
    else // Implicitly disabled archs
    {
        GTEST_SKIP();
    }

#else
    GTEST_SKIP();
#endif
};

std::vector<TestCase> GetTestCases(const std::string& precision)
{
    std::vector<std::string> igemm_fwd = {"MIOPEN_FIND_MODE=normal",
                                          "MIOPEN_DEBUG_FIND_ONLY_SOLVER=ConvMlirIgemmFwd"};
    std::string flags_fwd    = " --verbose --disable-backward-data --disable-backward-weights";
    std::string layout_fwd   = " --in_layout NHWC --fil_layout NHWC --out_layout NHWC";
    std::string groupCount_4 = " --group-count 4";

    // FWD test cases for precision == "--int8"
    std::vector<TestCase> test_cases = {
        // clang-format off
    TestCase{igemm_fwd, precision + flags_fwd + " --input 256 128  28 28 --weights 128  128  3 3 --pads_strides_dilations 1 1 1 1 1 1"},
    TestCase{igemm_fwd, precision + flags_fwd + " --input 256 128  28 28 --weights 128  128  3 3 --pads_strides_dilations 1 1 1 1 1 1" + layout_fwd},
    TestCase{igemm_fwd, precision + flags_fwd + " --input 128 512  7  7  --weights 512  512  3 3 --pads_strides_dilations 1 1 1 1 1 1"},
    TestCase{igemm_fwd, precision + flags_fwd + " --input 128 512  7  7  --weights 512  512  3 3 --pads_strides_dilations 1 1 1 1 1 1" + layout_fwd},
    TestCase{igemm_fwd, precision + flags_fwd + " --input 128 64   56 56 --weights 64   64   1 1 --pads_strides_dilations 0 0 1 1 1 1"},
    TestCase{igemm_fwd, precision + flags_fwd + " --input 128 64   56 56 --weights 64   64   1 1 --pads_strides_dilations 0 0 1 1 1 1" + layout_fwd},
    TestCase{igemm_fwd, precision + flags_fwd + " --input 256 256  56 56 --weights 256  64   1 1 --pads_strides_dilations 0 0 1 1 1 1" + groupCount_4}
        // clang-format on
    };

    std::vector<std::string> igemm_bwd = {"MIOPEN_FIND_MODE=normal",
                                          "MIOPEN_DEBUG_FIND_ONLY_SOLVER=ConvMlirIgemmBwd"};
    std::vector<std::string> igemm_wrw = {"MIOPEN_FIND_MODE=normal",
                                          "MIOPEN_DEBUG_FIND_ONLY_SOLVER=ConvMlirIgemmWrW"};

    std::string flags_bwd      = " --verbose --disable-forward --disable-backward-weights";
    std::string flags_wrw      = " --verbose --disable-forward --disable-backward-data";
    std::string layout_bwd_wrw = " --in_layout NHWC --fil_layout NHWC --out_layout NHWC";
    std::string groupCount_32  = " --group-count 32";

    // BWD WRW test cases
    const std::vector<TestCase> test_cases_bwd_wrw = {
        // clang-format off
    TestCase{igemm_bwd, precision + flags_bwd + " --input 256 1024 14 14 --weights 2048 1024 1 1 --pads_strides_dilations 0 0 2 2 1 1"},
    TestCase{igemm_bwd, precision + flags_bwd + " --input 256 1024 14 14 --weights 2048 1024 1 1 --pads_strides_dilations 0 0 2 2 1 1" + layout_bwd_wrw},
    TestCase{igemm_bwd, precision + flags_bwd + " --input 256 128  28 28 --weights 128  128  3 3 --pads_strides_dilations 1 1 1 1 1 1"},
    TestCase{igemm_bwd, precision + flags_bwd + " --input 256 128  28 28 --weights 128  128  3 3 --pads_strides_dilations 1 1 1 1 1 1" + layout_bwd_wrw},
    TestCase{igemm_bwd, precision + flags_bwd + " --input 128 512  7  7  --weights 512  512  3 3 --pads_strides_dilations 1 1 1 1 1 1"},
    TestCase{igemm_bwd, precision + flags_bwd + " --input 128 512  7  7  --weights 512  512  3 3 --pads_strides_dilations 1 1 1 1 1 1" + layout_bwd_wrw},
    TestCase{igemm_bwd, precision + flags_bwd + " --input 128 64   56 56 --weights 64   64   1 1 --pads_strides_dilations 0 0 1 1 1 1"},
    TestCase{igemm_bwd, precision + flags_bwd + " --input 128 64   56 56 --weights 64   64   1 1 --pads_strides_dilations 0 0 1 1 1 1" + layout_bwd_wrw},
    
    TestCase{igemm_wrw, precision + flags_wrw + " --input 64  1024 14 14 --weights 256  1024 1 1 --pads_strides_dilations 0 0 1 1 1 1"},
    TestCase{igemm_wrw, precision + flags_wrw + " --input 64  1024 14 14 --weights 256  1024 1 1 --pads_strides_dilations 0 0 1 1 1 1" + layout_bwd_wrw},
    TestCase{igemm_wrw, precision + flags_wrw + " --input 256 256  14 14 --weights 256  256  3 3 --pads_strides_dilations 0 0 2 2 1 1"},
    TestCase{igemm_wrw, precision + flags_wrw + " --input 256 256  14 14 --weights 256  256  3 3 --pads_strides_dilations 0 0 2 2 1 1" + layout_bwd_wrw},
    TestCase{igemm_wrw, precision + flags_wrw + " --input 128 2048 7  7  --weights 512  2048 1 1 --pads_strides_dilations 0 0 1 1 1 1"},
    TestCase{igemm_wrw, precision + flags_wrw + " --input 128 2048 7  7  --weights 512  2048 1 1 --pads_strides_dilations 0 0 1 1 1 1" + layout_bwd_wrw},    
    TestCase{igemm_wrw, precision + flags_wrw + " --input 128 64   56 56 --weights 64   64   1 1 --pads_strides_dilations 0 0 1 1 1 1" + layout_bwd_wrw},
    TestCase{igemm_wrw, precision + flags_wrw + " --input 256 1024 14 14 --weights 1024 32   1 1 --pads_strides_dilations 0 0 1 1 1 1" + groupCount_32}
        // clang-format on
    };

    // FWD BWD WRW cases in test_cases for precision == "--half"
    if(precision == "--half")
    {
        test_cases.reserve(test_cases_bwd_wrw.size());
        test_cases.insert(test_cases.end(), test_cases_bwd_wrw.begin(), test_cases_bwd_wrw.end());
    }

    return test_cases;
}
// Half for FWD, BWD, WRW
INSTANTIATE_TEST_SUITE_P(Conv2dGroup, Conv2dHalf, testing::Values(GetTestCases("--half")));
// Int8 for FWD
INSTANTIATE_TEST_SUITE_P(Conv2dGroup, Conv2dInt8, testing::Values(GetTestCases("--int8")));