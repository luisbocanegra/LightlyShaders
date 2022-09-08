#version 140

uniform sampler2D sampler;
uniform sampler2D radius_top_left_sampler;
uniform sampler2D radius_top_right_sampler;
uniform sampler2D radius_bottom_right_sampler;
uniform sampler2D radius_bottom_left_sampler;

uniform vec2 expanded_size;
uniform vec2 frame_size;
uniform vec2 csd_shadow_offset;
uniform int radius;
uniform int shadow_sample_offset;

uniform mat4 modelViewProjectionMatrix;

uniform vec4 modulation;
uniform float saturation;

in vec2 texcoord0;
out vec4 fragColor;

vec4 shapeWindow(vec4 tex, vec4 texCorner)
{
    if(texCorner.a == 1) {
        discard;
    } else {
        return vec4(tex.rgb*(1-texCorner.a), min(1-texCorner.a, tex.a));
    }
}

vec4 shapeCSDShadowWindow(float start_x, float start_y, vec4 tex, vec4 texCorner)
{
    vec2 ShadowHorCoord = vec2(texcoord0.x, start_y);
    vec2 ShadowVerCoord = vec2(start_x, texcoord0.y);
    vec2 Shadow0 = vec2(start_x, start_y);

    vec4 texShadowHorCur = texture(sampler, ShadowHorCoord);
    vec4 texShadowVerCur = texture(sampler, ShadowVerCoord);
    vec4 texShadow0 = texture(sampler, Shadow0);

    vec4 texShadow = texShadowHorCur + (texShadowVerCur - texShadow0);
    
    if(texCorner.a == 1) {
        return texShadow;
    } else if(texCorner.a > 0) {
        return mix(vec4(tex.rgb*(1-texCorner.a), min(1-texCorner.a, tex.a)), texShadow, texCorner.a);
    } else {
        return tex;
    }
}

void main(void)
{
    vec4 tex = texture(sampler, texcoord0);
    vec2 texture_size = textureSize(sampler, 0);
    vec2 coord0 = vec2(texcoord0.x*texture_size.x, texcoord0.y*texture_size.y);
    vec4 texCorner;
    vec4 outColor;
    float start_x;
    float start_y;

    //CSD without shadow
    if(texture_size == expanded_size && texture_size == frame_size) {
        //Left side
        if (coord0.x < radius) {
            //Top left corner
            if (coord0.y < radius) {
                texCorner = texture(radius_top_left_sampler, vec2((coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), (coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                
                outColor = shapeWindow(tex, texCorner);
            //Bottom left corner
            } else if (coord0.y > texture_size.y - radius) {
                texCorner = texture(radius_bottom_left_sampler, vec2((coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), 1 - (texture_size.y - coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                
                outColor = shapeWindow(tex, texCorner);
            //Center
            } else {
                outColor = tex;
            }
        //Right side
        } else if (coord0.x > texture_size.x - radius) {
            //Top right corner
            if (coord0.y < radius) {
                texCorner = texture(radius_top_right_sampler, vec2(1 - (texture_size.x - coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), (coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                
                outColor = shapeWindow(tex, texCorner);
            //Bottom right corner
            } else if (coord0.y > texture_size.y - radius) {
                texCorner = texture(radius_bottom_right_sampler, vec2(1 - (texture_size.x - coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), 1 - (texture_size.y - coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                
                outColor = shapeWindow(tex, texCorner);
            //Center
            } else {
                outColor = tex;
            }
        //Center
        } else {
            outColor = tex;
        }
    //CSD with shadow
    } else if(texture_size == expanded_size) {
        //Left side
        if (coord0.x > csd_shadow_offset.x - shadow_sample_offset && coord0.x < radius + csd_shadow_offset.x) {
            //Top left corner
            if (coord0.y > csd_shadow_offset.y - shadow_sample_offset && coord0.y < radius + csd_shadow_offset.y) {
                texCorner = texture(radius_top_left_sampler, vec2((coord0.x-csd_shadow_offset.x+shadow_sample_offset)/(radius+shadow_sample_offset), (coord0.y-csd_shadow_offset.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                start_x = (csd_shadow_offset.x - shadow_sample_offset)/texture_size.x;
                start_y = (csd_shadow_offset.y - shadow_sample_offset)/texture_size.y;
                
                outColor = shapeCSDShadowWindow(start_x, start_y, tex, texCorner);
            //Bottom left corner
            } else if (coord0.y > frame_size.y + csd_shadow_offset.y - radius && coord0.y < frame_size.y + csd_shadow_offset.y + shadow_sample_offset) {
                texCorner = texture(radius_bottom_left_sampler, vec2((coord0.x-csd_shadow_offset.x+shadow_sample_offset)/(radius+shadow_sample_offset), 1 - (frame_size.y + csd_shadow_offset.y - coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                start_x = (csd_shadow_offset.x - shadow_sample_offset)/texture_size.x;
                start_y = (csd_shadow_offset.y + frame_size.y + shadow_sample_offset)/texture_size.y;
                
                outColor = shapeCSDShadowWindow(start_x, start_y, tex, texCorner);
            //Center
            } else {
                outColor = tex;
            }
        //Right side
        } else if (coord0.x > csd_shadow_offset.x + frame_size.x - radius && coord0.x < csd_shadow_offset.x + frame_size.x + shadow_sample_offset) {
            //Top right corner
            if (coord0.y > csd_shadow_offset.y - shadow_sample_offset && coord0.y < radius + csd_shadow_offset.y) {
                texCorner = texture(radius_top_right_sampler, vec2(1 - (frame_size.x + csd_shadow_offset.x - coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), (coord0.y-csd_shadow_offset.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                start_x = (csd_shadow_offset.x + frame_size.x + shadow_sample_offset)/texture_size.x;
                start_y = (csd_shadow_offset.y - shadow_sample_offset)/texture_size.y;
                
                outColor = shapeCSDShadowWindow(start_x, start_y, tex, texCorner);
            //Bottom right corner
            } else if (coord0.y > frame_size.y + csd_shadow_offset.y - radius && coord0.y < frame_size.y + csd_shadow_offset.y + shadow_sample_offset) {
                texCorner = texture(radius_bottom_right_sampler, vec2(1 - (frame_size.x + csd_shadow_offset.x - coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), 1 - (frame_size.y + csd_shadow_offset.y - coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                start_x = (csd_shadow_offset.x + frame_size.x + shadow_sample_offset)/texture_size.x;
                start_y = (csd_shadow_offset.y + frame_size.y + shadow_sample_offset)/texture_size.y;
                
                outColor = shapeCSDShadowWindow(start_x, start_y, tex, texCorner);
            //Center
            } else {
                outColor = tex;
            }
        //Center
        } else {
            outColor = tex;
        }
    //Window shadow or titlebar
    } else if(texture_size != frame_size) {
        outColor = tex;
    //Window content
    } else {
        //Left side
        if (coord0.x < radius) {
            //Top left corner
            if (coord0.y < radius) {
                texCorner = texture(radius_top_left_sampler, vec2((coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), (coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                
                outColor = shapeWindow(tex, texCorner);
            //Bottom left corner
            } else if (coord0.y > texture_size.y - radius) {
                texCorner = texture(radius_bottom_left_sampler, vec2((coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), 1 - (texture_size.y - coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                
                outColor = shapeWindow(tex, texCorner);
            //Center
            } else {
                outColor = tex;
            }
        //Right side
        } else if (coord0.x > texture_size.x - radius) {
            //Top right corner
            if (coord0.y < radius) {
                texCorner = texture(radius_top_right_sampler, vec2(1 - (texture_size.x - coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), (coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                
                outColor = shapeWindow(tex, texCorner);
            //Bottom right corner
            } else if (coord0.y > texture_size.y - radius) {
                texCorner = texture(radius_bottom_right_sampler, vec2(1 - (texture_size.x - coord0.x+shadow_sample_offset)/(radius+shadow_sample_offset), 1 - (texture_size.y - coord0.y+shadow_sample_offset)/(radius+shadow_sample_offset)));
                
                outColor = shapeWindow(tex, texCorner);
            //Center
            } else {
                outColor = tex;
            }
        //Center
        } else {
            outColor = tex;
        }
    }

    //Support opacity
    if (saturation != 1.0) {
        vec3 desaturated = outColor.rgb * vec3( 0.30, 0.59, 0.11 );
        desaturated = vec3( dot( desaturated, outColor.rgb ));
        outColor.rgb = outColor.rgb * vec3( saturation ) + desaturated * vec3( 1.0 - saturation );
    }
    outColor *= modulation;
    
    //Output result
    fragColor = outColor;
}
