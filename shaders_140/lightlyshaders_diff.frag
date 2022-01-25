#version 140

uniform sampler2DRect shadow_sampler;

uniform int corner_number;
uniform vec2 sampler_size;

in vec2 texcoord0;
out vec4 fragColor;

void main(void)
{
    vec2 ShadowHorCoord;
    vec2 ShadowVerCoord;
    vec2 Shadow0;

    if (corner_number == 0) {
        ShadowHorCoord = vec2(texcoord0.x, sampler_size.y);
        ShadowVerCoord = vec2(0.0, sampler_size.y - texcoord0.y);
        Shadow0 = vec2(0.0, sampler_size.y);
    } else if (corner_number == 1) {
        ShadowHorCoord = vec2(texcoord0.x, 0.0);
        ShadowVerCoord = vec2(0.0, sampler_size.y - texcoord0.y);
        Shadow0 = vec2(0.0, 0.0);
    }

    vec4 texShadow0 = texture(shadow_sampler, Shadow0);

    vec4 texShadowHorCur = texture(shadow_sampler, ShadowHorCoord);
    vec4 texShadowVerCur = texture(shadow_sampler, ShadowVerCoord);

    vec3 tex = vec3(1.0, 1.0, 1.0);

    vec3 diffHorCur = tex.rgb-texShadowHorCur.rgb;
    vec3 diffVerCur = tex.rgb-texShadowVerCur.rgb;

    vec3 diff0 = tex.rgb - texShadow0.rgb;

    vec3 diff = diffHorCur + (diffVerCur-diff0);

    fragColor = vec4(diff.rgb, 1.0);
}
