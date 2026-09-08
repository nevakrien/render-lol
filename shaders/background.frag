#version 450
layout(location=0) in vec2 world;
layout(location=0) out vec4 color;
void main() {
    vec2 d=abs(world)-vec2(8.0,4.5);
    float edge=max(d.x,d.y);
    vec3 c=vec3(.037,.049,.075);
    if(edge<0.0) {
        c=vec3(.06,.078,.11);
        vec2 grid=abs(fract(world*.5+.5)-.5);
        float dotGrid=1.0-smoothstep(.009,.018,length(grid));
        c+=dotGrid*vec3(.06,.075,.09);
    }
    if(abs(edge)<.025) c=vec3(.23,.29,.37);
    color=vec4(c,1);
}
