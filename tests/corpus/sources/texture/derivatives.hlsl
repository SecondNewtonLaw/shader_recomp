// Derivatives: fine, coarse and filtered.

float4 main(float2 uv : TEXCOORD0) : SV_Target
{
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
    float width = fwidth(uv.x);

    float2 fine = ddx_fine(uv);
    float2 coarse = ddy_coarse(uv);

    return float4(dx.x, dy.y, width, fine.x + coarse.y);
}
