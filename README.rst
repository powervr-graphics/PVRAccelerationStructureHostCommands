
====================================
PVRAccelerationStructureHostCommands
====================================

VK_LAYER_POWERVR_acceleration_structure_host_commands is a Vulkan layer that emulates building acceleration structures (see the `VK_KHR_acceleration_structure Vulkan extension <https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_acceleration_structure.html>`_) on the host with drivers which would normally only support building them on device.

Note that it does not emulate the extension as a whole; it must be supported by the users graphics driver for this layer to do anything.

One use case for this layer might be to facilitate testing of an application written to support accelerationStructureHostCommands on a device that would otherwise not support it.

The layer achieves this functionality by doing two things:

* Enabling the accelerationStructureHostCommands feature in the VkPhysicalDeviceAccelerationStructureFeaturesKHR struct.

* Emulating calls to the vkBuildAccelerationStructuresKHR procedure by performing those builds on the device rather than on the host, which would be its typical behavior. Some caveats:

  - It will not attempt to defer operations if requested, but will instead run them as not deferred and return VK_OPERATION_NOT_DEFERRED_KHR.

  - Should the emulation fail unexpectedly, the procedure will return VK_ERROR_OUT_OF_DEVICE_MEMORY.

System Requirements
-------------------

The layer requires Vulkan Support. It has been tested to work properly on Windows and Linux.

Enabling the layer will not have any tangible effect on your application unless it supports the VK_KHR_acceleration_structure Vulkan extension.

Building it requires CMake 3.9+, and a C++11 compiler.

Building
--------

This  project is built using cmake, e.g. like so::

    mkdir build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build .

Setting up and enabling PVRAccelerationStructureHostCommands
------------------------------------------------------------

The layer is set up and enabled like most Vulkan Layers. See the detailed set of instructions below.

1. Ensure that the layer is visible to the Vulkan loader. For instance via setting the VK_LAYER_PATH environment variable to the directory or folder containing PVRAccelerationStructureHostCommands.json.

   - Optionally run vulkaninfo and search for the layer name VK_LAYER_POWERVR_acceleration_structure_host_commands to verify that the layer is in fact visible to the loader.

2. Enable the layer, for instance by setting the environment variable VK_INSTANCE_LAYERS to VK_LAYER_POWERVR_acceleration_structure_host_commands.

3. Run your Vulkan application.

Further Details
---------------

Developers can interact with our online Community at `developer.imaginationtech.com <https://developer.imaginationtech.com/>`_.

Release notes
-------------

For the latest version of the Release Notes detailing what has changed in this release, please visit `Release Notes <https://developer.imaginationtech.com/tools/release-notes/>`_.