#pragma once

#include <cmath>


// @INCOMPLETE: this is very rough.... but settings from an ini file or something like that is the ideal/goal... or further?
constexpr uint32_t SHADOWMAP_DIMENSION = 1024;
constexpr uint32_t SHADOWMAP_CASCADES  = 4;

constexpr unsigned int FRAME_OVERLAP = 2;

constexpr size_t RENDER_OBJECTS_MAX_CAPACITY = 10000;
constexpr size_t INSTANCE_PTR_MAX_CAPACITY   = 100000;

constexpr size_t MAX_NUM_MAPS = 128;
constexpr size_t MAX_NUM_VOXEL_FIELD_LIGHTMAPS = 8;
constexpr size_t MAX_NUM_MATERIALS = 256;
