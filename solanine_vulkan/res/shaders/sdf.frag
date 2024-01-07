#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;


layout(set = 1, binding = 0) uniform sampler2D samplerColor;

layout(set = 1, binding = 1) uniform UBO
{
	vec4 fillColor;
	vec4 outlineColor;
	float outlineWidth;
	float outline;
} ubo;


void main()
{
	// @NOTE: this is for an sdf font!!!!
	//
	// @NOTE: how to create an sdf font.
	//          - Get a ttf font.
	//          - Create an sdf font by injecting your own font with @font-face css and the repo https://github.com/ShoYamanishi/SDFont
	//          - Level it so that the highest level is around 255 (highest level was around 150~160 ish from the sdf font generator)
	//          - Change the luminosity curve so that it's un-gamma-ized (take the midpoint 0.5,0.5 and move it to 0.5,0.218 in lumunosity curve editor).
	//          - Copy and transform the ttf font info into the .fnt file type.
	//          - (OPTIONALLY) extract the kerning from the ttf font with https://github.com/adobe-type-tools/kern-dump
	//          - Load it in to textmesh (dividing the width by the font pt size (default: 32)).
	//      -Timo 2023/11/07
	float dist = texture(samplerColor, inUV).r;
	float smoothWidth = fwidth(dist);
	float alpha = 0.0;
	vec3 rgb = vec3(0.0);

	if (ubo.outline > 0.0)
	{
		float w = 1.0 - ubo.outlineWidth;
		float outlineAlpha = smoothstep(w - smoothWidth, w + smoothWidth, dist);
		alpha = max(alpha, outlineAlpha);
		rgb = mix(rgb, ubo.outlineColor.rgb, outlineAlpha);
	}

	{
		float fillAlpha = smoothstep(0.5 - smoothWidth, 0.5 + smoothWidth, dist);
		alpha = max(alpha, fillAlpha);
		rgb = mix(rgb, ubo.fillColor.rgb, fillAlpha);
	}


	outFragColor = vec4(rgb, alpha);
}
