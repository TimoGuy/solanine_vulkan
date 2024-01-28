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
#include <set>
#include <unordered_set>
#include <mutex>
#include <string>
#include <chrono>
#include <random>
#include <utility>
#include <thread>
#include <assert.h>  // Maybe....?
#include <atomic>

// https://stackoverflow.com/questions/6847360/error-lnk2019-unresolved-external-symbol-main-referenced-in-function-tmainc
#include <SDL2/SDL.h>
#undef main
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <ktx.h>
#include <ktxvulkan.h>

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

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/PhysicsScene.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>  // @TODO: don't need this.
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Math/Mat44.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/MathTypes.h>

#ifdef _DEVELOP
    #define HAWSOO_CRASH() abort()
    #define HAWSOO_PRINT_VEC3(v) std::cout << "X: " << v[0] << "\tY: " << v[1] << "\tZ: " << v[2] << std::endl
#else
    #define HAWSOO_CRASH()
    #define HAWSOO_PRINT_VEC3(v)
#endif

#if TRACY_ENABLE
    #include "tracy/tracy/Tracy.hpp"
    #include "tracy/tracy/TracyVulkan.hpp"
    #include "tracy/common/TracySystem.hpp"
#endif
