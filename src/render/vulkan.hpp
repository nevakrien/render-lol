#pragma once
#include "physics.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace toy {
void vkCheck(VkResult result, const char *operation);
struct Vertex {
    Vec2 position;
    Color color;
    Vec2 uv{};
};
struct RenderFrame {
    std::vector<BodyView> bodies;
    struct Ripple {
        Vec2 point;
        Color color;
        float age;
        float strength;
    };
    std::vector<Ripple> ripples;
    bool wireframe = false;
    uint64_t score = 0;
};
class Vulkan;
// Each experiment owns its shaders, pipeline, buffers and draw commands.
// The host only manages presentation and the ordered list of passes.
class DrawPass {
  public:
    virtual ~DrawPass() = default;
    virtual void prepare(const RenderFrame &) = 0;
    virtual void record(VkCommandBuffer) = 0;
    virtual void reloadShaders() {}
};
struct Viewport {
    float x, y, width, height;
};
Viewport boardViewport(int width, int height);
Vec2 screenToWorld(float x, float y, int width, int height);

class Vulkan {
  public:
    Vulkan(SDL_Window *window, bool validation, const std::string &shaderDir = "");
    ~Vulkan();
    Vulkan(const Vulkan &) = delete;
    Vulkan &operator=(const Vulkan &) = delete;
    VkDevice device() const { return device_; }
    VkPhysicalDevice physical() const { return physical_; }
    VkRenderPass renderPass() const { return renderPass_; }
    const std::string &shaderDir() const { return shaderDir_; }
    void addPass(std::unique_ptr<DrawPass> pass);
    bool draw(const RenderFrame &frame, const std::string &capture = "");
    void resize() { dirty_ = true; }
    unsigned validationErrors() const { return validationErrors_; }

  private:
    void initialize(bool validation);
    bool rebuild();
    void destroySwapchain();
    void cleanup();
    SDL_Window *window_;
    std::string shaderDir_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkSemaphore acquired_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> presented_;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<std::unique_ptr<DrawPass>> passes_;
    bool dirty_ = true, canCapture_ = false;
    unsigned validationErrors_ = 0;
};
class Buffer {
  public:
    Buffer(Vulkan &vk, VkDeviceSize size, VkBufferUsageFlags usage);
    ~Buffer();
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    VkBuffer handle = VK_NULL_HANDLE;
    void *mapped = nullptr;

  private:
    VkDevice device_;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
};
} // namespace toy
