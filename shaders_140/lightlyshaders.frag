#version 140

uniform sampler2DRect background_sampler;
uniform sampler2DRect shadow_sampler;
uniform sampler2DRect radius_sampler;

uniform int corner_number;

in vec2 texcoord0;
out vec4 fragColor;

void main(void)
{
    ivec2 textureSize2d = textureSize(background_sampler,0);

    vec2 TexCoordShift;
    vec2 CornerCoordShift;
    vec2 ShadowHorCoord;
    vec2 ShadowVerCoord;
    switch(corner_number) {
        case 0:
            TexCoordShift = vec2(1, 1);
            CornerCoordShift = vec2(0, 0);
            ShadowHorCoord = vec2(texcoord0.x, textureSize2d.y - 1);
            ShadowVerCoord = vec2(0, textureSize2d.y - texcoord0.y - 1);
            break;
        case 1:
            TexCoordShift = vec2(-1, 1);
            CornerCoordShift = vec2(-1, 0);
            ShadowHorCoord = vec2(texcoord0.x+1, textureSize2d.y - 1);
            ShadowVerCoord = vec2(textureSize2d.x-1, textureSize2d.y - texcoord0.y - 1);
            break;
        case 2:
            TexCoordShift = vec2(-1, -1);
            CornerCoordShift = vec2(-1, -1);
            ShadowHorCoord = vec2(texcoord0.x+1, 1);
            ShadowVerCoord = vec2(textureSize2d.x-1, textureSize2d.y - texcoord0.y - 2);
            break;
        case 3:
            TexCoordShift = vec2(1, -1);
            CornerCoordShift = vec2(0, -1);
            ShadowHorCoord = vec2(texcoord0.x + 1, 0);
            ShadowVerCoord = vec2(0, textureSize2d.y - texcoord0.y);
            break;
    }
    
    vec2 FragTexCoord = vec2(texcoord0.x + TexCoordShift.x, textureSize2d.y - texcoord0.y - TexCoordShift.y);
    vec2 cornerTexCoord = vec2(texcoord0.x + CornerCoordShift.x, texcoord0.y + CornerCoordShift.y);
    
    vec4 tex = texture(background_sampler, FragTexCoord);
    vec4 texCorner = texture(radius_sampler, cornerTexCoord);

    vec4 texHor = texture(background_sampler, ShadowHorCoord);
    vec4 texVer = texture(background_sampler, ShadowVerCoord);
    vec4 texShadowHor = texture(shadow_sampler, ShadowHorCoord);
    vec4 texShadowVer = texture(shadow_sampler, ShadowVerCoord);

    vec3 diff = max(texHor.rgb-texShadowHor.rgb, texVer.rgb-texShadowVer.rgb);

    tex.rgb = tex.rgb - diff.rgb;

    tex.a = texCorner.a;

    fragColor = tex;
}
