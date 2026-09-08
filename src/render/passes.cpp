#include "passes.hpp"
#include "shaders.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <sys/stat.h>

namespace toy {
namespace {
// Holds a SPIR-V source. Owns file bytes when loaded from disk; otherwise points
// at the embedded Python-generated fallback baked in at build time.
struct Spv {
    std::vector<uint32_t> owned; // file bytes when read from disk (word-aligned)
    const uint32_t *ptr = nullptr;
    size_t words = 0;
    bool embedded = false;
};
std::unordered_map<std::string, bool> &warned() {
    static std::unordered_map<std::string, bool> w;
    return w;
}
Spv loadSpv(const std::string &dir, const std::string &name, const uint32_t *emb, size_t embWords) {
    namespace fs = std::filesystem;
    Spv out;
    if (!dir.empty()) {
        auto path = fs::path(dir) / (name + ".spv");
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (f) {
            auto sz = f.tellg();
            f.seekg(0);
            out.owned.resize(size_t(sz) / 4);
            if (size_t(sz) % 4 != 0)
                out.owned.resize(size_t(sz) / 4 + 1);
            f.read(reinterpret_cast<char *>(out.owned.data()), sz);
            out.ptr = out.owned.data();
            out.words = (size_t(sz) + 3) / 4;
            out.embedded = false;
            return out;
        }
    }
    // Fall back to the embedded build-time bytes and warn once.
    auto &w = warned();
    if (!w[name]) {
        w[name] = true;
        std::fprintf(stderr, "render-lol: warning: no %s.spv in '%s'; using embedded copy "
                             "(recompile shaders for hot-reload)\n",
                     name.c_str(), dir.c_str());
    }
    out.ptr = emb;
    out.words = embWords;
    out.embedded = true;
    return out;
}
class Pipeline {
  public:
    Pipeline(Vulkan &vk, const std::string &vertName, const std::string &fragName,
             const uint32_t *vertEmb, size_t vertEmbWords, const uint32_t *fragEmb,
             size_t fragEmbWords, bool mesh, bool additive = false)
        : device_(vk.device()), renderPass_(vk.renderPass()),
          shaderDir_(vk.shaderDir()), vertName_(vertName), fragName_(fragName),
          vertEmb_(vertEmb), vertEmbWords_(vertEmbWords), fragEmb_(fragEmb),
          fragEmbWords_(fragEmbWords), mesh_(mesh), additive_(additive) {
        rebuild();
    }
    ~Pipeline() { destroy(); }
    void rebuild() {
        auto oldPipeline = handle_;
        auto oldLayout = layout_;
        handle_ = VK_NULL_HANDLE;
        layout_ = VK_NULL_HANDLE;
        auto vsSource = loadSpv(shaderDir_, vertName_, vertEmb_, vertEmbWords_);
        auto fsSource = loadSpv(shaderDir_, fragName_, fragEmb_, fragEmbWords_);
        VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
        try {
            auto module = [this](const Spv &s, VkShaderModule &out) {
                VkShaderModuleCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                ci.codeSize = s.words * 4;
                ci.pCode = s.ptr;
                vkCheck(vkCreateShaderModule(device_, &ci, nullptr, &out), "create shader");
            };
            module(vsSource, vs);
            module(fsSource, fs);
            VkPipelineShaderStageCreateInfo stages[2]{};
            for (auto &s : stages) {
                s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                s.pName = "main";
            }
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vs;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fs;
            VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
            VkVertexInputAttributeDescription attributes[] = {
                {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
                {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
                {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}};
            VkPipelineVertexInputStateCreateInfo vertex{};
            vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            if (mesh_) {
                vertex.vertexBindingDescriptionCount = 1;
                vertex.pVertexBindingDescriptions = &binding;
                vertex.vertexAttributeDescriptionCount = 3;
                vertex.pVertexAttributeDescriptions = attributes;
            }
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = 15;
            attachment.blendEnable = VK_TRUE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor =
                additive_ ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = states;
            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout_),
                    "create pipeline layout");
            VkGraphicsPipelineCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            ci.stageCount = 2;
            ci.pStages = stages;
            ci.pVertexInputState = &vertex;
            ci.pInputAssemblyState = &assembly;
            ci.pViewportState = &viewport;
            ci.pRasterizationState = &raster;
            ci.pMultisampleState = &multisample;
            ci.pColorBlendState = &blend;
            ci.pDynamicState = &dynamic;
            ci.layout = layout_;
            ci.renderPass = renderPass_;
            vkCheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &handle_),
                    "create graphics pipeline");
            vkDestroyShaderModule(device_, vs, nullptr);
            vkDestroyShaderModule(device_, fs, nullptr);
            if (oldPipeline)
                vkDestroyPipeline(device_, oldPipeline, nullptr);
            if (oldLayout)
                vkDestroyPipelineLayout(device_, oldLayout, nullptr);
        } catch (...) {
            if (vs)
                vkDestroyShaderModule(device_, vs, nullptr);
            if (fs)
                vkDestroyShaderModule(device_, fs, nullptr);
            if (handle_ && handle_ != oldPipeline)
                vkDestroyPipeline(device_, handle_, nullptr);
            if (layout_ && layout_ != oldLayout)
                vkDestroyPipelineLayout(device_, layout_, nullptr);
            handle_ = oldPipeline;
            layout_ = oldLayout;
            throw;
        }
    }
    void bind(VkCommandBuffer command) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, handle_);
    }

  private:
    void destroy() {
        if (handle_)
            vkDestroyPipeline(device_, handle_, nullptr);
        if (layout_)
            vkDestroyPipelineLayout(device_, layout_, nullptr);
    }
    VkDevice device_;
    VkRenderPass renderPass_;
    std::string shaderDir_;
    std::string vertName_, fragName_;
    const uint32_t *vertEmb_;
    size_t vertEmbWords_;
    const uint32_t *fragEmb_;
    size_t fragEmbWords_;
    bool mesh_, additive_;
    VkPipeline handle_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};
struct Mesh {
    explicit Mesh(Vulkan &vk)
        : buffer(vk, capacity * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {}
    static constexpr size_t capacity = 100000;
    Buffer buffer;
    std::vector<Vertex> vertices;
    void upload() {
        if (vertices.size() > capacity)
            throw std::runtime_error("Pass vertex budget exceeded");
        if (!vertices.empty())
            std::memcpy(buffer.mapped, vertices.data(), vertices.size() * sizeof(Vertex));
    }
    void draw(VkCommandBuffer c) {
        if (vertices.empty())
            return;
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(c, 0, 1, &buffer.handle, &offset);
        vkCmdDraw(c, uint32_t(vertices.size()), 1, 0, 0);
    }
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color color) {
        vertices.push_back({a, color, {}});
        vertices.push_back({b, color, {}});
        vertices.push_back({c, color, {}});
    }
    void rect(float x, float y, float w, float h, Color c) {
        triangle({x, y}, {x + w, y}, {x + w, y + h}, c);
        triangle({x, y}, {x + w, y + h}, {x, y + h}, c);
    }
    void line(Vec2 a, Vec2 b, float width, Color c) {
        float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        if (len < .0001f)
            return;
        Vec2 n{-dy / len * width * .5f, dx / len * width * .5f};
        Vec2 p{a.x + n.x, a.y + n.y}, q{a.x - n.x, a.y - n.y}, r{b.x - n.x, b.y - n.y},
            s{b.x + n.x, b.y + n.y};
        triangle(p, q, r, c);
        triangle(p, r, s, c);
    }
};
class BackgroundPass final : public DrawPass {
    Pipeline pipeline;

  public:
    explicit BackgroundPass(Vulkan &vk)
        : pipeline(vk, "background.vert", "background.frag", shaders::background_vert,
                   sizeof(shaders::background_vert) / sizeof(uint32_t), shaders::background_frag,
                   sizeof(shaders::background_frag) / sizeof(uint32_t), false) {}
    void prepare(const RenderFrame &) override {}
    void record(VkCommandBuffer c) override {
        pipeline.bind(c);
        vkCmdDraw(c, 3, 1, 0, 0);
    }
    void reloadShaders() override { pipeline.rebuild(); }
};
class BodyPass final : public DrawPass {
    Pipeline pipeline;
    Mesh mesh;

  public:
    explicit BodyPass(Vulkan &vk)
        : pipeline(vk, "mesh.vert", "body.frag", shaders::mesh_vert,
                   sizeof(shaders::mesh_vert) / sizeof(uint32_t), shaders::body_frag,
                   sizeof(shaders::body_frag) / sizeof(uint32_t), true), mesh(vk) {}
    void prepare(const RenderFrame &frame) override {
        mesh.vertices.clear();
        for (auto &b : frame.bodies) {
            for (size_t i = 0; i + 2 < b.triangles.size(); i += 3)
                mesh.triangle(b.triangles[i], b.triangles[i + 1], b.triangles[i + 2], b.color);
            Color edge{b.color.r * .6f, b.color.g * .6f, b.color.b * .6f, 1};
            for (size_t i = 0; i < b.outline.size(); ++i)
                mesh.line(b.outline[i], b.outline[(i + 1) % b.outline.size()], .028f, edge);
            // Filled marker for each rigid body. Shapes stay flat colored.
            mesh.rect(b.center.x - .045f, b.center.y - .045f, .09f, .09f, edge);
        }
        mesh.upload();
    }
    void record(VkCommandBuffer c) override {
        pipeline.bind(c);
        mesh.draw(c);
    }
    void reloadShaders() override { pipeline.rebuild(); }
};
class ImpactPass final : public DrawPass {
    Pipeline pipeline;
    Mesh mesh;

  public:
    explicit ImpactPass(Vulkan &vk)
        : pipeline(vk, "mesh.vert", "effect.frag", shaders::mesh_vert,
                   sizeof(shaders::mesh_vert) / sizeof(uint32_t), shaders::effect_frag,
                   sizeof(shaders::effect_frag) / sizeof(uint32_t), true, true),
          mesh(vk) {
    }
    void prepare(const RenderFrame &frame) override {
        mesh.vertices.clear();
        for (auto &r : frame.ripples) {
            float size = .12f + r.age * (1.5f + r.strength * .12f);
            Color color = r.color;
            color.a = (1 - r.age / .6f) * .8f;
            Vec2 corners[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int i : {0, 1, 2, 0, 2, 3})
                mesh.vertices.push_back(
                    {{r.point.x + corners[i].x * size, r.point.y + corners[i].y * size},
                     color,
                     corners[i]});
        }
        mesh.upload();
    }
    void record(VkCommandBuffer c) override {
        pipeline.bind(c);
        mesh.draw(c);
    }
    void reloadShaders() override { pipeline.rebuild(); }
};
using Glyph = std::array<unsigned char, 7>;
const std::unordered_map<char, Glyph> font = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}},      {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},       {'/', {1, 2, 2, 4, 8, 8, 16}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},       {':', {0, 4, 4, 0, 4, 4, 0}}};
class HudPass final : public DrawPass {
    Pipeline pipeline;
    Mesh mesh;
    void text(const std::string &str, float x, float y, float pixel, Color c) {
        for (char ch : str) {
            auto it = font.find(ch);
            if (it != font.end())
                for (int row = 0; row < 7; ++row)
                    for (int col = 0; col < 5; ++col)
                        if (it->second[row] & (1 << (4 - col)))
                            mesh.rect(x + col * pixel, y - row * pixel, pixel * .9f, pixel * .9f,
                                      c);
            x += 6 * pixel;
        }
    }

  public:
    explicit HudPass(Vulkan &vk)
        : pipeline(vk, "mesh.vert", "body.frag", shaders::mesh_vert,
                   sizeof(shaders::mesh_vert) / sizeof(uint32_t), shaders::body_frag,
                   sizeof(shaders::body_frag) / sizeof(uint32_t), true), mesh(vk) {}
    void prepare(const RenderFrame &frame) override {
        mesh.vertices.clear();
        text("RENDER LOL", -8, 5.14f, .047f, {.85f, .91f, 1});
        text("BOX2D", -2.5f, 5.07f, .024f, {.45f, .56f, .68f});
        text("SCORE " + std::to_string(frame.score), 4.6f, 5.1f, .035f, {.85f, .91f, 1});
        text("DRAG TO THROW / SPACE PAUSE / G GRAVITY / R RESET", -8, -4.84f, .026f,
             {.61f, .69f, .78f});
        text("1 CIRCLE  2 TRIANGLE  3 RECTANGLE / W MESH / M MUTE", -8, -5.17f,
             .023f, {.41f, .5f, .62f});
        mesh.upload();
    }
    void record(VkCommandBuffer c) override {
        pipeline.bind(c);
        mesh.draw(c);
    }
    void reloadShaders() override { pipeline.rebuild(); }
};
} // namespace
void installPasses(Vulkan &vk) {
    vk.addPass(std::make_unique<BackgroundPass>(vk));
    vk.addPass(std::make_unique<BodyPass>(vk));
    vk.addPass(std::make_unique<ImpactPass>(vk));
    vk.addPass(std::make_unique<HudPass>(vk));
}
} // namespace toy
