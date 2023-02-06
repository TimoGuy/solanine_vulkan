import sys
from PIL import Image
import numpy as np
import tkinter as tk
import tkinter.filedialog as tkfd
from random import random
from math import dist


root = tk.Tk()
root.withdraw()


class Island:
    x: int
    y: int

    def __init__(self, x, y):
        self.x = x
        self.y = y

if __name__ == '__main__':
    # Load in image
    image = Image.open('./base_white_canvas.png')  # NOTE: this is an rgb image (24 bit)
    image.show()

    # Change image to have NUM_ISLANDS amount of scattered dots (each representing the smallest type of island)
    NUM_ISLANDS = 500  # 25 for each world
    probability_spawn_island = NUM_ISLANDS / (image.width * image.height)

    gen_islands = []
    arr = np.array(image)
    for i in range(len(arr)):
        for j in range(len(arr[i])):
            if random() < probability_spawn_island:
                arr[i][j] = [128, 128, 128]
                gen_islands.append(Island(i, j))

    image_islands = Image.fromarray(arr)
    image_islands.show()

    # Pull clustered islands closer together. Perhaps have each island compute 3 closest islands to
    # each other and then define a vector to the median of those 3 positions and then have some velocity
    # to have the islands move in those directions at the same time.
    # This should help to bunch the islands closer together.
    moved_islands = []
    for island in gen_islands:
        three_closest = []
        for island_2 in gen_islands:
            if island is island_2:
                print('prog')
                continue

            d = dist([island.x, island.y], [island_2.x, island_2.y])

            # Find index to insert new distance entry at
            ins_ind = 0
            for i in range(len(three_closest)):
                if d < three_closest[i][1]:
                    ins_ind = i
                    break
                ins_ind = i+1
            
            # Insert and cut to size (< max if not at full capacity tho)
            three_closest.insert(ins_ind, [island, d])
            three_closest = three_closest[:min(3, len(three_closest))]
        
        # Calculate velocity
        median_target_pt = [0, 0]
        for isl in three_closest:
            median_target_pt[0] += isl[0].x / len(three_closest)
            median_target_pt[1] += isl[0].y / len(three_closest)

        moved_islands.append(Island(
            (median_target_pt[0] - island.x) * 1.0 + island.x,  # BUG: THIS APPEARS TO NOT BE MOVING AN INCH. CHECK TO SEE IF MEDIAN-ISLAND vector is non-zero
            (median_target_pt[1] - island.y) * 1.0 + island.y,
        ))

    # Color islands into new positions
    arr = np.array(image)
    for isl in moved_islands:
        arr[int(isl.x)][int(isl.y)] = [128, 128, 128]

    image_islands_moved = Image.fromarray(arr)
    image_islands_moved.show()

    # Cluster the nearest islands
    NUM_PER_WORLD = int(NUM_ISLANDS / 20)  # 20 is the number of different worlds there will be
    islands_in_groups = []
    while len(moved_islands):
        # Get initial cluster island
        core_cluster_island = moved_islands[int(random() * len(moved_islands))]
        moved_islands.remove(core_cluster_island)

        closest_islands = []  # [[<Island>, -1.0] for _ in range(NUM_PER_WORLD)]
        for island in moved_islands:
            d = dist([core_cluster_island.x, core_cluster_island.y], [island.x, island.y])

            # Find index to insert new distance entry at
            ins_ind = 0
            for i in range(len(closest_islands)):
                if d < closest_islands[i][1]:
                    ins_ind = i
                    break
                ins_ind = i+1
            
            # Insert and cut to size (< max if not at full capacity tho)
            closest_islands.insert(ins_ind, [island, d])
            closest_islands = closest_islands[:min(NUM_PER_WORLD-1, len(closest_islands))]

        # Setup the cluster
        new_group = [island_dist[0] for island_dist in closest_islands]
        new_group.insert(0, core_cluster_island)
        islands_in_groups.append(new_group)

        # Delete islands from the closest_islands calculation
        for el in new_group[1:]:
            moved_islands.remove(el)
    print(islands_in_groups)

    # Color the clusters
    for cluster in islands_in_groups:
        color = [random() * 255, random() * 255, random() * 255]
        for island in cluster:
            arr[island.x][island.y] = color

    image_islands_clustered = Image.fromarray(arr)
    image_islands_clustered.show()

    # TODO: Make way for larger islands to be detected so that they can either be made larger, be tagged to put a building on, or be placed lower than other ones (note: keep higher islands smaller bc there's less weight to them; keep lower islands larger bc there's more weight)
    # IDEA: Have each island find its 3 closest islands and if the summation of the distance of the 3 is the largest of all the islands (loner island), then make this one a large size island, and have the next 30% of the number of islands in the cluster be the mid sized islands.
    #       And then get this to represent in the pic by making mid sized islands a 3x3 block and large sized a 5x5 block

    sys.exit(0)

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
