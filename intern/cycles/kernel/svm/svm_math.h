/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

CCL_NAMESPACE_BEGIN

/* Nodes */

ccl_device void svm_node_math(KernelGlobals *kg,
                              ShaderData *sd,
                              float *stack,
                              uint type,
                              uint a_offset,
                              uint b_offset,
                              int *offset)
{
  float a = stack_load_float(stack, a_offset);
  float b = stack_load_float(stack, b_offset);
  float f = svm_math((NodeMathType)type, a, b);

  uint4 node1 = read_node(kg, offset);

  stack_store_float(stack, node1.y, f);
}

ccl_device void svm_node_vector_math(KernelGlobals *kg,
                                     ShaderData *sd,
                                     float *stack,
                                     uint type,
                                     uint a_offset,
                                     uint b_offset,
                                     int *offset)
{
  uint4 node1 = read_node(kg, offset);
  float3 a = stack_load_float3(stack, a_offset);
  float3 b = stack_load_float3(stack, b_offset);
  float scale = stack_load_float(stack, node1.y);

  float f;
  float3 v;
  svm_vector_math(&f, &v, (NodeVectorMathType)type, a, b, scale);

  if (stack_valid(node1.z))
    stack_store_float(stack, node1.z, f);
  if (stack_valid(node1.w))
    stack_store_float3(stack, node1.w, v);
}

CCL_NAMESPACE_END
