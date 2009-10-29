float4x4 matView, matProj;

struct VS_Input
{
   float4  Pos      : POSITION;
   float4  Color    : COLOR0;
};

struct VS_Output
{
   float4  Pos      : POSITION;
   float4  Color    : COLOR0;
};


VS_Output
MyVertexShader(VS_Input Input)
{
   VS_Output Output;

   Output.Pos = mul(mul(Input.Pos, matView), matProj);
   Output.Color = Input.Color;

   return Output;
}

struct PS_Input
{
   float4  Color    : COLOR0;
};

float4
MyPixelShader(PS_Input Input) : COLOR
{
   return Input.Color;
}
