// SR.VkRun: dispatch a compute shader with the test ABI (one storage buffer at
// set 0, binding 0) and print the buffer contents.
#include <cstdint>
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

struct Device
{
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice logical = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
};

Device CreateDevice(VkInstance instance)
{
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    for (const VkPhysicalDevice physical : devices)
    {
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());

        for (std::uint32_t i = 0; i < familyCount; ++i)
        {
            if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
                continue;

            float priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = i;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;

            // Buffer device address lets a shader read a V# base the harness
            // hands it, the way a console dispatch supplies user data. Ask the
            // device first: enabling a feature it does not report is invalid.
            VkPhysicalDeviceVulkan12Features supported{};
            supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

            VkPhysicalDeviceFeatures2 queried{};
            queried.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            queried.pNext = &supported;
            vkGetPhysicalDeviceFeatures2(physical, &queried);

            VkPhysicalDeviceVulkan12Features features12{};
            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features12.bufferDeviceAddress = supported.bufferDeviceAddress;

            VkDeviceCreateInfo deviceInfo{};
            deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceInfo.pNext = &features12;
            deviceInfo.queueCreateInfoCount = 1;
            deviceInfo.pQueueCreateInfos = &queueInfo;

            Device device;
            device.physical = physical;
            device.queueFamily = i;
            if (vkCreateDevice(physical, &deviceInfo, nullptr, &device.logical) != VK_SUCCESS)
                continue;
            vkGetDeviceQueue(device.logical, i, 0, &device.queue);
            return device;
        }
    }

    return {};
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: SR.VkRun <module.spv> [words] [--push d0,d1,...] [--vsharp N] [--init d0,d1,...]\n";
        return 2;
    }

    std::uint32_t wordCount = 16;
    std::vector<std::uint32_t> pushData(32, 0);
    std::vector<std::uint32_t> initialValues(64, 0);
    std::uint32_t vsharpAt = 0xFFFFFFFFu;
    int argumentStart = 2;
    for (int i = 2; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--push" && i + 1 < argc)
        {
            std::string_view text = argv[++i];
            std::size_t slot = 0;
            while (!text.empty() && slot < pushData.size())
            {
                const std::size_t comma = text.find(',');
                pushData[slot++] = static_cast<std::uint32_t>(std::stoul(std::string(text.substr(0, comma)), nullptr, 0));
                text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
            }
            continue;
        }
        if (argument == "--vsharp" && i + 1 < argc)
        {
            vsharpAt = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            continue;
        }
        if (argument == "--init" && i + 1 < argc)
        {
            std::string_view text = argv[++i];
            std::size_t slot = 0;
            while (!text.empty() && slot < initialValues.size())
            {
                const std::size_t comma = text.find(',');
                initialValues[slot++] = static_cast<std::uint32_t>(std::stoul(std::string(text.substr(0, comma)), nullptr, 0));
                text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
            }
            continue;
        }
        if (argument == "--words" && i + 1 < argc)
        {
            wordCount = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            continue;
        }
        if (argumentStart == 2 && argument.size() > 1 && argument[0] != '-')
        {
            wordCount = static_cast<std::uint32_t>(std::stoul(std::string(argument)));
            argumentStart = i + 1;
        }
    }
    const std::vector<std::uint32_t> code = ReadSpirv(argv[1]);
    if (code.empty())
    {
        std::cerr << "cannot read SPIR-V module\n";
        return 1;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SR.VkRun";
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

    Device device = CreateDevice(instance);
    if (device.logical == VK_NULL_HANDLE)
    {
        std::cerr << "no compute device\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    const VkDeviceSize bufferSize = 4096;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    vkCreateBuffer(device.logical, &bufferInfo, nullptr, &buffer);

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device.logical, buffer, &requirements);

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(device.physical, &memoryProperties);

    std::uint32_t memoryType = 0;
    for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        const bool hostVisible = (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        const bool coherent = (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        if ((requirements.memoryTypeBits & (1u << i)) != 0 && hostVisible && coherent)
        {
            memoryType = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    vkAllocateMemory(device.logical, &allocateInfo, nullptr, &memory);
    vkBindBufferMemory(device.logical, buffer, memory, 0);

    if (vsharpAt != 0xFFFFFFFFu)
    {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        const VkDeviceAddress address = vkGetBufferDeviceAddress(device.logical, &addressInfo);
        if (vsharpAt + 3 < pushData.size())
        {
            // A V# for the bound buffer: base address, then the record count.
            pushData[vsharpAt + 0] = static_cast<std::uint32_t>(address & 0xFFFFFFFFull);
            pushData[vsharpAt + 1] = static_cast<std::uint32_t>((address >> 32) & 0xFFFFull);
            pushData[vsharpAt + 2] = static_cast<std::uint32_t>(bufferSize);
            pushData[vsharpAt + 3] = 0;
        }
        else
        {
            std::cerr << "--vsharp index is outside the push block\n";
            return 1;
        }
    }

    void* mapped = nullptr;
    vkMapMemory(device.logical, memory, 0, bufferSize, 0, &mapped);
    std::memset(mapped, 0, bufferSize);
    {
        auto* initial = static_cast<std::uint32_t*>(mapped);
        for (std::size_t index = 0; index < initialValues.size(); ++index)
            initial[index] = initialValues[index];
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
    vkCreateDescriptorSetLayout(device.logical, &setLayoutInfo, nullptr, &setLayout);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(device.logical, &poolInfo, nullptr, &pool);

    VkDescriptorSetAllocateInfo setAllocateInfo{};
    setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocateInfo.descriptorPool = pool;
    setAllocateInfo.descriptorSetCount = 1;
    setAllocateInfo.pSetLayouts = &setLayout;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(device.logical, &setAllocateInfo, &descriptorSet);

    VkDescriptorBufferInfo bufferDescriptor{};
    bufferDescriptor.buffer = buffer;
    bufferDescriptor.offset = 0;
    bufferDescriptor.range = bufferSize;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferDescriptor;
    vkUpdateDescriptorSets(device.logical, 1, &write, 0, nullptr);

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size() * sizeof(std::uint32_t);
    moduleInfo.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device.logical, &moduleInfo, nullptr, &module) != VK_SUCCESS)
    {
        std::cerr << "driver rejected the module\n";
        return 1;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = static_cast<std::uint32_t>(pushData.size() * sizeof(std::uint32_t));

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(device.logical, &layoutInfo, nullptr, &pipelineLayout);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device.logical, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
    {
        std::cerr << "pipeline creation failed\n";
        return 1;
    }

    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.queueFamilyIndex = device.queueFamily;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    vkCreateCommandPool(device.logical, &commandPoolInfo, nullptr, &commandPool);

    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device.logical, &commandInfo, &command);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(command, &beginInfo);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       static_cast<std::uint32_t>(pushData.size() * sizeof(std::uint32_t)), pushData.data());
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(command, 1, 1, 1);

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = bufferSize;
    vkCmdPipelineBarrier(command,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &barrier,
                         0,
                         nullptr);
    vkEndCommandBuffer(command);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    vkQueueSubmit(device.queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(device.queue);

    const auto* values = static_cast<const std::uint32_t*>(mapped);
    for (std::uint32_t i = 0; i < wordCount; ++i)
        std::cout << values[i] << (i + 1 == wordCount ? '\n' : ' ');

    vkUnmapMemory(device.logical, memory);
    vkDestroyCommandPool(device.logical, commandPool, nullptr);
    vkDestroyPipeline(device.logical, pipeline, nullptr);
    vkDestroyPipelineLayout(device.logical, pipelineLayout, nullptr);
    vkDestroyShaderModule(device.logical, module, nullptr);
    vkDestroyDescriptorPool(device.logical, pool, nullptr);
    vkDestroyDescriptorSetLayout(device.logical, setLayout, nullptr);
    vkDestroyBuffer(device.logical, buffer, nullptr);
    vkFreeMemory(device.logical, memory, nullptr);
    vkDestroyDevice(device.logical, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
