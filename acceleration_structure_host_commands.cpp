/*
 * Copyright (C) 2021-2021 Imagination Technologies Limited.
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
 *
 * Author: PowerVR by Imagination, Developer Technology Team.
 *
 * This file is originally a heavily modified fork of
 * https://github.com/LunarG/VulkanTools/blob/394a63378dbcd4bcc710b3fce88d2724343f92eb/layersvt/device_simulation.cpp
 *
 * Author: Mike Weiblen <mikew@lunarg.com>
 * Author: Arda Coskunses <arda@lunarg.com>
 * Author: Jeremy Kniager <jeremyk@lunarg.com>
 * Author: PowerVR by Imagination, Developer Technology Team.
 *
 * Copyright (C) 2015-2021 Valve Corporation
 * Copyright (C) 2015-2021 LunarG, Inc.
 */
#include <assert.h>
#include <stdlib.h>
#include <cinttypes>

#include <functional>
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>
#include <string.h>

#include "vulkan/vulkan.h"
#include "vulkan/vk_layer.h"

namespace {

	// === Vulkan meta loader === 
	// Dispatch tables to load vulkan functions

#define DEFINE_PROC_MEMBER(proc_name) PFN_##proc_name proc_name
#define LOAD_PROC(proc) table.proc = (PFN_##proc)handler(source_handle, #proc);

	struct InstanceDispatchTable {
		DEFINE_PROC_MEMBER(vkEnumeratePhysicalDevices);
		DEFINE_PROC_MEMBER(vkEnumerateDeviceExtensionProperties);
		DEFINE_PROC_MEMBER(vkGetPhysicalDeviceFeatures);
		DEFINE_PROC_MEMBER(vkDestroyInstance);
		DEFINE_PROC_MEMBER(vkGetPhysicalDeviceFeatures2);
		DEFINE_PROC_MEMBER(vkGetPhysicalDeviceQueueFamilyProperties);
		DEFINE_PROC_MEMBER(vkGetInstanceProcAddr);
	};

	struct DeviceDispatchTable {
		DEFINE_PROC_MEMBER(vkCreateCommandPool);
		DEFINE_PROC_MEMBER(vkAllocateCommandBuffers);
		DEFINE_PROC_MEMBER(vkBeginCommandBuffer);
		DEFINE_PROC_MEMBER(vkCmdBuildAccelerationStructuresKHR);
		DEFINE_PROC_MEMBER(vkEndCommandBuffer);
		DEFINE_PROC_MEMBER(vkGetDeviceQueue);
		DEFINE_PROC_MEMBER(vkQueueSubmit);
		DEFINE_PROC_MEMBER(vkQueueWaitIdle);
		DEFINE_PROC_MEMBER(vkDestroyCommandPool);
		DEFINE_PROC_MEMBER(vkGetDeviceProcAddr);
		DEFINE_PROC_MEMBER(vkDestroyDevice);
	};

	template<typename DispatchableType>
	void** DispatchableObjectKeyP(DispatchableType inst)
	{
		return (void**)inst;
	}

	template<typename DispatchableType>
	void* DispatchableObjectKey(DispatchableType inst)
	{
		return *DispatchableObjectKeyP(inst);
	}

	// Map vulkan loader dispatch table pointer (retrievable through DispatchableObjectKey) to our dispatch tables
	// See e.g. https://renderdoc.org/vulkan-layer-guide.html for more information
	std::unordered_map<void*, DeviceDispatchTable> device_dispatch_table_map;
	std::unordered_map<void*, InstanceDispatchTable> instance_dispatch_table_map;

	void PopulateInstanceDispatchTable(VkInstance instance, PFN_vkGetInstanceProcAddr handler) {
		InstanceDispatchTable table = {};
		VkInstance source_handle = instance;
		LOAD_PROC(vkEnumeratePhysicalDevices);
		LOAD_PROC(vkEnumerateDeviceExtensionProperties);
		LOAD_PROC(vkGetPhysicalDeviceFeatures);
		LOAD_PROC(vkDestroyInstance);
		LOAD_PROC(vkGetPhysicalDeviceFeatures2);
		LOAD_PROC(vkGetPhysicalDeviceQueueFamilyProperties);
		LOAD_PROC(vkGetInstanceProcAddr);
		instance_dispatch_table_map.insert(std::make_pair(DispatchableObjectKey(instance), table));
	}

	void PopulateDeviceDispatchTable(VkDevice device, PFN_vkGetDeviceProcAddr handler) {
		DeviceDispatchTable table = {};
		VkDevice source_handle = device;
		LOAD_PROC(vkCreateCommandPool);
		LOAD_PROC(vkAllocateCommandBuffers);
		LOAD_PROC(vkBeginCommandBuffer);
		LOAD_PROC(vkCmdBuildAccelerationStructuresKHR);
		LOAD_PROC(vkEndCommandBuffer);
		LOAD_PROC(vkGetDeviceQueue);
		LOAD_PROC(vkQueueSubmit);
		LOAD_PROC(vkQueueWaitIdle);
		LOAD_PROC(vkDestroyCommandPool);
		LOAD_PROC(vkGetDeviceProcAddr);
		LOAD_PROC(vkDestroyDevice);
		device_dispatch_table_map.insert(std::make_pair(DispatchableObjectKey(device), table));
	}

#undef LOAD_PROC
#undef DEFINE_PROC_MEMBER

	// === Various small utility functions ===

	// Get all elements from a vkEnumerate*() lambda into a std::vector.
	template <typename T>
	VkResult EnumerateAll(std::vector<T>* vect, std::function<VkResult(uint32_t*, T*)> func) {
		VkResult result = VK_INCOMPLETE;
		do {
			uint32_t count = 0;
			result = func(&count, nullptr);
			assert(result == VK_SUCCESS);
			vect->resize(count);
			result = func(&count, vect->data());
		} while (result == VK_INCOMPLETE);
		return result;
	}

	// Retrieve chain info for device creation
	VkLayerDeviceCreateInfo* get_chain_info(const VkDeviceCreateInfo* pCreateInfo, VkLayerFunction func) {
		VkLayerDeviceCreateInfo* chain_info = (VkLayerDeviceCreateInfo*)pCreateInfo->pNext;
		while (chain_info && !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain_info->function == func)) {
			chain_info = (VkLayerDeviceCreateInfo*)chain_info->pNext;
		}
		assert(chain_info != NULL);
		return chain_info;
	}

	// Retrieve chain info for instance creation
	VkLayerInstanceCreateInfo* get_chain_info(const VkInstanceCreateInfo* pCreateInfo, VkLayerFunction func) {
		VkLayerInstanceCreateInfo* chain_info = (VkLayerInstanceCreateInfo*)pCreateInfo->pNext;
		while (chain_info && !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain_info->function == func)) {
			chain_info = (VkLayerInstanceCreateInfo*)chain_info->pNext;
		}
		assert(chain_info != NULL);
		return chain_info;
	}

	// === Misc. globals ===

	std::mutex global_lock;  // Enforce thread-safety for this layer. TODO: Possible optimization: Have multiple locks instead of a single global one.

	uint32_t loader_layer_iface_version = CURRENT_LOADER_LAYER_INTERFACE_VERSION;

	std::unordered_map<VkDevice, VkPhysicalDevice> logicalDeviceToPhysicalDevice;
	std::unordered_map<VkDevice, VkCommandPool> deviceToCommandPool;
	std::unordered_map<VkDevice, VkCommandBuffer> deviceToCommandBuffer;

	// Utility function for fetching elements from lists above
	template<typename KeyType, typename VkHandle>
	VkHandle GetMapValueOrNullHandle(const std::unordered_map<KeyType, VkHandle>& map, KeyType key) {
		auto iter = map.find(key);
		if (iter == map.end())
			return VK_NULL_HANDLE;
		else
			return iter->second;
	}

	// === Layer-specific wrappers for Vulkan functions, accessed via vkGet*ProcAddr() ===

	VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
		std::lock_guard<std::mutex> lock(global_lock);

		// Set up dispatch table
		VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
		assert(chain_info->u.pLayerInfo);

		PFN_vkGetInstanceProcAddr fp_get_instance_proc_addr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
		PFN_vkGetDeviceProcAddr fp_get_device_proc_addr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
		PFN_vkCreateDevice fp_create_device = (PFN_vkCreateDevice)fp_get_instance_proc_addr(nullptr, "vkCreateDevice");
		if (!fp_create_device) {
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
		VkResult result = fp_create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);

		PopulateDeviceDispatchTable(*pDevice, fp_get_device_proc_addr);
		logicalDeviceToPhysicalDevice.insert(std::pair<VkDevice, VkPhysicalDevice>(*pDevice, physicalDevice));

		return result;
	}

	VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
		if (device) {
			std::lock_guard<std::mutex> lock(global_lock);

			void* deviceKey = DispatchableObjectKey(device);
			auto ddt = device_dispatch_table_map.at(deviceKey);

			VkCommandBuffer commandBuffer = GetMapValueOrNullHandle(deviceToCommandBuffer, device);
			VkCommandPool commandPool = GetMapValueOrNullHandle(deviceToCommandPool, device);
			// When destroying the command pool corresponding command buffers will be freed automatically
			if (commandPool != VK_NULL_HANDLE) ddt.vkDestroyCommandPool(device, commandPool, NULL);

			deviceToCommandBuffer.erase(device);
			deviceToCommandPool.erase(device);
			logicalDeviceToPhysicalDevice.erase(device);
			device_dispatch_table_map.erase(deviceKey);

			ddt.vkDestroyDevice(device, pAllocator);
		}
	}

	// Generic layer dispatch table setup
	static VkResult LayerSetupCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
		VkInstance* pInstance) {

		VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
		assert(chain_info->u.pLayerInfo);

		PFN_vkGetInstanceProcAddr fp_get_instance_proc_addr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
		PFN_vkCreateInstance fp_create_instance = (PFN_vkCreateInstance)fp_get_instance_proc_addr(nullptr, "vkCreateInstance");
		if (!fp_create_instance) {
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
		VkResult result = fp_create_instance(pCreateInfo, pAllocator, pInstance);
		if (result != VK_SUCCESS) {
			return result;
		}

		PopulateInstanceDispatchTable(*pInstance, fp_get_instance_proc_addr);

		return VK_SUCCESS;
	}

	VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
		VkInstance* pInstance) {

		const VkApplicationInfo* app_info = pCreateInfo->pApplicationInfo;
		const uint32_t requested_version = (app_info && app_info->apiVersion) ? app_info->apiVersion : VK_API_VERSION_1_0;

		std::lock_guard<std::mutex> lock(global_lock);

		// Actually create the instance...
		VkResult result = LayerSetupCreateInstance(pCreateInfo, pAllocator, pInstance);
		if (result != VK_SUCCESS) {
			return result;
		}

		// Our layer-specific initialization...
		auto idt = instance_dispatch_table_map.at(DispatchableObjectKey(*pInstance));

		std::vector<VkPhysicalDevice> physical_devices;
		result = EnumerateAll<VkPhysicalDevice>(&physical_devices, [&](uint32_t* count, VkPhysicalDevice* results) {
			return idt.vkEnumeratePhysicalDevices(*pInstance, count, results);
			});
		if (result != VK_SUCCESS) {
			return result;
		}

		return result;
	}

	VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
		if (instance) {
			std::lock_guard<std::mutex> lock(global_lock);

			void* instanceKey = DispatchableObjectKey(instance);
			auto idt = instance_dispatch_table_map.at(instanceKey);
			idt.vkDestroyInstance(instance, pAllocator);
			instance_dispatch_table_map.erase(instanceKey);
		}
	}

	bool HasExtension(VkPhysicalDevice physical_device, std::string extension_name) {
		auto idt = instance_dispatch_table_map.at(DispatchableObjectKey(physical_device));

		std::vector<VkExtensionProperties> device_extensions;
		EnumerateAll<VkExtensionProperties>(&(device_extensions),
			[&](uint32_t* count, VkExtensionProperties* results) {
				return idt.vkEnumerateDeviceExtensionProperties(physical_device, nullptr, count, results);
			});

		for (auto extension : device_extensions) {
			if (extension.extensionName == extension_name) {
				return true;
			}
		}
		return false;
	}

	// Utility function for iterating through the pNext chain of certain Vulkan structs.
	void FillPNextChain(VkPhysicalDevice* physical_device, void* place) {
		while (place) {
			VkBaseOutStructure* structure = (VkBaseOutStructure*)place;
			if (structure->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR &&
				HasExtension(*physical_device, "VK_KHR_acceleration_structure")) {
				VkPhysicalDeviceAccelerationStructureFeaturesKHR* psf = (VkPhysicalDeviceAccelerationStructureFeaturesKHR*)place;
				psf->accelerationStructureHostCommands = 1;
			}
			place = structure->pNext;
		}
	}

	VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) {
		{
			std::lock_guard<std::mutex> lock(global_lock);
			auto idt = instance_dispatch_table_map.at(DispatchableObjectKey(physicalDevice));
			idt.vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
			idt.vkGetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);
		}
		FillPNextChain(&physicalDevice, pFeatures->pNext);
	}

	VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) {
		GetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
	}

	uint32_t GetComputeQueueFamilyIndex(const std::vector<VkQueueFamilyProperties>& queueFamilyProperties) {
		// First, look for a queue that supports compute but not graphics
		for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
			if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
				((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)) {
				return i;
			}
		}
		// If we can't find that, we just pick any old queue that supports compute
		for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
			if (queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
				return i;
			}
		}
		// There are no compute queues. Uh-oh.
		throw std::runtime_error("Could not find a queue that supports compute");
	}

	VKAPI_ATTR VkResult VKAPI_CALL
		BuildAccelerationStructuresKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount,
			const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
			const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos) {

		std::lock_guard<std::mutex> lock(global_lock);
		auto ddt = device_dispatch_table_map.at(DispatchableObjectKey(device));

		VkPhysicalDevice physicalDevice = logicalDeviceToPhysicalDevice.at(device);
		auto idt = instance_dispatch_table_map.at(DispatchableObjectKey(physicalDevice));

		// Retrieve queue family properties
		uint32_t queueFamilyCount;
		idt.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		assert(queueFamilyCount > 0);
		std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
		idt.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

		// This defines what queue we will be submitting to
		// TODO: Could be cached
		const uint32_t queueFamilyIndex = GetComputeQueueFamilyIndex(queueFamilyProperties);
		const uint32_t queueIndex = 0;  // We don't care which queue we use as long as it is from the right family - use the first one.

		// In case of error we return one of the two acceptable failure codes for this function.
		// VK_ERROR_OUT_OF_DEVICE_MEMORY is very often used as a "something went wrong" code.
#define CHECK_RESULT(res) if (res != VK_SUCCESS) return VK_ERROR_OUT_OF_DEVICE_MEMORY;

		// Create command pool if necessary
		VkCommandPool cmdPool = GetMapValueOrNullHandle(deviceToCommandPool, device);
		if (cmdPool == VK_NULL_HANDLE) {
			VkCommandPoolCreateInfo cmdPoolInfo = {};
			cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
			cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			CHECK_RESULT(ddt.vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool));
			deviceToCommandPool.insert(std::make_pair(device, cmdPool));
		}

		// Allocate command buffer if necessary
		const uint32_t commandBufferCount = 1;
		VkCommandBuffer commandBuffer = GetMapValueOrNullHandle(deviceToCommandBuffer, device);
		if (commandBuffer == VK_NULL_HANDLE) {
			VkCommandBufferAllocateInfo allocateInfo = {};
			allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocateInfo.commandPool = cmdPool;
			allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocateInfo.commandBufferCount = commandBufferCount;
			CHECK_RESULT(ddt.vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer));
			*DispatchableObjectKeyP(commandBuffer) = DispatchableObjectKey(device);
			deviceToCommandBuffer.insert(std::make_pair(device, commandBuffer));
		}

		// Begin command buffer
		VkCommandBufferBeginInfo commandBufferBeginInfo = {};
		commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		CHECK_RESULT(ddt.vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

		// Build acceleration structures on device
		ddt.vkCmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
		CHECK_RESULT(ddt.vkEndCommandBuffer(commandBuffer));

		// Submit to an appropriate queue
		VkQueue queue;
		ddt.vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, &queue);
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = commandBufferCount;
		submitInfo.pCommandBuffers = &commandBuffer;
		// Waiting for the queue to be idle before submit is inefficient and a bit blunt, but it garauntees that we have no issues
		CHECK_RESULT(ddt.vkQueueWaitIdle(queue));
		const uint32_t submitCount = 1;
		CHECK_RESULT(ddt.vkQueueSubmit(queue, submitCount, &submitInfo, VK_NULL_HANDLE));
		// The function we're emulating isn't deferred, so we should wait for it to finish
		CHECK_RESULT(ddt.vkQueueWaitIdle(queue));

#undef CHECK_RESULT

		// We do not currently support BuildAccelerationStructuresKHR as a deferred operation
		if (deferredOperation != NULL)
			return VK_OPERATION_NOT_DEFERRED_KHR;
		else
			return VK_SUCCESS;
	}

#define TRY_REPLACE_PROC_ADDR(func) if (strcmp("vk" #func, pName) == 0) return reinterpret_cast<PFN_vkVoidFunction>(func);

	VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName) {
		TRY_REPLACE_PROC_ADDR(GetInstanceProcAddr);
		TRY_REPLACE_PROC_ADDR(DestroyInstance);
		TRY_REPLACE_PROC_ADDR(GetPhysicalDeviceFeatures2);
		TRY_REPLACE_PROC_ADDR(GetPhysicalDeviceFeatures2KHR);
		TRY_REPLACE_PROC_ADDR(CreateDevice);
		TRY_REPLACE_PROC_ADDR(CreateInstance);
		TRY_REPLACE_PROC_ADDR(CreateDevice);
		TRY_REPLACE_PROC_ADDR(DestroyDevice);
		TRY_REPLACE_PROC_ADDR(BuildAccelerationStructuresKHR);

		if (!instance) {
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(global_lock);
		auto idt = instance_dispatch_table_map.at(DispatchableObjectKey(instance));

		if (!idt.vkGetInstanceProcAddr) {
			return nullptr;
		}
		return idt.vkGetInstanceProcAddr(instance, pName);
	}

	VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* pName) {
		TRY_REPLACE_PROC_ADDR(GetDeviceProcAddr);
		TRY_REPLACE_PROC_ADDR(CreateDevice);
		TRY_REPLACE_PROC_ADDR(DestroyDevice);
		TRY_REPLACE_PROC_ADDR(BuildAccelerationStructuresKHR);

		if (!device) {
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(global_lock);
		auto ddt = device_dispatch_table_map.at(DispatchableObjectKey(device));

		if (!ddt.vkGetDeviceProcAddr) {
			return nullptr;
		}
		return ddt.vkGetDeviceProcAddr(device, pName);
	}

#undef TRY_REPLACE_PROC_ADDR

}  // anonymous namespace

// === Function symbols statically exported by this layer's library ===
// Keep synchronized with VisualStudio's VkLayer_device_simulation.def

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
	return GetInstanceProcAddr(instance, pName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
	return GetDeviceProcAddr(device, pName);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
	return CreateInstance(pCreateInfo, pAllocator, pInstance);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice,
	const VkDeviceCreateInfo* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
	return CreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {

	assert(pVersionStruct != NULL);
	assert(pVersionStruct->sType == LAYER_NEGOTIATE_INTERFACE_STRUCT);

	if (pVersionStruct->loaderLayerInterfaceVersion > CURRENT_LOADER_LAYER_INTERFACE_VERSION) {
		// Loader is requesting newer interface version; reduce to the version we support.
		pVersionStruct->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
	}
	else if (pVersionStruct->loaderLayerInterfaceVersion < CURRENT_LOADER_LAYER_INTERFACE_VERSION) {
		// Loader is requesting older interface version; record the Loader's version
		loader_layer_iface_version = pVersionStruct->loaderLayerInterfaceVersion;
	}

	if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
		pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
		pVersionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
		pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
	}

	return VK_SUCCESS;
}
