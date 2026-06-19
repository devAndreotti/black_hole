#version 330 core
out vec4 FragColor;
uniform sampler2D screenTexture;
void main() {
    vec2 texCoord = gl_FragCoord.xy / textureSize(screenTexture, 0);
    vec4 screenColor = texture(screenTexture, texCoord);
    if (screenColor.a > 0.01) {
        discard;
    }
    FragColor = vec4(0.5, 0.5, 0.5, 0.7); // translucent lines
}
