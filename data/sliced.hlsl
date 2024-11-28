// Uniform variables set by OBS (required)
uniform float4x4 ViewProj; // View-projection matrix used in the vertex shader
uniform Texture2D image;   // Texture containing the source picture

// General properties
uniform bool show_uvs;
uniform float4 border;
uniform float2 output_size;
uniform float2 source_size;
uniform bool use_linear_filtering;
uniform bool debug;

// General constants
#define UVLow 0.0
#define UVHigh 1.0

// Interpolation method and wrap mode for sampling a texture
SamplerState point_clamp
{
	Filter = Point;        // Anisotropy / Point / Linear
	AddressU = Clamp;       // Wrap / Clamp / Mirror / border / MirrorOnce
	AddressV = Clamp;       // Wrap / Clamp / Mirror / border / MirrorOnce
	borderColor = 00000000; // Used only with border edges (optional)
};

// Interpolation method and wrap mode for sampling a texture
SamplerState linear_clamp
{
	Filter = Linear;         // Anisotropy / Point / Linear
	AddressU = Clamp;       // Wrap / Clamp / Mirror / border / MirrorOnce
	AddressV = Clamp;       // Wrap / Clamp / Mirror / border / MirrorOnce
	borderColor = 00000000; // Used only with border edges (optional)
};

// Data type of the input of the vertex shader
struct vertex_data
{
	float4 pos : POSITION;  // Homogeneous space coordinates XYZW
	float2 uv  : TEXCOORD0; // UV coordinates in the source picture
};

// Data type of the output returned by the vertex shader, and used as input
// for the pixel shader after interpolation for each pixel
struct pixel_data
{
	float4 pos : POSITION;  // Homogeneous screen coordinates XYZW
	float2 uv  : TEXCOORD0; // UV coordinates in the source picture
};

float map(float value, float min1, float max1, float min2, float max2) {
	return min2 + (value - min1) * (max2 - min2) / (max1 - min1);
}

float axis(float v, float2 from, float2 to)
{
	if (v < from.x)
	{
		return map(v, UVLow, from.x, UVLow, to.x);
	}
	else if (v <= from.y)
	{
		return map(v, from.x, from.y, to.x, to.y);
	}
	else
	{
		return map(v, from.y, UVHigh, to.y, UVHigh);
	}
}

pixel_data VS9Slice(vertex_data vertex)
{
	pixel_data pixel;
	pixel.pos = mul(float4(vertex.pos.xyz, 1.0), ViewProj);
	pixel.uv  = vertex.uv;
	return pixel;
}

float4 PS9Slice(pixel_data pixel) : TARGET
{    
	// Normalized pixel coordinates (from 0 to 1)
	float2 normalized_out_pos = pixel.uv;
	
	// get border texture -> texture uv space
	float4 border_tex_in_uv = border / source_size.yxyx; // top-left-bottom-right div by yxyx
	border_tex_in_uv.z = UVHigh - border_tex_in_uv.z; // invert bottom
	border_tex_in_uv.w = UVHigh - border_tex_in_uv.w; // invert right
	
	// get border out pixels in normalized space
	float4 border_out_normalized = border / output_size.yxyx; // top-left-bottom-right div by yxyx
	border_out_normalized.z = UVHigh - border_out_normalized.z; // invert bottom
	border_out_normalized.w = UVHigh - border_out_normalized.w; // invert right
	
	// for our current normalized space,
	// translate from border pixels in normalized space
	// to texture pixels in normalized space.
	float2 uv = float2(
		axis(normalized_out_pos.x, border_out_normalized.yw, border_tex_in_uv.yw),
		axis(normalized_out_pos.y, border_out_normalized.xz, border_tex_in_uv.xz)
	);

	// sample the texture or show UVs
	float4 color = show_uvs
		? float4(uv, 0.0, 1.0)
		: use_linear_filtering
			? image.Sample(linear_clamp, uv)
			: image.Sample(point_clamp, uv);

	// and return the color
	return color;
}

technique Draw
{
	pass
	{
		vertex_shader = VS9Slice(vertex);
		pixel_shader  = PS9Slice(pixel);
	}
}
