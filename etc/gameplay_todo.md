- [ ] Double down on physically based character controller.
    - [ ] Issues

        - [ ] Seams where one box collider begins and one ends makes weird non-collisions for 3 ticks.
            - [ ] 

        - [ ] Running up ramps then stopping causes char to jump up a little bit.
        - [ ] Running up ramps and making it to a flat top causes char to jump up a little bit.
            - [ ] Both of these need some kind of "if Y velo is > this, then leave the ground surface, if not, then stick to the ground and cancel out any upwards momentum"

        - [ ] Running down ramps at first causes char to be midair for like 6 ticks.
            - [ ] If starting on a ramp already, just rotate the movement direction downward whenever moving downwards.
            - [ ] If starting on flat and then moving to ramp, raycast down the moment !grounded and move char down to where raycast hit.

- [x] Engine Issues that need to get fixed (Add more and uncheck the box as needed).
    - [x] No shadows over in spawn point #2??????
        > So all the objects being drawn are the ones still in area #1... so something wrong is happening for sure. Culling issue maybe?
        > Issue was that the culling light view matrix was using the extents to calc the frustum center instead of the corners of the frustum to calc the frustum center. That quick fix fixed it!