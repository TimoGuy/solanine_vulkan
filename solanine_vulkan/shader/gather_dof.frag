#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outNearFieldColorWithCoCAlpha;
layout (location = 1) out vec4 outFarFieldColorWithCoCAlpha;


layout (push_constant) uniform GatherDOFParams
{
	float sampleRadiusMultiplier;
	vec2 oneOverArbitraryResExtent;
} gatherDOFParams;

layout (set = 0, binding = 0) uniform sampler2D nearFieldImage;
layout (set = 0, binding = 1) uniform sampler2D farFieldImage;
layout (set = 0, binding = 1) uniform sampler2D nearFieldDownsizedCoCImage;


#define NUM_SAMPLE_POINTS 36
#define BOKEH_SAMPLE_MULTIPLIER (1.0 / (NUM_SAMPLE_POINTS + 1.0))
const vec2 bokehFilter[NUM_SAMPLE_POINTS] = vec2[](  // 6x6 5-edge shape with 12deg rotation sample points. (Generated from `etc/calc_sample_points_bokeh.py`)
	vec2(-0.44611501104609963, -0.686955969333418),
	vec2(-0.6365611600251629, -0.5154775057634131),
	vec2(-0.8476731777402233, -0.3253914124871349),
	vec2(-0.9067365874836769, -0.047519979998283),
	vec2(-0.7911911661891852, 0.2119993111895597),
	vec2(-0.6869559846297066, 0.4461153854752446),
	vec2(-0.23500303480859286, -0.8770420252631433),
	vec2(-0.26766920024403323, -0.41217340726710455),
	vec2(-0.4632739512089036, -0.23605011812600024),
	vec2(-0.519232048938938, 0.027211973296334097),
	vec2(-0.4121734848085764, 0.26766946929599766),
	vec2(-0.5714107175235458, 0.7056343303503915),
	vec2(0.047519962824745916, -0.9067367246602648),
	vec2(-0.030291499940964924, -0.5779922442574394),
	vec2(-0.08922338944309711, -0.1373908451997735),
	vec2(-0.13739098498806473, 0.0892235531153614),
	vec2(-0.26276266757623845, 0.5157014317596875),
	vec2(-0.3253910642334371, 0.8476732519560028),
	vec2(0.2935398273821506, -0.7646970652869055),
	vec2(0.22067421244342653, -0.4330970978203769),
	vec2(0.15230011048401917, -0.0989039625180157),
	vec2(0.09890355568929557, 0.15229947508299388),
	vec2(0.025439045656346082, 0.4854102164859654),
	vec2(-0.04286859385516632, 0.8179789490530025),
	vec2(0.515477544426832, -0.6365612426457546),
	vec2(0.4568989451922215, -0.2967126879115018),
	vec2(0.5018448529668484, -0.02630032732781246),
	vec2(0.4477607790380152, 0.22814523537022977),
	vec2(0.29671225900498566, 0.4568982579296796),
	vec2(0.21199861805644116, 0.7911913304242488),
	vec2(0.761497472792535, -0.49452154643265084),
	vec2(0.8770421706386646, -0.2350022062612793),
	vec2(0.8179789046123497, 0.04286861350398243),
	vec2(0.7646971487290897, 0.29353956656671293),
	vec2(0.7056338827027746, 0.5714103863319747),
	vec2(0.4945209623236479, 0.7614970407760532)
);


const uint MODE_NEARFIELD = 0;
const uint MODE_FARFIELD = 1;

vec4 gatherDOF(vec4 colorAndCoC, float sampleRadius, uint mode)
{
	vec4 accumulatedColor = colorAndCoC * BOKEH_SAMPLE_MULTIPLIER;
	for (int i = 0; i < NUM_SAMPLE_POINTS; i++)
	{
		vec2 sampleUV = inUV + bokehFilter[i] * sampleRadius;
		if (mode == MODE_NEARFIELD)
			accumulatedColor += texture(nearFieldImage, sampleUV) * BOKEH_SAMPLE_MULTIPLIER;
		else
			accumulatedColor += texture(farFieldImage, sampleUV) * BOKEH_SAMPLE_MULTIPLIER;
	}

	return accumulatedColor;
}

void main()
{
	vec4 nearField = texture(nearFieldImage, inUV);
	vec4 farField = texture(farFieldImage, inUV);

	if (nearField.a > 0.0)
	{
		float nearFieldCoC = texture(nearFieldDownsizedCoCImage, inUV).r;
		nearField = gatherDOF(nearField, nearFieldCoC * gatherDOFParams.sampleRadiusMultiplier, MODE_NEARFIELD);
	}

	if (farField.a > 0.0)
	{
		farField = gatherDOF(farField, farField.a * gatherDOFParams.sampleRadiusMultiplier, MODE_FARFIELD);
	}

	outNearFieldColorWithCoCAlpha = nearField;
	outFarFieldColorWithCoCAlpha = farField;
}
