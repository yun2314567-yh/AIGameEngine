#include "TinyEngine/Renderer/GraphicsContext.h"

#if defined(TINYENGINE_WITH_VULKAN_NVRHI) && defined(TINYENGINE_WITH_GLFW)

#include "TinyEngine/Assets/GltfImporter.h"
#include "TinyEngine/Platform/Window.h"
#include "TinyEngine/Renderer/ShaderTypes.h"
#include "TinyEngine/Renderer/Environment.h"
#include "TinyEngine/Scene.h"

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <nvrhi/vulkan.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
#if defined(TINYENGINE_WITH_IMGUI_VULKAN)
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace TinyEngine {
namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool resolveAsset(const std::filesystem::path& root, const std::string& name,
                  std::filesystem::path& result, std::string* error) {
    const std::filesystem::path relative{name};
    if (relative.empty() || relative.has_root_path() || name.find(':') != std::string::npos)
        return fail(error, "Asset path must be relative to the asset root: " + name);
    for (const auto& part : relative) if (part == "..") return fail(error, "Asset path escapes the asset root: " + name);
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) return fail(error, "Cannot resolve asset root: " + ec.message());
    result = std::filesystem::weakly_canonical(canonicalRoot / relative, ec);
    if (ec) return fail(error, "Cannot resolve asset: " + name);
    const auto contained = result.lexically_relative(canonicalRoot);
    if (contained.empty() || contained.has_root_path()) return fail(error, "Asset is outside the asset root: " + name);
    for (const auto& part : contained) if (part == "..") return fail(error, "Asset is outside the asset root: " + name);
    return true;
}

bool check(VkResult result, const char* operation, std::string* error) {
    return result == VK_SUCCESS || fail(error, std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
}

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& value) {
        return std::strcmp(value.extensionName, name) == 0;
    });
}

// Data URI images use the same bounded decoder path as embedded GLB images.
std::vector<std::uint8_t> decodeDataUri(const std::string& uri) {
    const auto comma = uri.find(',');
    if (!uri.starts_with("data:") || comma == std::string::npos || uri.substr(0, comma).find(";base64") == std::string::npos) return {};
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> bytes;
    std::uint32_t value = 0;
    unsigned bits = 0;
    for (std::size_t index = comma + 1; index < uri.size(); ++index) {
        if (uri[index] == '=') break;
        const auto digit = alphabet.find(uri[index]);
        if (digit == std::string_view::npos) return {};
        value = (value << 6u) | static_cast<std::uint32_t>(digit);
        bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push_back(static_cast<std::uint8_t>(value >> bits)); }
    }
    return bytes;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto length = input.tellg();
    if (length <= 0 || length > 256 * 1024 * 1024) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) return {};
    return bytes;
}

Mat4 transpose(const Mat4& matrix) {
    Mat4 result;
    for (std::uint32_t row = 0; row < 4; ++row)
        for (std::uint32_t column = 0; column < 4; ++column) result(column, row) = matrix(row, column);
    return result;
}

} // namespace

struct GraphicsContext::Impl final : nvrhi::IMessageCallback {
    struct Primitive {
        nvrhi::BufferHandle Vertices;
        nvrhi::BufferHandle Indices;
        std::uint32_t IndexCount = 0;
        PbrMaterial Material;
        std::array<nvrhi::TextureHandle, 5> Textures;
        std::uint32_t TextureMask = 0;
    };
    struct MeshAsset {
        GltfAsset Source;
        std::vector<std::vector<Primitive>> Meshes;
    };
    struct Draw {
        Primitive* Geometry = nullptr;
        Mat4 Model;
        nvrhi::BufferHandle ObjectConstants;
        nvrhi::BufferHandle MaterialConstants;
        nvrhi::BindingSetHandle DepthBindings;
        nvrhi::BindingSetHandle PbrBindings;
        bool CastShadows = true;
        bool Blend = false;
        float DistanceSquared = 0.0f;
    };
    struct Resources {
        std::vector<nvrhi::TextureHandle> SwapImages;
        std::vector<nvrhi::FramebufferHandle> SwapFramebuffers;
        std::vector<bool> Presented;
        nvrhi::TextureHandle Depth, Shadow, AoRaw, Ao, Hdr, Display, White, Environment, Irradiance, Brdf;
        nvrhi::TextureHandle DefaultEnvironment, DefaultBrdf;
        nvrhi::FramebufferHandle DepthFramebuffer, ShadowFramebuffer, HdrFramebuffer;
        nvrhi::BufferHandle FrameConstants, SsaoConstants;
        nvrhi::SamplerHandle WrapSampler, ClampSampler, PointSampler, ShadowSampler, EnvironmentSampler;
        nvrhi::InputLayoutHandle InputLayout, DepthInputLayout;
        nvrhi::BindingLayoutHandle DepthLayout, PbrLayout, SsaoLayout, BlurLayout, ToneLayout, PresentLayout;
        nvrhi::GraphicsPipelineHandle DepthPipeline, ShadowPipeline, PbrPipeline, BlendPipeline, PresentPipeline;
        nvrhi::ComputePipelineHandle SsaoPipeline, BlurPipeline, TonePipeline;
        nvrhi::BindingSetHandle SsaoBindings, BlurBindings, ToneBindings, PresentBindings;
        std::map<std::string, MeshAsset> Assets;
        std::map<std::string, nvrhi::ShaderHandle> Shaders;
        nvrhi::CommandListHandle Commands;
    };

    Window* Owner = nullptr;
    std::filesystem::path ShaderDirectory, AssetRoot;
    VkInstance Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice LogicalDevice = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    std::uint32_t QueueFamily = 0;
    VkPhysicalDeviceProperties Properties{};
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    VkSemaphore Acquired = VK_NULL_HANDLE, Rendered = VK_NULL_HANDLE;
    VkRenderPass UiRenderPass = VK_NULL_HANDLE;
    std::vector<VkImageView> UiViews;
    std::vector<VkFramebuffer> UiFramebuffers;
    VkFormat SwapFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D Extent{};
    std::uint32_t MinImageCount = 2;
    nvrhi::vulkan::DeviceHandle Device;
    std::unique_ptr<Resources> R;
    EnvironmentMap EnvironmentSettings;
    std::uint32_t EnvironmentMips = 1;
    std::uint32_t ShadowResolution = 0;
    std::uint32_t FrameIndex = 0;
    bool ImGuiInitialized = false;
    bool SwapDirty = false;
    std::mutex MessageMutex;
    std::string GpuError;

    void message(nvrhi::MessageSeverity severity, const char* text) override {
        if (severity >= nvrhi::MessageSeverity::Error) {
            std::lock_guard lock(MessageMutex);
            if (GpuError.empty()) GpuError = text;
        }
        if (severity >= nvrhi::MessageSeverity::Warning) std::cerr << "NVRHI: " << text << '\n';
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            static_cast<Impl*>(user)->message(nvrhi::MessageSeverity::Error, data->pMessage);
        else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            static_cast<Impl*>(user)->message(nvrhi::MessageSeverity::Warning, data->pMessage);
        return VK_FALSE;
    }

    ~Impl() override {
        if (Device) Device->waitForIdle();
#if defined(TINYENGINE_WITH_IMGUI_VULKAN)
        if (ImGuiInitialized) ImGui_ImplVulkan_Shutdown();
#endif
        destroySwapchain();
        R.reset();
        Device = nullptr;
        if (Rendered) vkDestroySemaphore(LogicalDevice, Rendered, nullptr);
        if (Acquired) vkDestroySemaphore(LogicalDevice, Acquired, nullptr);
        if (LogicalDevice) vkDestroyDevice(LogicalDevice, nullptr);
        if (Surface) vkDestroySurfaceKHR(Instance, Surface, nullptr);
        if (DebugMessenger) {
            auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy) destroy(Instance, DebugMessenger, nullptr);
        }
        if (Instance) vkDestroyInstance(Instance, nullptr);
    }

    void destroySwapchain() {
        if (R) { R->SwapFramebuffers.clear(); R->SwapImages.clear(); }
        for (auto framebuffer : UiFramebuffers) vkDestroyFramebuffer(LogicalDevice, framebuffer, nullptr);
        UiFramebuffers.clear();
        for (auto view : UiViews) vkDestroyImageView(LogicalDevice, view, nullptr);
        UiViews.clear();
        if (UiRenderPass) vkDestroyRenderPass(LogicalDevice, UiRenderPass, nullptr);
        UiRenderPass = VK_NULL_HANDLE;
        if (Swapchain) vkDestroySwapchainKHR(LogicalDevice, Swapchain, nullptr);
        Swapchain = VK_NULL_HANDLE;
    }

    bool initialize(std::string* error) {
        if (!glfwVulkanSupported()) return fail(error, "The Vulkan loader or a Vulkan-capable device is unavailable");
        std::uint32_t count = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&count);
        if (!glfwExtensions || count == 0) return fail(error, "GLFW has no Vulkan surface extensions");
        std::vector<const char*> instanceExtensions(glfwExtensions, glfwExtensions + count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> supported(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, supported.data());
        bool debug = hasExtension(supported, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (debug) instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        VkInstanceCreateFlags flags = 0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (hasExtension(supported, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());
        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        const bool validation = std::any_of(layers.begin(), layers.end(), [validationLayer](const auto& layer) {
            return std::strcmp(layer.layerName, validationLayer) == 0;
        });
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "TinyEngine";
        application.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.flags = flags;
        instanceInfo.pApplicationInfo = &application;
        instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
        instanceInfo.enabledLayerCount = validation ? 1u : 0u;
        instanceInfo.ppEnabledLayerNames = validation ? &validationLayer : nullptr;
        if (!check(vkCreateInstance(&instanceInfo, nullptr, &Instance), "vkCreateInstance", error)) return false;
        if (debug) {
            VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            info.pfnUserCallback = debugCallback;
            info.pUserData = this;
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));
            if (create && !check(create(Instance, &info, nullptr, &DebugMessenger), "vkCreateDebugUtilsMessengerEXT", error)) return false;
        }
        if (!check(glfwCreateWindowSurface(Instance, static_cast<GLFWwindow*>(Owner->nativeHandle()), nullptr, &Surface), "glfwCreateWindowSurface", error)) return false;
        if (!check(vkEnumeratePhysicalDevices(Instance, &count, nullptr), "vkEnumeratePhysicalDevices", error)) return false;
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(Instance, &count, devices.data());
        std::vector<const char*> deviceExtensions;
        for (auto device : devices) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_2) continue;
            VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
            VkPhysicalDeviceSynchronization2FeaturesKHR synchronization{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
            VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
            synchronization.pNext = &dynamicRendering;
            timeline.pNext = &synchronization;
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &timeline;
            vkGetPhysicalDeviceFeatures2(device, &features);
            if (!timeline.timelineSemaphore || !synchronization.synchronization2 || !dynamicRendering.dynamicRendering) continue;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
            std::vector<VkExtensionProperties> extensions(count);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
            if (!hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
                !hasExtension(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) ||
                !hasExtension(extensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) continue;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
            for (std::uint32_t index = 0; index < count; ++index) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, index, Surface, &present);
                if (present && (families[index].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                    PhysicalDevice = device; QueueFamily = index; Properties = properties;
                    deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};
                    if (hasExtension(extensions, "VK_KHR_portability_subset")) deviceExtensions.push_back("VK_KHR_portability_subset");
                    break;
                }
            }
            if (PhysicalDevice) break;
        }
        if (!PhysicalDevice) return fail(error, "No Vulkan 1.2 device supports presentation, graphics, compute, timeline semaphores, synchronization2 and dynamic rendering");
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = QueueFamily; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(PhysicalDevice, &features);
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
        timeline.timelineSemaphore = VK_TRUE;
        VkPhysicalDeviceSynchronization2FeaturesKHR synchronization{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
        synchronization.synchronization2 = VK_TRUE;
        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
        dynamicRendering.dynamicRendering = VK_TRUE;
        synchronization.pNext = &dynamicRendering;
        timeline.pNext = &synchronization;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = &timeline; deviceInfo.pEnabledFeatures = &features;
        deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
        deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
        if (!check(vkCreateDevice(PhysicalDevice, &deviceInfo, nullptr, &LogicalDevice), "vkCreateDevice", error)) return false;
        vkGetDeviceQueue(LogicalDevice, QueueFamily, 0, &Queue);
        std::clog << "Vulkan device: " << Properties.deviceName << '\n';
        nvrhi::vulkan::DeviceDesc desc{};
        desc.instance = Instance; desc.physicalDevice = PhysicalDevice; desc.device = LogicalDevice;
        desc.graphicsQueue = Queue; desc.graphicsQueueIndex = static_cast<int>(QueueFamily);
        desc.errorCB = this;
        desc.instanceExtensions = instanceExtensions.data(); desc.numInstanceExtensions = instanceExtensions.size();
        desc.deviceExtensions = deviceExtensions.data(); desc.numDeviceExtensions = deviceExtensions.size();
        VULKAN_HPP_DEFAULT_DISPATCHER.init(Instance, vkGetInstanceProcAddr, LogicalDevice);
        Device = nvrhi::vulkan::createDevice(desc);
        if (!Device) return fail(error, "NVRHI device creation failed");
        R = std::make_unique<Resources>();
        R->Commands = Device->createCommandList();
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!check(vkCreateSemaphore(LogicalDevice, &semaphore, nullptr, &Acquired), "vkCreateSemaphore", error) ||
            !check(vkCreateSemaphore(LogicalDevice, &semaphore, nullptr, &Rendered), "vkCreateSemaphore", error)) return false;
        std::cout << "Vulkan device: " << Properties.deviceName << (validation ? " (validation enabled)" : "") << '\n';
        return createSwapchain(error) && createResources(error);
    }

    bool createSwapchain(std::string* error) {
        Device->waitForIdle();
        if (!check(vkQueueWaitIdle(Queue), "vkQueueWaitIdle", error)) return false;
        const auto previousFormat = SwapFormat;
        destroySwapchain();
        VkSurfaceCapabilitiesKHR capabilities;
        if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", error)) return false;
        std::uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &count, formats.data());
        const auto format = std::find_if(formats.begin(), formats.end(), [](const auto& candidate) {
            return (candidate.format == VK_FORMAT_B8G8R8A8_SRGB || candidate.format == VK_FORMAT_R8G8B8A8_SRGB) && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (format == formats.end()) return fail(error, "The surface has no sRGB swapchain format");
        SwapFormat = format->format;
        if (ImGuiInitialized && previousFormat != SwapFormat) return fail(error, "Surface format changed; recreate the graphics context");
        Extent = capabilities.currentExtent;
        if (Extent.width == std::numeric_limits<std::uint32_t>::max()) {
            Extent.width = std::clamp(Owner->width(), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            Extent.height = std::clamp(Owner->height(), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        MinImageCount = std::max(2u, capabilities.minImageCount);
        count = MinImageCount + 1;
        if (capabilities.maxImageCount > 0) count = std::min(count, capabilities.maxImageCount);
        if (count < MinImageCount) return fail(error, "Surface requires fewer than two swapchain images");
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = Surface; info.minImageCount = count; info.imageFormat = SwapFormat;
        info.imageColorSpace = format->colorSpace; info.imageExtent = Extent; info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
            if (capabilities.supportedCompositeAlpha & alpha) { info.compositeAlpha = alpha; break; }
        }
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE;
        if (!check(vkCreateSwapchainKHR(LogicalDevice, &info, nullptr, &Swapchain), "vkCreateSwapchainKHR", error)) return false;
        vkGetSwapchainImagesKHR(LogicalDevice, Swapchain, &count, nullptr);
        std::vector<VkImage> images(count);
        vkGetSwapchainImagesKHR(LogicalDevice, Swapchain, &count, images.data());
        VkAttachmentDescription attachment{};
        attachment.format = SwapFormat; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo renderPass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPass.attachmentCount = 1; renderPass.pAttachments = &attachment;
        renderPass.subpassCount = 1; renderPass.pSubpasses = &subpass;
        renderPass.dependencyCount = 1; renderPass.pDependencies = &dependency;
        if (!check(vkCreateRenderPass(LogicalDevice, &renderPass, nullptr, &UiRenderPass), "vkCreateRenderPass", error)) return false;
        R->Presented.assign(images.size(), false);
        for (auto image : images) {
            nvrhi::TextureDesc desc;
            desc.width = Extent.width; desc.height = Extent.height;
            desc.format = SwapFormat == VK_FORMAT_B8G8R8A8_SRGB ? nvrhi::Format::SBGRA8_UNORM : nvrhi::Format::SRGBA8_UNORM;
            desc.isRenderTarget = true; desc.isShaderResource = false;
            desc.initialState = nvrhi::ResourceStates::Present; desc.keepInitialState = true;
            auto texture = Device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), desc);
            R->SwapImages.push_back(texture);
            R->SwapFramebuffers.push_back(Device->createFramebuffer(nvrhi::FramebufferDesc{}.addColorAttachment(texture)));
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = SwapFormat;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView imageView;
            if (!check(vkCreateImageView(LogicalDevice, &view, nullptr, &imageView), "vkCreateImageView", error)) return false;
            UiViews.push_back(imageView);
            VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer.renderPass = UiRenderPass; framebuffer.attachmentCount = 1; framebuffer.pAttachments = &UiViews.back();
            framebuffer.width = Extent.width; framebuffer.height = Extent.height; framebuffer.layers = 1;
            VkFramebuffer handle;
            if (!check(vkCreateFramebuffer(LogicalDevice, &framebuffer, nullptr, &handle), "vkCreateFramebuffer", error)) return false;
            UiFramebuffers.push_back(handle);
        }
#if defined(TINYENGINE_WITH_IMGUI_VULKAN)
        if (ImGuiInitialized) ImGui_ImplVulkan_SetMinImageCount(MinImageCount);
#endif
        SwapDirty = false;
        return true;
    }

    nvrhi::TextureHandle texture(std::uint32_t width, std::uint32_t height, nvrhi::Format format, bool target, bool uav, const char* name) {
        nvrhi::TextureDesc desc;
        desc.width = width; desc.height = height; desc.format = format; desc.debugName = name;
        desc.isRenderTarget = target; desc.isUAV = uav;
        desc.initialState = nvrhi::ResourceStates::ShaderResource; desc.keepInitialState = true;
        return Device->createTexture(desc);
    }

    nvrhi::BufferHandle constantBuffer(std::size_t size) {
        return Device->createBuffer(nvrhi::BufferDesc{}.setByteSize(size).setIsConstantBuffer(true)
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer).setKeepInitialState(true));
    }

    nvrhi::ShaderHandle shader(const std::string& name, nvrhi::ShaderType type, std::string* error) {
        if (auto iterator = R->Shaders.find(name); iterator != R->Shaders.end()) return iterator->second;
        const auto bytes = readBytes(ShaderDirectory / (name + ".hlsl.spv"));
        if (bytes.size() < 20 || bytes.size() % 4 != 0) { fail(error, "Missing or invalid SPIR-V shader: " + name); return {}; }
        nvrhi::ShaderDesc desc(type);
        desc.entryName = "main"; desc.debugName = name;
        auto handle = Device->createShader(desc, bytes.data(), bytes.size());
        if (!handle) { fail(error, "Could not create shader " + name); return {}; }
        R->Shaders.emplace(name, handle);
        return handle;
    }

    nvrhi::BindingLayoutHandle layout(std::initializer_list<nvrhi::BindingLayoutItem> items, nvrhi::ShaderType visibility) {
        nvrhi::BindingLayoutDesc desc;
        desc.visibility = visibility;
        desc.bindingOffsets = {0, 0, 0, 0};
        for (const auto& item : items) desc.bindings.push_back(item);
        return Device->createBindingLayout(desc);
    }

    bool createResources(std::string* error) {
        using L = nvrhi::BindingLayoutItem;
        const auto all = nvrhi::ShaderType::All;
        R->DepthLayout = layout({L::ConstantBuffer(0), L::ConstantBuffer(1), L::ConstantBuffer(2), L::Texture_SRV(16), L::Sampler(32)}, all);
        R->PbrLayout = layout({L::ConstantBuffer(0), L::ConstantBuffer(1), L::ConstantBuffer(2),
            L::Texture_SRV(16), L::Texture_SRV(17), L::Texture_SRV(18), L::Texture_SRV(19), L::Texture_SRV(20),
            L::Texture_SRV(21), L::Texture_SRV(22), L::Texture_SRV(23), L::Texture_SRV(24), L::Texture_SRV(25),
            L::Sampler(32), L::Sampler(33), L::Sampler(34), L::Sampler(35)}, all);
        R->SsaoLayout = layout({L::ConstantBuffer(0), L::ConstantBuffer(1), L::Texture_SRV(16), L::Texture_UAV(48), L::Sampler(32)}, nvrhi::ShaderType::Compute);
        R->BlurLayout = layout({L::ConstantBuffer(0), L::Texture_SRV(16), L::Texture_SRV(17), L::Texture_UAV(48)}, nvrhi::ShaderType::Compute);
        R->ToneLayout = layout({L::ConstantBuffer(0), L::Texture_SRV(16), L::Texture_UAV(48)}, nvrhi::ShaderType::Compute);
        R->PresentLayout = layout({L::Texture_SRV(16), L::Sampler(32)}, all);
        R->FrameConstants = constantBuffer(sizeof(GpuFrameConstants));
        R->SsaoConstants = constantBuffer(sizeof(GpuSsaoConstants));
        R->WrapSampler = Device->createSampler(nvrhi::SamplerDesc{}.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));
        R->ClampSampler = Device->createSampler(nvrhi::SamplerDesc{});
        R->PointSampler = Device->createSampler(nvrhi::SamplerDesc{}.setAllFilters(false));
        R->ShadowSampler = Device->createSampler(nvrhi::SamplerDesc{}.setReductionType(nvrhi::SamplerReductionType::Comparison));
        R->EnvironmentSampler = Device->createSampler(nvrhi::SamplerDesc{}.setAddressU(nvrhi::SamplerAddressMode::Wrap));
        R->White = texture(1, 1, nvrhi::Format::RGBA8_UNORM, false, false, "Default white");
        R->Environment = texture(1, 1, nvrhi::Format::RGBA32_FLOAT, false, false, "Default environment");
        R->Irradiance = R->Environment;
        const std::uint32_t white = 0xffffffffu;
        const float ambient[] = {0.15f, 0.18f, 0.24f, 1.0f};
        FilteredEnvironment filtered;
        if (!FilterEnvironment(ambient, 1, 1, 1, 1, 1, filtered, error)) return false;
        R->Brdf = texture(filtered.Brdf.Width, filtered.Brdf.Height, nvrhi::Format::RGBA32_FLOAT, false, false, "GGX BRDF LUT");
        if (!R->White || !R->Environment || !R->Brdf) return fail(error, "Default environment allocation failed");
        R->DefaultEnvironment = R->Environment; R->DefaultBrdf = R->Brdf;
        R->Commands->open();
        R->Commands->writeTexture(R->White, 0, 0, &white, 4);
        R->Commands->writeTexture(R->Environment, 0, 0, ambient, sizeof(ambient));
        R->Commands->writeTexture(R->Brdf, 0, 0, filtered.Brdf.Pixels.data(), static_cast<std::size_t>(filtered.Brdf.Width) * sizeof(float) * 4);
        R->Commands->close(); Device->executeCommandList(R->Commands); Device->waitForIdle();
        auto vs = shader("Pbr.vert", nvrhi::ShaderType::Vertex, error);
        if (!vs) return false;
        std::array<nvrhi::VertexAttributeDesc, 4> attributes;
        attributes[0].setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(GltfVertex, Position));
        attributes[1].setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(GltfVertex, Normal));
        attributes[2].setName("TANGENT").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(GltfVertex, Tangent));
        attributes[3].setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(GltfVertex, U));
        for (auto& attribute : attributes) attribute.elementStride = sizeof(GltfVertex);
        R->InputLayout = Device->createInputLayout(attributes.data(), static_cast<std::uint32_t>(attributes.size()), vs);
        const std::array depthAttributes{attributes[0], attributes[3]};
        auto depthVs = shader("Depth.vert", nvrhi::ShaderType::Vertex, error);
        if (!depthVs) return false;
        R->DepthInputLayout = Device->createInputLayout(depthAttributes.data(), static_cast<std::uint32_t>(depthAttributes.size()), depthVs);
        if (!R->InputLayout || !R->DepthInputLayout) return fail(error, "Could not create mesh input layouts");
        return resizeTargets(2048, error);
    }

    bool resizeTargets(std::uint32_t shadowResolution, std::string* error) {
        using B = nvrhi::BindingSetItem;
        R->Depth = texture(Extent.width, Extent.height, nvrhi::Format::D32, true, false, "Scene depth");
        R->Shadow = texture(shadowResolution, shadowResolution, nvrhi::Format::D32, true, false, "Shadow depth");
        R->Ao = texture(Extent.width, Extent.height, nvrhi::Format::R32_FLOAT, false, true, "SSAO");
        R->AoRaw = texture(Extent.width, Extent.height, nvrhi::Format::R32_FLOAT, false, true, "Unfiltered SSAO");
        R->Hdr = texture(Extent.width, Extent.height, nvrhi::Format::RGBA32_FLOAT, true, false, "HDR lighting");
        R->Display = texture(Extent.width, Extent.height, nvrhi::Format::RGBA32_FLOAT, false, true, "Tone mapped display");
        ShadowResolution = shadowResolution;
        R->DepthFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc{}.setDepthAttachment(R->Depth));
        R->ShadowFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc{}.setDepthAttachment(R->Shadow));
        R->HdrFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc{}.addColorAttachment(R->Hdr).setDepthAttachment(R->Depth));
        const auto graphicsPipeline = [&](const char* name, nvrhi::IBindingLayout* bindingLayout, nvrhi::IFramebuffer* framebuffer, bool depth, bool blend) {
            nvrhi::GraphicsPipelineDesc desc;
            desc.VS = shader(std::string(name) + ".vert", nvrhi::ShaderType::Vertex, error);
            desc.PS = shader(std::string(name) + ".frag", nvrhi::ShaderType::Pixel, error);
            if (!desc.VS || !desc.PS) return nvrhi::GraphicsPipelineHandle{};
            if (std::strcmp(name, "Present") != 0)
                desc.inputLayout = std::strcmp(name, "Pbr") == 0 ? R->InputLayout : R->DepthInputLayout;
            desc.bindingLayouts = {bindingLayout};
            desc.renderState.rasterState.setCullNone().setDepthClipEnable(true);
            desc.renderState.depthStencilState.setDepthTestEnable(depth).setDepthWriteEnable(depth && !blend)
                .setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
            if (blend) desc.renderState.blendState.targets[0].setBlendEnable(true)
                .setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                .setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
            return Device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
        };
        R->DepthPipeline = graphicsPipeline("Depth", R->DepthLayout, R->DepthFramebuffer, true, false);
        R->ShadowPipeline = graphicsPipeline("Shadow", R->DepthLayout, R->ShadowFramebuffer, true, false);
        R->PbrPipeline = graphicsPipeline("Pbr", R->PbrLayout, R->HdrFramebuffer, true, false);
        R->BlendPipeline = graphicsPipeline("Pbr", R->PbrLayout, R->HdrFramebuffer, true, true);
        R->PresentPipeline = graphicsPipeline("Present", R->PresentLayout, R->SwapFramebuffers.front(), false, false);
        nvrhi::ComputePipelineDesc compute;
        compute.CS = shader("Ssao.comp", nvrhi::ShaderType::Compute, error); compute.bindingLayouts = {R->SsaoLayout};
        if (!compute.CS) return false;
        R->SsaoPipeline = Device->createComputePipeline(compute);
        compute.CS = shader("SsaoBlur.comp", nvrhi::ShaderType::Compute, error); compute.bindingLayouts = {R->BlurLayout};
        if (!compute.CS) return false;
        R->BlurPipeline = Device->createComputePipeline(compute);
        compute.CS = shader("ToneMap.comp", nvrhi::ShaderType::Compute, error); compute.bindingLayouts = {R->ToneLayout};
        if (!compute.CS) return false;
        R->TonePipeline = Device->createComputePipeline(compute);
        nvrhi::BindingSetDesc bindings;
        bindings.bindings = {B::ConstantBuffer(0, R->FrameConstants), B::ConstantBuffer(1, R->SsaoConstants),
            B::Texture_SRV(16, R->Depth), B::Texture_UAV(48, R->AoRaw), B::Sampler(32, R->PointSampler)};
        R->SsaoBindings = Device->createBindingSet(bindings, R->SsaoLayout);
        bindings.bindings = {B::ConstantBuffer(0, R->FrameConstants), B::Texture_SRV(16, R->Depth),
            B::Texture_SRV(17, R->AoRaw), B::Texture_UAV(48, R->Ao)};
        R->BlurBindings = Device->createBindingSet(bindings, R->BlurLayout);
        bindings.bindings = {B::ConstantBuffer(0, R->FrameConstants), B::Texture_SRV(16, R->Hdr), B::Texture_UAV(48, R->Display)};
        R->ToneBindings = Device->createBindingSet(bindings, R->ToneLayout);
        bindings.bindings = {B::Texture_SRV(16, R->Display), B::Sampler(32, R->ClampSampler)};
        R->PresentBindings = Device->createBindingSet(bindings, R->PresentLayout);
        if (!R->DepthPipeline || !R->ShadowPipeline || !R->PbrPipeline || !R->BlendPipeline || !R->PresentPipeline ||
            !R->SsaoPipeline || !R->BlurPipeline || !R->TonePipeline) return fail(error, "Could not create the rendering pipelines");
        std::lock_guard lock(MessageMutex);
        return GpuError.empty() || fail(error, GpuError);
    }

    nvrhi::TextureHandle loadTexture(const GltfAsset& asset, std::int32_t textureIndex, bool srgb, std::string* error) {
        if (textureIndex < 0) return R->White;
        if (static_cast<std::size_t>(textureIndex) >= asset.Textures.size()) { fail(error, "Invalid material texture index"); return {}; }
        const auto imageIndex = asset.Textures[textureIndex].ImageIndex;
        if (imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= asset.Images.size()) { fail(error, "Invalid material image index"); return {}; }
        const auto& image = asset.Images[imageIndex];
        auto bytes = image.Data;
        if (bytes.empty() && image.Uri.starts_with("data:")) bytes = decodeDataUri(image.Uri);
        if (bytes.empty()) { fail(error, "Cannot read image " + image.Uri); return {}; }
        int width = 0, height = 0, channels = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) ||
            width <= 0 || height <= 0 || width > 16384 || height > 16384) { fail(error, "Invalid or oversized glTF image"); return {}; }
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4), &stbi_image_free);
        if (!pixels) { fail(error, "Image decoding failed: " + image.Uri); return {}; }
        auto handle = texture(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM, false, false, "glTF texture");
        if (!handle) { fail(error, "Could not create glTF texture"); return {}; }
        R->Commands->writeTexture(handle, 0, 0, pixels.get(), static_cast<std::size_t>(width) * 4);
        return handle;
    }

    MeshAsset* loadMesh(const std::string& meshPath, std::string* error) {
        if (auto found = R->Assets.find(meshPath); found != R->Assets.end()) return &found->second;
        MeshAsset mesh;
        std::filesystem::path sourcePath;
        if (!resolveAsset(AssetRoot, meshPath, sourcePath, error) ||
            !GltfImporter::loadFromFile(sourcePath.string(), mesh.Source, error, {true, AssetRoot})) return nullptr;
        for (const auto& source : mesh.Source.Meshes) {
            std::vector<Primitive> primitives;
            for (const auto& primitive : source.Primitives) {
                if (primitive.Vertices.empty() || primitive.Indices.empty()) { fail(error, "Mesh has no geometry"); return nullptr; }
                Primitive result;
                auto vertices = primitive.Vertices;
                if (primitive.NormalAccessor < 0) {
                    for (std::size_t index = 0; index < primitive.Indices.size(); index += 3) {
                        auto& a = vertices[primitive.Indices[index]];
                        auto& b = vertices[primitive.Indices[index + 1]];
                        auto& c = vertices[primitive.Indices[index + 2]];
                        const Vec3 normal = cross(b.Position - a.Position, c.Position - a.Position);
                        a.Normal += normal; b.Normal += normal; c.Normal += normal;
                    }
                    for (auto& vertex : vertices) vertex.Normal = vertex.Normal.normalized();
                }
                result.Vertices = Device->createBuffer(nvrhi::BufferDesc{}.setByteSize(vertices.size() * sizeof(GltfVertex))
                    .setIsVertexBuffer(true).setInitialState(nvrhi::ResourceStates::VertexBuffer).setKeepInitialState(true));
                result.Indices = Device->createBuffer(nvrhi::BufferDesc{}.setByteSize(primitive.Indices.size() * sizeof(std::uint32_t))
                    .setIsIndexBuffer(true).setInitialState(nvrhi::ResourceStates::IndexBuffer).setKeepInitialState(true));
                if (!result.Vertices || !result.Indices) { fail(error, "Geometry buffer allocation failed"); return nullptr; }
                R->Commands->writeBuffer(result.Vertices, vertices.data(), vertices.size() * sizeof(GltfVertex));
                R->Commands->writeBuffer(result.Indices, primitive.Indices.data(), primitive.Indices.size() * sizeof(std::uint32_t));
                result.IndexCount = static_cast<std::uint32_t>(primitive.Indices.size());
                if (primitive.MaterialIndex >= 0) result.Material = mesh.Source.Materials[primitive.MaterialIndex];
                const std::int32_t textures[] = {result.Material.BaseColorTexture, result.Material.MetallicRoughnessTexture,
                    result.Material.NormalTexture, result.Material.OcclusionTexture, result.Material.EmissiveTexture};
                for (std::size_t index = 0; index < result.Textures.size(); ++index) {
                    result.Textures[index] = loadTexture(mesh.Source, textures[index], index == 0 || index == 4, error);
                    if (!result.Textures[index]) return nullptr;
                    if (textures[index] >= 0) result.TextureMask |= 1u << index;
                }
                primitives.push_back(std::move(result));
            }
            mesh.Meshes.push_back(std::move(primitives));
        }
        return &R->Assets.emplace(meshPath, std::move(mesh)).first->second;
    }

    bool prepareDraws(Scene& scene, const Vec3& cameraPosition, std::vector<Draw>& draws, std::string* error) {
        using B = nvrhi::BindingSetItem;
        for (auto entity : scene.registry().entities()) {
            auto* renderer = scene.registry().tryGet<MeshRendererComponent>(entity);
            auto* transform = scene.registry().tryGet<TransformComponent>(entity);
            if (!renderer || !renderer->Visible || !transform || renderer->Mesh.empty()) continue;
            auto* mesh = loadMesh(renderer->Mesh, error);
            if (!mesh) return false;
            std::vector<std::pair<std::int32_t, Mat4>> instances;
            if (mesh->Source.Nodes.empty()) {
                for (std::size_t index = 0; index < mesh->Meshes.size(); ++index) instances.emplace_back(static_cast<std::int32_t>(index), transform->localMatrix());
            } else {
                std::vector<std::int32_t> roots;
                if (!mesh->Source.Scenes.empty()) {
                    const auto sceneIndex = mesh->Source.DefaultScene >= 0 ? mesh->Source.DefaultScene : 0;
                    roots = mesh->Source.Scenes[sceneIndex].RootNodes;
                } else for (std::size_t index = 0; index < mesh->Source.Nodes.size(); ++index)
                    if (mesh->Source.Nodes[index].ParentIndex < 0) roots.push_back(static_cast<std::int32_t>(index));
                std::vector<std::pair<std::int32_t, Mat4>> stack;
                for (auto root : roots) stack.emplace_back(root, transform->localMatrix());
                std::size_t visited = 0;
                while (!stack.empty()) {
                    const auto [index, parent] = stack.back(); stack.pop_back();
                    if (index < 0 || static_cast<std::size_t>(index) >= mesh->Source.Nodes.size() || ++visited > mesh->Source.Nodes.size()) return fail(error, "Invalid or cyclic glTF node hierarchy");
                    const auto& node = mesh->Source.Nodes[index];
                    const Mat4 model = parent * (node.HasMatrix ? node.Matrix : Mat4::trs(node.Translation, node.Rotation, node.Scale));
                    if (node.MeshIndex >= 0) instances.emplace_back(node.MeshIndex, model);
                    for (auto child : node.Children) stack.emplace_back(child, model);
                }
            }
            for (const auto& [meshIndex, model] : instances) {
                if (meshIndex < 0 || static_cast<std::size_t>(meshIndex) >= mesh->Meshes.size()) return fail(error, "Invalid glTF mesh index");
                for (auto& primitive : mesh->Meshes[meshIndex]) {
                    Draw draw;
                    draw.Geometry = &primitive; draw.Model = model; draw.CastShadows = renderer->CastShadows;
                    draw.DistanceSquared = (model.transformPoint({}) - cameraPosition).lengthSquared();
                    Mat4 inverted;
                    if (!inverse(model, inverted)) continue; // Zero-scale objects do not produce valid surface normals.
                    const GpuObjectConstants object{model, transpose(inverted)};
                    draw.ObjectConstants = constantBuffer(sizeof(object));
                    draw.MaterialConstants = constantBuffer(sizeof(GpuMaterialConstants));
                    auto material = primitive.Material;
                    if (!renderer->Material.empty()) {
                        std::filesystem::path materialPath;
                        if (!resolveAsset(AssetRoot, renderer->Material, materialPath, error) ||
                            !LoadPbrMaterialOverride(materialPath, material, error)) return false;
                    }
                    draw.Blend = material.Alpha == PbrMaterial::AlphaMode::Blend;
                    GpuMaterialConstants gpu;
                    gpu.BaseColorFactor = {material.BaseColor.x, material.BaseColor.y, material.BaseColor.z, material.BaseAlpha};
                    gpu.MaterialFactors = {material.Metallic, material.Roughness, material.NormalScale, material.OcclusionStrength};
                    gpu.EmissiveAlphaCutoff = {material.Emissive.x, material.Emissive.y, material.Emissive.z, material.AlphaCutoff};
                    gpu.TextureMaskAlphaMode = {primitive.TextureMask, static_cast<std::uint32_t>(material.Alpha), material.DoubleSided ? 1u : 0u, renderer->ReceiveShadows ? 1u : 0u};
                    R->Commands->writeBuffer(draw.ObjectConstants, &object, sizeof(object));
                    R->Commands->writeBuffer(draw.MaterialConstants, &gpu, sizeof(gpu));
                    nvrhi::BindingSetDesc bindings;
                    bindings.bindings = {B::ConstantBuffer(0, R->FrameConstants), B::ConstantBuffer(1, draw.ObjectConstants), B::ConstantBuffer(2, draw.MaterialConstants), B::Texture_SRV(16, primitive.Textures[0]), B::Sampler(32, R->WrapSampler)};
                    draw.DepthBindings = Device->createBindingSet(bindings, R->DepthLayout);
                    for (std::uint32_t index = 1; index < 5; ++index) bindings.bindings.push_back(B::Texture_SRV(16 + index, primitive.Textures[index]));
                    bindings.bindings.push_back(B::Texture_SRV(21, R->Shadow));
                    bindings.bindings.push_back(B::Texture_SRV(22, R->Ao));
                    bindings.bindings.push_back(B::Texture_SRV(23, R->Environment));
                    bindings.bindings.push_back(B::Texture_SRV(24, R->Irradiance));
                    bindings.bindings.push_back(B::Texture_SRV(25, R->Brdf));
                    bindings.bindings.push_back(B::Sampler(33, R->ClampSampler));
                    bindings.bindings.push_back(B::Sampler(34, R->ShadowSampler));
                    bindings.bindings.push_back(B::Sampler(35, R->EnvironmentSampler));
                    draw.PbrBindings = Device->createBindingSet(bindings, R->PbrLayout);
                    draws.push_back(std::move(draw));
                }
            }
        }
        std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) {
            const bool aBlend = a.Blend;
            const bool bBlend = b.Blend;
            if (aBlend != bBlend) return !aBlend;
            return aBlend ? a.DistanceSquared > b.DistanceSquared : a.DistanceSquared < b.DistanceSquared;
        });
        return true;
    }

    void drawGeometry(const std::vector<Draw>& draws, nvrhi::IFramebuffer* framebuffer, nvrhi::IGraphicsPipeline* pipeline, bool shadow, bool pbr) {
        for (const auto& draw : draws) {
            const bool blend = draw.Blend;
            if ((shadow && !draw.CastShadows) || (!pbr && blend)) continue;
            nvrhi::GraphicsState state;
            state.pipeline = pbr && blend ? R->BlendPipeline.Get() : pipeline;
            state.framebuffer = framebuffer;
            const auto& info = framebuffer->getFramebufferInfo();
            state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(info.width), static_cast<float>(info.height)));
            state.bindings = {pbr ? draw.PbrBindings.Get() : draw.DepthBindings.Get()};
            state.vertexBuffers = {{draw.Geometry->Vertices, 0, 0}};
            state.indexBuffer = {draw.Geometry->Indices, nvrhi::Format::R32_UINT, 0};
            R->Commands->setGraphicsState(state);
            R->Commands->drawIndexed(nvrhi::DrawArguments{}.setVertexCount(draw.Geometry->IndexCount));
        }
    }

    bool render(Scene& scene, const RenderSettings& settings, const Mat4& view, const Mat4& projection, const Vec3& cameraPosition, void* ui, std::string* error) {
        if (Owner->width() == 0 || Owner->height() == 0) return true;
        if (!ValidateRenderSettings(settings, error)) return false;
        if (settings.ShadowMapResolution > Properties.limits.maxImageDimension2D)
            return fail(error, "Shadow resolution exceeds the device limit");
        if (SwapDirty || Extent.width != Owner->width() || Extent.height != Owner->height()) {
            if (!createSwapchain(error) || !resizeTargets(settings.ShadowMapResolution, error)) return false;
        } else if (ShadowResolution != settings.ShadowMapResolution) {
            Device->waitForIdle();
            if (!resizeTargets(settings.ShadowMapResolution, error)) return false;
        }
        GpuFrameConstants frame;
        // NVRHI uses a negative-height Vulkan viewport to match its D3D
        // coordinate contract. Undo the public Vulkan projection's Y flip.
        frame.View = view; frame.Projection = projection;
        for (std::uint32_t column = 0; column < 4; ++column) frame.Projection(column, 1) *= -1.0f;
        frame.ViewProjection = frame.Projection * view;
        if (!inverse(frame.Projection, frame.InverseProjection)) return fail(error, "Invalid camera projection");
        Vec3 lightDirection = Vec3{-0.5f, -1.0f, -0.4f}.normalized();
        Vec3 lightColor{1.0f, 0.95f, 0.85f};
        float intensity = 0.0f;
        bool shadows = false;
        bool hasSun = false;
        for (auto entity : scene.registry().entities()) {
            const auto* light = scene.registry().tryGet<LightComponent>(entity);
            const auto* transform = scene.registry().tryGet<TransformComponent>(entity);
            if (!light || !transform) continue;
            if (!ValidateLight(*light, error)) return false;
            const auto direction = transform->Rotation.normalized().rotate({0, 0, -1});
            if (light->Type == LightType::Directional && !hasSun) {
                hasSun = true;
                lightDirection = direction;
                lightColor = light->Color; intensity = light->Intensity;
                shadows = settings.EnableShadows && light->CastShadows;
            } else {
                if (frame.AdditionalLightCount.X >= GpuMaxAdditionalLights) return fail(error, "Scene exceeds the limit of 32 additional lights");
                auto& gpu = frame.AdditionalLights[frame.AdditionalLightCount.X++];
                gpu.PositionRange = {transform->Translation.x, transform->Translation.y, transform->Translation.z, light->Range};
                gpu.ColorIntensity = {light->Color.x, light->Color.y, light->Color.z, light->Intensity};
                gpu.DirectionOuterCone = {direction.x, direction.y, direction.z, std::cos(light->OuterConeRadians)};
                gpu.Parameters = {std::cos(light->InnerConeRadians), static_cast<float>(light->Type), 0, 0};
            }
        }
        const Vec3 shadowCenter{cameraPosition.x, 0.0f, cameraPosition.z - 5.0f};
        frame.LightViewProjection = orthographicVulkan(-24, 24, -24, 24, 0.1f, 100.0f) * lookAt(shadowCenter - lightDirection * 40.0f, shadowCenter);
        for (std::uint32_t column = 0; column < 4; ++column) frame.LightViewProjection(column, 1) *= -1.0f;
        frame.CameraPositionExposure = {cameraPosition.x, cameraPosition.y, cameraPosition.z, settings.Exposure};
        frame.LightDirectionIntensity = {lightDirection.x, lightDirection.y, lightDirection.z, intensity};
        frame.LightColor = {lightColor.x, lightColor.y, lightColor.z, 0};
        frame.ViewportSizeInverse = {static_cast<float>(Extent.width), static_cast<float>(Extent.height), 1.0f / static_cast<float>(Extent.width), 1.0f / static_cast<float>(Extent.height)};
        frame.ShadowParameters = {1.0f / static_cast<float>(ShadowResolution), 1.0f / static_cast<float>(ShadowResolution), settings.ShadowDepthBias, settings.ShadowDepthBias};
        frame.EnvironmentParameters = {EnvironmentSettings.Intensity, EnvironmentSettings.RotationRadians, static_cast<float>(EnvironmentMips), settings.EnableIbl ? 1.0f : 0.0f};
        frame.FeatureParameters = {shadows ? 1u : 0u, settings.EnableSsao ? 1u : 0u, settings.ShadowPcfSamples, static_cast<std::uint32_t>(settings.Tonemapper)};
        GpuSsaoConstants ssao;
        ssao.RadiusBiasPower = {settings.SsaoRadius, settings.SsaoBias, 1.5f, 0};
        ssao.SampleCountFrame = {settings.SsaoSampleCount, 0, 0, 0};
        std::mt19937 generator(0x54494e59u);
        std::uniform_real_distribution<float> random(0.0f, 1.0f);
        for (std::size_t index = 0; index < ssao.Kernel.size(); ++index) {
            Vec3 sample{random(generator) * 2 - 1, random(generator) * 2 - 1, random(generator)};
            const float factor = static_cast<float>(index) / static_cast<float>(ssao.Kernel.size());
            sample = sample.normalized() * random(generator) * (0.1f + 0.9f * factor * factor);
            ssao.Kernel[index] = {sample.x, sample.y, sample.z, 0};
        }
        // Finish all fallible asset imports before acquiring an image. This keeps
        // acquire semaphores reusable even if a malformed asset rejects the frame.
        R->Commands->open();
        std::vector<Draw> draws;
        if (!prepareDraws(scene, cameraPosition, draws, error)) {
            R->Commands->close(); Device->executeCommandList(R->Commands); Device->waitForIdle();
            return false;
        }
        R->Commands->writeBuffer(R->FrameConstants, &frame, sizeof(frame));
        R->Commands->writeBuffer(R->SsaoConstants, &ssao, sizeof(ssao));
        R->Commands->clearDepthStencilTexture(R->Shadow, nvrhi::AllSubresources, true, 1.0f, false, 0);
        if (shadows) drawGeometry(draws, R->ShadowFramebuffer, R->ShadowPipeline, true, false);
        R->Commands->clearDepthStencilTexture(R->Depth, nvrhi::AllSubresources, true, 1.0f, false, 0);
        drawGeometry(draws, R->DepthFramebuffer, R->DepthPipeline, false, false);
        if (settings.EnableSsao) {
            R->Commands->setComputeState(nvrhi::ComputeState{}.setPipeline(R->SsaoPipeline).addBindingSet(R->SsaoBindings));
            R->Commands->dispatch((Extent.width + 7) / 8, (Extent.height + 7) / 8, 1);
            R->Commands->setComputeState(nvrhi::ComputeState{}.setPipeline(R->BlurPipeline).addBindingSet(R->BlurBindings));
            R->Commands->dispatch((Extent.width + 7) / 8, (Extent.height + 7) / 8, 1);
        } else R->Commands->clearTextureFloat(R->Ao, nvrhi::AllSubresources, nvrhi::Color(1.0f));
        R->Commands->clearTextureFloat(R->Hdr, nvrhi::AllSubresources, nvrhi::Color(0.025f, 0.04f, 0.07f, 1.0f));
        drawGeometry(draws, R->HdrFramebuffer, R->PbrPipeline, false, true);
        R->Commands->setComputeState(nvrhi::ComputeState{}.setPipeline(R->TonePipeline).addBindingSet(R->ToneBindings));
        R->Commands->dispatch((Extent.width + 7) / 8, (Extent.height + 7) / 8, 1);
        std::uint32_t imageIndex = 0;
        const VkResult acquired = vkAcquireNextImageKHR(LogicalDevice, Swapchain, UINT64_MAX, Acquired, VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            R->Commands->close(); Device->executeCommandList(R->Commands); Device->waitForIdle();
            SwapDirty = true;
            return true;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            R->Commands->close(); Device->executeCommandList(R->Commands); Device->waitForIdle();
            return check(acquired, "vkAcquireNextImageKHR", error);
        }
        if (acquired == VK_SUBOPTIMAL_KHR) SwapDirty = true;
        R->Commands->beginTrackingTextureState(R->SwapImages[imageIndex], nvrhi::AllSubresources,
            R->Presented[imageIndex] ? nvrhi::ResourceStates::Present : nvrhi::ResourceStates::Common);
        nvrhi::GraphicsState present;
        present.pipeline = R->PresentPipeline; present.framebuffer = R->SwapFramebuffers[imageIndex]; present.bindings = {R->PresentBindings};
        present.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(Extent.width), static_cast<float>(Extent.height)));
        R->Commands->setGraphicsState(present);
        R->Commands->draw(nvrhi::DrawArguments{}.setVertexCount(3));
        R->Commands->clearState();
#if defined(TINYENGINE_WITH_IMGUI_VULKAN)
        if (ui && ImGuiInitialized) {
            auto commandBuffer = static_cast<VkCommandBuffer>(R->Commands->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer).pointer);
            VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            begin.renderPass = UiRenderPass; begin.framebuffer = UiFramebuffers[imageIndex]; begin.renderArea.extent = Extent;
            vkCmdBeginRenderPass(commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
            ImGui_ImplVulkan_RenderDrawData(static_cast<ImDrawData*>(ui), commandBuffer);
            vkCmdEndRenderPass(commandBuffer);
        }
#else
        (void)ui;
#endif
        R->Commands->setTextureState(R->SwapImages[imageIndex], nvrhi::AllSubresources, nvrhi::ResourceStates::Present);
        R->Commands->commitBarriers();
        R->Commands->close();
        Device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, Acquired, 0);
        Device->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, Rendered, 0);
        Device->executeCommandList(R->Commands);
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1; presentInfo.pWaitSemaphores = &Rendered;
        presentInfo.swapchainCount = 1; presentInfo.pSwapchains = &Swapchain; presentInfo.pImageIndices = &imageIndex;
        const auto presented = vkQueuePresentKHR(Queue, &presentInfo);
        // A single synchronized frame owns these resources. Waiting for present
        // completion also proves binary semaphore reuse and resize destruction safe.
        if (!check(vkQueueWaitIdle(Queue), "vkQueueWaitIdle", error)) return false;
        Device->runGarbageCollection();
        R->Presented[imageIndex] = true;
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) SwapDirty = true;
        else if (!check(presented, "vkQueuePresentKHR", error)) return false;
        ++FrameIndex;
        std::lock_guard lock(MessageMutex);
        return GpuError.empty() || fail(error, GpuError);
    }
};

GraphicsContext::GraphicsContext() : m_Impl(std::make_unique<Impl>()) {}
GraphicsContext::~GraphicsContext() = default;
bool GraphicsContext::available() { return true; }

std::unique_ptr<GraphicsContext> GraphicsContext::create(Window& window, const std::filesystem::path& shaderDirectory, std::string* error) {
    auto context = std::unique_ptr<GraphicsContext>(new GraphicsContext());
    context->m_Impl->Owner = &window;
    context->m_Impl->ShaderDirectory = shaderDirectory;
    try {
        if (!context->m_Impl->initialize(error)) return {};
    } catch (const std::exception& exception) {
        fail(error, std::string("Graphics initialization failed: ") + exception.what());
        return {};
    }
    return context;
}

bool GraphicsContext::initializeImGui(std::string* error) {
#if defined(TINYENGINE_WITH_IMGUI_VULKAN)
    auto& impl = *m_Impl;
    if (impl.ImGuiInitialized) return true;
    if (!ImGui::GetCurrentContext()) return fail(error, "Create the ImGui context before initializing its Vulkan backend");
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_2; info.Instance = impl.Instance; info.PhysicalDevice = impl.PhysicalDevice;
    info.Device = impl.LogicalDevice; info.QueueFamily = impl.QueueFamily; info.Queue = impl.Queue;
    info.RenderPass = impl.UiRenderPass; info.MinImageCount = impl.MinImageCount;
    info.ImageCount = static_cast<std::uint32_t>(impl.R->SwapImages.size()); info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.DescriptorPoolSize = 256;
    if (!ImGui_ImplVulkan_Init(&info)) return fail(error, "ImGui Vulkan backend initialization failed");
    impl.ImGuiInitialized = true;
    return ImGui_ImplVulkan_CreateFontsTexture() || fail(error, "ImGui font upload failed");
#else
    return fail(error, "This build does not include the ImGui Vulkan integration");
#endif
}

void GraphicsContext::setAssetRoot(std::filesystem::path root) {
    if (root == m_Impl->AssetRoot) return;
    m_Impl->Device->waitForIdle();
    m_Impl->R->Assets.clear();
    m_Impl->AssetRoot = std::move(root);
}

bool GraphicsContext::setEnvironment(const EnvironmentMap& settings, std::string* error) try {
    if (!ValidateEnvironmentMap(settings, error)) return false;
    auto& impl = *m_Impl;
    if (settings.EquirectangularPath.empty()) {
        impl.Device->waitForIdle();
        impl.R->Environment = impl.R->DefaultEnvironment;
        impl.R->Irradiance = impl.R->DefaultEnvironment;
        impl.R->Brdf = impl.R->DefaultBrdf;
        impl.EnvironmentMips = 1; impl.EnvironmentSettings = settings;
        return true;
    }
    std::filesystem::path environmentPath;
    if (!resolveAsset(impl.AssetRoot, settings.EquirectangularPath, environmentPath, error)) return false;
    const auto bytes = readBytes(environmentPath);
    if (bytes.empty()) return fail(error, "Cannot read HDR environment " + settings.EquirectangularPath);
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) || width <= 0 || height <= 0 || width > 16384 || height > 8192 ||
        static_cast<std::uint64_t>(width) * height > 16 * 1024 * 1024)
        return fail(error, "Invalid or oversized environment image");
    std::unique_ptr<float, decltype(&stbi_image_free)> pixels(stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4), &stbi_image_free);
    if (!pixels) return fail(error, "HDR environment decoding failed");
    FilteredEnvironment filtered;
    if (!FilterEnvironment({pixels.get(), static_cast<std::size_t>(width) * height * 4},
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), settings.CubemapResolution,
        settings.PrefilteredMipLevels, settings.IrradianceResolution, filtered, error)) return false;
    impl.Device->waitForIdle();
    nvrhi::TextureDesc desc;
    desc.width = filtered.SpecularMips.front().Width; desc.height = filtered.SpecularMips.front().Height;
    desc.format = nvrhi::Format::RGBA32_FLOAT; desc.keepInitialState = true; desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.mipLevels = settings.PrefilteredMipLevels;
    auto environment = impl.Device->createTexture(desc);
    if (!environment) return fail(error, "Environment texture allocation failed");
    auto irradianceDesc = desc; irradianceDesc.mipLevels = 1;
    irradianceDesc.width = filtered.Irradiance.Width; irradianceDesc.height = filtered.Irradiance.Height;
    auto irradiance = impl.Device->createTexture(irradianceDesc);
    auto brdfDesc = irradianceDesc; brdfDesc.width = filtered.Brdf.Width; brdfDesc.height = filtered.Brdf.Height;
    auto brdf = impl.Device->createTexture(brdfDesc);
    if (!irradiance || !brdf) return fail(error, "Environment convolution texture allocation failed");
    auto commands = impl.Device->createCommandList();
    if (!commands) return fail(error, "Environment upload command allocation failed");
    commands->open();
    for (std::uint32_t level = 0; level < desc.mipLevels; ++level) {
        const auto& mip = filtered.SpecularMips[level];
        commands->writeTexture(environment, 0, level, mip.Pixels.data(), static_cast<std::size_t>(mip.Width) * sizeof(float) * 4);
    }
    commands->writeTexture(irradiance, 0, 0, filtered.Irradiance.Pixels.data(), static_cast<std::size_t>(filtered.Irradiance.Width) * sizeof(float) * 4);
    commands->writeTexture(brdf, 0, 0, filtered.Brdf.Pixels.data(), static_cast<std::size_t>(filtered.Brdf.Width) * sizeof(float) * 4);
    commands->close(); impl.Device->executeCommandList(commands); impl.Device->waitForIdle();
    {
        std::lock_guard lock(impl.MessageMutex);
        if (!impl.GpuError.empty()) return fail(error, impl.GpuError);
    }
    impl.R->Environment = environment; impl.EnvironmentMips = desc.mipLevels; impl.EnvironmentSettings = settings;
    impl.R->Irradiance = irradiance; impl.R->Brdf = brdf;
    return true;
} catch (const std::exception& exception) {
    return fail(error, std::string("Environment upload failed: ") + exception.what());
}

bool GraphicsContext::render(Scene& scene, const RenderSettings& settings, const Mat4& view, const Mat4& projection,
    const Vec3& cameraPosition, void* ui, std::string* error) {
    try { return m_Impl->render(scene, settings, view, projection, cameraPosition, ui, error); }
    catch (const std::exception& exception) { return fail(error, std::string("Rendering failed: ") + exception.what()); }
}

bool GraphicsContext::dispatch(const ComputeDispatch& dispatch, std::string* error) {
    std::vector<ComputeBuffer> ignored;
    return this->dispatch(dispatch, ignored, error);
}

bool GraphicsContext::dispatch(const ComputeDispatch& dispatch, std::vector<ComputeBuffer>& destination, std::string* error) {
    try {
    auto& impl = *m_Impl;
    const auto& limits = impl.Properties.limits;
    if (!ValidateComputeDispatch(dispatch, {limits.maxComputeWorkGroupCount[0], limits.maxComputeWorkGroupCount[1], limits.maxComputeWorkGroupCount[2]}, error)) return false;
    std::filesystem::path shaderPath;
    if (!resolveAsset(impl.ShaderDirectory, dispatch.Shader, shaderPath, error)) return false;
    const auto bytes = readBytes(shaderPath);
    if (bytes.size() < 20 || bytes.size() % 4 != 0) return fail(error, "Missing or invalid compute SPIR-V");
    std::uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), sizeof(magic));
    if (magic != 0x07230203u) return fail(error, "Invalid compute SPIR-V header");
    auto shader = impl.Device->createShader(nvrhi::ShaderDesc(nvrhi::ShaderType::Compute), bytes.data(), bytes.size());
    if (!shader) return fail(error, "Compute shader creation failed");

    std::vector<nvrhi::BufferHandle> buffers, staging;
    std::vector<ComputeBuffer> result = dispatch.Buffers;
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindingOffsets = {0, 0, 0, 0};
    nvrhi::BindingSetDesc setDesc;
    for (const auto& buffer : dispatch.Buffers) {
        const auto byteSize = buffer.Words.size() * sizeof(std::uint32_t);
        if (byteSize > limits.maxStorageBufferRange) return fail(error, "Compute buffer exceeds the device storage range");
        auto gpu = impl.Device->createBuffer(nvrhi::BufferDesc{}.setByteSize(byteSize)
            .setCanHaveUAVs(true).setCanHaveRawViews(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess).setKeepInitialState(true));
        auto readback = impl.Device->createBuffer(nvrhi::BufferDesc{}.setByteSize(byteSize)
            .setCpuAccess(nvrhi::CpuAccessMode::Read).setInitialState(nvrhi::ResourceStates::CopyDest));
        if (!gpu || !readback) return fail(error, "Compute buffer allocation failed");
        layoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::RawBuffer_UAV(48 + buffer.Binding));
        setDesc.bindings.push_back(nvrhi::BindingSetItem::RawBuffer_UAV(48 + buffer.Binding, gpu));
        buffers.push_back(gpu); staging.push_back(readback);
    }
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BindingSetHandle bindings;
    nvrhi::ComputePipelineDesc desc; desc.CS = shader;
    if (!buffers.empty()) {
        layout = impl.Device->createBindingLayout(layoutDesc);
        if (!layout) return fail(error, "Compute binding layout creation failed");
        bindings = impl.Device->createBindingSet(setDesc, layout);
        if (!bindings) return fail(error, "Compute binding set creation failed");
        desc.bindingLayouts.push_back(layout);
    }
    auto pipeline = impl.Device->createComputePipeline(desc);
    if (!pipeline) return fail(error, "Compute pipeline creation failed");
    auto commands = impl.Device->createCommandList();
    if (!commands) return fail(error, "Compute command allocation failed");
    commands->open();
    for (std::size_t index = 0; index < buffers.size(); ++index)
        commands->writeBuffer(buffers[index], dispatch.Buffers[index].Words.data(), dispatch.Buffers[index].Words.size() * sizeof(std::uint32_t));
    nvrhi::ComputeState state;
    state.setPipeline(pipeline);
    if (bindings) state.addBindingSet(bindings);
    commands->setComputeState(state);
    commands->dispatch(dispatch.WorkGroups[0], dispatch.WorkGroups[1], dispatch.WorkGroups[2]);
    for (std::size_t index = 0; index < buffers.size(); ++index)
        commands->copyBuffer(staging[index], 0, buffers[index], 0, result[index].Words.size() * sizeof(std::uint32_t));
    commands->close(); impl.Device->executeCommandList(commands); impl.Device->waitForIdle();
    for (std::size_t index = 0; index < staging.size(); ++index) {
        const void* mapped = impl.Device->mapBuffer(staging[index], nvrhi::CpuAccessMode::Read);
        if (!mapped) return fail(error, "Compute readback mapping failed");
        std::memcpy(result[index].Words.data(), mapped, result[index].Words.size() * sizeof(std::uint32_t));
        impl.Device->unmapBuffer(staging[index]);
    }
    {
        std::lock_guard lock(impl.MessageMutex);
        if (!impl.GpuError.empty()) return fail(error, impl.GpuError);
    }
    destination = std::move(result);
    return true;
    } catch (const std::exception& exception) {
        return fail(error, std::string("Compute dispatch failed: ") + exception.what());
    }
}

bool GraphicsContext::CaptureFrame(FrameCapture& destination, std::string* error) {
    auto& impl = *m_Impl;
    if (impl.FrameIndex == 0) return fail(error, "Render a frame before requesting readback");
    auto desc = impl.R->Display->getDesc();
    desc.isUAV = false;
    desc.isRenderTarget = false;
    desc.isShaderResource = false;
    desc.keepInitialState = false;
    desc.initialState = nvrhi::ResourceStates::CopyDest;
    auto staging = impl.Device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
    if (!staging) return fail(error, "Frame readback allocation failed");
    impl.R->Commands->open();
    impl.R->Commands->copyTexture(staging, nvrhi::TextureSlice{}, impl.R->Display, nvrhi::TextureSlice{});
    impl.R->Commands->close();
    impl.Device->executeCommandList(impl.R->Commands);
    impl.Device->waitForIdle();
    std::size_t rowPitch = 0;
    const auto* pixels = static_cast<const std::uint8_t*>(impl.Device->mapStagingTexture(staging, {}, nvrhi::CpuAccessMode::Read, &rowPitch));
    if (!pixels) return fail(error, "Frame readback mapping failed");
    FrameCapture captured;
    captured.Width = desc.width;
    captured.Height = desc.height;
    captured.Pixels.resize(static_cast<std::size_t>(desc.width) * desc.height * 4);
    const auto rowBytes = static_cast<std::size_t>(desc.width) * sizeof(float) * 4;
    for (std::uint32_t row = 0; row < desc.height; ++row)
        std::memcpy(captured.Pixels.data() + static_cast<std::size_t>(row) * desc.width * 4, pixels + row * rowPitch, rowBytes);
    impl.Device->unmapStagingTexture(staging);
    std::lock_guard lock(impl.MessageMutex);
    if (!impl.GpuError.empty()) return fail(error, impl.GpuError);
    destination = std::move(captured);
    return true;
}

} // namespace TinyEngine
#endif
