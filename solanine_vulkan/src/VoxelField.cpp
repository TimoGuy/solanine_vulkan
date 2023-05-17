#include "VoxelField.h"

#include "Imports.h"
#include "VkglTFModel.h"
#include "RenderObject.h"
#include "PhysicsEngine.h"
#include "DataSerialization.h"
#include "imgui/imgui.h"


struct VoxelField_XData
{
    RenderObjectManager* rom;
    vkglTF::Model* voxelModel;
    std::vector<RenderObject*> voxelRenderObjs;
    std::vector<vec3> voxelOffsets;

    physengine::VoxelFieldPhysicsData* vfpd = nullptr;
};

inline void    buildDefaultVoxelData(VoxelField_XData& data);
inline void    assembleVoxelRenderObjects(VoxelField_XData& data, const std::string& attachedEntityGuid);
inline void    deleteVoxelRenderObjects(VoxelField_XData& data);


VoxelField::VoxelField(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds) : Entity(em, ds), _data(new VoxelField_XData())
{
    Entity::_enablePhysicsUpdate = true;
    Entity::_enableUpdate = true;
    Entity::_enableLateUpdate = true;

    _data->rom = rom;

    if (ds)
        load(*ds);

    //
    // Initialization
    //
    if (_data->vfpd == nullptr)
        buildDefaultVoxelData(*_data);

    _data->voxelModel = _data->rom->getModel("DevBoxWood", this, [](){});
    assembleVoxelRenderObjects(*_data, getGUID());
}

VoxelField::~VoxelField()
{
    deleteVoxelRenderObjects(*_data);
    physengine::destroyVoxelField(_data->vfpd);
    delete _data;
}

void VoxelField::physicsUpdate(const float_t& physicsDeltaTime)
{

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
    ds.dumpVec3(vec3(_data->vfpd->sizeX, _data->vfpd->sizeY, _data->vfpd->sizeZ));

    size_t totalSize = _data->vfpd->sizeX * _data->vfpd->sizeY * _data->vfpd->sizeZ;
    for (size_t i = 0; i < totalSize; i++)
        ds.dumpFloat((float_t)_data->vfpd->voxelData[i]);
}

void VoxelField::load(DataSerialized& ds)
{
    Entity::load(ds);
    mat4 load_transform = ds.loadMat4();
    vec3 load_size      = ds.loadVec3();

    size_t    totalSize      = (size_t)load_size.x * (size_t)load_size.y * (size_t)load_size.z;
    uint8_t*  load_voxelData = new uint8_t[totalSize];
    for (size_t i = 0; i < totalSize; i++)
        load_voxelData[i] = (uint8_t)ds.loadFloat();

    // Create Voxel Field Physics Data
    _data->vfpd = physengine::createVoxelField(load_size.x, load_size.y, load_size.z, load_voxelData);
    _data->vfpd->transform = load_transform;
}

void VoxelField::reportMoved(void* matrixMoved)
{
    // Search for which block was moved
    size_t i = 0;
    for (; i < _data->voxelRenderObjs.size(); i++)
        if (&_data->voxelRenderObjs[i]->transformMatrix == matrixMoved)
            break;
    
    _data->vfpd->transform = glm::translate(*(mat4*)matrixMoved, -_data->voxelOffsets[i]);

    // Move all blocks according to the transform
    for (size_t i2 = 0; i2 < _data->voxelOffsets.size(); i2++)
    {
        if (i2 == i)
            continue;
        _data->voxelRenderObjs[i2]->transformMatrix = glm::translate(_data->vfpd->transform, _data->voxelOffsets[i2]);
    }
}

void VoxelField::renderImGui()
{
    ImGui::Text("Hello there!");
}


inline void buildDefaultVoxelData(VoxelField_XData& data)
{
    size_t sizeX = 8, sizeY = 8, sizeZ = 8;
    uint8_t* vd = new uint8_t[sizeX * sizeY * sizeZ];
    for (size_t i = 0; i < sizeX; i++)
    for (size_t j = 0; j < sizeY; j++)
    for (size_t k = 0; k < sizeZ; k++)
        vd[i * sizeY * sizeZ + j * sizeZ + k] = 1;
    data.vfpd = physengine::createVoxelField(sizeX, sizeY, sizeZ, vd);
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
                vec3 offset = vec3(i, j, k) + vec3(0.5f, 0.5f, 0.5f);
                RenderObject* newRO =
                    data.rom->registerRenderObject({
                        .model = data.voxelModel,
                        .transformMatrix = glm::translate(data.vfpd->transform, offset),
                        .renderLayer = RenderLayer::VISIBLE,
                        .attachedEntityGuid = attachedEntityGuid,
                    });
                data.voxelRenderObjs.push_back(newRO);
                data.voxelOffsets.push_back(offset);
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
