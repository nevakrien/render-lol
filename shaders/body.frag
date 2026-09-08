#version 450
layout(location=0) in vec4 vertexColor;
layout(location=1) in vec2 local;
layout(location=0) out vec4 color;
void main() { color=vertexColor; }
