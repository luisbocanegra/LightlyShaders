#version 110

uniform sampler2DRect background_sampler;
uniform sampler2DRect shadow_sampler;
uniform sampler2DRect radius_sampler;

uniform int corner_number;
uniform vec2 sampler_size;

varying vec2 texcoord0;

void main()
{
    vec2 TexCoordShift;
    vec2 CornerCoordShift;
    vec2 ShadowHorCoord;
    vec2 ShadowVerCoord;

    if (corner_number == 0) {
        TexCoordShift = vec2(1.0, 0.0);
        CornerCoordShift = vec2(0.0, 0.0);
        ShadowHorCoord = vec2(texcoord0.x, sampler_size.y);
        ShadowVerCoord = vec2(0.0, sampler_size.y - texcoord0.y);
    } else if (corner_number == 1) {
        TexCoordShift = vec2(-1.0, 0.0);
        CornerCoordShift = vec2(-1.0, 0.0);
        ShadowHorCoord = vec2(texcoord0.x + 1.0, sampler_size.y);
        ShadowVerCoord = vec2(sampler_size.x, sampler_size.y - texcoord0.y);
    } else if (corner_number == 2) {
        TexCoordShift = vec2(-1.0, -2.0);
        CornerCoordShift = vec2(-1.0, -1.0);
        ShadowHorCoord = vec2(texcoord0.x + 1.0, 0.0);
        ShadowVerCoord = vec2(sampler_size.x, sampler_size.y - texcoord0.y - 1.0);
    } else if (corner_number == 3) {
        TexCoordShift = vec2(1.0, -2.0);
        CornerCoordShift = vec2(0.0, -1.0);
        ShadowHorCoord = vec2(texcoord0.x + 1.0, 0.0);
        ShadowVerCoord = vec2(0.0, sampler_size.y - texcoord0.y);
    }
    
    vec2 FragTexCoord = vec2(texcoord0.x + TexCoordShift.x, sampler_size.y - texcoord0.y - TexCoordShift.y);
    vec2 cornerTexCoord = vec2(texcoord0.x + CornerCoordShift.x, texcoord0.y + CornerCoordShift.y);
    
    vec4 tex = texture2DRect(background_sampler, FragTexCoord);
    vec4 texCorner = texture2DRect(radius_sampler, cornerTexCoord);

    vec4 texHor = texture2DRect(background_sampler, ShadowHorCoord);
    vec4 texVer = texture2DRect(background_sampler, ShadowVerCoord);
    vec4 texShadowHor = texture2DRect(shadow_sampler, ShadowHorCoord);
    vec4 texShadowVer = texture2DRect(shadow_sampler, ShadowVerCoord);

    vec3 diff = max(texHor.rgb-texShadowHor.rgb, texVer.rgb-texShadowVer.rgb);

    tex.rgb = tex.rgb - diff.rgb;

    tex.a = texCorner.a;

    gl_FragColor = tex;
}
