#version 140

uniform sampler2DRect background_sampler;
uniform sampler2DRect shadow_sampler;
uniform sampler2DRect radius_sampler;

uniform int corner_number;
uniform vec2 sampler_size;

in vec2 texcoord0;
out vec4 fragColor;

void main(void)
{
    vec2 ShadowCoord;

    if (corner_number == 0) {
        ShadowCoord = vec2(texcoord0.x, sampler_size.y - texcoord0.y);
    } else if (corner_number == 1) {
        ShadowCoord = vec2(sampler_size.x - texcoord0.x, sampler_size.y - texcoord0.y);
    } else if (corner_number == 2) {
        ShadowCoord = vec2(sampler_size.x - texcoord0.x, sampler_size.y - texcoord0.y);
    } else if (corner_number == 3) {
        ShadowCoord = vec2(texcoord0.x, sampler_size.y - texcoord0.y);
    }

    vec2 FragTexCoord = vec2(texcoord0.x, sampler_size.y - texcoord0.y);
    
    vec4 tex = texture(background_sampler, FragTexCoord);
    vec4 texCorner = texture(radius_sampler, texcoord0);

    vec4 texDiff = texture(shadow_sampler, ShadowCoord);

    tex.rgb = tex.rgb - texDiff.rgb*tex.rgb;

    tex.a = texCorner.a;

    fragColor = tex;
}
