> Reference: https://www.youtube.com/watch?v=v9x_50czf-4
> Reference: https://www.adriancourreges.com/blog/2018/12/02/ue4-optimized-post-effects/

- [ ] Implement "focus depth" and "focus length" parameters
    - [x] Simple mockup
        - [x] Just color everything outside of the focal length black to highlight what is in view.
        - [x] Calculate the circle of confusion for stuff outside based off of these two parameters.

    - [x] Create pass where CoC is computed after the z buffer.
        - Here is the order of things:
            1. Downscale the scene by 1/2 the size.
            2. Use the depth texture to render the CoC. At the same time, multiply the color with the CoC to near and far field scene render targets.
            3. Downscale the near field (keeping the max value of the CoC) to 1/8th the size. Then, blur the near CoC.
            5. Use the downscaled near CoC to sample for the correct radius to do the pixels.
            6. Do the same for far CoC except use the full res CoC map.
            7. ~~Gaussian blur both near and far images (8x8).~~
            8. ~~Flood-fill for the maximum light intensity (3x3).~~

    - [ ] Fix these 3 vkimageviews that aren't getting deleted.
        > VkImageView 0xcad092000000000d
        > VkImageView 0x967dd1000000000e
        > VkImageView 0xe88693000000000c


        - [ ] Debug view: fade towards red for the close CoC and green for the far CoC.