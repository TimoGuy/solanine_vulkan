> Reference: https://www.youtube.com/watch?v=NCptEJ1Uevg

- [ ] Advanced, fast pcf stochastic shadows.
    - [ ] Generate sample points
        - [ ] Transform into the ringed form, with the outer ring first.
    - [ ] Render the sample points into the screenspace random sampling texture.
    - [ ] Read from the texture to sample from shadowmap.
- [ ] Cascade fading.
    - [ ] Change the cascade borders for near field to include some of the previous cascade.
    - [ ] If within the blend range, run shadow for both.
        - [ ] Maybe there's an optimization where only if the shadow value isn't 0 or 1 then you shouldn't have to run shadow calc for the other cascade.
    - [ ] Set fade out distance for final cascade to fade to no shadow.