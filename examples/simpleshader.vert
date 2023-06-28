// #version 450

// layout(location = 0) in vec3 position;
// layout(location = 1) in vec3 color;
// layout(location = 2) in vec2 tex_coord;

// layout(binding = 0) uniform UniformBufferObject {
//     mat4 model;
//     mat4 view;
//     mat4 proj;
// } ubo;

// layout(location = 0) out vec3 fragColor;
// layout(location = 1) out vec2 fragTexCoord;

// // vec2 positions[3] = vec2[](
// //     vec2(0.0, -0.5),
// //     vec2(0.5, 0.5),
// //     vec2(-0.5, 0.5)
// // );

// // vec3 colors[3] = vec3[](
// //     vec3(1.0, 0.0, 0.0),
// //     vec3(0.0, 1.0, 0.0),
// //     vec3(0.0, 0.0, 1.0)
// // );

// void main() {
//     gl_Position = ubo.proj * ubo.view * ubo.model * vec4(position, 1.0);
//     fragColor = color;
//     fragTexCoord = tex_coord;
// }

#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
