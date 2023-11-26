#pragma once


// @INCOMPLETE: this is very rough.... but settings from an ini file or something like that is the ideal/goal... or further?
constexpr uint32_t SHADOWMAP_DIMENSION             = 1024;  // @NOTE: for higher quality shadows, 2048 is really the highest you'll have to go imo  -Timo 2023/08/31
constexpr uint32_t SHADOWMAP_CASCADES              = 4;
constexpr uint32_t SHADOWMAP_JITTERMAP_DIMENSION_X = 256;
constexpr uint32_t SHADOWMAP_JITTERMAP_DIMENSION_Y = 144;

// @NOTE: for noisiness comparison, look at files in `etc/soft_shadow_sample_size_pixel_peeping`.
// constexpr uint32_t SHADOWMAP_JITTERMAP_DIMENSION_Z = 64 / 2;  // 8x8 samples (~1.0ms on 2080 Ti)
// constexpr uint32_t SHADOWMAP_JITTERMAP_DIMENSION_Z = 36 / 2;  // 6x6 samples (??ms on 2080 Ti)
// constexpr uint32_t SHADOWMAP_JITTERMAP_DIMENSION_Z = 16 / 2;  // 4x4 samples (??ms on 2080 Ti)
constexpr uint32_t SHADOWMAP_JITTERMAP_DIMENSION_Z = 4 / 2;  // 2x2 samples (??ms on 2080 Ti)    @NOTE: I think the smaller the better, and then just using a shadow denoiser is the best thing to do. Spending 1.0ms on shadow "PCF+" rendering is NG, I believe.  -Timo 2023/10/09

constexpr unsigned int FRAME_OVERLAP = 2;

constexpr size_t RENDER_OBJECTS_MAX_CAPACITY = 10000;
constexpr size_t INSTANCE_PTR_MAX_CAPACITY   = 100000;

constexpr size_t MAX_NUM_MAPS = 128;
constexpr size_t MAX_NUM_VOXEL_FIELD_LIGHTMAPS = 8;
constexpr size_t MAX_NUM_MATERIALS = 256;
