float4x4 matWorldViewProj;
float    timestep;

struct VS_Output
{
   float4  Pos      : POSITION;
   float4  Coord    : TEXCOORD0;
};

VS_Output
MyVertexShader(float4 inputPos : POSITION)
{
   VS_Output Output;
   float4 objectCoord = inputPos;

   float dist = pow(objectCoord.x, 2) + pow(objectCoord.y, 2);
   objectCoord.z = sin(dist * 8.0 + timestep) / (1 + dist * 10.0);

   Output.Pos = mul(objectCoord, matWorldViewProj);
   Output.Coord = objectCoord;

   return Output;
}

struct PS_Input
{
   float4  Coord    : TEXCOORD0;
};

float4
MyPixelShader(PS_Input Input) : COLOR
{
   /*
    * Simple 2D procedural checkerboard.
    */

   const float4 color1 = { 0.25, 0.25, 0.25, 1.0 };
   const float4 color2 = { 1.0, 1.0, 1.0, 1.0 };
   const float checkerSize = 0.2;

   float2 s = fmod(Input.Coord.xy / checkerSize, 1);

   float check = ( (float)(s.x > 0.5 || (s.x < 0 && s.x > -0.5)) +
                   (float)(s.y > 0.5 || (s.y < 0 && s.y > -0.5)) );

   float4 color = lerp(color1, color2, fmod(check, 2));

   /*
    * Do a little fake shading
    */

   const float4 shadeTop = { 1.0, 1.0, 0.5, 1.0 };
   const float4 shadeBottom = { 0.5, 0.5, 1.0, 1.0 };
   float z = Input.Coord.z * 2;

   color = lerp(color, shadeBottom, clamp(z, 0, 0.25));
   color = lerp(color, shadeTop, clamp(-z, 0, 0.25));

   return color;
}
