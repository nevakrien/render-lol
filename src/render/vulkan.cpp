#include "vulkan.hpp"
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#ifdef __ANDROID__
#include <dirent.h>
#endif
#include <unordered_map>

namespace toy {
void vkCheck(VkResult r, const char *op) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(op) + " failed: " + std::to_string(r));
}
Viewport boardViewport(int w, int h) {
    float scale = std::min(w / 18.f, h / 11.f);
    return {(w - 18 * scale) * .5f, (h - 11 * scale) * .5f, 18 * scale, 11 * scale};
}
Vec2 screenToWorld(float x, float y, int w, int h) {
    auto v = boardViewport(w, h);
    if (v.width <= 0 || v.height <= 0)
        return {};
    return {(x - v.x) / v.width * 18 - 9, 5.5f - (y - v.y) / v.height * 11};
}
namespace {
VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT *data,
                                            void *user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ++*static_cast<unsigned *>(user);
    SDL_Log("Vulkan: %s", data->pMessage);
    return VK_FALSE;
}
bool hasExtension(VkPhysicalDevice device, const char *name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> list(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, list.data());
    for (auto &e : list)
        if (std::strcmp(e.extensionName, name) == 0)
            return true;
    return false;
}
} // namespace
Vulkan::Vulkan(SDL_Window *w, bool validation, const std::string &shaderDir)
    : window_(w), shaderDir_(shaderDir) {
    try {
        initialize(validation);
    } catch (...) {
        cleanup();
        throw;
    }
}
void Vulkan::initialize(bool validation) {
    uint32_t count = 0;
    auto extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions)
        throw std::runtime_error(SDL_GetError());
    std::vector<const char *> ext(extensions, extensions + count), layers;
    VkInstanceCreateFlags flags = 0;
    uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
    for (auto &e : available)
        if (std::strcmp(e.extensionName, "VK_KHR_portability_enumeration") == 0) {
            ext.push_back("VK_KHR_portability_enumeration");
            flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    if (validation) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> found(n);
        vkEnumerateInstanceLayerProperties(&n, found.data());
        bool ok = false;
        for (auto &l : found)
            if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                ok = true;
        if (!ok)
            throw std::runtime_error("--validate requested but "
                                     "VK_LAYER_KHRONOS_validation is unavailable");
        layers.push_back("VK_LAYER_KHRONOS_validation");
        ext.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "render-lol";
    app.apiVersion = VK_API_VERSION_1_1;
    VkDebugUtilsMessengerCreateInfoEXT debug{};
    debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debugMessage;
    debug.pUserData = &validationErrors_;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.flags = flags;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = uint32_t(ext.size());
    info.ppEnabledExtensionNames = ext.data();
    info.enabledLayerCount = uint32_t(layers.size());
    info.ppEnabledLayerNames = layers.data();
    info.pNext = validation ? &debug : nullptr;
    vkCheck(vkCreateInstance(&info, nullptr, &instance_), "create instance");
    if (validation) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        vkCheck(create(instance_, &debug, nullptr, &messenger_), "create debug messenger");
    }
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_))
        throw std::runtime_error(SDL_GetError());
    uint32_t n = 0;
    vkCheck(vkEnumeratePhysicalDevices(instance_, &n, nullptr), "enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(n);
    vkCheck(vkEnumeratePhysicalDevices(instance_, &n, devices.data()), "enumerate GPUs");
    for (auto d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.apiVersion < VK_API_VERSION_1_1 ||
            !hasExtension(d, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if (present && (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                physical_ = d;
                family_ = i;
                break;
            }
        }
        if (physical_) {
            SDL_Log("GPU: %s", props.deviceName);
            break;
        }
    }
    if (!physical_)
        throw std::runtime_error("No Vulkan 1.1 GPU with a graphics/presentation queue");
    float priority = 1;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    std::vector<const char *> deviceExt = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (hasExtension(physical_, "VK_KHR_portability_subset"))
        deviceExt.push_back("VK_KHR_portability_subset");
    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = uint32_t(deviceExt.size());
    di.ppEnabledExtensionNames = deviceExt.data();
    vkCheck(vkCreateDevice(physical_, &di, nullptr, &device_), "create device");
    vkGetDeviceQueue(device_, family_, 0, &queue_);
    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.queueFamilyIndex = family_;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCheck(vkCreateCommandPool(device_, &pi, nullptr, &pool_), "create command pool");
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(device_, &ai, &command_), "allocate commands");
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCheck(vkCreateFence(device_, &fi, nullptr, &fence_), "create fence");
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCheck(vkCreateSemaphore(device_, &si, nullptr, &acquired_), "create semaphore");
    if (!rebuild())
        throw std::runtime_error("Cannot initialize a zero-sized surface");
}
void Vulkan::destroySwapchain() {
    for (auto f : framebuffers_)
        vkDestroyFramebuffer(device_, f, nullptr);
    framebuffers_.clear();
    for (auto v : views_)
        vkDestroyImageView(device_, v, nullptr);
    views_.clear();
    for (auto s : presented_)
        vkDestroySemaphore(device_, s, nullptr);
    presented_.clear();
    if (swapchain_)
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    images_.clear();
}
bool Vulkan::rebuild() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w <= 0 || h <= 0)
        return false;
    vkCheck(vkDeviceWaitIdle(device_), "wait before resize");
    VkSurfaceCapabilitiesKHR caps;
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps),
            "surface capabilities");
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
    if (formats.empty())
        throw std::runtime_error("Surface has no formats");
    auto chosen = formats[0];
    for (auto f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            chosen = f;
    if (chosen.format == VK_FORMAT_UNDEFINED)
        chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
    if (format_ != VK_FORMAT_UNDEFINED && format_ != chosen.format)
        throw std::runtime_error("Surface format changed; restart the sketch");
    format_ = chosen.format;
    extent_ = caps.currentExtent;
    if (extent_.width == std::numeric_limits<uint32_t>::max())
        extent_ = {std::clamp(uint32_t(w), caps.minImageExtent.width, caps.maxImageExtent.width),
                   std::clamp(uint32_t(h), caps.minImageExtent.height, caps.maxImageExtent.height)};
    if (!extent_.width || !extent_.height)
        return false;
    destroySwapchain();
    canCapture_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount)
        ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
    ci.imageFormat = format_;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (canCapture_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    for (auto alpha :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if (caps.supportedCompositeAlpha & alpha) {
            ci.compositeAlpha = alpha;
            break;
        }
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    vkCheck(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_), "create swapchain");
    if (!renderPass_) {
        VkAttachmentDescription attachment{};
        attachment.format = format_;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ri.attachmentCount = 1;
        ri.pAttachments = &attachment;
        ri.subpassCount = 1;
        ri.pSubpasses = &sub;
        ri.dependencyCount = 1;
        ri.pDependencies = &dep;
        vkCheck(vkCreateRenderPass(device_, &ri, nullptr, &renderPass_), "create render pass");
    }
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
    for (auto image : images_) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view;
        vkCheck(vkCreateImageView(device_, &vi, nullptr, &view), "create image view");
        views_.push_back(view);
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = renderPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &views_.back();
        fi.width = extent_.width;
        fi.height = extent_.height;
        fi.layers = 1;
        VkFramebuffer framebuffer;
        vkCheck(vkCreateFramebuffer(device_, &fi, nullptr, &framebuffer), "create framebuffer");
        framebuffers_.push_back(framebuffer);
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore semaphore;
        vkCheck(vkCreateSemaphore(device_, &si, nullptr, &semaphore),
                "create presentation semaphore");
        presented_.push_back(semaphore);
    }
    dirty_ = false;
    return true;
}
void Vulkan::addPass(std::unique_ptr<DrawPass> pass) { passes_.push_back(std::move(pass)); }
namespace {
std::unordered_map<std::string, time_t> &shaderTimestamps() {
    static std::unordered_map<std::string, time_t> ts;
    return ts;
}
bool checkShaderReloads(const std::string &dir) {
    if (dir.empty())
        return false;
    bool changed = false;
#ifdef __ANDROID__
    DIR *d = opendir(dir.c_str());
    if (!d)
        return false;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name(ent->d_name);
        if (name.size() < 4 || name.substr(name.size() - 4) != ".spv")
            continue;
        std::string path = dir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            continue;
        auto &ts = shaderTimestamps();
        auto prev = ts.find(path);
        if (prev == ts.end() || prev->second != st.st_mtime) {
            ts[path] = st.st_mtime;
            changed = true;
        }
    }
    closedir(d);
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec)
        return false;
    for (auto &entry : fs::directory_iterator(dir, ec)) {
        if (ec)
            break;
        if (entry.path().extension() != ".spv")
            continue;
        struct stat st;
        if (stat(entry.path().c_str(), &st) != 0)
            continue;
        auto &ts = shaderTimestamps();
        auto prev = ts.find(entry.path().string());
        if (prev == ts.end() || prev->second != st.st_mtime) {
            ts[entry.path().string()] = st.st_mtime;
            changed = true;
        }
    }
#endif
    return changed;
}
} // namespace
bool Vulkan::draw(const RenderFrame &frame, const std::string &capture) {
#ifdef __ANDROID__
    int windowWidth = 0, windowHeight = 0;
    SDL_GetWindowSizeInPixels(window_, &windowWidth, &windowHeight);
    if (!dirty_ && windowWidth > 0 && windowHeight > 0 &&
        (windowWidth > windowHeight) != (extent_.width > extent_.height))
        dirty_ = true;
#endif
    if (dirty_ && !rebuild())
        return false;
    if (checkShaderReloads(shaderDir_)) {
        for (auto &pass : passes_)
            pass->reloadShaders();
        SDL_Log("render-lol: hot-reloaded shaders from '%s'", shaderDir_.c_str());
    }
    vkCheck(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "wait for frame");
    uint32_t index = 0;
    auto result =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquired_, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        dirty_ = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR)
        dirty_ = true;
    else
        vkCheck(result, "acquire image");
    for (auto &pass : passes_)
        pass->prepare(frame);
    std::unique_ptr<Buffer> readback;
    if (!capture.empty()) {
        if (!canCapture_ ||
            (format_ != VK_FORMAT_B8G8R8A8_UNORM && format_ != VK_FORMAT_R8G8B8A8_UNORM &&
             format_ != VK_FORMAT_B8G8R8A8_SRGB && format_ != VK_FORMAT_R8G8B8A8_SRGB))
            throw std::runtime_error("Screenshot unsupported for this surface");
        readback = std::make_unique<Buffer>(*this, VkDeviceSize(extent_.width) * extent_.height * 4,
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    vkCheck(vkResetCommandBuffer(command_, 0), "reset commands");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(command_, &begin), "begin commands");
    VkClearValue clear{};
    clear.color = {{.023f, .03f, .046f, 1}};
    VkRenderPassBeginInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    ri.renderPass = renderPass_;
    ri.framebuffer = framebuffers_[index];
    ri.renderArea.extent = extent_;
    ri.clearValueCount = 1;
    ri.pClearValues = &clear;
    vkCmdBeginRenderPass(command_, &ri, VK_SUBPASS_CONTENTS_INLINE);
    auto box = boardViewport(int(extent_.width), int(extent_.height));
    VkViewport viewport{box.x, box.y, box.width, box.height, 0, 1};
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(command_, 0, 1, &viewport);
    vkCmdSetScissor(command_, 0, 1, &scissor);
    for (auto &pass : passes_)
        pass->record(command_);
    vkCmdEndRenderPass(command_);
    if (readback) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images_[index];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(command_, images_[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback->handle, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
    }
    vkCheck(vkEndCommandBuffer(command_), "end commands");
    vkCheck(vkResetFences(device_, 1, &fence_), "reset fence");
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired_;
    submit.pWaitDstStageMask = &stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &presented_[index];
    vkCheck(vkQueueSubmit(queue_, 1, &submit, fence_), "submit frame");
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &presented_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    result = vkQueuePresentKHR(queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        dirty_ = true;
    else
        vkCheck(result, "present");
    if (readback) {
        vkCheck(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "wait for screenshot");
        std::ofstream out(capture, std::ios::binary);
        out << "P6\n" << extent_.width << ' ' << extent_.height << "\n255\n";
        auto *bytes = static_cast<unsigned char *>(readback->mapped);
        bool bgra = format_ == VK_FORMAT_B8G8R8A8_UNORM || format_ == VK_FORMAT_B8G8R8A8_SRGB;
        for (size_t i = 0; i < size_t(extent_.width) * extent_.height; ++i) {
            char rgb[] = {char(bytes[i * 4 + (bgra ? 2 : 0)]), char(bytes[i * 4 + 1]),
                          char(bytes[i * 4 + (bgra ? 0 : 2)])};
            out.write(rgb, 3);
        }
        if (!out)
            throw std::runtime_error("Could not write screenshot: " + capture);
    }
    return true;
}
Vulkan::~Vulkan() { cleanup(); }
void Vulkan::cleanup() {
    if (device_)
        vkDeviceWaitIdle(device_);
    passes_.clear();
    if (device_) {
        destroySwapchain();
        if (renderPass_)
            vkDestroyRenderPass(device_, renderPass_, nullptr);
        if (acquired_)
            vkDestroySemaphore(device_, acquired_, nullptr);
        if (fence_)
            vkDestroyFence(device_, fence_, nullptr);
        if (pool_)
            vkDestroyCommandPool(device_, pool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_)
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    if (messenger_) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        destroy(instance_, messenger_, nullptr);
    }
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
}
Buffer::Buffer(Vulkan &vk, VkDeviceSize size, VkBufferUsageFlags usage) : device_(vk.device()) {
    try {
        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = size;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCheck(vkCreateBuffer(device_, &ci, nullptr, &handle), "create buffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device_, handle, &requirements);
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(vk.physical(), &properties);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                type = i;
                break;
            }
        if (type == UINT32_MAX)
            throw std::runtime_error("No host-visible coherent memory");
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = requirements.size;
        ai.memoryTypeIndex = type;
        vkCheck(vkAllocateMemory(device_, &ai, nullptr, &memory_), "allocate buffer memory");
        vkCheck(vkBindBufferMemory(device_, handle, memory_, 0), "bind buffer");
        vkCheck(vkMapMemory(device_, memory_, 0, size, 0, &mapped), "map buffer");
    } catch (...) {
        if (handle)
            vkDestroyBuffer(device_, handle, nullptr);
        if (memory_)
            vkFreeMemory(device_, memory_, nullptr);
        throw;
    }
}
Buffer::~Buffer() {
    if (mapped)
        vkUnmapMemory(device_, memory_);
    if (handle)
        vkDestroyBuffer(device_, handle, nullptr);
    if (memory_)
        vkFreeMemory(device_, memory_, nullptr);
}
} // namespace toy
