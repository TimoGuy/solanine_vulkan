- [ ] Double down on physically based character controller.
    - [ ] Issues

        - [x] isPrevGrounded lags by one frame. Updating that information should happen in another function like `PostSimulationUpdate()` or something like that for simulation character instead of after setting the linear velocity (since that doesn't immediately update the physics world).
            > Or it could happen by querying the information at the beginning of the frame instead of the end after setting velocity.
            > This immediately makes everything so much tighter.
            - [ ] @TODO: Test if this change makes a difference with moving platforms.

        - [ ] Seams where one box collider begins and one ends makes weird non-collisions for 3 ticks.
            - [ ] 

        - [ ] Running up ramps then stopping causes char to jump up a little bit.
        - [ ] Running up ramps and making it to a flat top causes char to jump up a little bit.
            - [ ] Both of these need some kind of "if Y velo is > this, then leave the ground surface, if not, then stick to the ground and cancel out any upwards momentum"

        - [ ] Running down ramps at first causes char to be midair for like 6 ticks.
            - [ ] If starting on a ramp already, just rotate the movement direction downward whenever moving downwards.
            - [ ] If starting on flat and then moving to ramp, raycast down the moment !grounded and move char down to where raycast hit.
                > It's kinda shotty, and I even tried to fix it with a second raycast... but maybe the best thing is to just use a spherecast?
                > Or another idea I have is using the surface normal, calc a bunch of down raycasts from the bottom of the char in the opposite direction of the surface normal to see which one is lowest distance?
                    > I think this one is best. (Refer to `try_midair_correction_method.png` in etc directory)

- [ ] Engine Issues that need to get fixed (Add more and uncheck the box as needed).
    - [x] No shadows over in spawn point #2??????
        > So all the objects being drawn are the ones still in area #1... so something wrong is happening for sure. Culling issue maybe?
        > Issue was that the culling light view matrix was using the extents to calc the frustum center instead of the corners of the frustum to calc the frustum center. That quick fix fixed it!
    - [ ] Sometimes voxelfield will have `:` as a numeric thing... that's not allowed!!!
