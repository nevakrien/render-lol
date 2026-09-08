#include "audio.hpp"
#include "gameplay_tuning.hpp"
#include "physics.hpp"
#include "render/passes.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#ifdef __ANDROID__
#include <sys/stat.h>
#endif

namespace {
struct AppSettings {
    float volume = 1.0f;
    bool muted = false;
    int explosionSize = 2;
};

std::string settingsPath() {
#ifdef __ANDROID__
    if (const char *path = SDL_GetAndroidInternalStoragePath())
        return std::string(path) + "/settings.ini";
#endif
    if (const char *path = SDL_GetBasePath())
        return std::string(path) + "settings.ini";
    return "settings.ini";
}

std::string scorePath() {
#ifdef __ANDROID__
    if (const char *path = SDL_GetAndroidInternalStoragePath())
        return std::string(path) + "/score.bin";
#endif
    if (const char *path = SDL_GetBasePath())
        return std::string(path) + "score.bin";
    return "score.bin";
}

AppSettings loadSettings(const std::string &path) {
    AppSettings settings;
    std::ifstream input(path);
    std::string key;
    while (input >> key) {
        if (key == "volume")
            input >> settings.volume;
        else if (key == "muted")
            input >> settings.muted;
        else if (key == "explosion_size")
            input >> settings.explosionSize;
        else {
            std::string ignored;
            std::getline(input, ignored);
        }
    }
    settings.volume = std::clamp(settings.volume, 0.0f, 2.0f);
    settings.explosionSize = std::clamp(settings.explosionSize, 0, 3);
    return settings;
}

void saveSettings(const std::string &path, const AppSettings &settings) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        SDL_Log("Could not save settings to %s", path.c_str());
        return;
    }
    output << "volume " << settings.volume << '\n'
           << "muted " << settings.muted << '\n'
           << "explosion_size " << settings.explosionSize << '\n';
}

uint64_t loadScore(const std::string &path) {
    uint64_t score = 0;
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char *>(&score), sizeof(score));
    return input.gcount() == sizeof(score) ? score : 0;
}

void saveScore(const std::string &path, uint64_t score) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        SDL_Log("Could not save score to %s", path.c_str());
        return;
    }
    output.write(reinterpret_cast<const char *>(&score), sizeof(score));
}
} // namespace

int main(int argc, char **argv) {
    bool validation = false, smoke = false;
    int frameLimit = 0;
    std::string capture, shaderDir;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--validate")
            validation = true;
        else if (arg == "--shaders" && i + 1 < argc)
            shaderDir = argv[++i];
        else if (arg == "--smoke") {
            smoke = true;
            frameLimit = 240;
        } else if (arg == "--capture" && i + 1 < argc)
            capture = argv[++i];
        else if (arg == "--frames" && i + 1 < argc) {
            try {
                frameLimit = std::stoi(argv[++i]);
            } catch (...) {
                frameLimit = -1;
            }
            if (frameLimit <= 0) {
                std::cerr << "--frames requires a positive count\n";
                return 1;
            }
        } else {
            std::cout << "Usage: render-lol [--validate] [--smoke] [--frames N] "
                         "[--capture frame.ppm] [--shaders DIR]\n";
            return arg == "--help" ? 0 : 1;
        }
    }
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Landscape");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL: %s", SDL_GetError());
        return 1;
    }
    int result = 0;
    try {
        int winW = 1152, winH = 704;
#ifdef __ANDROID__
        SDL_Rect bounds;
        if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &bounds)) {
            winW = bounds.w;
            winH = bounds.h;
        }
#endif
        auto window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>(
            SDL_CreateWindow("render-lol", winW, winH,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                 SDL_WINDOW_HIGH_PIXEL_DENSITY),
            SDL_DestroyWindow);
        if (!window)
            throw std::runtime_error(SDL_GetError());
#ifdef __ANDROID__
        SDL_SetWindowFullscreen(window.get(), true);
#endif
#ifndef RUNTIME_SHADER_DIR
#define RUNTIME_SHADER_DIR ""
#endif
        std::string shaders = shaderDir.empty() ? RUNTIME_SHADER_DIR : shaderDir;
#ifdef __ANDROID__
        if (shaders.empty()) {
            const char *internal = SDL_GetAndroidInternalStoragePath();
            if (internal) {
                shaders = std::string(internal) + "/shaders";
                mkdir(shaders.c_str(), 0755);
            }
        }
#endif
        toy::Vulkan renderer(window.get(), validation, shaders);
        toy::installPasses(renderer);
        toy::Physics physics;
        toy::Audio audio;
        toy::RenderFrame frame;
        const std::string configPath = settingsPath();
        const std::string savedScorePath = scorePath();
        AppSettings settings = loadSettings(configPath);
        physics.setScore(loadScore(savedScorePath));
        audio.setVolume(settings.volume);
        if (settings.muted)
            audio.toggleMute();
        saveSettings(configPath, settings);
        bool running = true, paused = false, gravity = false, background = false;
        int menuScreen = 0; // 0: game, 1: pause, 2: settings
        int explosionSize = settings.explosionSize;
        SDL_FingerID finger = 0;
        bool touchActive = false;
        toy::Vec2 pointer{};
        int frames = 0, spawnNumber = 0;
        double accumulator = 0;
        Uint64 last = SDL_GetPerformanceCounter();
        const double frequency = double(SDL_GetPerformanceFrequency());
        auto map = [&](float x, float y) {
            int w = 0, h = 0;
            SDL_GetWindowSize(window.get(), &w, &h);
            return toy::screenToWorld(x, y, w, h);
        };
        auto title = [&]() {
            std::string text = "render-lol | " +
                               std::string(paused || menuScreen ? "PAUSED" : "playing") +
                               " | gravity " +
                                (gravity ? "on" : "off") + " | audio " +
                                (audio.muted() ? "off" : "on") + " | volume " +
                                std::to_string(int(audio.volume() * 50)) + "%";
            SDL_SetWindowTitle(window.get(), text.c_str());
        };
        auto inside = [](toy::Vec2 p, float x, float y, float w, float h) {
            return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
        };
        auto handleUi = [&](toy::Vec2 p) {
            bool settingsChanged = false;
            if (menuScreen == 0) {
                if (!inside(p, -8.0f, 4.65f, 2.15f, .7f))
                    return false;
                menuScreen = 1;
                physics.endDrag();
            } else if (menuScreen == 1) {
                if (inside(p, -2.5f, .65f, 5, .7f)) {
                    menuScreen = 0;
                    paused = false;
                } else if (inside(p, -2.5f, -.25f, 5, .7f))
                    menuScreen = 2;
                else if (inside(p, -2.5f, -1.15f, 5, .7f)) {
                    physics.reset();
                    gravity = false;
                    paused = false;
                    frame.ripples.clear();
                    menuScreen = 0;
                } else if (inside(p, -2.5f, -2.05f, 5, .7f))
                    running = false;
                else
                    return false;
            } else {
                if (inside(p, -2.5f, .75f, 1.0f, .7f)) {
                    audio.setVolume(audio.volume() - .2f);
                    settingsChanged = true;
                } else if (inside(p, 1.5f, .75f, 1.0f, .7f)) {
                    audio.setVolume(audio.volume() + .2f);
                    settingsChanged = true;
                } else if (inside(p, -2.5f, -.2f, 1.0f, .7f)) {
                    explosionSize = std::max(0, explosionSize - 1);
                    settingsChanged = true;
                } else if (inside(p, 1.5f, -.2f, 1.0f, .7f)) {
                    explosionSize = std::min(3, explosionSize + 1);
                    settingsChanged = true;
                } else if (inside(p, -2.5f, -1.15f, 5, .7f)) {
                    audio.toggleMute();
                    settingsChanged = true;
                } else if (inside(p, -2.5f, -2.1f, 5, .7f))
                    menuScreen = 1;
                else
                    return false;
            }
            if (settingsChanged) {
                settings.volume = audio.volume();
                settings.muted = audio.muted();
                settings.explosionSize = explosionSize;
                saveSettings(configPath, settings);
            }
            title();
            return true;
        };
        title();
        while (running && (!frameLimit || frames < frameLimit)) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                    running = false;
                else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                         e.type == SDL_EVENT_WINDOW_RESIZED)
                    renderer.resize();
                else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    physics.endDrag();
                    touchActive = false;
                } else if (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
                    background = true;
                    physics.endDrag();
                    touchActive = false;
                } else if (e.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
                    background = false;
                    renderer.resize();
                } else if (e.type == SDL_EVENT_MOUSE_MOTION &&
                           e.motion.which != SDL_TOUCH_MOUSEID) {
                    pointer = map(e.motion.x, e.motion.y);
                    if (!touchActive)
                        physics.moveDrag(pointer);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                            e.button.which != SDL_TOUCH_MOUSEID &&
                            e.button.button == SDL_BUTTON_LEFT && !touchActive) {
                    pointer = map(e.button.x, e.button.y);
                    if (!handleUi(pointer) && menuScreen == 0)
                        physics.beginDrag(pointer);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                           e.button.which != SDL_TOUCH_MOUSEID &&
                           e.button.button == SDL_BUTTON_LEFT && !touchActive)
                    physics.endDrag();
                else if (e.type == SDL_EVENT_FINGER_DOWN && !touchActive) {
                    int w, h;
                    SDL_GetWindowSize(window.get(), &w, &h);
                    pointer = map(e.tfinger.x * w, e.tfinger.y * h);
                    if (handleUi(pointer))
                        continue;
                    if (menuScreen != 0)
                        continue;
                    touchActive = true;
                    finger = e.tfinger.fingerID;
                    physics.beginDrag(pointer);
                } else if (e.type == SDL_EVENT_FINGER_MOTION && touchActive &&
                           e.tfinger.fingerID == finger) {
                    int w, h;
                    SDL_GetWindowSize(window.get(), &w, &h);
                    pointer = map(e.tfinger.x * w, e.tfinger.y * h);
                    physics.moveDrag(pointer);
                } else if (e.type == SDL_EVENT_FINGER_UP && touchActive &&
                           e.tfinger.fingerID == finger) {
                    physics.endDrag();
                    touchActive = false;
                } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                    if (e.key.key == SDLK_ESCAPE) {
                        menuScreen = menuScreen == 2 ? 1 : (menuScreen == 1 ? 0 : 1);
                        physics.endDrag();
                        title();
                        continue;
                    }
                    if (menuScreen != 0)
                        continue;
                    switch (e.key.key) {
                    case SDLK_SPACE:
                        paused = !paused;
                        break;
                    case SDLK_G:
                        gravity = !gravity;
                        physics.setGravity(gravity);
                        break;
                    case SDLK_R:
                        physics.reset();
                        gravity = false;
                        frame.ripples.clear();
                        break;
                    case SDLK_W:
                        frame.wireframe = !frame.wireframe;
                        break;
                    case SDLK_M:
                        audio.toggleMute();
                        settings.muted = audio.muted();
                        saveSettings(configPath, settings);
                        break;
                    case SDLK_1:
                    case SDLK_2:
                    case SDLK_3: {
                        toy::Shape shape = e.key.key == SDLK_1   ? toy::Shape::Circle
                                           : e.key.key == SDLK_2 ? toy::Shape::Triangle
                                                                 : toy::Shape::Rectangle;
                        // Find an unoccupied patch, avoiding explosive initial overlaps.
                        auto bodies = physics.snapshot();
                        bool spawned = false;
                        for (int attempt = 0; attempt < 40 && !spawned; ++attempt) {
                            int cell = (spawnNumber + attempt) % 40;
                            toy::Vec2 p{-6.5f + (cell % 8) * 1.85f, -3.f + (cell / 8) * 1.5f};
                            bool free = true;
                            for (auto &b : bodies) {
                                float dx = p.x - b.center.x, dy = p.y - b.center.y;
                                if (dx * dx + dy * dy < 4.4f)
                                    free = false;
                            }
                            if (free) {
                                physics.spawn(shape, p, {1.2f, -.8f});
                                spawnNumber = cell + 1;
                                spawned = true;
                            }
                        }
                        break;
                    }
                    default:
                        break;
                    }
                    title();
                }
            }
            if (!running)
                break;
            Uint64 now = SDL_GetPerformanceCounter();
            double dt = std::min(.05, double(now - last) / frequency);
            last = now;
            if (background || (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED)) {
                accumulator = 0;
                SDL_Delay(20);
                continue;
            }
            if (smoke) {
                dt = 1.0 / 60;
                if (frames == 25) {
                    auto b = physics.snapshot().front();
                    physics.beginDrag(b.center);
                }
                if (frames >= 25 && frames < 70)
                    physics.moveDrag({-3.f + (frames - 25) * .04f, 2.5f});
                if (frames == 70)
                    physics.endDrag();
                if (frames == 90)
                    SDL_SetWindowSize(window.get(), 900, 700);
                if (frames == 125) {
                    auto b = physics.snapshot()[1];
                    physics.beginDrag(b.center);
                }
                if (frames >= 125 && frames < 165)
                    physics.moveDrag({1.5f, 2.8f});
                if (frames == 165)
                    physics.endDrag();
                if (frames == 180)
                    SDL_SetWindowSize(window.get(), 1152, 704);
            }
            if (!paused && menuScreen == 0) {
                accumulator += dt;
                while (accumulator >= toy::Physics::stepSize) {
                    physics.step();
                    accumulator -= toy::Physics::stepSize;
                    auto hits = physics.takeImpacts();
                    audio.play(hits);
                    for (auto &hit : hits) {
                        if (hit.strength > toy::tuning::visibleImpactThreshold &&
                            frame.ripples.size() < 128)
                            frame.ripples.push_back({hit.point, hit.color, 0, hit.strength,
                                                     hit.shapeId, hit.rotation, hit.seed});
                    }
                }
                for (auto &ripple : frame.ripples)
                    ripple.age += float(dt);
                frame.ripples.erase(std::remove_if(frame.ripples.begin(), frame.ripples.end(),
                                                   [](auto &r) {
                                                       return r.age >=
                                                              toy::tuning::impactLifetimeSeconds;
                                                   }),
                                     frame.ripples.end());
            } else
                accumulator = 0;
            frame.bodies = physics.snapshot();
            frame.score = physics.score();
            saveScore(savedScorePath, frame.score);
            frame.menuScreen = menuScreen;
            frame.volume = audio.volume();
            frame.muted = audio.muted();
            frame.explosionSize = explosionSize;
            bool captureNow = !capture.empty() && ((frameLimit && frames == frameLimit - 1) ||
                                                   (!frameLimit && frames == 0));
            if (renderer.draw(frame, captureNow ? capture : "")) {
                ++frames;
                if (captureNow)
                    capture.clear();
            }
            if (smoke && !physics.healthy())
                throw std::runtime_error("Physics became unstable during desktop smoke test");
        }
        if (renderer.validationErrors())
            throw std::runtime_error("Vulkan validation reported errors");
        if (smoke)
            SDL_Log("Desktop smoke passed: %d frames, score %llu", frames,
                    static_cast<unsigned long long>(physics.score()));
    } catch (const std::exception &e) {
        SDL_Log("Error: %s", e.what());
        result = 1;
    }
    SDL_Quit();
    return result;
}
