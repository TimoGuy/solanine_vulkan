> Reference: https://www.youtube.com/watch?v=NCptEJ1Uevg
> Better Reference: https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-17-efficient-soft-edged-shadows-using

- [x] Advanced, fast pcf stochastic shadows.
    - [x] Generate sample points
        - [x] Transform into the ringed form, with the outer ring first.
        - [x] Fix bug where the loaded 3d texture with the offsets get weirdly large numbers in red and blue channels.
            > It was because the texture format was using 16bit floats but the input data had 32bit floats. I ended up changing the texture format to 32bit floats but blitting it to 16bit floats or 8bit floats would give back performance.
    - ~~[ ] Render the sample points into the screenspace random sampling texture.~~
    - [x] Write the sample points into a buffer and create the image from a buffer.
    - [x] Read from the texture to sample from shadowmap.
- [x] Cascade fading.
    - [ ] Change the cascade borders for near field to include some of the previous cascade.
        > I guess (at least for now....) this isn't needed.  -Timo 2023/09/01
    - [x] If within the blend range, run shadow for both.
        - [x] Maybe there's an optimization where only if the shadow value isn't 0 or 1 then you shouldn't have to run shadow calc for the other cascade.
    - [x] Set fade out distance for final cascade to fade to no shadow.