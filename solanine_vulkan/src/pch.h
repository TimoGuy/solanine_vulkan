#pragma once

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <deque>
#include <format>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <mutex>
#include <string>
#include <chrono>
#include <random>
#include <utility>
#include <thread>
#include <assert.h>  // Maybe....?

#include <SDL2/SDL.h>
#undef main
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <stb_image.h>
#include <stb_image_write.h>

#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/for_each.hpp>

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>
#include <cglm/struct.h>

#include <fmod_studio.hpp>
#include <fmod.hpp>
#include <fmod_errors.h>

#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/implot.h"
#include "imgui/ImGuizmo.h"

// @TODO: include the Jolt stuff! (need to rename Character.h/.cpp)
