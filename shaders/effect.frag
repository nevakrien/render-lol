#version 450
layout(location=0) in vec4 vertexColor;
layout(location=1) in vec2 local;
layout(location=0) out vec4 color;
void main() {
    float radius=length(local);
    float ring=(1.0-smoothstep(.08,.18,abs(radius-.74)))*(1.0-smoothstep(.94,1.0,radius));
    color=vec4(vertexColor.rgb,vertexColor.a*ring);
}
