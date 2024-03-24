#ifndef PYGS_ENGINE_VULKAN_SHADER_ORDER_H
#define PYGS_ENGINE_VULKAN_SHADER_ORDER_H

namespace pygs {
namespace vk {
namespace shader {

const char* order_comp = R"shader(
#version 460

layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout (set = 0, binding = 0) uniform Camera {
  mat4 projection;
  mat4 view;
};

layout (set = 1, binding = 0) uniform Info {
  uint point_count;
};

layout (std430, set = 1, binding = 1) readonly buffer GaussianPosition {
  float gaussian_position[];  // (N, 3)
};

layout (std430, set = 1, binding = 2) readonly buffer GaussianCov3d {
  float gaussian_cov3d[];  // (N, 6)
};

layout (std430, set = 2, binding = 0) writeonly buffer DrawIndirect {
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int vertexOffset;
  uint firstInstance;
};

layout (std430, set = 2, binding = 2) writeonly buffer InstanceKey {
  uint key[];
};

layout (std430, set = 2, binding = 3) writeonly buffer InstanceIndex {
  uint index[];
};

void main() {
  uint id = gl_GlobalInvocationID.x;
  if (id >= point_count) return;

  vec4 pos = vec4(gaussian_position[id * 3 + 0], gaussian_position[id * 3 + 1], gaussian_position[id * 3 + 2], 1.f);
  pos = projection * view * pos;
  pos = pos / pos.w;
  float depth = pos.z;

  // valid only when center is inside NDC clip space.
  if (abs(pos.x) <= 1.f && abs(pos.y) <= 1.f && pos.z >= 0.f && pos.z <= 1.f) {
    vec3 v0 = vec3(gaussian_cov3d[id * 6 + 0], gaussian_cov3d[id * 6 + 1], gaussian_cov3d[id * 6 + 2]);
    vec3 v1 = vec3(gaussian_cov3d[id * 6 + 3], gaussian_cov3d[id * 6 + 4], gaussian_cov3d[id * 6 + 5]);
    vec4 pos = vec4(gaussian_position[id * 3 + 0], gaussian_position[id * 3 + 1], gaussian_position[id * 3 + 2], 1.f);
    // [v0.x v0.y v0.z]
    // [v0.y v1.x v1.y]
    // [v0.z v1.y v1.z]
    mat3 cov3d = mat3(v0, v0.y, v1.xy, v0.z, v1.yz);

    // view matrix
    mat3 view3d = mat3(view);
    cov3d = view3d * cov3d * transpose(view3d);
    pos = view * pos;

    // projection
    float r = length(vec3(pos));
    mat3 J = mat3(
      -1.f / pos.z, 0.f, -2.f * pos.x / r,
      0.f, -1.f / pos.z, -2.f * pos.y / r,
      pos.x / pos.z / pos.z, pos.y / pos.z / pos.z, -2.f * pos.z / r
    );
    cov3d = J * cov3d * transpose(J);

    // projection xy
    mat2 projection_scale = mat2(projection);
    mat2 cov2d = projection_scale * mat2(cov3d) * projection_scale;
    
    float a = cov2d[0][0];
    float b = cov2d[1][1];
    float c = cov2d[1][0];
    float D = sqrt((a - b) * (a - b) + 4.f * c * c);
    float s0 = sqrt(0.5f * (a + b + D));
    float s1 = sqrt(0.5f * (a + b - D));

    // TODO: select size threshold
    float size_threshold = 0.0001f;
    if (s0 >= size_threshold && s1 >= size_threshold) {
      uint instance_index = atomicAdd(instanceCount, 1);
      key[instance_index] = floatBitsToUint(1.f - depth);
      index[instance_index] = id;
    }
  }
}
)shader";

}
}  // namespace vk
}  // namespace pygs

#endif  // PYGS_ENGINE_VULKAN_SHADER_ORDER_H
