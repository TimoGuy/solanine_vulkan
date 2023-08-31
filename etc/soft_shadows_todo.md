> Reference: https://www.youtube.com/watch?v=NCptEJ1Uevg
> Better Reference: https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-17-efficient-soft-edged-shadows-using

- [ ] Advanced, fast pcf stochastic shadows.
    - [ ] Generate sample points
        - [ ] Transform into the ringed form, with the outer ring first.
        - [ ] Fix bug where the loaded 3d texture with the offsets get weirdly large numbers in red and blue channels.
    - ~~[ ] Render the sample points into the screenspace random sampling texture.~~
    - [ ] Write the sample points into a buffer and create the image from a buffer.
    - [x] Read from the texture to sample from shadowmap.
- [ ] Cascade fading.
    - [ ] Change the cascade borders for near field to include some of the previous cascade.
    - [ ] If within the blend range, run shadow for both.
        - [ ] Maybe there's an optimization where only if the shadow value isn't 0 or 1 then you shouldn't have to run shadow calc for the other cascade.
    - [ ] Set fade out distance for final cascade to fade to no shadow.