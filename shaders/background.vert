#version 450
layout(location=0) out vec2 world;
void main() {
    vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2)*2.0-1.0;
    world=p*vec2(9.0,-5.5);
    gl_Position=vec4(p,0,1);
}
