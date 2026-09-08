#include "passes.hpp"
#include "gameplay_tuning.hpp"
#include "shaders.hpp"
#include <algorithm>
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
                {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
                {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, shapeId)},
                {4, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, effectAge)},
                {5, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, effectSeed)},
                {6, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, effectStrength)}};
            VkPipelineVertexInputStateCreateInfo vertex{};
            vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            if (mesh_) {
                vertex.vertexBindingDescriptionCount = 1;
                vertex.pVertexBindingDescriptions = &binding;
                vertex.vertexAttributeDescriptionCount = 7;
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
    void circle(Vec2 center, float radius, Color color, int segments = 12) {
        for (int i = 0; i < segments; ++i) {
            float a = float(i) / segments * 2.0f * 3.14159265f;
            float b = float(i + 1) / segments * 2.0f * 3.14159265f;
            triangle(center, {center.x + cosf(a) * radius, center.y + sinf(a) * radius},
                     {center.x + cosf(b) * radius, center.y + sinf(b) * radius}, color);
        }
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
            float critical =
                std::clamp((b.heat - tuning::heatPeak) / tuning::heatVisualDamageRange, 0.0f,
                           1.0f);
            Color bodyColor = b.color;

            float shake = critical * critical * tuning::maximumShake;
            Vec2 offset{shake * sinf(b.effectTime * 47.0f + b.id * 2.1f),
                        shake * sinf(b.effectTime * 61.0f + b.id * 3.7f)};
            auto shifted = [&](Vec2 p) { return Vec2{p.x + offset.x, p.y + offset.y}; };
            auto random = [&](int salt) {
                float value = sinf(b.id * 12.9898f + salt * 78.233f) * 43758.5453f;
                return value - floorf(value);
            };
            float firePresence = std::clamp((b.heat - tuning::heatFireAppearance) /
                                                (tuning::heatPeak - tuning::heatFireAppearance),
                                            0.0f, 1.0f);
            float blue = std::clamp((b.heat - tuning::heatPeak) /
                                        tuning::heatPowerDeclineRange,
                                    0.0f, 1.0f);
            float burned = std::clamp((b.heat - tuning::heatPowerDeclineEnd) /
                                          tuning::heatBurnoutRange,
                                      0.0f, 1.0f);
            float fireOpacity =
                firePresence * (tuning::burnedSurfaceOpacity +
                                (tuning::heatSurfaceOpacity - tuning::burnedSurfaceOpacity) *
                                    tuning::normalizedHeatPower(b.heat));
            for (size_t i = 0; i + 2 < b.triangles.size(); i += 3)
                mesh.triangle(shifted(b.triangles[i]), shifted(b.triangles[i + 1]),
                              shifted(b.triangles[i + 2]), bodyColor);
            Color edge{bodyColor.r * .6f, bodyColor.g * .6f, bodyColor.b * .6f, 1};
            for (size_t i = 0; i < b.outline.size(); ++i)
                mesh.line(shifted(b.outline[i]), shifted(b.outline[(i + 1) % b.outline.size()]),
                          .028f, edge);

            if (firePresence > 0 && !b.outline.empty()) {
                Vec2 center = shifted(b.center);
                int spotLimit =
                    tuning::minimumFireSpots + int(random(200) * tuning::additionalFireSpots);
                int spotCount = int(firePresence * spotLimit + .5f);
                for (int i = 0; i < spotCount; ++i) {
                    size_t edgeIndex =
                        size_t(random(201 + i * 6) * b.outline.size()) % b.outline.size();
                    size_t next = (edgeIndex + 1) % b.outline.size();
                    float along = random(202 + i * 6);
                    Vec2 a = shifted(b.outline[edgeIndex]);
                    Vec2 c = shifted(b.outline[next]);
                    Vec2 boundary{a.x + (c.x - a.x) * along, a.y + (c.y - a.y) * along};
                    float depth = .12f + random(203 + i * 6) * .7f;
                    Vec2 point{center.x + (boundary.x - center.x) * depth,
                               center.y + (boundary.y - center.y) * depth};

                    float core = random(204 + i * 6);
                    Color warm{1.0f, .08f + core * .6f, .015f + core * .06f, 1};
                    Color blueFire{.06f + core * .3f, .24f + core * .5f, 1.0f, 1};
                    Color fire{warm.r + (blueFire.r - warm.r) * blue,
                               warm.g + (blueFire.g - warm.g) * blue,
                               warm.b + (blueFire.b - warm.b) * blue, 1};
                    constexpr Color ash{.35f, .37f, .4f, 1};
                    fire.r += (ash.r - fire.r) * burned;
                    fire.g += (ash.g - fire.g) * burned;
                    fire.b += (ash.b - fire.b) * burned;
                    fire.a = fireOpacity * (.55f + random(205 + i * 6) * .45f);
                    float radius = tuning::minimumFireSpotRadius +
                                   random(206 + i * 6) * tuning::additionalFireSpotRadius;
                    mesh.circle(point, radius, fire, 9);
                }
            }

            if (critical > tuning::damageAppearanceThreshold && !b.outline.empty()) {
                Vec2 center = shifted(b.center);
                int spotLimit = tuning::minimumDamageSpots +
                                int(random(1) * tuning::additionalDamageSpots);
                int spotCount = int(critical * spotLimit + .5f);
                for (int i = 0; i < spotCount; ++i) {
                    size_t edge = size_t(random(10 + i * 5) * b.outline.size()) % b.outline.size();
                    size_t next = (edge + 1) % b.outline.size();
                    float along = random(11 + i * 5);
                    Vec2 a = shifted(b.outline[edge]);
                    Vec2 c = shifted(b.outline[next]);
                    Vec2 boundary{a.x + (c.x - a.x) * along, a.y + (c.y - a.y) * along};
                    float depth = .16f + random(12 + i * 5) * .53f;
                    Vec2 point{center.x + (boundary.x - center.x) * depth,
                               center.y + (boundary.y - center.y) * depth};
                    float shade = .07f + random(13 + i * 5) * .09f;
                    Color spot{shade * .85f, shade, shade * .82f,
                               (.2f + random(14 + i * 5) * .3f) * critical};
                    mesh.circle(point, .025f + random(15 + i * 5) * .075f, spot, 9);
                }

                int crackLimit =
                    tuning::minimumCracks + int(random(2) * tuning::additionalCracks);
                int crackCount = std::max(1, int(critical * crackLimit + random(3)));
                Color crack{.09f, .08f, .08f, .45f + critical * .5f};
                for (int i = 0; i < crackCount; ++i) {
                    size_t edge = size_t(random(100 + i * 8) * b.outline.size()) % b.outline.size();
                    size_t next = (edge + 1) % b.outline.size();
                    float along = random(101 + i * 8);
                    Vec2 a = shifted(b.outline[edge]);
                    Vec2 c = shifted(b.outline[next]);
                    Vec2 boundary{a.x + (c.x - a.x) * along, a.y + (c.y - a.y) * along};
                    Vec2 direction{boundary.x - center.x, boundary.y - center.y};
                    Vec2 perpendicular{-direction.y, direction.x};
                    auto pathPoint = [&](float depth, float bend) {
                        return Vec2{center.x + direction.x * depth + perpendicular.x * bend,
                                    center.y + direction.y * depth + perpendicular.y * bend};
                    };
                    float bendA = (random(102 + i * 8) - .5f) * .3f;
                    float bendB = (random(103 + i * 8) - .5f) * .24f;
                    Vec2 outer = pathPoint(.72f + random(104 + i * 8) * .18f, 0);
                    Vec2 middleA = pathPoint(.55f, bendA);
                    Vec2 middleB = pathPoint(.34f, bendB);
                    Vec2 inner = pathPoint(.15f + random(105 + i * 8) * .12f, 0);
                    float width = .022f + random(106 + i * 8) * .017f;
                    mesh.line(outer, middleA, width, crack);
                    mesh.line(middleA, middleB, width * .9f, crack);
                    mesh.line(middleB, inner, width * .75f, crack);
                    float branchSide = random(107 + i * 8) < .5f ? -.15f : .15f;
                    Vec2 branch = pathPoint(.43f + random(108 + i * 8) * .1f, branchSide);
                    mesh.line(middleA, branch, width * .65f, crack);
                }
            }

            if (critical > tuning::smokeAppearanceThreshold) {
                Vec2 center = shifted(b.center);
                for (int i = 0; i < 3; ++i) {
                    float phase = std::fmod(b.effectTime * (.35f + i * .04f) + b.id * .17f +
                                                i * .31f,
                                            1.0f);
                    float side = sinf(b.id * 4.3f + i * 2.7f) * (.18f + phase * .12f);
                    Color smoke{.22f, .23f, .25f, critical * (1.0f - phase) * .32f};
                    mesh.circle({center.x + side, center.y + .3f + phase * .85f},
                                .08f + phase * .18f, smoke);
                }
            }
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
        constexpr float multipliers[] = {1, 2, 3, 10};
        float sizeScale = multipliers[std::clamp(frame.explosionSize, 0, 3)] / 3.0f;
        for (auto &r : frame.ripples) {
            float size = (.12f + r.age * (1.5f + r.strength * .12f)) * sizeScale;
            Color color = r.color;
            color.a = (1 - r.age / tuning::impactLifetimeSeconds) * tuning::impactBaseOpacity;
            float cs = cosf(r.rotation), sn = sinf(r.rotation);
            Vec2 corners[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int i : {0, 1, 2, 0, 2, 3}) {
                float lx = corners[i].x * size, ly = corners[i].y * size;
                mesh.vertices.push_back(
                    {{r.point.x + lx * cs - ly * sn, r.point.y + lx * sn + ly * cs},
                     color,
                     corners[i],
                     r.shapeId,
                     r.age,
                     r.seed,
                     r.strength});
            }
        }
        mesh.upload();
    }
    void record(VkCommandBuffer c) override {
        pipeline.bind(c);
        mesh.draw(c);
    }
    void reloadShaders() override { pipeline.rebuild(); }
};
class ExplosionPass final : public DrawPass {
    Pipeline pipeline;
    Mesh mesh;

  public:
    explicit ExplosionPass(Vulkan &vk)
        : pipeline(vk, "mesh.vert", "explosion.frag", shaders::mesh_vert,
                   sizeof(shaders::mesh_vert) / sizeof(uint32_t), shaders::explosion_frag,
                   sizeof(shaders::explosion_frag) / sizeof(uint32_t), true),
          mesh(vk) {}
    void prepare(const RenderFrame &frame) override {
        mesh.vertices.clear();
        constexpr float multipliers[] = {1, 2, 3, 10};
        float multiplier = multipliers[std::clamp(frame.explosionSize, 0, 3)];
        for (auto &r : frame.ripples) {
            float scaledStrength = multiplier * r.strength * 100.0f;
            float size = .09f * (5.0f + .14f * scaledStrength);
            Vec2 corners[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int i : {0, 1, 2, 0, 2, 3})
                mesh.vertices.push_back({{r.point.x + corners[i].x * size,
                                          r.point.y + corners[i].y * size},
                                         {},
                                         corners[i],
                                         0,
                                         r.age,
                                         r.seed,
                                         r.strength * multiplier});
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
        if (frame.menuScreen != 0) {
            mesh.rect(-8, -4.5f, 16, 9, {.025f, .032f, .05f, .94f});
            mesh.rect(-3.5f, -2.55f, 7, 5.1f, {.075f, .095f, .14f, .98f});
            Color button{.12f, .16f, .23f, 1};
            Color label{.86f, .91f, 1};
            if (frame.menuScreen == 1) {
                text("PAUSED", -1.35f, 1.95f, .075f, label);
                for (float y : {.65f, -.25f, -1.15f, -2.05f})
                    mesh.rect(-2.5f, y, 5, .7f, button);
                text("RESUME", -.92f, 1.16f, .05f, label);
                text("SETTINGS", -1.25f, .26f, .05f, label);
                text("RESET", -.75f, -.64f, .05f, label);
                text("QUIT", -.55f, -1.54f, .05f, {1, .58f, .48f});
            } else {
                text("SETTINGS", -1.65f, 1.95f, .075f, label);
                text("VOLUME " + std::to_string(int(std::round(frame.volume * 50))), -1.35f,
                     1.31f, .045f, label);
                mesh.rect(-2.5f, .75f, 1, .7f, button);
                mesh.rect(1.5f, .75f, 1, .7f, button);
                text("-", -2.18f, 1.27f, .065f, label);
                text("+", 1.79f, 1.27f, .065f, label);

                constexpr const char *sizes[] = {"SMALL", "MEDIUM", "BIG", "ABSURD"};
                text(std::string("EXPLOSION ") + sizes[std::clamp(frame.explosionSize, 0, 3)],
                     -1.7f, .36f, .04f, {1, .78f, .25f});
                mesh.rect(-2.5f, -.2f, 1, .7f, button);
                mesh.rect(1.5f, -.2f, 1, .7f, button);
                text("-", -2.18f, .32f, .065f, label);
                text("+", 1.79f, .32f, .065f, label);

                mesh.rect(-2.5f, -1.15f, 5, .7f, button);
                text(std::string("SOUND ") + (frame.muted ? "OFF" : "ON"), -.98f, -.63f,
                     .05f, label);
                mesh.rect(-2.5f, -2.1f, 5, .7f, button);
                text("BACK", -.55f, -1.58f, .05f, label);
            }
            mesh.upload();
            return;
        }
        text("SCORE " + std::to_string(frame.score), 4.6f, 5.1f, .035f, {.85f, .91f, 1});
        mesh.rect(-8.0f, 4.65f, 2.15f, .7f, {.1f, .14f, .21f, .92f});
        text("PAUSE", -7.72f, 5.16f, .05f, {.86f, .91f, 1});
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
    vk.addPass(std::make_unique<ExplosionPass>(vk));
    vk.addPass(std::make_unique<ImpactPass>(vk));
    vk.addPass(std::make_unique<HudPass>(vk));
}
} // namespace toy
