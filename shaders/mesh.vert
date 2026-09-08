#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec4 tint;
layout(location=2) in vec2 uv;
layout(location=3) in float shape;
layout(location=0) out vec4 vertexColor;
layout(location=1) out vec2 local;
layout(location=2) out float shapeId;
void main() {
    gl_Position=vec4(position/vec2(9.0,-5.5),0,1);
    vertexColor=tint;
    local=uv;
    shapeId=shape;
}
