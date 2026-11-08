#version 440
// texture is premultiplied, the vertex colour straight: the scene graph wants premultiplied output
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vCol;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};
layout(binding = 1) uniform sampler2D tex;
void main()
{
    vec4 t = texture(tex, vUv);
    fragColor = vec4(t.rgb * vCol.rgb * vCol.a, t.a * vCol.a);
}
