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
        //
        // Get the GLSL shader source code
        //
        std::ifstream readFileStream(sourceCodePath, std::ios::in);
        if (!readFileStream.is_open())
        {
            std::cerr << "ERROR: shader source file " << sourceCodePath << " could not be read. File does not exist, osoraku" << std::endl;
            return false;
        }

        std::string sourceCode;
        while (!readFileStream.eof())
        {
            std::string line = "";
            std::getline(readFileStream, line);
            sourceCode.append(line + "\n");
        }
        readFileStream.close();

        //
        // Decipher the shader stage via filename
        //
        if (!sourceCodePath.has_extension())
        {
            std::cerr << "ERROR: shader source file " << sourceCodePath << " does not have an extension!" << std::endl;
            return false;
        }

        glslang_stage_t stage;

        const auto& ext = sourceCodePath.extension();
        if (ext.compare(".vert") == 0)
            stage = GLSLANG_STAGE_VERTEX;
        else if (ext.compare(".tesc") == 0)
            stage = GLSLANG_STAGE_TESSCONTROL;
        else if (ext.compare(".tese") == 0)
            stage = GLSLANG_STAGE_TESSEVALUATION;
        else if (ext.compare(".geom") == 0)
            stage = GLSLANG_STAGE_GEOMETRY;
        else if (ext.compare(".frag") == 0)
            stage = GLSLANG_STAGE_FRAGMENT;
        else if (ext.compare(".comp") == 0)
            stage = GLSLANG_STAGE_COMPUTE;
        else if (ext.compare(".rgen") == 0)
            stage = GLSLANG_STAGE_RAYGEN;
        else if (ext.compare(".rint") == 0)
            stage = GLSLANG_STAGE_INTERSECT;
        else if (ext.compare(".rahit") == 0)
            stage = GLSLANG_STAGE_ANYHIT;
        else if (ext.compare(".rchit") == 0)
            stage = GLSLANG_STAGE_CLOSESTHIT;
        else if (ext.compare(".rmiss") == 0)
            stage = GLSLANG_STAGE_MISS;
        else if (ext.compare(".rcall") == 0)
            stage = GLSLANG_STAGE_CALLABLE;
        else if (ext.compare(".mesh") == 0)
            stage = GLSLANG_STAGE_MESH;
        else if (ext.compare(".task") == 0)
            stage = GLSLANG_STAGE_TASK;
        else
        {
            std::cerr << "ERROR: unknown stage with shader source code path: " << ext << std::endl;
            return false;
        }

        //
        // Compile the GLSL shader source into SPIRV
        //
        const glslang_input_t input = {
            .language = GLSLANG_SOURCE_GLSL,
            .stage = stage,
            .client = GLSLANG_CLIENT_VULKAN,
            .client_version = GLSLANG_TARGET_VULKAN_1_3,
            .target_language = GLSLANG_TARGET_SPV,
            .target_language_version = GLSLANG_TARGET_SPV_1_6,
            .code = sourceCode.c_str(),
            .default_version = 110,		// @NOTE: By default we're using opengl 4.5.0 glsl code... don't know what that means for some kind of gl es implementation tho  -Timo
            .default_profile = GLSLANG_NO_PROFILE,
            .force_default_version_and_profile = false,
            .forward_compatible = false,
            .messages = GLSLANG_MSG_DEFAULT_BIT,
            .resource = &defaultResourceLimits
        };

        glslang_initialize_process();

        glslang_shader_t* shader = nullptr;
        if (!(shader = glslang_shader_create(&input)))
        {
            std::cerr << "ERROR: GLSL shader creation failed:" << std::endl
                << "Log:\t" << glslang_shader_get_info_log(shader) << std::endl
                << "Debug Log:\t" << glslang_shader_get_info_debug_log(shader) << std::endl;
            glslang_finalize_process();
            return false;
        }

        // Preprocessing
        if (!glslang_shader_preprocess(shader, &input))
        {
            std::cerr << "ERROR: GLSL preprocessing failed:" << std::endl
                << "Log:\t" << glslang_shader_get_info_log(shader) << std::endl
                << "Debug Log:\t" << glslang_shader_get_info_debug_log(shader) << std::endl;
            glslang_finalize_process();
            return false;
        }

        // Parse into tree representation in compiler
        if (!glslang_shader_parse(shader, &input))
        {
            std::cerr << "ERROR: GLSL parsing failed:" << std::endl
                << "Log:\t" << glslang_shader_get_info_log(shader) << std::endl
                << "Debug Log:\t" << glslang_shader_get_info_debug_log(shader) << std::endl
                << "Preprocessed Code:\t" << glslang_shader_get_preprocessed_code(shader) << std::endl;
            glslang_finalize_process();
            return false;
        }

        // Link to program to generate binary code
        glslang_program_t* program = glslang_program_create();
        glslang_program_add_shader(program, shader);
        int messages = GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT;
        if (!glslang_program_link(program, messages))
        {
            std::cerr << "ERROR: GLSL linking failed:" << std::endl
                << "Log:\t" << glslang_shader_get_info_log(shader) << std::endl
                << "Debug Log:\t" << glslang_shader_get_info_debug_log(shader) << std::endl;
            glslang_finalize_process();
            return false;
        }

        // Generate SPIRV binary code
        glslang_program_SPIRV_generate(program, stage);

        std::vector<uint32_t> spirvBuffer;
        spirvBuffer.resize(glslang_program_SPIRV_get_size(program));
        glslang_program_SPIRV_get(program, spirvBuffer.data());

        // Check for lingering messages and cleanup
        const char* spirvMessages = glslang_program_SPIRV_get_messages(program);
        if (spirvMessages)
            std::cout << "Messages while generating SPIRV:" << std::endl << spirvMessages << std::endl;

        glslang_program_delete(program);
        glslang_shader_delete(shader);

        glslang_finalize_process();

        //
        // Write the generated binary to .spv file
        //
        auto spvPath = sourceCodePath;
        spvPath += ".spv";

        std::ofstream writeFileStream(spvPath, std::ios::out | std::ios::binary);
        if (!writeFileStream)
        {
            std::cerr << "ERROR: cannot open file " << spvPath << " for writing!" << std::endl;
            return false;
        }

        writeFileStream.write((const char*)spirvBuffer.data(), spirvBuffer.size() * sizeof(uint32_t));
        writeFileStream.close();

        if (!writeFileStream.good())
        {
            std::cerr << "ERROR: an error occurred when writing to file " << spvPath << std::endl;
            return false;
        }

        return true;
    }
}

#endif