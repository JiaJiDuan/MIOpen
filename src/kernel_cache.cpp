/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2017 Advanced Micro Devices, Inc.
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
/* ************************************************************************
 * Copyright 2015 Vratis, Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ************************************************************************ */

#include <miopen/env.hpp>
#include <miopen/errors.hpp>
#include <miopen/kernel_cache.hpp>
#include <miopen/logger.hpp>
#include <miopen/stringutils.hpp>

#include <iostream>
#include <iterator>

MIOPEN_DECLARE_ENV_VAR(MIOPEN_DEVICE_ARCH)

namespace miopen {

const std::vector<Kernel>& KernelCache::GetKernels(const std::string& algorithm,
                                                   const std::string& network_config)
{

    std::pair<std::string, std::string> key = std::make_pair(algorithm, network_config);

    const auto it = kernel_map.find(key);
    if(it != kernel_map.end())
    {
        MIOPEN_LOG_I2(it->second.size()
                      << " kernels for key: " << key.first << " \"" << key.second << '\"');
        return it->second;
    }

    static const std::vector<Kernel> empty{};
    MIOPEN_LOG_I2("0 kernels for key: " << key.first << " \"" << key.second << '\"');
    return empty;
}

bool KernelCache::HasKernels(const std::string& algorithm, const std::string& network_config) const
{
    const auto key = std::make_pair(algorithm, network_config);
#ifndef NDEBUG
    MIOPEN_LOG_I("Key: " << key.first << " \"" << key.second << '\"');
#endif
    const auto it = kernel_map.find(key);
    if(it == kernel_map.end())
        return false;

    if(it->second.empty())
    {
        MIOPEN_THROW("There should be at least one kernel in kernel cache if an entry exists");
    }

    return true;
}

bool KernelCache::HasProgram(const std::string& name, const std::string& params) const
{
    const auto key = std::make_pair(name, params);
    return program_map.count(key) > 0;
}

void KernelCache::ClearProgram(const std::string& name, const std::string& params)
{
    if(HasProgram(name, params))
    {
        const auto key = std::make_pair(name, params);
        program_map.erase(key);
    }
}

void KernelCache::AddProgram(Program prog, const std::string& program_name, std::string params)
{
    program_map[std::make_pair(program_name, params)] = prog;
}

Kernel KernelCache::AddKernel(const Handle& h,
                              const std::string& algorithm,
                              const std::string& network_config,
                              const solver::KernelBuildDefinition& build_definition,
                              std::size_t cache_index)
{
    const std::pair<std::string, std::string> key = std::make_pair(algorithm, network_config);
    if(!network_config.empty() || !algorithm.empty()) // Don't log only _empty_ keys.
        MIOPEN_LOG_I2("Key: " << key.first << " \"" << key.second << '\"');

    auto built      = solver::BuildProgramResult{};
    auto params     = build_definition.stringifier(build_definition.build_parameters);
    auto program_it = program_map.find(std::make_pair(build_definition.kernel_file, params));

    if(program_it != program_map.end())
    {
        built = build_definition(program_it->second);
    }
    else
    {
        built = h.LoadProgram(build_definition);
        program_map[std::make_pair(build_definition.kernel_file, params)] = built.program;
    }

    Kernel kernel{};
    const char* const arch = miopen::GetStringEnv(MIOPEN_DEVICE_ARCH{});
    if(arch != nullptr && strlen(arch) > 0)
    {
        kernel = Kernel{built.program, build_definition.kernel_name};
    }
    else
    {
        kernel = Kernel{built.program, build_definition.kernel_name, built.l_wk, built.g_wk};
    }

    if(!network_config.empty() && !algorithm.empty())
    {
        this->AddKernel(key, kernel, cache_index);
    }
    return kernel;
}

Kernel KernelCache::AddKernel(const Handle& h,
                              const std::string& algorithm,
                              const std::string& network_config,
                              const std::string& program_name,
                              const std::string& kernel_name,
                              const std::vector<size_t>& vld,
                              const std::vector<size_t>& vgd,
                              std::string params,
                              std::size_t cache_index,
                              bool is_kernel_miopengemm_str,
                              const std::string& kernel_src)
{
    const auto build_definition = [&]() {
        auto kernel_info = solver::KernelInfo{};

        kernel_info.kernel_file  = program_name;
        kernel_info.kernel_name  = kernel_name;
        kernel_info.l_wk         = vld;
        kernel_info.g_wk         = vgd;
        kernel_info.comp_options = params;

        auto result = solver::KernelBuildDefinition{kernel_info};

        // default value
        if(!is_kernel_miopengemm_str)
            is_kernel_miopengemm_str = algorithm.find("ImplicitGEMM") == std::string::npos &&
                                       algorithm.find("GEMM") != std::string::npos;

        if(is_kernel_miopengemm_str)
            result.kernel_src = kernel_src;

        return result;
    }();

    return AddKernel(h, algorithm, network_config, build_definition, cache_index);
}

void KernelCache::AddKernel(Key key, Kernel k, std::size_t cache_index)
{
    auto&& v = kernel_map[key];
    if(cache_index >= v.size())
    {
        v.resize(cache_index + 1);
    }
    v[cache_index] = k;
}

void KernelCache::ClearKernels(const std::string& algorithm, const std::string& network_config)
{
    if(network_config.empty() || algorithm.empty())
    {
        MIOPEN_THROW("Network config or algorithm empty.");
    }
    const std::pair<std::string, std::string> key = std::make_pair(algorithm, network_config);
    auto&& v                                      = this->kernel_map[key];
    if(!v.empty())
    {
        MIOPEN_LOG_I2(v.size() << " kernels for key: " << key.first << " \"" << key.second << '\"');
    }
    v.clear();
}

KernelCache::KernelCache() {}

} // namespace miopen
