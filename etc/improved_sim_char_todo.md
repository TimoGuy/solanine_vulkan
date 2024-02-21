# Improved sim char.


- [x] Backend movement system.
    - [x] Grounded collide and slide.
        - [x] Steep slopes.
        - [x] Up slopes.
        - [x] Up steps.
        - [x] Down slopes.
        - [x] On level ground.
    - [x] Airborne collide and slide.
        - [x] Ceiling detection. Reset vertical velocity when ceiling is detected too.

- [ ] Front end movement system (player).
    - [x] Grounded
        - [x] Move in specific direction and turn instead of directly changing velocity.
        - [x] If move in opposite direction, skid and turn around.
        - [x] Face character in the direction heading.
    - [x] Air
        - [x] Whatever direction held, velocity is added. No turning.
    - [x] Landing
        - [x] Whatever direction is moving when landing is immediate set facing direction.
    - [ ] Jumping
        - [ ] Immediate change in facing direction of what direction is held.
    - [ ] Misc.
        - [ ] Moving up slopes makes movement super slow. Ignore previous frame's velocity and just base off of grounded, standing normal.




- [ ] FUTURE
    - [ ] Detect if almost on edge when jumping up and scoot out of the way even though it's flat.

