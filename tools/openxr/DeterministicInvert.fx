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
	return float4(1.0 - color.rgb, color.a);
}

technique DeterministicInvert
{
	pass
	{
		VertexShader = FullscreenVS;
		PixelShader = InvertPS;
	}
}