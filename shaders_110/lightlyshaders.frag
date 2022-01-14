#version 110

uniform sampler2DRect background_sampler;
uniform sampler2DRect shadow_sampler;
uniform sampler2DRect radius_sampler;

uniform int corner_number;
uniform vec2 sampler_size;

varying vec2 texcoord0;

void main()
{
    vec2 ShadowHorCoord;
    vec2 ShadowVerCoord;

    if (corner_number == 0) {
        ShadowHorCoord = vec2(texcoord0.x, sampler_size.y);
        ShadowVerCoord = vec2(0.0, sampler_size.y - texcoord0.y);
    } else if (corner_number == 1) {
        ShadowHorCoord = vec2(texcoord0.x, sampler_size.y);
        ShadowVerCoord = vec2(sampler_size.x, sampler_size.y - texcoord0.y);
    } else if (corner_number == 2) {
        ShadowHorCoord = vec2(texcoord0.x, 0.0);
        ShadowVerCoord = vec2(sampler_size.x, sampler_size.y - texcoord0.y);
    } else if (corner_number == 3) {
        ShadowHorCoord = vec2(texcoord0.x, 0.0);
        ShadowVerCoord = vec2(0.0, sampler_size.y - texcoord0.y);
    }
    
    vec2 FragTexCoord = vec2(texcoord0.x, sampler_size.y - texcoord0.y);
    
    vec4 tex = texture2DRect(background_sampler, FragTexCoord);
    vec4 texCorner = texture2DRect(radius_sampler, texcoord0);

    vec4 texHorCur = texture2DRect(background_sampler, ShadowHorCoord);
    vec4 texVerCur = texture2DRect(background_sampler, ShadowVerCoord);
    vec4 texShadowHorCur = texture2DRect(shadow_sampler, ShadowHorCoord);
    vec4 texShadowVerCur = texture2DRect(shadow_sampler, ShadowVerCoord);

    vec4 texHorPrev = texture2DRect(background_sampler, ShadowHorCoord-vec2(-1.0, 0.0));
    vec4 texVerPrev = texture2DRect(background_sampler, ShadowVerCoord-vec2(0.0, -1.0));
    vec4 texShadowHorPrev = texture2DRect(shadow_sampler, ShadowHorCoord-vec2(-1.0, 0.0));
    vec4 texShadowVerPrev = texture2DRect(shadow_sampler, ShadowVerCoord-vec2(0.0, -1.0));

    vec4 texHorNext = texture2DRect(background_sampler, ShadowHorCoord-vec2(1.0, 0.0));
    vec4 texVerNext = texture2DRect(background_sampler, ShadowVerCoord-vec2(0.0, 1.0));
    vec4 texShadowHorNext = texture2DRect(shadow_sampler, ShadowHorCoord-vec2(1.0, 0.0));
    vec4 texShadowVerNext = texture2DRect(shadow_sampler, ShadowVerCoord-vec2(0.0, 1.0));

    vec3 diffHorPrev = texHorPrev.rgb - texShadowHorPrev.rgb;
    vec3 diffVerPrev = texVerPrev.rgb - texShadowVerPrev.rgb;
    
    vec3 diffHorCur = texHorCur.rgb-texShadowHorCur.rgb;
    vec3 diffVerCur = texVerCur.rgb-texShadowVerCur.rgb;
    
    vec3 diffHorNext = texHorNext.rgb-texShadowHorNext.rgb;
    vec3 diffVerNext = texVerNext.rgb-texShadowVerNext.rgb;

    vec3 diffPrev = max(diffHorPrev, diffVerPrev);
    vec3 diffCur = max(diffHorCur, diffVerCur);
    vec3 diffNext = max(diffHorNext, diffVerNext);

    vec3 diff1 = max(diffPrev, diffCur);
    vec3 diff2 = max(diff1, diffNext);

    tex.rgb = tex.rgb - diff2.rgb;

    tex.a = texCorner.a;

    gl_FragColor = tex;
}
