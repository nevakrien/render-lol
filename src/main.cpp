#include "audio.hpp"
#include "physics.hpp"
#include "render/passes.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
    bool validation = false, smoke = false;
    int frameLimit = 0;
    std::string capture;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--validate")
            validation = true;
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
                         "[--capture frame.ppm]\n";
            return arg == "--help" ? 0 : 1;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL: %s", SDL_GetError());
        return 1;
    }
    int result = 0;
    try {
        auto window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>(
            SDL_CreateWindow("render-lol", 1152, 704,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                 SDL_WINDOW_HIGH_PIXEL_DENSITY),
            SDL_DestroyWindow);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        toy::Vulkan renderer(window.get(), validation);
        toy::installPasses(renderer);
        toy::Physics physics;
        toy::Audio audio;
        toy::RenderFrame frame;
        bool running = true, paused = false, gravity = false, background = false;
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
            std::string text = "render-lol | " + std::string(paused ? "PAUSED" : "playing") +
                               " | gravity " +
                               (gravity ? "on" : "off") + " | audio " +
                               (audio.muted() ? "off" : "on");
            SDL_SetWindowTitle(window.get(), text.c_str());
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
                    physics.beginDrag(pointer);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                           e.button.which != SDL_TOUCH_MOUSEID &&
                           e.button.button == SDL_BUTTON_LEFT && !touchActive)
                    physics.endDrag();
                else if (e.type == SDL_EVENT_FINGER_DOWN && !touchActive) {
                    int w, h;
                    SDL_GetWindowSize(window.get(), &w, &h);
                    pointer = map(e.tfinger.x * w, e.tfinger.y * h);
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
                    switch (e.key.key) {
                    case SDLK_ESCAPE:
                        running = false;
                        break;
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
            if (!paused) {
                accumulator += dt;
                while (accumulator >= toy::Physics::stepSize) {
                    physics.step();
                    accumulator -= toy::Physics::stepSize;
                    auto hits = physics.takeImpacts();
                    audio.play(hits);
                    for (auto &hit : hits) {
                        if (frame.ripples.size() < 128)
                            frame.ripples.push_back({hit.point, hit.color, 0, hit.strength});
                    }
                }
                for (auto &ripple : frame.ripples)
                    ripple.age += float(dt);
                frame.ripples.erase(std::remove_if(frame.ripples.begin(), frame.ripples.end(),
                                                   [](auto &r) { return r.age >= .6f; }),
                                    frame.ripples.end());
            } else
                accumulator = 0;
            frame.bodies = physics.snapshot();
            frame.score = physics.score();
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
