// SR.ISAProbe: compile a SPIR-V module with the installed Vulkan driver and
// print the ISA it produced (VK_AMD_shader_info). This is the round-trip half
// of the divergence harness: SPIR-V -> vendor compiler -> gfx ISA.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace {

std::vector<std::uint32_t> ReadSpirv(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (bytes.empty() || bytes.size() % 4 != 0)
        return {};
    std::vector<std::uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    for (const auto& extension : extensions)
    {
        if (std::strcmp(extension.extensionName, name) == 0)
            return true;
    }
    return false;
}

struct DeviceSelection
{
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkPhysicalDeviceProperties properties{};
    bool shaderInfo = false;
};

DeviceSelection PickDevice(VkInstance instance)
{
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    for (const VkPhysicalDevice device : devices)
    {
        std::uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

        std::uint32_t computeFamily = 0;
        bool hasCompute = false;
        for (std::uint32_t i = 0; i < familyCount; ++i)
        {
            if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
            {
                computeFamily = i;
                hasCompute = true;
                break;
            }
        }

        if (!hasCompute)
            continue;

        DeviceSelection selection;
        selection.physical = device;
        selection.queueFamily = computeFamily;
        selection.shaderInfo = HasExtension(extensions, VK_AMD_SHADER_INFO_EXTENSION_NAME);
        vkGetPhysicalDeviceProperties(device, &selection.properties);
        return selection;
    }

    return {};
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: SR.ISAProbe <module.spv> [--disassembly|--binary]\n";
        return 2;
    }

    const bool binary = argc >= 3 && std::strcmp(argv[2], "--binary") == 0;
    const std::vector<std::uint32_t> words = ReadSpirv(argv[1]);
    if (words.empty())
    {
        std::cerr << "cannot read SPIR-V module '" << argv[1] << "'\n";
        return 1;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SR.ISAProbe";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS)
    {
        std::cerr << "failed to create a Vulkan instance\n";
        return 1;
    }

    const DeviceSelection device = PickDevice(instance);
    if (device.physical == VK_NULL_HANDLE)
    {
        std::cerr << "no Vulkan device with a compute queue\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::cout << "device: " << device.properties.deviceName << "\n";
    std::cout << "VK_AMD_shader_info: " << (device.shaderInfo ? "yes" : "no") << "\n";

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = device.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    std::vector<const char*> enabledExtensions;
    if (device.shaderInfo)
        enabledExtensions.push_back(VK_AMD_SHADER_INFO_EXTENSION_NAME);

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
    deviceInfo.ppEnabledExtensionNames = enabledExtensions.data();
    deviceInfo.pEnabledFeatures = &features;

    VkDevice logical = VK_NULL_HANDLE;
    if (vkCreateDevice(device.physical, &deviceInfo, nullptr, &logical) != VK_SUCCESS)
    {
        std::cerr << "failed to create a Vulkan device\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = words.size() * sizeof(std::uint32_t);
    moduleInfo.pCode = words.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(logical, &moduleInfo, nullptr, &module) != VK_SUCCESS)
    {
        std::cerr << "the driver rejected the SPIR-V module\n";
        vkDestroyDevice(logical, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &binding;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    vkCreateDescriptorSetLayout(logical, &setLayoutInfo, nullptr, &setLayout);

    // A module may read user data through push constants; declaring the
    // console's 128-byte block keeps the driver's compiler happy.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = 128;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(logical, &layoutInfo, nullptr, &pipelineLayout);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult = vkCreateComputePipelines(logical, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (pipelineResult != VK_SUCCESS)
    {
        std::cerr << "pipeline creation failed: " << pipelineResult << '\n';
        vkDestroyPipelineLayout(logical, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(logical, setLayout, nullptr);
        vkDestroyShaderModule(logical, module, nullptr);
        vkDestroyDevice(logical, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    const auto getShaderInfo = reinterpret_cast<PFN_vkGetShaderInfoAMD>(
        vkGetDeviceProcAddr(logical, "vkGetShaderInfoAMD"));
    if (device.shaderInfo && getShaderInfo != nullptr)
    {
        const VkShaderInfoTypeAMD type =
            binary ? VK_SHADER_INFO_TYPE_BINARY_AMD : VK_SHADER_INFO_TYPE_DISASSEMBLY_AMD;

        std::size_t size = 0;
        VkResult result = getShaderInfo(logical, pipeline, VK_SHADER_STAGE_COMPUTE_BIT, type, &size, nullptr);
        if (result == VK_SUCCESS && size > 0)
        {
            std::vector<char> data(size);
            result = getShaderInfo(logical, pipeline, VK_SHADER_STAGE_COMPUTE_BIT, type, &size, data.data());
            if (result == VK_SUCCESS)
            {
                std::cout << (binary ? "binary bytes: " : "disassembly bytes: ") << size << "\n";
                std::cout.write(data.data(), static_cast<std::streamsize>(size));
                if (!binary)
                    std::cout << '\n';
            }
            else
            {
                std::cerr << "shader info query failed: " << result << '\n';
            }
        }
        else
        {
            std::cerr << "shader info query returned no data: " << result << '\n';
        }
    }
    else
    {
        std::cerr << "VK_AMD_shader_info is not available\n";
    }

    vkDestroyPipeline(logical, pipeline, nullptr);
    vkDestroyPipelineLayout(logical, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(logical, setLayout, nullptr);
    vkDestroyShaderModule(logical, module, nullptr);
    vkDestroyDevice(logical, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
