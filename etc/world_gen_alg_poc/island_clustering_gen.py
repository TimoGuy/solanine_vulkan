from array import array
import sys
from xmlrpc.client import Boolean
from PIL import Image
import numpy as np
import tkinter as tk
import tkinter.filedialog as tkfd
from random import random
from math import dist, cos, sin, pi, ceil


root = tk.Tk()
root.withdraw()


def read_image(path: str) -> Image:
    try:
        image = Image.open(path)
        return image
    except Exception as e:
        print(e)


if __name__ == '__main__':
    img = read_image('base_white_canvas.png')
    NUM_ISLANDS = 500
    NUM_BIOMES = 20

    # Build islands
    islands = []
    for _ in range(NUM_ISLANDS):
        x = random()
        y = random()

        px = cos(x * 2.0 * pi) * (1 - y ** 2)
        py = sin(x * 2.0 * pi) * (1 - y ** 2)

        indx = px * 0.5 * img.width + 0.5 * img.width
        indy = py * 0.5 * img.height + 0.5 * img.height

        islands.append([indx, indy])
    
    # Show built
    arr = np.array(img)
    for i in islands:
        arr[int(i[0])][int(i[1])] = [128, 128, 128]
    Image.fromarray(arr).show()

    # Squish closer by finding delta towards closest island
    i_deltas = []
    for i in islands:
        closest_island = [i[0], i[1], 10000000000000]
        for i2 in islands:
            if i == i2:
                continue
            distance = dist(i, i2)
            if distance < closest_island[2]:
                closest_island = [i2[0], i2[1], distance]
        i_deltas.append(closest_island)

    for idx, i in enumerate(islands):
        i[0] += (i_deltas[idx][0] - i[0]) * 0.5
        i[1] += (i_deltas[idx][1] - i[1]) * 0.5
    
    # Show squished
    arr = np.array(img)
    for i in islands:
        arr[int(i[0])][int(i[1])] = [128, 128, 128]
    Image.fromarray(arr).show()


    # Cluster together closest islands
    num_islands_per_cluster = ceil(NUM_ISLANDS / NUM_BIOMES)
    islands_clustered = []
    cluster_colors = []

    num_checked = 0
    i_checked = [False for _ in islands]
    while num_checked < len(islands):
        # Pick a point to start clustering (if checked already, keep moving forward until you find an unchecked one)
        c_ind = int(random() * len(islands))
        while i_checked[c_ind]:
            c_ind = (c_ind + 1) % len(i_checked)
        i_checked[c_ind] = True
        pivot_i = islands[c_ind]
        
        # Find the closest islands
        close_is = [[-1, -1, -1, 100000000000] for _ in range(num_islands_per_cluster - 1)]
        for idx, i in enumerate(islands):
            if i == pivot_i:
                continue
            if i_checked[idx]:
                continue

            # Try adding an entry into closest islands
            d = dist(i, pivot_i)
            for cind in range(len(close_is)):
                ci = close_is[cind]
                if d < ci[3]:
                    # Insert into close islands
                    close_is.insert(cind, [*i, idx, d])
                    close_is = close_is[:(num_islands_per_cluster - 1)]  # Truncate
                    break

        # Reserve all the closest islands as part of a new cluster
        cluster_is = []
        cluster_is.append(pivot_i)
        for ci in close_is:
            idx = ci[2]
            i_checked[idx] = True
            cluster_is.append(ci[:2])
        islands_clustered.append(cluster_is)
        cluster_colors.append([int(random() * 255) for _ in range(3)])
        num_checked += len(cluster_is)


    # Show clustered together closest islands
    arr = np.array(img)
    for x in range(len(arr)):
        for y in range(len(arr[0])):
            arr[x][y] = [0, 0, 0]
    for idx, _islands in enumerate(islands_clustered):
        for i in _islands:
            arr[int(i[0])][int(i[1])] = [c / 2.0 for c in cluster_colors[idx]]
    Image.fromarray(arr).show()


    # Show voronoi'd
    arr = np.array(img)
    for x in range(len(arr)):
        for y in range(len(arr[0])):
            distance = 10000000000000
            cluster_idx = -1
            for idx, _islands in enumerate(islands_clustered):
                for i in _islands:
                    d = dist([x, y], i)
                    if d < distance:
                        distance = d
                        cluster_idx = idx

            d_norm = distance / img.width * 255.0
            d_norm_cooked = 255.0 - (d_norm * 4.0)
            arr[x][y] = [c * d_norm_cooked / 255.0 for c in cluster_colors[cluster_idx]]  # 255 is multiplied in twice, so divide it out once!
    
    Image.fromarray(arr).show()

    # Edge out the voronoi (kinda clustering)
    for x in range(len(arr)):
        for y in range(len(arr[0])):
            arr[x][y] = [255, 255, 255] if arr[x][y][0] < 8 else [0, 0, 0]
    
    Image.fromarray(arr).show()



    sys.exit(0)





class Quad:
    # NOTE: this class is simply two triangles calculated in the evaluate function. It holds 4 vertices however.
    four_indices = []
    """
            The four_indices are defined in this order:

            0 -- 1
            |    |
            2 -- 3
    """

    def __init__(self):
        self.four_indices = []

    def evaluate(self, wavefront_obj):
        # The indices
        i0 = self.four_indices[0]
        i1 = self.four_indices[1]
        i2 = self.four_indices[2]
        i3 = self.four_indices[3]

        # Get count of number of inactive vertices (NOTE: this system may actually create dangling vertices)
        count_inactive = 0
        for index in self.four_indices:
            if index < 0:
                count_inactive += 1

        if count_inactive > 1:
            return  # This is an impossible quad
        elif count_inactive == 1:
            # Create the face where the 3 remaining active vertices are
            if i0 < 0:
                wavefront_obj.faces.append(f'f {i1} {i2} {i3}')
            elif i1 < 0:
                wavefront_obj.faces.append(f'f {i0} {i2} {i3}')
            elif i2 < 0:
                wavefront_obj.faces.append(f'f {i0} {i3} {i1}')
            elif i3 < 0:
                wavefront_obj.faces.append(f'f {i0} {i2} {i1}')
            return  # Let the rest of the method do the normal 4 active vertices calc

        # Find how to arrange the triangles
        v0 = wavefront_obj.registered_vertices_values[i0 - 1]
        v1 = wavefront_obj.registered_vertices_values[i1 - 1]
        v2 = wavefront_obj.registered_vertices_values[i2 - 1]
        v3 = wavefront_obj.registered_vertices_values[i3 - 1]
        height_diff_03 = abs(v0.y - v3.y)
        height_diff_12 = abs(v1.y - v2.y)

        if height_diff_03 < height_diff_12:
            wavefront_obj.faces.append(f'f {i0} {i3} {i1}')
            wavefront_obj.faces.append(f'f {i0} {i2} {i3}')
        else:
            wavefront_obj.faces.append(f'f {i0} {i2} {i1}')
            wavefront_obj.faces.append(f'f {i1} {i2} {i3}')


class Vertex:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    is_active: bool = False

    def __init__(self):
        self.x = 0.0
        self.y = 0.0
        self.z = 0.0
        self.is_active = False


class WavefrontObject:      # NOTE: for now at least, there's only one bc we're putting the whole thing in one.
    name: str

    registered_vertices: dict
    next_vertex_index: int
    registered_vertices_values = []

    vertices = []
    faces = []

    def __init__(self):
        self.name = ''
        self.registered_vertices = dict()
        self.next_vertex_index = 1
        self.registered_vertices_values = []
        self.vertices = []
        self.faces = []

    def as_string(self):
        giant_string = f'o {self.name}\n'

        giant_string += '\n# Vertices\n'
        for vert in self.vertices:
            giant_string += f'{vert}\n'

        giant_string += '\n# Faces\n'
        for face in self.faces:
            giant_string += f'{face}\n'
        
        return giant_string


# class WavefrontFile:
#     objects = [WavefrontObject]


if __name__ == '__main__':
    #
    # Load in image
    #
    fname = tkfd.askopenfilename(initialdir='.')
    if not fname:
        sys.exit(0)

    image = read_image(fname)
    image.show()

    #
    # Change image to green out lower threshold (and make it transparent)
    # NOTE: this also builds up the image mask as well
    #
    img_mask = []
    arr = np.array(image)
    for i in range(len(arr)):
        img_mask_row = []
        for j in range(len(arr[i])):
            mask_val = True
            if arr[i][j][0] < IMAGE_CUTOUT_THRESHOLD:
                arr[i][j] = GREEN_OUT_COLOR
                mask_val = False
            img_mask_row.append(mask_val)
        img_mask.append(img_mask_row)

    image_greened = Image.fromarray(arr)
    image_greened.show()

    save_fname = tkfd.asksaveasfilename(initialdir='.', confirmoverwrite=True, initialfile='output_greened.png')
    if not save_fname:
        sys.exit(0)

    image_greened.save(save_fname)

    #
    # Gather together the values from the image mask
    # NOTE: this is destructive to img_mask btw
    #
    def check_img_mask_location(i: int, j: int) -> bool:
        if i < 0 or i >= len(img_mask) or \
            j < 0 or j >= len(img_mask[i]):
            return False
        return img_mask[i][j]

    img_mask_clusters = []
    for i in range(len(img_mask)):
        for j in range(len(img_mask[i])):
            new_cluster = []

            # Try looking for any new clusters, and hope that the cells get added in
            # I feel kinda clever for coming up with this iterative solution hehehehe
            check_index = 0
            check_positions = []
            check_positions.append((i, j))
            while check_index < len(check_positions):
                pos = check_positions[check_index]
                check_i = pos[0]
                check_j = pos[1]
                if check_img_mask_location(check_i, check_j):
                    new_cluster.append((check_i, check_j))
                    img_mask[check_i][check_j] = False      # Destruct this node bc it's visited now

                    check_positions.append((check_i+1, check_j))        # Add more nodes to visit bc this was a success
                    check_positions.append((check_i-1, check_j))
                    check_positions.append((check_i, check_j+1))
                    check_positions.append((check_i, check_j-1))

                check_index += 1
            
            if len(new_cluster) > 0:
                img_mask_clusters.append(new_cluster)
    
    #
    # With the new clusters, cut out small clusters
    #
    for cluster in img_mask_clusters:
        if len(cluster) < CLUSTER_SIZE_THRESHOLD:
            for coordinate in cluster:
                i = coordinate[0]
                j = coordinate[1]
                arr[i][j] = GREEN_OUT_COLOR

    #
    # Generate .obj objects
    #
    obj_obj = WavefrontObject()
    obj_obj.name = 'the_ho_thing'

    # Create the vertex grid
    print("Creating the Vertex Grid")
    vertices = []
    for i in range(len(arr)):
        new_vertices_row = []
        for j in range(len(arr[i])):
            col = arr[i][j]
            new_vert = Vertex()
            new_vert.is_active = False if col[3] == 0 else True
            new_vert.x = i
            new_vert.y = float(col[0]) / 20.0 * 1.5
            new_vert.z = j
            new_vertices_row.append(new_vert)
        vertices.append(new_vertices_row)

    # Create the quad grid
    print("Creating the Quad Grid")
    quads = []
    for x in range(len(vertices) - 1):
        new_quads_row = []
        for z in range(len(vertices[x]) - 1):
            offsets = [[0,0], [1,0], [0,1], [1,1]]
            new_quad = Quad()

            for off in offsets:
                # Convert vertices into the indices
                vertex = vertices[x + off[0]][z + off[1]]

                insertion_index = -1    # This is the inactive flag value
                if vertex.is_active:
                    if vertex in obj_obj.registered_vertices.keys():
                        # Use previously registered index for this vertex
                        insertion_index = obj_obj.registered_vertices[vertex]
                    else:
                        # Register the unknown vertex
                        insertion_index = obj_obj.next_vertex_index
                        obj_obj.next_vertex_index += 1
                        obj_obj.registered_vertices[vertex] = insertion_index
                        obj_obj.registered_vertices_values.append(vertex)
                        obj_obj.vertices.append(f'v {vertex.x} {vertex.y} {vertex.z}')

                # Add the index into the quad
                new_quad.four_indices.append(insertion_index)
            
            # Add the quad
            new_quads_row.append(new_quad)
        quads.append(new_quads_row)
    
    # Evaluate the quads
    print("Evaluating Quads into Triangles")
    for quad_row in quads:
        for quad in quad_row:
            quad.evaluate(obj_obj)

    # Write out the obj file
    print("Done.")
    save_fname_objfile = tkfd.asksaveasfilename(initialdir='.', confirmoverwrite=True, initialfile='output_3d_model.obj')
    if not save_fname_objfile:
        sys.exit(0)

    print("Saving...")
    with open(save_fname_objfile, 'w') as fo:
        fo.write(obj_obj.as_string())
    print("Done.")


    #
    # With the new clusters, color them with random colors
    #
    for cluster in img_mask_clusters:
        random_color = GREEN_OUT_COLOR if len(cluster) < CLUSTER_SIZE_THRESHOLD else [int(random() * 255), int(random() * 255), int(random() * 255), 255]
        for coordinate in cluster:
            i = coordinate[0]
            j = coordinate[1]
            arr[i][j] = random_color
    
    image_greened_clustered = Image.fromarray(arr)
    image_greened_clustered.show()

    save_fname2 = tkfd.asksaveasfilename(initialdir='.', confirmoverwrite=True, initialfile='output_clustered.png')
    if not save_fname2:
        sys.exit(0)

    image_greened_clustered.save(save_fname2)
