#include "VoxelField.h"

#include "Imports.h"
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "PhysicsEngine.h"
#include "PhysUtil.h"
#include "DataSerialization.h"
#include "InputManager.h"
#include "VulkanEngine.h"
#include "Camera.h"
#include "imgui/imgui.h"


struct VoxelField_XData
{
    VulkanEngine* engine;
    RenderObjectManager* rom;
    vkglTF::Model* voxelModel;
    std::vector<RenderObject*> voxelRenderObjs;
    std::vector<vec3s> voxelOffsets;  // @NOCHECKIN

    physengine::VoxelFieldPhysicsData* vfpd = nullptr;
    bool isPicked = false;
    
    struct EditorState
    {
        bool editing = false;
        bool isEditAnAppend = false;
        ivec3 flatAxis = { 0, 0, 0 };
        ivec3 editStartPosition = { 0, 0, 0 };
        ivec3 editEndPosition = { 0, 0, 0 };
    } editorState;
    bool isLightingDirty = true;  // True unless built lighting was loaded in automatically.
};

inline void buildDefaultVoxelData(VoxelField_XData& data, const std::string& myGuid);
inline void assembleVoxelRenderObjects(VoxelField_XData& data, const std::string& attachedEntityGuid, std::vector<ivec3s> dirtyPositions);
inline void deleteVoxelRenderObjects(VoxelField_XData& data, std::vector<ivec3s> dirtyPositions);


VoxelField::VoxelField(VulkanEngine* engine, EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _data(new VoxelField_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->engine = engine;
    _data->rom = rom;

    if (ds)
        load(*ds);

    //
    // Initialization
    //
    if (_data->vfpd == nullptr)
        buildDefaultVoxelData(*_data, getGUID());

    _data->voxelModel = _data->rom->getModel("DevBoxWood", this, [](){});
    assembleVoxelRenderObjects(*_data, getGUID(), {});
}

VoxelField::~VoxelField()
{
    deleteVoxelRenderObjects(*_data, {});
    physengine::destroyVoxelField(_data->vfpd);
    delete _data;
}

void calculateObjectSpaceCameraLinecastPoints(VulkanEngine* engine, physengine::VoxelFieldPhysicsData* vfpd, vec3& outLinecastPt1, vec3& outLinecastPt2)
{
	vec3 linecastPt1, linecastPt2;
    glm_vec3_copy(engine->_camera->sceneCamera.gpuCameraData.cameraPosition, linecastPt1);
    ImGuiIO& io = ImGui::GetIO();
    glm_unproject(
        vec3{ io.MousePos.x, io.MousePos.y, 1.0f },
        engine->_camera->sceneCamera.gpuCameraData.projectionView,
        vec4{ 0, 0, (float_t)engine->_windowExtent.width, (float_t)engine->_windowExtent.height },
        linecastPt2
    );

    // Convert linecast points to object space
    mat4 vfpdTransInv;
    glm_mat4_inv(vfpd->transform, vfpdTransInv);
    glm_mat4_mulv3(vfpdTransInv, linecastPt1, 1.0f, outLinecastPt1);
    glm_mat4_mulv3(vfpdTransInv, linecastPt2, 1.0f, outLinecastPt2);
}

bool intersectAABB(vec3 rayOrigin, vec3 rayDirection, vec3 aabbMin, vec3 aabbMax, float_t& outTNear, float_t& outTFar)  // @NOTE: this is the slab method (https://gist.github.com/DomNomNom/46bb1ce47f68d255fd5d)
{
    vec3 aabbMinSubRO;
    glm_vec3_sub(aabbMin, rayOrigin, aabbMinSubRO);
    vec3 aabbMaxSubRO;
    glm_vec3_sub(aabbMax, rayOrigin, aabbMaxSubRO);

    vec3 tMin, tMax;
    vec3 oneOverRO;
    glm_vec3_div(vec3{ 1.0f, 1.0f, 1.0f }, rayDirection, oneOverRO);
    glm_vec3_mul(aabbMinSubRO, oneOverRO, tMin);
    glm_vec3_mul(aabbMaxSubRO, oneOverRO, tMax);

    vec3 t1, t2;
    glm_vec3_minv(tMin, tMax, t1);
    glm_vec3_maxv(tMin, tMax, t2);

    float_t tNear = glm_vec3_max(t1);
    float_t tFar = glm_vec3_min(t2);

    if (tNear > tFar)  // No intersection.
        return false;

    outTNear = tNear;
    outTFar = tFar;
    return true;
}

bool raycastMouseToVoxel(VulkanEngine* engine, physengine::VoxelFieldPhysicsData* vfpd, ivec3& outPickedBlock, ivec3& outFlatAxis)
{
	vec3 linecastPt1, linecastPt2;
    calculateObjectSpaceCameraLinecastPoints(engine, vfpd, linecastPt1, linecastPt2);
    vec3 linecastRayDelta;
    glm_vec3_sub(linecastPt2, linecastPt1, linecastRayDelta);
    vec3 linecastRayDeltaNormalized;
    glm_vec3_normalize_to(linecastRayDelta, linecastRayDeltaNormalized);

    // Broadphase check if raycast will hit bounding box.
    float_t tNear, tFar;
    if (!intersectAABB(linecastPt1, linecastRayDeltaNormalized, vec3{ 0, 0, 0 }, vec3{ (float_t)vfpd->sizeX, (float_t)vfpd->sizeY, (float_t)vfpd->sizeZ }, tNear, tFar))
        return false;  // Main aabb for voxel field didn't collide. Broadphase failed. Abort.

    // Find voxels to check (calc checking bounds).
    vec3 pos;
    glm_vec3_copy(linecastPt1, pos);
    glm_vec3_muladds(linecastRayDeltaNormalized, tNear + 0.001f, pos);
    ivec3 checkBounds1 = { floor(pos[0]), floor(pos[1]), floor(pos[2]) };
    glm_vec3_copy(linecastPt1, pos);
    glm_vec3_muladds(linecastRayDeltaNormalized, tFar - 0.001f, pos);
    ivec3 checkBounds2 = { floor(pos[0]), floor(pos[1]), floor(pos[2]) };

    ivec3 checkBoundsMin, checkBoundsMax;
    glm_ivec3_minv(checkBounds1, checkBounds2, checkBoundsMin);
    glm_ivec3_maxv(checkBounds1, checkBounds2, checkBoundsMax);

    // Iterate thru checking bounds and find closest voxel.
    float_t closestDist = std::numeric_limits<float_t>::max();
    int32_t x, y, z;
    for (x = checkBoundsMin[0]; x <= checkBoundsMax[0]; x++)
    for (y = checkBoundsMin[1]; y <= checkBoundsMax[1]; y++)
    for (z = checkBoundsMin[2]; z <= checkBoundsMax[2]; z++)
    {
        // Check if ray is intersecting with the single voxel and if it's a closer hit than before.
        if (physengine::getVoxelDataAtPosition(*vfpd, x, y, z) != 0 &&
            intersectAABB(linecastPt1, linecastRayDeltaNormalized, vec3{ (float_t)x, (float_t)y, (float_t)z }, vec3{ x + 1.0f, y + 1.0f, z + 1.0f }, tNear, tFar) &&
            tNear < closestDist)
        {
            closestDist = tNear;
            vec3 hitPos;
            glm_vec3_copy(linecastPt1, hitPos);
            glm_vec3_muladds(linecastRayDeltaNormalized, tNear + 0.001f, hitPos);

            // Just store the best case scenario in the out variables.
            glm_ivec3_copy(ivec3{ (int32_t)floor(hitPos[0]), (int32_t)floor(hitPos[1]), (int32_t)floor(hitPos[2]) }, outPickedBlock);

            vec3 voxelNormalRaw;
            glm_vec3_sub(hitPos, vec3{ x + 0.5f, y + 0.5f, z + 0.5f }, voxelNormalRaw);
            float_t highestAbs = abs(voxelNormalRaw[0]);
            glm_ivec3_copy(ivec3{ (int32_t)glm_signf(voxelNormalRaw[0]), 0, 0 }, outFlatAxis);
            if (highestAbs < abs(voxelNormalRaw[1]))
            {
                highestAbs = abs(voxelNormalRaw[1]);
                glm_ivec3_copy(ivec3{ 0, (int32_t)glm_signf(voxelNormalRaw[1]), 0 }, outFlatAxis);
            }
            if (highestAbs < abs(voxelNormalRaw[2]))
            {
                highestAbs = abs(voxelNormalRaw[2]);
                glm_ivec3_copy(ivec3{ 0, 0, (int32_t)glm_signf(voxelNormalRaw[2]) }, outFlatAxis);
            }
        }
    }

    return true;
}

bool calculatePositionOnVoxelPlane(VulkanEngine* engine, physengine::VoxelFieldPhysicsData* vfpd, ivec3 osStartPosition, ivec3 osNormal, vec3& outProjectedPosition)
{
	vec3 linecastPt1, linecastPt2;
    calculateObjectSpaceCameraLinecastPoints(engine, vfpd, linecastPt1, linecastPt2);

    vec3 linecastRayDeltaNormalized;
    glm_vec3_sub(linecastPt2, linecastPt1, linecastRayDeltaNormalized);
    glm_vec3_normalize(linecastRayDeltaNormalized);

    vec3 osNormalFloat = { (float_t)osNormal[0], (float_t)osNormal[1], (float_t)osNormal[2] };
    glm_vec3_normalize(osNormalFloat);
    float_t denom = glm_vec3_dot(osNormalFloat, linecastRayDeltaNormalized);
    if (abs(denom) < 1e-6)
        return false;  // Abort to prevent divide by zero.

    vec3 planeOrigin = { osStartPosition[0] + 0.5f, osStartPosition[1] + 0.5f, osStartPosition[2] + 0.5f };
    glm_vec3_muladds(osNormalFloat, 0.5f - 0.001f, planeOrigin);

    vec3 rayOriginPlaneOriginDelta;
    glm_vec3_sub(planeOrigin, linecastPt1, rayOriginPlaneOriginDelta);
    float_t t = glm_vec3_dot(rayOriginPlaneOriginDelta, osNormalFloat) / denom;
    if (t < 0.0f)
        return false;  // No intersection occurred. Abort.

    glm_vec3_copy(linecastPt1, outProjectedPosition);
    glm_vec3_muladds(linecastRayDeltaNormalized, t, outProjectedPosition);
    return true;
}

bool setVoxelDataAtPositionNonDestructive(physengine::VoxelFieldPhysicsData* vfpd, ivec3 position, uint8_t data)
{
    if (physengine::getVoxelDataAtPosition(*vfpd, position[0], position[1], position[2]) != 0)
        return false;  // The space is already occupied. Don't fill in this position.

    physengine::setVoxelDataAtPosition(*vfpd, position[0], position[1], position[2], data);
    return true;
}

void drawSquareForVoxel(mat4 vfpdTransform, vec3 pos, vec3 normal)
{
    vec3 normalAbs;
    glm_vec3_abs(normal, normalAbs);

    std::array<vec3, 4> vertices;
    if (normalAbs[0] > 0.9f)
    {
        glm_vec3_add(pos, vec3{ 0.0f, -0.5f, -0.5f }, vertices[0]);
        glm_vec3_add(pos, vec3{ 0.0f, -0.5f,  0.5f }, vertices[1]);
        glm_vec3_add(pos, vec3{ 0.0f,  0.5f,  0.5f }, vertices[2]);
        glm_vec3_add(pos, vec3{ 0.0f,  0.5f, -0.5f }, vertices[3]);
    }
    else if (normalAbs[1] > 0.9f)
    {
        glm_vec3_add(pos, vec3{ -0.5f, 0.0f, -0.5f }, vertices[0]);
        glm_vec3_add(pos, vec3{ -0.5f, 0.0f,  0.5f }, vertices[1]);
        glm_vec3_add(pos, vec3{  0.5f, 0.0f,  0.5f }, vertices[2]);
        glm_vec3_add(pos, vec3{  0.5f, 0.0f, -0.5f }, vertices[3]);
    }
    else
    {
        glm_vec3_add(pos, vec3{ -0.5f, -0.5f, 0.0f }, vertices[0]);
        glm_vec3_add(pos, vec3{ -0.5f,  0.5f, 0.0f }, vertices[1]);
        glm_vec3_add(pos, vec3{  0.5f,  0.5f, 0.0f }, vertices[2]);
        glm_vec3_add(pos, vec3{  0.5f, -0.5f, 0.0f }, vertices[3]);
    }

    for (size_t i = 0; i < vertices.size(); i++)
        glm_mat4_mulv3(vfpdTransform, vertices[i], 1.0f, vertices[i]);
    
    for (size_t i = 0; i < vertices.size(); i++)
        physengine::drawDebugVisLine(vertices[i], vertices[(i + 1) % vertices.size()]);
}

void drawVoxelEditingVisualization(VoxelField_XData* d)
{
    // Get start and end positions.
    vec3 normal = { (float_t)d->editorState.flatAxis[0], (float_t)d->editorState.flatAxis[1], (float_t)d->editorState.flatAxis[2] };
    vec3 pt1 = { (float_t)d->editorState.editStartPosition[0], (float_t)d->editorState.editStartPosition[1], (float_t)d->editorState.editStartPosition[2] };
    glm_vec3_add(pt1, vec3{ 0.5f, 0.5f, 0.5f }, pt1);
    glm_vec3_muladds(normal, 0.5f - 0.001f, pt1);
    vec3 pt2;
    calculatePositionOnVoxelPlane(d->engine, d->vfpd, d->editorState.editStartPosition, d->editorState.flatAxis, pt2);

    // Iterate and draw all squares.
    int32_t x, y, z;
    for (x = (int32_t)floor(pt1[0]); ; x = physutil::moveTowards(x, (int32_t)floor(pt2[0]), 1))
    {
        for (y = (int32_t)floor(pt1[1]); ; y = physutil::moveTowards(y, (int32_t)floor(pt2[1]), 1))
        {
            for (z = (int32_t)floor(pt1[2]); ; z = physutil::moveTowards(z, (int32_t)floor(pt2[2]), 1))
            {
                vec3 drawPos = { x + 0.5f, y + 0.5f, z + 0.5f };
                glm_vec3_muladds(normal, 0.5f, drawPos);
                drawSquareForVoxel(d->vfpd->transform, drawPos, normal);
                if (z == (int32_t)floor(pt2[2])) break;
            }
            if (y == (int32_t)floor(pt2[1])) break;
        }
        if (x == (int32_t)floor(pt2[0])) break;
    }
}

void VoxelField::physicsUpdate(const float_t& physicsDeltaTime)
{
    if (_data->isPicked)  // @NOTE: this picked checking system, bc physicsupdate() runs outside of the render thread, could easily get out of sync, but as long as the render thread is >40fps it should be fine.
    {
        static bool prevCorXPressed = false;

        if (_data->editorState.editing)
        {
            drawVoxelEditingVisualization(_data);

            // Commit interaction.
            if (input::keyEscPressed)
            {
                // Exit editing with no changes
                _data->editorState.editing = false;
            }
            else if (input::keyEnterPressed || (!prevCorXPressed && (input::keyCPressed || input::keyXPressed)))
            {
                // Exit editing, saving changes
                std::vector<ivec3s> dirtyPositions;
                vec3 projectedPosition;
                if (calculatePositionOnVoxelPlane(
                    _data->engine,
                    _data->vfpd,
                    _data->editorState.editStartPosition,
                    _data->editorState.flatAxis,
                    projectedPosition))
                {
                    glm_ivec3_copy(
                        ivec3{ (int32_t)floor(projectedPosition[0]), (int32_t)floor(projectedPosition[1]), (int32_t)floor(projectedPosition[2]) },
                        _data->editorState.editEndPosition
                    );
                    std::cout << "ENDING EDITING, saving changes at { " << _data->editorState.editEndPosition[0] << ", " << _data->editorState.editEndPosition[1] << ", " << _data->editorState.editEndPosition[2] << " }" << std::endl;

                    if (_data->editorState.isEditAnAppend)
                    {
                        // @NOTE: only append at spots that are empty
                        if (glm_ivec3_distance2(_data->editorState.editStartPosition, _data->editorState.editEndPosition) == 0)
                        {
                            // Insert one sticking out
                            glm_ivec3_add(_data->editorState.editStartPosition, _data->editorState.flatAxis, _data->editorState.editStartPosition);
                            glm_ivec3_add(_data->editorState.editEndPosition, _data->editorState.flatAxis, _data->editorState.editEndPosition);
                        }

                        // Resize the bounds if needed
                        ivec3 editingBoundsMin, editingBoundsMax;
                        glm_ivec3_minv(_data->editorState.editStartPosition, _data->editorState.editEndPosition, editingBoundsMin);
                        glm_ivec3_maxv(_data->editorState.editStartPosition, _data->editorState.editEndPosition, editingBoundsMax);
                        ivec3 offset;
                        physengine::expandVoxelFieldBounds(*_data->vfpd, editingBoundsMin, editingBoundsMax, offset);
                        glm_ivec3_add(_data->editorState.editStartPosition, offset, _data->editorState.editStartPosition);  // Change the edit positions to account for the offset.
                        glm_ivec3_add(_data->editorState.editEndPosition, offset, _data->editorState.editEndPosition);

                        // Insert the resized offset into renderobject offsets.
                        for (size_t i = 0; i < _data->voxelOffsets.size(); i++)
                            glm_vec3_add(_data->voxelOffsets[i].raw, vec3{ (float_t)offset[0], (float_t)offset[1], (float_t)offset[2] }, _data->voxelOffsets[i].raw);

                        // Create new voxels (for every spot that's empty) in range.
                        int32_t x, y, z;
                        for (x = _data->editorState.editStartPosition[0]; ; x = physutil::moveTowards(x, _data->editorState.editEndPosition[0], 1))
                        {
                            for (y = _data->editorState.editStartPosition[1]; ; y = physutil::moveTowards(y, _data->editorState.editEndPosition[1], 1))
                            {
                                for (z = _data->editorState.editStartPosition[2]; ; z = physutil::moveTowards(z, _data->editorState.editEndPosition[2], 1))
                                {
                                    if (setVoxelDataAtPositionNonDestructive(_data->vfpd, ivec3{ x, y, z }, 1))
                                        dirtyPositions.push_back(ivec3s{ x, y, z });
                                    if (z == _data->editorState.editEndPosition[2]) break;
                                }
                                if (y == _data->editorState.editEndPosition[1]) break;
                            }
                            if (x == _data->editorState.editEndPosition[0]) break;
                        }
                    }
                    else
                    {
                        // Delete all voxels in range.
                        int32_t x, y, z;
                        for (x = _data->editorState.editStartPosition[0]; ; x = physutil::moveTowards(x, _data->editorState.editEndPosition[0], 1))
                        {
                            for (y = _data->editorState.editStartPosition[1]; ; y = physutil::moveTowards(y, _data->editorState.editEndPosition[1], 1))
                            {
                                for (z = _data->editorState.editStartPosition[2]; ; z = physutil::moveTowards(z, _data->editorState.editEndPosition[2], 1))
                                {
                                    physengine::setVoxelDataAtPosition(*_data->vfpd, x, y, z, 0);
                                    dirtyPositions.push_back(ivec3s{ x, y, z });
                                    if (z == _data->editorState.editEndPosition[2]) break;
                                }
                                if (y == _data->editorState.editEndPosition[1]) break;
                            }
                            if (x == _data->editorState.editEndPosition[0]) break;
                        }

                        // Resize the bounds now that stuff got deleted.
                        ivec3 offset;
                        physengine::shrinkVoxelFieldBoundsAuto(*_data->vfpd, offset);

                        // Insert the resized offset into renderobject offsets.
                        // @COPYPASTA
                        for (size_t i = 0; i < _data->voxelOffsets.size(); i++)
                            glm_vec3_add(_data->voxelOffsets[i].raw, vec3{ (float_t)offset[0], (float_t)offset[1], (float_t)offset[2] }, _data->voxelOffsets[i].raw);
                    }
                }
                _data->editorState.editing = false;

                if (!dirtyPositions.empty())
                {
                    assembleVoxelRenderObjects(*_data, getGUID(), dirtyPositions);
                    _data->isLightingDirty = true;
                }
            }
        }
        else if (!prevCorXPressed)
        {
            if (input::keyCPressed)
            {
                if (raycastMouseToVoxel(_data->engine, _data->vfpd, _data->editorState.editStartPosition, _data->editorState.flatAxis))
                {
                    // Enter append mode
                    _data->editorState.editing = true;
                    _data->editorState.isEditAnAppend = true;
                    std::cout << "STARTING EDITING (APPEND) at { " << _data->editorState.editStartPosition[0] << ", " << _data->editorState.editStartPosition[1] << ", " << _data->editorState.editStartPosition[2] << " } with axis { " << _data->editorState.flatAxis[0] << ", " << _data->editorState.flatAxis[1] << ", " << _data->editorState.flatAxis[2] << " }" << std::endl;
                }
            }
            else if (input::keyXPressed)
            {
                if (raycastMouseToVoxel(_data->engine, _data->vfpd, _data->editorState.editStartPosition, _data->editorState.flatAxis))
                {
                    // Enter remove mode
                    _data->editorState.editing = true;
                    _data->editorState.isEditAnAppend = false;
                    std::cout << "STARTING EDITING (REMOVE) at { " << _data->editorState.editStartPosition[0] << ", " << _data->editorState.editStartPosition[1] << ", " << _data->editorState.editStartPosition[2] << " } with axis { " << _data->editorState.flatAxis[0] << ", " << _data->editorState.flatAxis[1] << ", " << _data->editorState.flatAxis[2] << " }" << std::endl;
                }
            }
        }

        prevCorXPressed = input::keyCPressed || input::keyXPressed;
        _data->isPicked = false;
    }
}

void VoxelField::update(const float_t& deltaTime)
{
}

void VoxelField::lateUpdate(const float_t& deltaTime)
{

}

void VoxelField::dump(DataSerializer& ds)
{
    Entity::dump(ds);
    ds.dumpMat4(_data->vfpd->transform);
    vec3 sizeXYZ = { _data->vfpd->sizeX, _data->vfpd->sizeY, _data->vfpd->sizeZ };
    ds.dumpVec3(sizeXYZ);

    size_t totalSize = _data->vfpd->sizeX * _data->vfpd->sizeY * _data->vfpd->sizeZ;

    //
    // Write out voxel data.
    //
    struct VoxelDataChunk
    {
        int8_t count;
        int8_t voxelType;
    } chunk;
    std::string voxelDataStringified;
    for (size_t i = 0; i < totalSize; i++)
    {
        int8_t currentVoxelType = (int8_t)_data->vfpd->voxelData[i];
        if (i == 0)
        {
            // Start new chunk.
            chunk.count = 1;
            chunk.voxelType = currentVoxelType;
        }
        else
        {
            if (currentVoxelType == chunk.voxelType && chunk.count + 33 + 1 < 127)  // @NOTE: int8_t (char) has a limit of 127, but we're sticking to the printable range (which is 32-127, but 32 is Space, and 35 is '#' (treated like comments), so exclude those).
                chunk.count++;
            else
            {
                // Write chunk onto string.
                int8_t writeCount = chunk.count + 33;
                int8_t writeVoxelType = chunk.voxelType + 33;
                writeCount = (writeCount >= '#' ? writeCount + 1 : writeCount);
                writeVoxelType = (writeVoxelType >= '#' ? writeVoxelType + 1 : writeVoxelType);
                voxelDataStringified += writeCount;
                voxelDataStringified += writeVoxelType;

                // Start new chunk.
                chunk.count = 1;
                chunk.voxelType = currentVoxelType;
            }
        }
    }
    // Write chunk onto string.
    int8_t writeCount = chunk.count + 33;
    int8_t writeVoxelType = chunk.voxelType + 33;
    writeCount = (writeCount >= '#' ? writeCount + 1 : writeCount);
    writeVoxelType = (writeVoxelType >= '#' ? writeVoxelType + 1 : writeVoxelType);
    voxelDataStringified += writeCount;
    voxelDataStringified += writeVoxelType;
    ds.dumpString(voxelDataStringified);
}

void VoxelField::load(DataSerialized& ds)
{
    Entity::load(ds);
    mat4 load_transform;
    ds.loadMat4(load_transform);
    vec3 load_size;
    ds.loadVec3(load_size);

    size_t    totalSize      = (size_t)load_size[0] * (size_t)load_size[1] * (size_t)load_size[2];
    std::string load_voxelDataStringified;
    ds.loadString(load_voxelDataStringified);

    //
    // Load in voxel data.
    //
    uint8_t*  load_voxelData = new uint8_t[totalSize];
    size_t writeIndex = 0;
    for (size_t i = 0; i < load_voxelDataStringified.length(); i += 2)
    {
        int8_t count = load_voxelDataStringified[i];  // Revert from printable ascii range to actual values.
        int8_t voxelType = load_voxelDataStringified[i + 1];
        count = (count >= '#' ? count - 1 : count);
        voxelType = (voxelType >= '#' ? voxelType - 1 : voxelType);
        count -= 33;
        voxelType -= 33;
        
        for (size_t j = 0; j < count; j++)
            load_voxelData[writeIndex++] = (uint8_t)voxelType;
    }

    // Create Voxel Field Physics Data
    _data->vfpd = physengine::createVoxelField(getGUID(), load_size[0], load_size[1], load_size[2], load_voxelData);
    glm_mat4_copy(load_transform, _data->vfpd->transform);
}

void VoxelField::reportMoved(mat4* matrixMoved)
{
    // Search for which block was moved
    size_t i = 0;
    for (; i < _data->voxelRenderObjs.size(); i++)
        if (matrixMoved == &_data->voxelRenderObjs[i]->transformMatrix)
            break;
    
    glm_mat4_copy(*matrixMoved, _data->vfpd->transform);
    vec3 negVoxelOffsets;
    glm_vec3_negate_to(_data->voxelOffsets[i].raw, negVoxelOffsets);
    glm_translate(_data->vfpd->transform, negVoxelOffsets);

    // Move all blocks according to the transform
    for (size_t i2 = 0; i2 < _data->voxelOffsets.size(); i2++)
    {
        if (i2 == i)
            continue;
        glm_mat4_copy(_data->vfpd->transform, _data->voxelRenderObjs[i2]->transformMatrix);
        glm_translate(_data->voxelRenderObjs[i2]->transformMatrix, _data->voxelOffsets[i2].raw);
    }
}

bool isOutsideLightGrid(ivec3 position)
{
    return false;  // @INCOMPLETE.
}

float_t shootRayForLightBuilding(ivec3 origin, ivec3 delta, bool enableCheckForStaggeredBlocks)
{
    ivec3 nextPosition;
    glm_ivec3_add(origin, delta, nextPosition);
    if (isOutsideLightGrid(nextPosition))
        return 1.0f;

    ivec3 deltaAbs;
    glm_ivec3_abs(delta, deltaAbs);
    int32_t manhattanDistance = deltaAbs[0] + deltaAbs[1] + deltaAbs[2];
    if (manhattanDistance == 1)
    {
        // Cardinal direction ray
        if (enableCheckForStaggeredBlocks)  // @NOTE: this is the only situation where you can get blocked by staggered blocks: as a cardinal direction ray.
        {

        }

        // Recurse, recurse!
        return shootRayForLightBuilding(nextPosition, delta, true);
    }
    else if (manhattanDistance == 2)
    {
        // Edge direction ray
    }
    else if (manhattanDistance == 3)
    {
        // Corner direction ray
    }
    else
    {
        std::cerr << "ERROR: ray for light building is incorrect. Manhattan distance: " << manhattanDistance << std::endl;
    }
}

void buildLighting()
{
    // FOREACH position in the lightgrid
    ivec3 position;
    {
        // Cardinal directions.
        shootRayForLightBuilding(position, ivec3{  1,  0,  0 }, false);
        shootRayForLightBuilding(position, ivec3{ -1,  0,  0 }, false);
        shootRayForLightBuilding(position, ivec3{  0,  1,  0 }, false);
        shootRayForLightBuilding(position, ivec3{  0, -1,  0 }, false);
        shootRayForLightBuilding(position, ivec3{  0,  0,  1 }, false);
        shootRayForLightBuilding(position, ivec3{  0,  0, -1 }, false);

        // Edge directions.
        shootRayForLightBuilding(position, ivec3{  1,  1,  0 }, false);
        shootRayForLightBuilding(position, ivec3{ -1,  1,  0 }, false);
        shootRayForLightBuilding(position, ivec3{  0,  1,  1 }, false);
        shootRayForLightBuilding(position, ivec3{  0,  1, -1 }, false);
        shootRayForLightBuilding(position, ivec3{  1, -1,  0 }, false);
        shootRayForLightBuilding(position, ivec3{ -1, -1,  0 }, false);
        shootRayForLightBuilding(position, ivec3{  0, -1,  1 }, false);
        shootRayForLightBuilding(position, ivec3{  0, -1, -1 }, false);
        shootRayForLightBuilding(position, ivec3{  1,  0,  1 }, false);
        shootRayForLightBuilding(position, ivec3{ -1,  0,  1 }, false);
        shootRayForLightBuilding(position, ivec3{  1,  0, -1 }, false);
        shootRayForLightBuilding(position, ivec3{ -1,  0, -1 }, false);

        // Corner directions.
        shootRayForLightBuilding(position, ivec3{  1,  1,  1 }, false);
        shootRayForLightBuilding(position, ivec3{ -1,  1,  1 }, false);
        shootRayForLightBuilding(position, ivec3{  1,  1, -1 }, false);
        shootRayForLightBuilding(position, ivec3{ -1,  1, -1 }, false);
        shootRayForLightBuilding(position, ivec3{  1, -1,  1 }, false);
        shootRayForLightBuilding(position, ivec3{ -1, -1,  1 }, false);
        shootRayForLightBuilding(position, ivec3{  1, -1, -1 }, false);
        shootRayForLightBuilding(position, ivec3{ -1, -1, -1 }, false);

    }
}

void VoxelField::renderImGui()
{
    ImGui::Text("Hello there!");

    if (_data->isLightingDirty)
    {
        if (ImGui::Button("Build Lighting (Baking, essentially)"))
        {
            buildLighting();
            _data->isLightingDirty = false;
        }
    }
    else
        ImGui::Text("Lighting up to date.");

    _data->isPicked = true;
}

inline void buildDefaultVoxelData(VoxelField_XData& data, const std::string& myGuid)
{
    size_t sizeX = 8, sizeY = 1, sizeZ = 8;
    uint8_t* vd = new uint8_t[sizeX * sizeY * sizeZ];
    for (size_t i = 0; i < sizeX; i++)
    for (size_t j = 0; j < sizeY; j++)
    for (size_t k = 0; k < sizeZ; k++)
        vd[i * sizeY * sizeZ + j * sizeZ + k] = 1;
    data.vfpd = physengine::createVoxelField(myGuid, sizeX, sizeY, sizeZ, vd);
}

inline void assembleVoxelRenderObjects(VoxelField_XData& data, const std::string& attachedEntityGuid, std::vector<ivec3s> dirtyPositions)
{
    deleteVoxelRenderObjects(data, dirtyPositions);

    // Check for if voxel is filled and not surrounded
    std::vector<RenderObject> inROs;
    std::vector<RenderObject**> outRORefs;
    size_t startRenderObjectIndex = data.voxelRenderObjs.size();  // Set offset so only create new render objects for new renderobjects created in this round (in case there are already existing ones).
    for (int32_t i = 0; i < data.vfpd->sizeX; i++)
    for (int32_t j = 0; j < data.vfpd->sizeY; j++)
    for (int32_t k = 0; k < data.vfpd->sizeZ; k++)
    {
        if (physengine::getVoxelDataAtPosition(*data.vfpd, i, j, k))
        {
            // Check if surrounded. If not, add a renderobject for this voxel
            if (!physengine::getVoxelDataAtPosition(*data.vfpd, i + 1, j, k) ||
                !physengine::getVoxelDataAtPosition(*data.vfpd, i - 1, j, k) ||
                !physengine::getVoxelDataAtPosition(*data.vfpd, i, j + 1, k) ||
                !physengine::getVoxelDataAtPosition(*data.vfpd, i, j - 1, k) ||
                !physengine::getVoxelDataAtPosition(*data.vfpd, i, j, k + 1) ||
                !physengine::getVoxelDataAtPosition(*data.vfpd, i, j, k - 1))
            {
                if (!dirtyPositions.empty())
                {
                    // Check to see if voxel to add is within dirtypositions.
                    bool withinRange = false;
                    for (ivec3s& dp : dirtyPositions)  // @COPYPASTA
                    {
                        ivec3 diff;
                        glm_ivec3_sub(ivec3{ i, j, k }, dp.raw, diff);
                        glm_ivec3_abs(diff, diff);
                        int32_t distance = diff[0] + diff[1] + diff[2];  // Manhattan distance.
                        if (distance <= 1)
                        {
                            withinRange = true;
                            break;
                        }
                    }
                    if (!withinRange)
                        continue;  // Skip evaluating this voxel bc the render object should already exist here.
                }

                vec3s ijk_0_5 = { i + 0.5f, j + 0.5f, k + 0.5f };
                RenderObject newRO = {
                    .model = data.voxelModel,
                    .renderLayer = RenderLayer::VISIBLE,
                    .attachedEntityGuid = attachedEntityGuid,
                };
                glm_mat4_copy(data.vfpd->transform, newRO.transformMatrix);
                glm_translate(newRO.transformMatrix, ijk_0_5.raw);

                inROs.push_back(newRO);
                data.voxelRenderObjs.push_back(nullptr);
                data.voxelOffsets.push_back(ijk_0_5);  // @NOCHECKIN: error with adding vec3's
            }
        }
    }
    for (size_t i = startRenderObjectIndex; i < data.voxelRenderObjs.size(); i++)
        outRORefs.push_back(&data.voxelRenderObjs[i]);
    data.rom->registerRenderObjects(inROs, outRORefs);
}

inline void deleteVoxelRenderObjects(VoxelField_XData& data, std::vector<ivec3s> dirtyPositions)
{
    if (dirtyPositions.empty())
    {
        // Clear all.
        data.rom->unregisterRenderObjects(data.voxelRenderObjs);
        data.voxelRenderObjs.clear();
        data.voxelOffsets.clear();
        return;
    }

    // Clear only dirtypositions.
    std::vector<RenderObject*> rosToDelete;
    for (int32_t i = data.voxelRenderObjs.size() - 1; i >= 0; i--)
    {
        ivec3 offsetAsInt = { floor(data.voxelOffsets[i].x), floor(data.voxelOffsets[i].y), floor(data.voxelOffsets[i].z) };
        for (ivec3s& dp : dirtyPositions)
        {
            ivec3 diff;
            glm_ivec3_sub(offsetAsInt, dp.raw, diff);
            glm_ivec3_abs(diff, diff);
            int32_t distance = diff[0] + diff[1] + diff[2];  // Manhattan distance.
            if (distance <= 1)
            {
                rosToDelete.push_back(data.voxelRenderObjs[i]);
                data.voxelRenderObjs.erase(data.voxelRenderObjs.begin() + i);
                data.voxelOffsets.erase(data.voxelOffsets.begin() + i);
                break;
            }
        }
    }
    data.rom->unregisterRenderObjects(rosToDelete);
}
