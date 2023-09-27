Reference: https://vkguide.dev/docs/gpudriven/compute_culling/

- [ ] Create `indirect_cull.comp`
    - [ ] Go thru all the models in the indirect batch and test `isvisible()` (@NOTE: just do frustum culling for now).
    - [ ] Build the batch to actually render using this compute buffer.
- [ ] Create hierarchical Z buffer
    - [ ] Create MAX Sampler for depth
    - [ ] Downsample IDK how many times with this.
- [ ] Test occlusion using spheres.
    - TODO: add todos here from what has happened thus far.
