import math

POINT_MATRIX_SIZE = 6
EDGE_COUNT        = 6
POINTS_ROTATION   = 0.0  # 12deg

PI        = 3.1415927
PI_OVER_2 = 1.5707963
PI_OVER_4 = 0.785398
EPSILON   = 0.000001

class float2:
    x: float
    y: float

    def __init__(self, x: float, y: float):
        self.x = x
        self.y = y

# Maps a unit square in [-1, 1] to a unit disk in [-1, 1]. Shirley 97 "A Low Distortion Map Between Disk and Square"
# Inputs: cartesian coordinates
# Return: new circle-mapped polar coordinates (radius, angle)
def unitSquareToUnitDiskPolar(a: float, b: float):
    if abs(a) > abs(b):  # First region (left and right quadrants of the disk)
        radius = a
        angle = b / (a + EPSILON) * PI_OVER_4
    else:  # Second region (top and botom quadrants of the disk)
        radius = b
        angle = PI_OVER_2 - (a / (b + EPSILON) * PI_OVER_4)

    if radius < 0:  # Always keep radius positive
        radius *= -1.0
        angle += PI

    return float2(radius, angle)


# Remap a unit square in [0, 1] to a polygon in [-1, 1] with <edgeCount> edges rotated by <shapeRotation> radians
# Inputs: cartesian coordinates
# Return: new polygon-mapped cartesian coordinates
def SquareToPolygonMapping(uv: float2, edgeCount: float, shapeRotation: float):
    PolarCoord = unitSquareToUnitDiskPolar(uv.x, uv.y)  # (radius, angle)

    # Re-scale radius to match a polygon shape
    PolarCoord.x *=                                      math.cos(PI / edgeCount)                                                    \
                                                                    /                                                                \
                    math.cos(PolarCoord.y - (2.0 * PI / edgeCount) * math.floor((edgeCount*PolarCoord.y + PI) / 2.0 / PI ) )

    # Apply a rotation to the polygon shape
    PolarCoord.y += shapeRotation;

    return float2(PolarCoord.x * math.cos(PolarCoord.y), PolarCoord.x * math.sin(PolarCoord.y));


if __name__ == '__main__':
    # Generate points in grid of SIZE by SIZE.
    points = []
    stride = 2.0 / (POINT_MATRIX_SIZE - 1)
    for i in range(POINT_MATRIX_SIZE):
        x = -1.0 + stride * i
        for j in range(POINT_MATRIX_SIZE):
            y = -1.0 + stride * j
            points.append(float2(x, y))

    # Pass points into polygon mapping.
    poly_mapped_points = []
    for point in points:
        poly_mapped_points.append(SquareToPolygonMapping(point, EDGE_COUNT, POINTS_ROTATION))
    
    # Print out points.
    print_string_desmos = ''
    for point in poly_mapped_points:
        print_string_desmos += f'({point.x},{point.y}),'
    print("DESMOS:")
    print(print_string_desmos[:-1])

    print_string_glsl = f'const vec2 bokehFilter[{POINT_MATRIX_SIZE * POINT_MATRIX_SIZE}] = vec2[](\n'
    for point in poly_mapped_points:
        print_string_glsl += f'\tvec2({point.x}, {point.y}),\n'
    print_string_glsl += ');'
    print("GLSL:")
    print(print_string_glsl)
