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
};

inline void buildDefaultVoxelData(VoxelField_XData& data, const std::string& myGuid);
inline void assembleVoxelRenderObjects(VoxelField_XData& data, const std::string& attachedEntityGuid);
inline void deleteVoxelRenderObjects(VoxelField_XData& data);


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
    assembleVoxelRenderObjects(*_data, getGUID());
}

VoxelField::~VoxelField()
{
    deleteVoxelRenderObjects(*_data);
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

    vec3 linecastRayDelta;
    glm_vec3_sub(linecastPt2, linecastPt1, linecastRayDelta);
    glm_vec3_normalize(linecastRayDelta);
    vec3 osNormalFloat = { (float_t)osNormal[0], (float_t)osNormal[1], (float_t)osNormal[2] };
    glm_vec3_normalize(osNormalFloat);
    float_t lookDotNormal = glm_vec3_dot(linecastRayDelta, osNormalFloat);
    bool rayDirectionFacingAwayFromPlane = (lookDotNormal >= 0.0f);

    vec3 poijpoij;
    glm_vec3_sub(linecastPt1, vec3{ (float_t)osStartPosition[0], (float_t)osStartPosition[1], (float_t)osStartPosition[2] }, poijpoij);
    glm_vec3_normalize(poijpoij);
    float_t signedDistanceFromPlane = glm_vec3_dot(poijpoij, osNormalFloat);
    bool rayOriginInFrontOfPlane = (signedDistanceFromPlane >= 0.0f);

    if ((rayOriginInFrontOfPlane && rayDirectionFacingAwayFromPlane) ||
        (!rayOriginInFrontOfPlane && !rayDirectionFacingAwayFromPlane))
        return false;  // Impossible for an intersection. Abort.

    // Project linecast ray onto plane (not a test for intersection)
    vec3 linecastRayDeltaProjectedOntoPlane;
    glm_vec3_scale(linecastRayDelta, abs(signedDistanceFromPlane), linecastRayDeltaProjectedOntoPlane);
    glm_vec3_add(linecastPt1, linecastRayDeltaProjectedOntoPlane, outProjectedPosition);

    return true;
}

bool setVoxelDataAtPositionNonDestructive(physengine::VoxelFieldPhysicsData* vfpd, ivec3 position, uint8_t data)
{
    if (physengine::getVoxelDataAtPosition(*vfpd, position[0], position[1], position[2]) != 0)
        return false;  // The space is already occupied. Don't fill in this position.

    physengine::setVoxelDataAtPosition(*vfpd, position[0], position[1], position[2], data);
    return true;
}

void VoxelField::physicsUpdate(const float_t& physicsDeltaTime)
{
    if (_data->isPicked)  // @NOTE: this picked checking system, bc physicsupdate() runs outside of the render thread, could easily get out of sync, but as long as the render thread is >40fps it should be fine.
    {
        if (_data->editorState.editing)
        {
            // Draw debug visualization
            vec3 linePt1 = { (float_t)_data->editorState.editStartPosition[0], (float_t)_data->editorState.editStartPosition[1], (float_t)_data->editorState.editStartPosition[2] };
            vec3 linePt2;
            calculatePositionOnVoxelPlane(_data->engine, _data->vfpd, _data->editorState.editStartPosition, _data->editorState.flatAxis, linePt2);
            glm_mat4_mulv3(_data->vfpd->transform, linePt1, 1.0f, linePt1);
            glm_mat4_mulv3(_data->vfpd->transform, linePt2, 1.0f, linePt2);
            physengine::drawDebugVisLine(linePt1, linePt2);

            if (input::keyEscPressed)
            {
                // Exit editing with no changes
                _data->editorState.editing = false;
            }
            else if (input::keyEnterPressed)
            {
                // Exit editing, saving changes
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
                            ivec3 insertPos;
                            glm_ivec3_add(_data->editorState.editStartPosition, _data->editorState.flatAxis, insertPos);
                            setVoxelDataAtPositionNonDestructive(_data->vfpd, insertPos, 1);
                        }
                        else
                        {
                            int32_t x, y, z;
                            for (x = _data->editorState.editStartPosition[0]; ; x = physutil::moveTowards(x, _data->editorState.editEndPosition[0], 1))
                            {
                                for (y = _data->editorState.editStartPosition[1]; ; y = physutil::moveTowards(y, _data->editorState.editEndPosition[1], 1))
                                {
                                    for (z = _data->editorState.editStartPosition[2]; ; z = physutil::moveTowards(z, _data->editorState.editEndPosition[2], 1))
                                    {
                                        setVoxelDataAtPositionNonDestructive(_data->vfpd, ivec3{ x, y, z }, 1);
                                        if (z == _data->editorState.editEndPosition[2]) break;
                                    }
                                    if (y == _data->editorState.editEndPosition[1]) break;
                                }
                                if (x == _data->editorState.editEndPosition[0]) break;
                            }
                        }
                    }
                    else
                    {

                    }
                }
                _data->editorState.editing = false;
                assembleVoxelRenderObjects(*_data, getGUID());
            }
        }
        else
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
    for (size_t i = 0; i < totalSize; i++)
        ds.dumpFloat((float_t)_data->vfpd->voxelData[i]);
}

void VoxelField::load(DataSerialized& ds)
{
    Entity::load(ds);
    mat4 load_transform;
    ds.loadMat4(load_transform);
    vec3 load_size;
    ds.loadVec3(load_size);

    size_t    totalSize      = (size_t)load_size[0] * (size_t)load_size[1] * (size_t)load_size[2];
    uint8_t*  load_voxelData = new uint8_t[totalSize];
    for (size_t i = 0; i < totalSize; i++)
    {
        float_t lf;
        ds.loadFloat(lf);
        load_voxelData[i] = (uint8_t)lf;
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

void VoxelField::renderImGui()
{
    ImGui::Text("Hello there!");

    _data->isPicked = true;
}


inline void buildDefaultVoxelData(VoxelField_XData& data, const std::string& myGuid)
{
    size_t sizeX = 8, sizeY = 8, sizeZ = 8;
    uint8_t* vd = new uint8_t[sizeX * sizeY * sizeZ];
    for (size_t i = 0; i < sizeX; i++)
    for (size_t j = 0; j < sizeY; j++)
    for (size_t k = 0; k < sizeZ; k++)
        vd[i * sizeY * sizeZ + j * sizeZ + k] = 0;  //1;
    vd[0] = 1;
    data.vfpd = physengine::createVoxelField(myGuid, sizeX, sizeY, sizeZ, vd);
}

inline void assembleVoxelRenderObjects(VoxelField_XData& data, const std::string& attachedEntityGuid)
{
    deleteVoxelRenderObjects(data);

    // Check for if voxel is filled and not surrounded
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
                vec3s ijk_0_5 = { i + 0.5f, j + 0.5f, k + 0.5f };
                RenderObject* newRO =
                    data.rom->registerRenderObject({
                        .model = data.voxelModel,
                        .renderLayer = RenderLayer::VISIBLE,
                        .attachedEntityGuid = attachedEntityGuid,
                    });
                glm_mat4_copy(data.vfpd->transform, newRO->transformMatrix);
                glm_translate(newRO->transformMatrix, ijk_0_5.raw);

                data.voxelRenderObjs.push_back(newRO);
                data.voxelOffsets.push_back(ijk_0_5);  // @NOCHECKIN: error with adding vec3's
            }
        }
    }
}

inline void deleteVoxelRenderObjects(VoxelField_XData& data)
{
    for (auto& v : data.voxelRenderObjs)
        data.rom->unregisterRenderObject(v);
    data.voxelRenderObjs.clear();
    data.voxelOffsets.clear();
}
