#version 440
// ImGui vertices: position (item units), atlas uv, straight-alpha colour
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 col;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vCol;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};
void main()
{
    vUv = uv;
    vCol = vec4(col.rgb, col.a * qt_Opacity);
    gl_Position = qt_Matrix * vec4(pos, 0.0, 1.0);
}
