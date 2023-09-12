#version 460

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outNearFieldColorWithCoCAlpha;
layout (location = 1) out vec4 outFarFieldColorWithCoCAlpha;


layout (push_constant) uniform GatherDOFParams
{
	float sampleRadiusMultiplier;
	float oneOverArbitraryResExtentX;
	float oneOverArbitraryResExtentY;
} gatherDOFParams;

layout (set = 0, binding = 0) uniform sampler2D nearFieldImage;
layout (set = 0, binding = 1) uniform sampler2D farFieldImage;
layout (set = 0, binding = 2) uniform sampler2D nearFieldDownsizedCoCImage;


#define NUM_SAMPLE_POINTS 36
#define BOKEH_SAMPLE_MULTIPLIER (1.0 / NUM_SAMPLE_POINTS)

#if 0
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
#else
const vec2 bokehFilter[NUM_SAMPLE_POINTS] = vec2[](  // 6x6 6-edge shape sample points. (Generated from `etc/calc_sample_points_bokeh.py`)
	vec2(-0.6339750881227882, -0.6339743243277791),
	vec2(-0.866025379437842, -0.4412624284684379),
	vec2(-0.8660253935510618, -0.1371650989014556),
	vec2(-0.8660254062827791, 0.13716501851662002),
	vec2(-0.8660254203959992, 0.4412623480836023),
	vec2(-0.6339738039033809, 0.6339750409010357),
	vec2(-0.393707130312402, -0.7726930746234514),
	vec2(-0.3803853054105913, -0.38038444879443045),
	vec2(-0.5196152334884269, -0.13923072070520276),
	vec2(-0.5196152464118776, 0.13923067247430165),
	vec2(-0.3803840298067675, 0.380385170341933),
	vec2(-0.39370641802846706, 0.7726934907252797),
	vec2(-0.14511476168948914, -0.9162179250557586),
	vec2(-0.1392306563281321, -0.519615130461564),
	vec2(-0.12679552270043246, -0.1267945732599052),
	vec2(-0.12679425571219183, 0.12679529978165358),
	vec2(-0.13923023543464344, 0.5196153883872209),
	vec2(-0.1451144903435477, 0.9162181173478277),
	vec2(0.1451148032037205, -0.9162179545331142),
	vec2(0.139230680443625, -0.5196151389224831),
	vec2(0.1267955768096386, -0.12679453953278869),
	vec2(0.12679426590451207, 0.12679529638418935),
	vec2(0.13923025955005347, 0.5196153670028207),
	vec2(0.14511453387305864, 0.9162180744008326),
	vec2(0.3937071626889476, -0.7726930632275603),
	vec2(0.3803854677548758, -0.3803843476035365),
	vec2(0.519615239950153, -0.13923021058379492),
	vec2(0.519615239950153, 0.1392302105837951),
	vec2(0.3803840603838292, 0.38038516014956003),
	vec2(0.3937064573736, 0.7726934655770736),
	vec2(0.6339750960066398, -0.6339742824692115),
	vec2(0.8660253999169214, -0.44126136016537687),
	vec2(0.8660253999169214, -0.1371647798141503),
	vec2(0.8660253999169214, 0.13716477981415043),
	vec2(0.8660253999169214, 0.44126136016537704),
	vec2(0.6339738548651843, 0.6339750239137543)
);
#endif


const uint MODE_NEARFIELD = 0;
const uint MODE_FARFIELD = 1;

vec4 gatherDOF(vec4 colorAndCoC, float sampleRadius, uint mode)
{
	vec4 accumulatedColor = vec4(0.0);
	int numColors = 0;
	for (int i = 0; i < NUM_SAMPLE_POINTS; i++)
	{
		vec2 sampleUV =
			inUV +
			bokehFilter[i] * vec2(gatherDOFParams.oneOverArbitraryResExtentX, gatherDOFParams.oneOverArbitraryResExtentY) * sampleRadius;
		vec4 sampled;
		if (mode == MODE_NEARFIELD)
			sampled = texture(nearFieldImage, sampleUV);
		else
			sampled = texture(farFieldImage, sampleUV);

		accumulatedColor.rgb += sampled.rgb;
		accumulatedColor.a += sampled.a * BOKEH_SAMPLE_MULTIPLIER;
		if (sampled.a > 0.0)
			numColors++;
	}

	if (numColors > 0)
	{
		accumulatedColor.rgb = accumulatedColor.rgb / numColors;
		accumulatedColor.a = clamp(accumulatedColor.a, 0.0, 1.0);
	}

	return accumulatedColor;
}

void main()
{
	vec4 nearField = texture(nearFieldImage, inUV);
	vec4 farField = texture(farFieldImage, inUV);
	float nearFieldCoC = texture(nearFieldDownsizedCoCImage, inUV).r;

	if (nearFieldCoC > 0.0)
	{
		// nearFieldCoC = clamp(nearFieldCoC * 2.0, 0.0, 1.0);  // For compensating with the blur.
		nearField = gatherDOF(nearField, nearFieldCoC * gatherDOFParams.sampleRadiusMultiplier, MODE_NEARFIELD);
		nearField.a = clamp(nearField.a * 2.0, 0.0, 1.0);  // @NOTE: since a blur is essentially going on, I want to keep the outside of the blur (NOTE that the midline of the blur, 0.5, is where the near original pixels will be, so it will make it look like the original pixels are peeking out of the blurred near field).  -Timo 2023/09/10
	}

	if (farField.a > 0.0)
	{
		farField = gatherDOF(farField, farField.a * gatherDOFParams.sampleRadiusMultiplier, MODE_FARFIELD);
	}

	outNearFieldColorWithCoCAlpha = nearField;
	outFarFieldColorWithCoCAlpha = farField;
}
