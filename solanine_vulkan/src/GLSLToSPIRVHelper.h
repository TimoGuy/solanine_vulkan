#pragma once


#ifdef _DEVELOP

#include <iostream>
#include <fstream>
#include <filesystem>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Include/ResourceLimits.h>

namespace glslToSPIRVHelper
{
    const glslang_resource_t defaultResourceLimits = {      // @NOTE: copied from (https://github.com/KhronosGroup/glslang/blob/master/StandAlone/ResourceLimits.cpp)
        .max_lights = 32,
        .max_clip_planes = 6,
        .max_texture_units = 32,
        .max_texture_coords = 32,
        .max_vertex_attribs = 64,
        .max_vertex_uniform_components = 4096,
        .max_varying_floats = 64,
        .max_vertex_texture_image_units = 32,
        .max_combined_texture_image_units = 80,
        .max_texture_image_units = 32,
        .max_fragment_uniform_components = 4096,
        .max_draw_buffers = 32,
        .max_vertex_uniform_vectors = 128,
        .max_varying_vectors = 8,
        .max_fragment_uniform_vectors = 16,
        .max_vertex_output_vectors = 16,
        .max_fragment_input_vectors = 15,
        .min_program_texel_offset = -8,
        .max_program_texel_offset = 7,
        .max_clip_distances = 8,
        .max_compute_work_group_count_x = 65535,
        .max_compute_work_group_count_y = 65535,
        .max_compute_work_group_count_z = 65535,
        .max_compute_work_group_size_x = 1024,
        .max_compute_work_group_size_y = 1024,
        .max_compute_work_group_size_z = 64,
        .max_compute_uniform_components = 1024,
        .max_compute_texture_image_units = 16,
        .max_compute_image_uniforms = 8,
        .max_compute_atomic_counters = 8,
        .max_compute_atomic_counter_buffers = 1,
        .max_varying_components = 60,
        .max_vertex_output_components = 64,
        .max_geometry_input_components = 64,
        .max_geometry_output_components = 128,
        .max_fragment_input_components = 128,
        .max_image_units = 8,
        .max_combined_image_units_and_fragment_outputs = 8,
        .max_combined_shader_output_resources = 8,
        .max_image_samples = 0,
        .max_vertex_image_uniforms = 0,
        .max_tess_control_image_uniforms = 0,
        .max_tess_evaluation_image_uniforms = 0,
        .max_geometry_image_uniforms = 0,
        .max_fragment_image_uniforms = 8,
        .max_combined_image_uniforms = 8,
        .max_geometry_texture_image_units = 16,
        .max_geometry_output_vertices = 256,
        .max_geometry_total_output_components = 1024,
        .max_geometry_uniform_components = 1024,
        .max_geometry_varying_components = 64,
        .max_tess_control_input_components = 128,
        .max_tess_control_output_components = 128,
        .max_tess_control_texture_image_units = 16,
        .max_tess_control_uniform_components = 1024,
        .max_tess_control_total_output_components = 4096,
        .max_tess_evaluation_input_components = 128,
        .max_tess_evaluation_output_components = 128,
        .max_tess_evaluation_texture_image_units = 16,
        .max_tess_evaluation_uniform_components = 1024,
        .max_tess_patch_components = 120,
        .max_patch_vertices = 32,
        .max_tess_gen_level = 64,
        .max_viewports = 16,
        .max_vertex_atomic_counters = 0,
        .max_tess_control_atomic_counters = 0,
        .max_tess_evaluation_atomic_counters = 0,
        .max_geometry_atomic_counters = 0,
        .max_fragment_atomic_counters = 8,
        .max_combined_atomic_counters = 8,
        .max_atomic_counter_bindings = 1,
        .max_vertex_atomic_counter_buffers = 0,
        .max_tess_control_atomic_counter_buffers = 0,
        .max_tess_evaluation_atomic_counter_buffers = 0,
        .max_geometry_atomic_counter_buffers = 0,
        .max_fragment_atomic_counter_buffers = 1,
        .max_combined_atomic_counter_buffers = 1,
        .max_atomic_counter_buffer_size = 16384,
        .max_transform_feedback_buffers = 4,
        .max_transform_feedback_interleaved_components = 64,
        .max_cull_distances = 8,
        .max_combined_clip_and_cull_distances = 8,
        .max_samples = 4,
        .max_mesh_output_vertices_nv = 256,
        .max_mesh_output_primitives_nv = 512,
        .max_mesh_work_group_size_x_nv = 32,
        .max_mesh_work_group_size_y_nv = 1,
        .max_mesh_work_group_size_z_nv = 1,
        .max_task_work_group_size_x_nv = 32,
        .max_task_work_group_size_y_nv = 1,
        .max_task_work_group_size_z_nv = 1,
        .max_mesh_view_count_nv = 4,
        .maxDualSourceDrawBuffersEXT = 1,

        .limits = {
            .non_inductive_for_loops = 1,
            .while_loops = 1,
            .do_while_loops = 1,
            .general_uniform_indexing = 1,
            .general_attribute_matrix_vector_indexing = 1,
            .general_varying_indexing = 1,
            .general_sampler_indexing = 1,
            .general_variable_indexing = 1,
            .general_constant_matrix_vector_indexing = 1,
        }
    };


    bool compileGLSLShaderToSPIRV(const std::filesystem::path& sourceCodePath)
    {
        std::cout << "INFO: compiling shader source file " << sourceCodePath << " to SPIRV..." << std::endl;

        if (!std::filesystem::exists(sourceCodePath))
        {
            std::cerr << "ERROR: shader source file " << sourceCodePath << " does not exist, osoraku" << std::endl;
            std::cout << "\t\t\tFAILURE" << std::endl;
            return false;
        }

        //
        // Compile the file and save the results in a .spv file and the messages in a .log file
        //
        const static std::string compilerPath = "C:/VulkanSDK/1.3.224.1/Bin/glslc.exe";
        auto spvPath = sourceCodePath;  spvPath += ".spv";
        int compilerBit = system((compilerPath + " " + sourceCodePath.string() + " -o " + spvPath.string()).c_str());  // @NOTE: errors and output get routed to the console anyways! Yay!

        //
        // Read the .log file if there was an error
        //
        if (compilerBit != 0)
        {
            std::cout << "\t\t\tFAILURE" << std::endl;
            return false;
        }

        std::cout << "\t\t\tSUCCESS" << std::endl;
        return true;
    }
}

#endif