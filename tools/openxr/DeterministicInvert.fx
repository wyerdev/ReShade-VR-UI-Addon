// Test effect for the VR menu. The defaults give an exact inversion, so pixel checks stay deterministic;
// the settings exist to exercise the menu's sliders, colour values, whole-number values and switches.

uniform float Strength <
	ui_type = "slider";
	ui_min = 0.0; ui_max = 1.0;
	ui_label = "Invert strength";
	ui_tooltip = "1 inverts fully, 0 shows the original picture.";
> = 1.0;

uniform float Brightness <
	ui_type = "slider";
	ui_min = -0.5; ui_max = 0.5;
	ui_label = "Brightness";
> = 0.0;

uniform float Contrast <
	ui_type = "slider";
	ui_min = 0.25; ui_max = 2.0;
	ui_label = "Contrast";
> = 1.0;

uniform float3 Tint <
	ui_type = "color";
	ui_label = "Tint";
> = float3(1.0, 1.0, 1.0);

uniform int PosterizeLevels <
	ui_type = "slider";
	ui_min = 0; ui_max = 32;
	ui_label = "Posterize levels";
	ui_tooltip = "0 turns posterizing off.";
> = 0;

uniform bool SwapRedBlue <
	ui_label = "Swap red and blue";
> = false;

texture BackBuffer : COLOR;

sampler BackBufferSampler
{
	Texture = BackBuffer;
};

void FullscreenVS(uint vertex_id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0)
{
	texcoord.x = (vertex_id == 2) ? 2.0 : 0.0;
	texcoord.y = (vertex_id == 1) ? 2.0 : 0.0;
	position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 InvertPS(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target
{
	const float4 color = tex2D(BackBufferSampler, texcoord);
	// Written so that the default values add exactly zero and multiply by exactly one.
	float3 result = (1.0 - color.rgb) * Strength + color.rgb * (1.0 - Strength);
	if (Contrast != 1.0)
		result = (result - 0.5) * Contrast + 0.5;
	result += Brightness;
	result *= Tint;
	if (PosterizeLevels > 0)
		result = floor(saturate(result) * PosterizeLevels + 0.5) / PosterizeLevels;
	if (SwapRedBlue)
		result = result.bgr;
	return float4(result, color.a);
}

technique DeterministicInvert
{
	pass
	{
		VertexShader = FullscreenVS;
		PixelShader = InvertPS;
	}
}