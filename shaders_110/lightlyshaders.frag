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

    vec4 texHor = texture2DRect(background_sampler, ShadowHorCoord);
    vec4 texVer = texture2DRect(background_sampler, ShadowVerCoord);
    vec4 texShadowHor = texture2DRect(shadow_sampler, ShadowHorCoord);
    vec4 texShadowVer = texture2DRect(shadow_sampler, ShadowVerCoord);

    vec4 texHorPrev = texture2DRect(background_sampler, ShadowHorCoord-vec2(-1.0, 0.0));
    vec4 texVerPrev = texture2DRect(background_sampler, ShadowVerCoord-vec2(0.0, -1.0));
    vec4 texShadowHorPrev = texture2DRect(shadow_sampler, ShadowHorCoord-vec2(-1.0, 0.0));
    vec4 texShadowVerPrev = texture2DRect(shadow_sampler, ShadowVerCoord-vec2(0.0, -1.0));

    vec3 diffHorPrev = texHorPrev.rgb - texShadowHorPrev.rgb;
    vec3 diffVerPrev = texVerPrev.rgb - texShadowVerPrev.rgb;
    vec3 diffHor = texHor.rgb-texShadowHor.rgb;
    vec3 diffVer = texVer.rgb-texShadowVer.rgb;

    vec3 diffPrev = max(diffHorPrev, diffVerPrev);
    vec3 diffCur = max(diffHor, diffVer);

    vec3 diff = max(diffPrev, diffCur);
    tex.rgb = tex.rgb - diff.rgb;

    tex.a = texCorner.a;

    gl_FragColor = tex;
}
