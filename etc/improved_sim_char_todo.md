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

- [x] Front end movement system (player).
    - [x] Grounded
        - [x] Move in specific direction and turn instead of directly changing velocity.
        - [x] If move in opposite direction, skid and turn around.
        - [x] Face character in the direction heading.
    - [x] Air
        - [x] Whatever direction held, velocity is added. No turning.
    - [x] Landing
        - [x] Whatever direction is moving when landing is immediate set facing direction.
    - [x] Jumping
        - [x] Immediate change in facing direction of what direction is held.
    - [x] Misc.
        - [x] Moving up slopes makes movement super slow. Ignore previous frame's velocity and just base off of grounded, standing normal.
    > @NOTE: in the future you'll have to improve the sim char when implementing moving platforms, high velocity, etc. bc there's nothing that "resets" their velocity, so if they move really fast into a wall, the velocity doesn't get zero'd out.  -Timo 2024/02/22




- [ ] FUTURE
    - [ ] Detect if almost on edge when jumping up and scoot out of the way even though it's flat.

