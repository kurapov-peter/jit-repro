// Copyright 2022 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <iostream>

#include <level_zero/ze_api.h>

namespace
{

    template <typename F>
    auto catchAll(F &&func)
    {
        try
        {
            return func();
        }
        catch (const std::exception &e)
        {
            fprintf(stdout, "An exception was thrown: %s\n", e.what());
            fflush(stdout);
            abort();
        }
        catch (...)
        {
            fprintf(stdout, "An unknown exception was thrown\n");
            fflush(stdout);
            abort();
        }
    }

    inline void checkResult(ze_result_t res, const char *func)
    {
        if (res != ZE_RESULT_SUCCESS)
            throw std::runtime_error(std::string(func) +
                                     " failed: " + std::to_string(res));
    }

#define CHECK_ZE_RESULT(expr) checkResult((expr), #expr);

} // namespace

static std::pair<ze_driver_handle_t, ze_device_handle_t>
getDriverAndDevice(ze_device_type_t deviceType = ZE_DEVICE_TYPE_GPU)
{

    CHECK_ZE_RESULT(zeInit(ZE_INIT_FLAG_GPU_ONLY));
    uint32_t driverCount = 0;
    CHECK_ZE_RESULT(zeDriverGet(&driverCount, nullptr));

    std::vector<ze_driver_handle_t> allDrivers{driverCount};
    CHECK_ZE_RESULT(zeDriverGet(&driverCount, allDrivers.data()));

    // Find a driver instance with a GPU device
    std::vector<ze_device_handle_t> devices;
    std::cout << "driverCount: " << driverCount << std::endl;
    for (uint32_t i = 0; i < driverCount; ++i)
    {
        uint32_t deviceCount = 0;
        CHECK_ZE_RESULT(zeDeviceGet(allDrivers[i], &deviceCount, nullptr));
        std::cout << "device count: " << deviceCount << std::endl;
        if (deviceCount == 0)
            continue;
        devices.resize(deviceCount);
        CHECK_ZE_RESULT(zeDeviceGet(allDrivers[i], &deviceCount, devices.data()));
        for (uint32_t d = 0; d < deviceCount; ++d)
        {
            ze_device_properties_t device_properties = {};
            CHECK_ZE_RESULT(zeDeviceGetProperties(devices[d], &device_properties));
            std::cout << "device name: " << device_properties.name << std::endl;
            std::cout << "device type: " << device_properties.type << std::endl;
            if (deviceType == device_properties.type)
            {
                auto driver = allDrivers[i];
                auto device = devices[d];
                return {driver, device};
            }
        }
    }
    throw std::runtime_error("getDevice failed");
}

struct GPUL0QUEUE
{

    ze_driver_handle_t zeDriver_ = nullptr;
    ze_device_handle_t zeDevice_ = nullptr;
    ze_context_handle_t zeContext_ = nullptr;
    ze_command_list_handle_t zeCommandList_ = nullptr;

    GPUL0QUEUE()
    {
        auto driverAndDevice = getDriverAndDevice();
        zeDriver_ = driverAndDevice.first;
        zeDevice_ = driverAndDevice.second;

        ze_context_desc_t contextDesc = {ZE_STRUCTURE_TYPE_CONTEXT_DESC, nullptr,
                                         0};
        CHECK_ZE_RESULT(zeContextCreate(zeDriver_, &contextDesc, &zeContext_));

        uint32_t numQueueGroups = 0;
        CHECK_ZE_RESULT(zeDeviceGetCommandQueueGroupProperties(
            zeDevice_, &numQueueGroups, nullptr));

        std::vector<ze_command_queue_group_properties_t> queueProperties(
            numQueueGroups);
        CHECK_ZE_RESULT(zeDeviceGetCommandQueueGroupProperties(
            zeDevice_, &numQueueGroups, queueProperties.data()));

        ze_command_queue_desc_t desc = {};
        desc.mode = ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS;
        for (uint32_t i = 0; i < numQueueGroups; i++)
        {
            if (queueProperties[i].flags &
                ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE)
            {
                desc.ordinal = i;
            }
        }
        CHECK_ZE_RESULT(zeCommandListCreateImmediate(zeContext_, zeDevice_, &desc,
                                                     &zeCommandList_));
    }

    ~GPUL0QUEUE()
    {
        // Device and Driver resource management is dony by L0.
        // Just release context and commandList.
        // TODO: Use unique ptrs.
        if (zeContext_)
            CHECK_ZE_RESULT(zeContextDestroy(zeContext_));

        if (zeCommandList_)
            CHECK_ZE_RESULT(zeCommandListDestroy(zeCommandList_));
    }
};

static ze_module_handle_t loadModule(GPUL0QUEUE *queue, const void *data,
                                     size_t dataSize)
{
    assert(data);
    auto gpuL0Queue = queue;
    ze_module_handle_t zeModule;
    ze_module_desc_t desc = {};

    const char *build_flags = nullptr;

    if (getenv("ENABLE_VC_PATH"))
    {
        build_flags = "-vc-codegen";
    }

    if (getenv("MIMIC_OCLOC"))
    {
        build_flags = "-ze-intel-has-buffer-offset-arg "
                      "-cl-intel-greater-than-4GB-buffer-required "
                      "-cl-store-cache-default=2 -cl-load-cache-default=4";
    }
    if (getenv("COMPILE_IGC_FLAGS")) {
        build_flags = getenv("COMPILE_IGC_FLAGS");
    }

    std::cerr << "L0 module build flags: "
              << (build_flags ? build_flags : "empty") << std::endl;

    ze_module_build_log_handle_t buildlog = nullptr;

    desc.stype = ZE_STRUCTURE_TYPE_MODULE_DESC;
    desc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    desc.pInputModule = static_cast<const uint8_t *>(data);
    desc.inputSize = dataSize;
    desc.pBuildFlags = build_flags;
    CHECK_ZE_RESULT(zeModuleCreate(gpuL0Queue->zeContext_, gpuL0Queue->zeDevice_,
                                   &desc, &zeModule, nullptr));
    size_t logSize = 0;
    CHECK_ZE_RESULT(zeModuleBuildLogGetString(buildlog, &logSize, nullptr));
    std::string strLog(logSize, ' ');
    CHECK_ZE_RESULT(zeModuleBuildLogGetString(buildlog, &logSize, strLog.data()));
    if (logSize > 0)
    {
        std::cerr << "L0 Module build log:\n"
                  << strLog << std::endl;
    }

    return zeModule;
}

// Wrappers
extern "C" GPUL0QUEUE *
gpuCreateStream(void *device, void *context)
{
    return catchAll([&]()
                    { return new GPUL0QUEUE(); });
}

extern "C" void gpuStreamDestroy(GPUL0QUEUE *queue)
{
    catchAll([&]()
             { delete queue; });
}

extern "C" ze_module_handle_t
gpuModuleLoad(GPUL0QUEUE *queue, const void *data, size_t dataSize)
{
    return catchAll([&]()
                    { return loadModule(queue, data, dataSize); });
}
