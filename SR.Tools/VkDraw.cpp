// Renders one triangle with a translated vertex/fragment pair and reports the
// result. This is the graphics half of the divergence harness: the SPIR-V comes
// from SR.Backend, the driver compiles it to ISA, and the pixels say whether
// the stage contract (positions, interpolation, export) holds on real hardware.
//
// usage: SR.VkDraw <vertex.spv> <fragment.spv> [--gs <geometry.spv>] [--isa] [--size N]
//                  [--clear RRGGBB]

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "SR/Common/Assertions.hpp"

namespace {

std::vector<std::uint32_t> ReadSpirv(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
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
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    bool shaderInfo = false;
};

bool HasExtension(VkPhysicalDevice device, const char* name)
{
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> properties(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, properties.data());
    for (const auto& property : properties)
    {
        if (std::strcmp(property.extensionName, name) == 0)
            return true;
    }
    return false;
}

VkDeviceMemory Allocate(VkDevice device, VkPhysicalDeviceMemoryProperties properties, VkMemoryRequirements requirements,
                        VkMemoryPropertyFlags flags)
{
    std::uint32_t type = 0;
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
    {
        if ((requirements.memoryTypeBits & (1u << i)) != 0 && (properties.memoryTypes[i].propertyFlags & flags) == flags)
        {
            type = i;
            break;
        }
    }

    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = requirements.size;
    info.memoryTypeIndex = type;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    vkAllocateMemory(device, &info, nullptr, &memory);
    return memory;
}

Device CreateDevice(VkInstance instance, bool wantShaderInfo)
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
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
                continue;

            const bool shaderInfo = HasExtension(physical, VK_AMD_SHADER_INFO_EXTENSION_NAME);
            std::vector<const char*> extensions;
            if (wantShaderInfo && shaderInfo)
                extensions.push_back(VK_AMD_SHADER_INFO_EXTENSION_NAME);

            float priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = i;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;

            VkPhysicalDeviceFeatures features{};
            features.geometryShader = VK_TRUE;

            VkDeviceCreateInfo deviceInfo{};
            deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceInfo.pEnabledFeatures = &features;
            deviceInfo.queueCreateInfoCount = 1;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
            deviceInfo.ppEnabledExtensionNames = extensions.data();

            Device device;
            device.physical = physical;
            device.queueFamily = i;
            device.shaderInfo = wantShaderInfo && shaderInfo;
            if (vkCreateDevice(physical, &deviceInfo, nullptr, &device.logical) != VK_SUCCESS)
                continue;
            vkGetDeviceQueue(device.logical, i, 0, &device.queue);
            return device;
        }
    }

    return {};
}

void PrintIsa(VkDevice device, VkPipeline pipeline, VkShaderStageFlagBits stage, const char* label)
{
    const auto getShaderInfo = reinterpret_cast<PFN_vkGetShaderInfoAMD>(vkGetDeviceProcAddr(device, "vkGetShaderInfoAMD"));
    if (getShaderInfo == nullptr)
        return;

    std::size_t size = 0;
    VkResult result = getShaderInfo(device, pipeline, stage, VK_SHADER_INFO_TYPE_DISASSEMBLY_AMD, &size, nullptr);
    if (result != VK_SUCCESS || size == 0)
    {
        std::cerr << label << " ISA query returned no data: " << result << '\n';
        return;
    }

    std::vector<char> data(size);
    result = getShaderInfo(device, pipeline, stage, VK_SHADER_INFO_TYPE_DISASSEMBLY_AMD, &size, data.data());
    if (result != VK_SUCCESS)
    {
        std::cerr << label << " ISA query failed: " << result << '\n';
        return;
    }

    std::cout << "--- " << label << " ISA ---\n";
    std::cout.write(data.data(), static_cast<std::streamsize>(size));
    std::cout << '\n';
}

std::uint32_t ParseHex(std::string_view text)
{
    std::uint32_t value = 0;
    for (const char character : text)
    {
        value <<= 4;
        if (character >= '0' && character <= '9')
            value |= static_cast<std::uint32_t>(character - '0');
        else if (character >= 'a' && character <= 'f')
            value |= static_cast<std::uint32_t>(character - 'a' + 10);
        else if (character >= 'A' && character <= 'F')
            value |= static_cast<std::uint32_t>(character - 'A' + 10);
    }
    return value;
}

VkShaderModule CreateModule(VkDevice device, const std::vector<std::uint32_t>& words, const char* path)
{
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(std::uint32_t);
    info.pCode = words.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
    {
        std::cerr << "the driver rejected " << path << '\n';
        return VK_NULL_HANDLE;
    }
    return module;
}

} // namespace

int main(int argc, char** argv)
{
    // A driver fault must not swallow the progress already reported.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SR::InstallAssertionHandler("SR.VkDraw");

    if (argc < 3)
    {
        std::cerr << "usage: SR.VkDraw <vertex.spv> <fragment.spv> [--isa] [--size N] [--clear RRGGBB]\n";
        return 2;
    }

    const std::vector<std::uint32_t> vertexWords = ReadSpirv(argv[1]);
    const std::vector<std::uint32_t> fragmentWords = ReadSpirv(argv[2]);
    if (vertexWords.empty() || fragmentWords.empty())
    {
        std::cerr << "cannot read SPIR-V modules\n";
        return 1;
    }

    std::uint32_t size = 64;
    std::uint32_t clear = 0x00000000u; // RRGGBB
    bool isa = false;
    std::vector<std::uint32_t> geometryWords;
    for (int i = 3; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--isa")
            isa = true;
        else if (argument == "--gs" && i + 1 < argc)
        {
            geometryWords = ReadSpirv(argv[++i]);
            if (geometryWords.empty())
            {
                std::cerr << "cannot read the geometry module\n";
                return 1;
            }
        }
        else if (argument == "--size" && i + 1 < argc)
            size = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        else if (argument == "--clear" && i + 1 < argc)
            clear = ParseHex(argv[++i]);
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SR.VkDraw";
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

    Device device = CreateDevice(instance, isa);
    if (device.logical == VK_NULL_HANDLE)
    {
        std::cerr << "no graphics device\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device.physical, &properties);
    std::cout << "device: " << properties.deviceName << '\n';

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(device.physical, &memoryProperties);

    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {size, size, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    vkCreateImage(device.logical, &imageInfo, nullptr, &image);

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(device.logical, image, &imageRequirements);
    VkDeviceMemory imageMemory =
        Allocate(device.logical, memoryProperties, imageRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(device.logical, image, imageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(device.logical, &viewInfo, nullptr, &view);

    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colourReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colourReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &attachment;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    vkCreateRenderPass(device.logical, &passInfo, nullptr, &renderPass);

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &view;
    framebufferInfo.width = size;
    framebufferInfo.height = size;
    framebufferInfo.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    vkCreateFramebuffer(device.logical, &framebufferInfo, nullptr, &framebuffer);

    VkShaderModule vertexModule = CreateModule(device.logical, vertexWords, argv[1]);
    VkShaderModule fragmentModule = CreateModule(device.logical, fragmentWords, argv[2]);
    VkShaderModule geometryModule = VK_NULL_HANDLE;
    if (!geometryWords.empty())
    {
        geometryModule = CreateModule(device.logical, geometryWords, "geometry");
        if (geometryModule == VK_NULL_HANDLE)
            return 1;
    }
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE)
        return 1;

    std::array<VkPipelineShaderStageCreateInfo, 3> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";
    std::uint32_t stageCount = 2;
    if (geometryModule != VK_NULL_HANDLE)
    {
        stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[2].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        stages[2].module = geometryModule;
        stages[2].pName = "main";
        stageCount = 3;
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {size, size}};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                                   | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(device.logical, &layoutInfo, nullptr, &layout);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = stageCount;
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = renderPass;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult =
        vkCreateGraphicsPipelines(device.logical, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (pipelineResult != VK_SUCCESS)
    {
        std::cerr << "graphics pipeline creation failed: " << pipelineResult << '\n';
        return 1;
    }

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(size) * size * 4;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBuffer readback = VK_NULL_HANDLE;
    vkCreateBuffer(device.logical, &bufferInfo, nullptr, &readback);
    VkMemoryRequirements bufferRequirements{};
    vkGetBufferMemoryRequirements(device.logical, readback, &bufferRequirements);
    VkDeviceMemory readbackMemory = Allocate(device.logical,
                                             memoryProperties,
                                             bufferRequirements,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkBindBufferMemory(device.logical, readback, readbackMemory, 0);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device.queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(device.logical, &poolInfo, nullptr, &pool);

    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;

    VkCommandBuffer command = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device.logical, &commandInfo, &command);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command, &begin);

    const float red = static_cast<float>((clear >> 16) & 0xFFu) / 255.0f;
    const float green = static_cast<float>((clear >> 8) & 0xFFu) / 255.0f;
    const float blue = static_cast<float>(clear & 0xFFu) / 255.0f;
    VkClearValue clearValue{};
    clearValue.color = {{red, green, blue, 1.0f}};

    VkRenderPassBeginInfo passBegin{};
    passBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passBegin.renderPass = renderPass;
    passBegin.framebuffer = framebuffer;
    passBegin.renderArea = {{0, 0}, {size, size}};
    passBegin.clearValueCount = 1;
    passBegin.pClearValues = &clearValue;
    vkCmdBeginRenderPass(command, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {size, size, 1};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);

    vkEndCommandBuffer(command);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device.logical, &fenceInfo, nullptr, &fence);
    vkQueueSubmit(device.queue, 1, &submit, fence);

    const VkResult waited = vkWaitForFences(device.logical, 1, &fence, VK_TRUE, 30ull * 1000 * 1000 * 1000);
    if (waited != VK_SUCCESS)
    {
        std::cerr << "the device did not finish (timeout or device loss: " << waited << ")\n";
        return 1;
    }

    void* mapped = nullptr;
    vkMapMemory(device.logical, readbackMemory, 0, bufferSize, 0, &mapped);
    const auto* pixels = static_cast<const std::uint8_t*>(mapped);

    std::uint32_t nonClear = 0;
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            const std::uint8_t* pixel = pixels + (static_cast<std::size_t>(y) * size + x) * 4;
            if (pixel[0] != ((clear >> 16) & 0xFF) || pixel[1] != ((clear >> 8) & 0xFF)
                || pixel[2] != (clear & 0xFF))
            {
                ++nonClear;
            }
        }
    }

    const std::uint8_t* centre = pixels + (static_cast<std::size_t>(size / 2) * size + size / 2) * 4;
    std::cout << "pixels written: " << nonClear << " / " << (size * size) << '\n';
    std::cout << "centre rgba: " << static_cast<int>(centre[0]) << ' ' << static_cast<int>(centre[1]) << ' '
              << static_cast<int>(centre[2]) << ' ' << static_cast<int>(centre[3]) << '\n';

    if (isa && device.shaderInfo)
    {
        PrintIsa(device.logical, pipeline, VK_SHADER_STAGE_VERTEX_BIT, "vertex");
        if (geometryModule != VK_NULL_HANDLE)
            PrintIsa(device.logical, pipeline, VK_SHADER_STAGE_GEOMETRY_BIT, "geometry");
        PrintIsa(device.logical, pipeline, VK_SHADER_STAGE_FRAGMENT_BIT, "fragment");
    }

    vkUnmapMemory(device.logical, readbackMemory);
    vkDestroyFence(device.logical, fence, nullptr);
    vkDestroyCommandPool(device.logical, pool, nullptr);
    vkDestroyBuffer(device.logical, readback, nullptr);
    vkFreeMemory(device.logical, readbackMemory, nullptr);
    vkDestroyPipeline(device.logical, pipeline, nullptr);
    vkDestroyPipelineLayout(device.logical, layout, nullptr);
    vkDestroyShaderModule(device.logical, vertexModule, nullptr);
    if (geometryModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(device.logical, geometryModule, nullptr);
    vkDestroyShaderModule(device.logical, fragmentModule, nullptr);
    vkDestroyFramebuffer(device.logical, framebuffer, nullptr);
    vkDestroyRenderPass(device.logical, renderPass, nullptr);
    vkDestroyImageView(device.logical, view, nullptr);
    vkDestroyImage(device.logical, image, nullptr);
    vkFreeMemory(device.logical, imageMemory, nullptr);
    vkDestroyDevice(device.logical, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
