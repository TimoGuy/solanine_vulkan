# Transparency Renderpass.

> Rationale is to have a transparency renderpass that can support both a write-to-depth w/ zprepass (opaque-like or performs refraction) and a no-write-to-depth w/o zprepass in the same subpass (blending is a priority). This allows for things like water or ice w/ writing to depth, and particles with blending.

> Likely to get this to happen, we would need to integrate transparent materials into the material organizer. We would need to organize write-to-depth w/ zprepass meshes into the same way as opaque meshes (optimize model and material switches). We would need to organize no-write-to-depth w/o zprepass meshes into a back-to-front order (traditional transparency). Before rendering anything in this subpass, we would have to copy the whole opaque rendering so far (for refraction, aka the write-to-depth w/ zprepass... also, this is exactly where volumetric things are an issue.\*). With no-write-to-depth w/o zprepass meshes, they'll be small particles invisible far away or larger particles only close to the camera, so just simply drawing them over volumetric objects is acceptable, and using the opaque rendering is unnecessary.

> **\*** If volumetric clouds are used, generally the opaque rendering is copied over without clouds, so in that case the clouds will be "cut out" with a section without clouds. If we include clouds, it would have to be included in the opaque rendering (this gets used for refraction with the no-write-to-depth w/o zprepass materials). Perhaps doing a 2-layer depth peel could work (clouds after all transparent depths in one texture, then clouds before in different texture). *Actually, the depth peel logically feels like it's the best solution.  -Timo (2024/02/25)*

> **NOTE**: bc it's so much harder to implement the water/ice "write-to-depth w/ zprepass meshes" bc of the depth peeling and opaque render texture, just focus on particle-style sorted transparency, then develop the opaque render/depth peeling along with the volumetric materials!  -Timo (2024/02/25)



## Todo List

- [ ] No-write-to-depth w/o zprepass sorted transparency.
    - [ ] Mark certain materials as `Transparent`.
        - [ ] Exclude them from opaque ZPrepass.
        - [ ] Exclude from shadow render.
            > @NOTE: These are going to be very small particles or things that shouldn't be casting a shadow.
        - [ ] Create bucket for transparent materials.
            > @NOTE: it might be good to look into encoding an int64 into a std::map instead of using the bucket system for the sake of memory and speed.
                - Though, the only thing that's hampering speed is the amount of time it takes for the skinned mesh vertices and indices to get written out.
        - [ ] Load all the meshes that use transparent into a list to get sorted.

    - [ ] Depth sorting (NOTE: has to be CPU-side).
        - ~~NOTE: this is probably the one thing that would be requiring recording the command buffer every frame, which if this were solved would make the engine run a lot faster.~~
            - NEVERMIND: I thought we could batch all vertices and indices together in a gi-huge model so that a new model doesn't have to be bound, but there's no way to do that with materials. Thus, the command buffer would have to be recorded every frame anyway. Oh well.
        - [ ] Use std::map

    - [ ] Write commands, trying to switch materials and models as little as possible.
        - But there's very little control on how many times the model and material has to change.
    
    - [ ] Implement same frustum culling as opaque materials.
        > NOTE: do not do occlusion culling with these nor the zprepass transparency. Only include opaque-only objects in the occlusion culling queries!
        > NOTE2: There's no time to do a loopback to the CPU, so there might be material/model switches on invisible things!
        > NOTE3: Or, instead of doing GPU culling, do CPU culling and then there will definitely be saved driver overhead on material/model switches!
        - [ ] Do this culling on the CPU instead, so that depth sorting can be done on the CPU-side with the culling information.



- [f] Write-to-depth w/ zprepass transparency.
    > **PREREQUISITE**: This should be worked on at a point where you're developing the volumetric material and can implement depth peeling.
    - [ ] Mark certain materials as `Transparent-Solid-Depth`.
        - [ ] Exclude them from opaque ZPrepass.
        - [ ] Include them in Transparent-Solid-Depth ZPrepass.
        - [ ] Include in shadow render.
            > @NOTE: If light passes thru a surface, you would think that there is no shadow cast, but if the light gets refracted, it does in fact redirect the light that passes thru, thus causing a shadow to be cast, though it looks different from a typical shadow. For this and our limited 2024 GTX 1070 technology, just do a normal shadow.
    - [ ] ZPrepass.
    - [ ] Depth Peeling opaque render texture.
    - [ ] Actual render.
