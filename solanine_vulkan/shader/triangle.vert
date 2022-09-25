#version 450
#extension GL_KHR_vulkan_glsl : enable


void main()
{
	// Const array of positions for the triangle
	const vec3 positions[3] = vec3[3](
		vec3( 1.0,  1.0,  0.0),
		vec3(-1.0,  1.0,  0.0),
		vec3( 0.0, -1.0,  0.0)
	);

	// Output the position of each vertex
	gl_Position = vec4(positions[gl_VertexIndex], 1.0);
}
